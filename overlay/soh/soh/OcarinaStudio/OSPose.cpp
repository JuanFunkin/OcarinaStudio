#include "soh/OcarinaStudio/OSInternal.h"
#include "soh/OcarinaStudio/OSGame.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "soh/OcarinaStudio/OSPoseMath.h"

/*
 * Animator: poses and keyframe animations for Link.
 *
 * While the animator is active Link is frozen on a "base" pose (his standing pose, captured when the
 * animator starts) and EVERY limb is driven by the animator, so none of the game's own animation shows
 * through. His controller input is disabled and the game's head / upper body "look at" rotation is cleared,
 * so the head no longer drags the torso around.
 *
 * Editing works like a normal animation tool: pick a frame on the timeline, pick a limb, move a slider.
 * That creates (or updates) a keyframe at the current frame (auto key).
 *
 *   jointTable[0]          root translation (x, y, z)
 *   jointTable[limb - 1]   rotation of that limb (PLAYER_LIMB_WAIST == 2 -> jointTable[1])
 *
 * Rotations are s16 angles (65536 = 360 degrees). The waist yaw is special: the game feeds it back into the
 * actor's facing every tick, so it is applied to the actor's rotation instead (see ApplyJoints).
 *
 * Poses are saved in <app dir>/OcarinaStudio/poses/*.json and animations in .../animations/*.json.
 * Animations play at the game rate (20 ticks per second).
 */

namespace {

using namespace OcarinaStudio::PoseMath;

struct LimbInfo {
    const char* name;
    const char* group;
};

// Index in this table == slot in Pose::rot.
const LimbInfo kLimbs[kLimbCount] = {
    { "Root position (hips)", "Body" }, { "Waist (turn whole body)", "Body" }, { "Lower body", "Body" },
    { "Right thigh", "Right leg" },     { "Right shin", "Right leg" },         { "Right foot", "Right leg" },
    { "Left thigh", "Left leg" },       { "Left shin", "Left leg" },           { "Left foot", "Left leg" },
    { "Upper body", "Body" },           { "Head", "Head" },                    { "Hat", "Head" },
    { "Collar", "Left arm" },           { "Left shoulder", "Left arm" },       { "Left forearm", "Left arm" },
    { "Left hand", "Left arm" },        { "Right shoulder", "Right arm" },     { "Right forearm", "Right arm" },
    { "Right hand", "Right arm" },      { "Sheath", "Items" },                 { "Torso", "Body" },
};

// Order shown in the limb list.
const int kDisplayOrder[kLimbCount] = { 0, 1, 2, 9, 20, 10, 11, 12, 13, 14, 15, 16, 17, 18, 6, 7, 8, 3, 4, 5, 19 };

struct AnimatorState {
    bool active = false;

    Pose base;               // frozen standing pose
    bool haveBase = false;
    int captureDelay = 0;    // ticks to wait (Link locked, game animation settling) before capturing
    int16_t baseYaw = 0;     // actor facing when the base was captured

    std::vector<Keyframe> keys;
    int length = 40;         // animation length in ticks
    float time = 0.0f;       // current time, in ticks
    bool playing = false;
    bool loop = true;
    float speed = 1.0f;
    Easing easing = Easing::Smooth;

    int selected = 10;       // selected limb slot (head)
    bool autoKey = true;
} sState;

std::string sStatus;

// ---- helpers ----------------------------------------------------------------------------------

Player* CurrentPlayer() {
    return gPlayState != nullptr ? GET_PLAYER(gPlayState) : nullptr;
}

int CurrentFrame() {
    return static_cast<int>(std::lround(sState.time));
}

int KeyIndexAt(int frame) {
    for (size_t i = 0; i < sState.keys.size(); i++) {
        if (sState.keys[i].frame == frame) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int LastKeyFrame() {
    return sState.keys.empty() ? 0 : sState.keys.back().frame;
}

int TotalLength() {
    return std::max(sState.length, LastKeyFrame());
}

// The pose shown at `time`: the animation sampled there, with the base pose underneath so every limb is set.
Pose PoseAt(float time) {
    Pose out = sState.base;
    for (int i = 0; i < kLimbCount; i++) {
        out.enabled[i] = true;
    }
    if (sState.keys.empty()) {
        return out;
    }
    const Pose sampled = Sample(sState.keys, time, sState.easing);
    for (int i = 0; i < kLimbCount; i++) {
        if (sampled.enabled[i]) {
            for (int axis = 0; axis < 3; axis++) {
                out.rot[i][axis] = sampled.rot[i][axis];
            }
        }
    }
    return out;
}

// Stores `pose` as the keyframe at the current frame (creating the frame 0 key from the base first).
void UpsertKey(const Pose& pose) {
    if (sState.keys.empty() && CurrentFrame() != 0) {
        Keyframe first;
        first.frame = 0;
        first.pose = PoseAt(0.0f);
        sState.keys.push_back(first);
    }
    const int frame = CurrentFrame();
    const int index = KeyIndexAt(frame);
    if (index >= 0) {
        sState.keys[index].pose = pose;
    } else {
        Keyframe key;
        key.frame = frame;
        key.pose = pose;
        sState.keys.push_back(key);
        SortKeys(sState.keys);
    }
}

// Reads the joints the game computed this tick into a pose with every limb enabled.
Pose ReadGamePose(Player* player) {
    Pose pose;
    const Vec3s* joints = player->skelAnime.jointTable;
    for (int i = 0; i < kLimbCount; i++) {
        pose.rot[i][0] = joints[i].x;
        pose.rot[i][1] = joints[i].y;
        pose.rot[i][2] = joints[i].z;
        pose.enabled[i] = true;
    }
    pose.rot[1][1] = 0; // the game already moved this into the actor's facing
    return pose;
}

void ApplyJoints(Player* player, const Pose& pose) {
    Vec3s* joints = player->skelAnime.jointTable;
    for (int i = 0; i < kLimbCount; i++) {
        if (!pose.enabled[i]) {
            continue;
        }
        joints[i].x = pose.rot[i][0];
        joints[i].y = pose.rot[i][1];
        joints[i].z = pose.rot[i][2];
    }
    // The game adds jointTable[1].y to the actor's facing every tick and then zeroes it, which would make
    // Link spin. Apply the waist turn to the actor directly instead.
    const int16_t facing = static_cast<int16_t>(sState.baseYaw + pose.rot[1][1]);
    joints[1].y = 0;
    player->actor.shape.rot.y = facing;
    player->actor.world.rot.y = facing;
    player->yaw = facing;
}

// The game turns the head / upper body towards what Link looks at, which drags the torso along. Clear it.
void ClearLookAt(Player* player) {
    player->headLimbRot.x = player->headLimbRot.y = player->headLimbRot.z = 0;
    player->upperLimbRot.x = player->upperLimbRot.y = player->upperLimbRot.z = 0;
    player->upperLimbYawSecondary = 0;
}

// Link ignores the controller while locked. We remember if we set the flag so we never clear one the game set.
void SetLock(Player* player, bool lock) {
    static bool sWeSetIt = false;
    constexpr uint32_t kFlag = PLAYER_STATE1_INPUT_DISABLED;
    if (lock) {
        if (!(player->stateFlags1 & kFlag)) {
            player->stateFlags1 |= kFlag;
            sWeSetIt = true;
        }
        player->linearVelocity = 0.0f;
        player->actor.speedXZ = 0.0f;
    } else if (sWeSetIt) {
        player->stateFlags1 &= ~kFlag;
        sWeSetIt = false;
    }
}

// ---- files ------------------------------------------------------------------------------------

std::vector<std::string> ListJson(const std::string& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool SavePose(const std::string& name, const Pose& pose) {
    std::ofstream file(std::filesystem::path(OcarinaStudio::DataDir("poses")) / (name + ".json"));
    if (!file.is_open()) {
        return false;
    }
    file << PoseToJson(pose).dump(2);
    return true;
}

bool LoadPose(const std::string& name, Pose& out) {
    std::ifstream file(std::filesystem::path(OcarinaStudio::DataDir("poses")) / (name + ".json"));
    if (!file.is_open()) {
        return false;
    }
    nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
    return !j.is_discarded() && PoseFromJson(j, out);
}

bool SaveAnimation(const std::string& name) {
    std::ofstream file(std::filesystem::path(OcarinaStudio::DataDir("animations")) / (name + ".json"));
    if (!file.is_open()) {
        return false;
    }
    nlohmann::json j = AnimationToJson(sState.keys, sState.loop, sState.easing);
    j["length"] = TotalLength();
    file << j.dump(2);
    return true;
}

bool LoadAnimation(const std::string& name) {
    std::ifstream file(std::filesystem::path(OcarinaStudio::DataDir("animations")) / (name + ".json"));
    if (!file.is_open()) {
        return false;
    }
    nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
    if (j.is_discarded() || !AnimationFromJson(j, sState.keys, sState.loop, sState.easing)) {
        return false;
    }
    sState.length = std::max(j.value("length", 0), std::max(LastKeyFrame(), 1));
    sState.time = 0.0f;
    sState.playing = false;
    return true;
}

// ---- timeline widget --------------------------------------------------------------------------

// A scrubbable bar with a diamond on every keyframe. Returns true if the time changed.
bool Timeline() {
    const float height = 26.0f;
    const ImVec2 size(ImGui::GetContentRegionAvail().x, height);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##timeline", size);
    const int total = std::max(TotalLength(), 1);
    bool changed = false;
    if (ImGui::IsItemActive()) {
        const float t = std::clamp((ImGui::GetIO().MousePos.x - pos.x) / size.x, 0.0f, 1.0f);
        sState.time = std::round(t * static_cast<float>(total));
        sState.playing = false;
        changed = true;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(30, 30, 36, 255), 3.0f);
    const int tickStep = total > 120 ? 20 : 10;
    for (int f = 0; f <= total; f += tickStep) {
        const float x = pos.x + size.x * static_cast<float>(f) / static_cast<float>(total);
        draw->AddLine(ImVec2(x, pos.y + size.y - 6), ImVec2(x, pos.y + size.y), IM_COL32(120, 120, 130, 255));
    }
    for (const Keyframe& key : sState.keys) {
        const float x = pos.x + size.x * static_cast<float>(key.frame) / static_cast<float>(total);
        const float y = pos.y + size.y * 0.45f;
        draw->AddQuadFilled(ImVec2(x, y - 6), ImVec2(x + 6, y), ImVec2(x, y + 6), ImVec2(x - 6, y),
                            IM_COL32(255, 200, 40, 255));
    }
    const float px = pos.x + size.x * std::clamp(sState.time / static_cast<float>(total), 0.0f, 1.0f);
    draw->AddLine(ImVec2(px, pos.y), ImVec2(px, pos.y + size.y), IM_COL32(80, 200, 255, 255), 2.0f);
    return changed;
}

} // namespace

namespace OcarinaStudio {

void PoseOnSceneInit() {
    // A new Player object exists after a scene change: capture the standing pose again.
    sState.haveBase = false;
    sState.captureDelay = 12;
}

// Runs once per game tick, right after Player_Update.
void ApplyPose() {
    Player* player = CurrentPlayer();
    if (player == nullptr || player->skelAnime.jointTable == nullptr) {
        return;
    }
    // Animator mode (OcarinaAnimator): start posing as soon as Link exists.
    static bool sAutoStarted = false;
    if (!sAutoStarted && IsAnimatorMode()) {
        sAutoStarted = true;
        sState.active = true;
        sState.haveBase = false;
        sState.captureDelay = 12;
    }

    if (!sState.active) {
        SetLock(player, false);
        return;
    }

    SetLock(player, true);
    ClearLookAt(player);

    // Let the game settle Link into his idle pose, then freeze that as the base.
    if (!sState.haveBase) {
        if (sState.captureDelay > 0) {
            sState.captureDelay--;
            return;
        }
        sState.base = ReadGamePose(player);
        sState.baseYaw = player->actor.shape.rot.y;
        sState.haveBase = true;
    }

    if (sState.playing) {
        sState.time = AdvanceTime(sState.time, sState.speed, static_cast<float>(TotalLength()), sState.loop,
                                  sState.playing);
    }
    ApplyJoints(player, PoseAt(sState.time));
}

void DrawTransportControls() {
    // ---- Timeline and playback ----
    ImGui::Text("Frame %d / %d    (%.2f s)", CurrentFrame(), TotalLength(),
                static_cast<float>(CurrentFrame()) / static_cast<float>(kTicksPerSecond));
    Timeline();

    int frame = CurrentFrame();
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::InputInt("Go to frame", &frame, 1, 5)) {
        sState.time = static_cast<float>(std::clamp(frame, 0, TotalLength()));
        sState.playing = false;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputInt("Length", &sState.length, 5, 20);
    sState.length = std::clamp(sState.length, 1, 6000);

    if (ImGui::Button(sState.playing ? "Pause" : "Play")) {
        sState.playing = !sState.playing;
        if (sState.playing && sState.time >= static_cast<float>(TotalLength())) {
            sState.time = 0.0f;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("|<")) {
        sState.time = 0.0f;
        sState.playing = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("<key")) {
        for (int i = static_cast<int>(sState.keys.size()) - 1; i >= 0; i--) {
            if (sState.keys[i].frame < CurrentFrame()) {
                sState.time = static_cast<float>(sState.keys[i].frame);
                sState.playing = false;
                break;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("key>")) {
        for (const Keyframe& key : sState.keys) {
            if (key.frame > CurrentFrame()) {
                sState.time = static_cast<float>(key.frame);
                sState.playing = false;
                break;
            }
        }
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &sState.loop);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("Speed", &sState.speed, 0.1f, 3.0f, "%.2fx");
    ImGui::SameLine();
    int easing = static_cast<int>(sState.easing);
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Combo("Easing", &easing, "Linear\0Smooth\0")) {
        sState.easing = static_cast<Easing>(easing);
    }

    // ---- Keyframes ----
    const bool onKey = KeyIndexAt(CurrentFrame()) >= 0;
    ImGui::Checkbox("Auto key (moving a slider creates a keyframe)", &sState.autoKey);
    if (ImGui::Button(onKey ? "Update key here" : "Add key here")) {
        UpsertKey(PoseAt(sState.time));
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!onKey);
    if (ImGui::Button("Delete key here")) {
        sState.keys.erase(sState.keys.begin() + KeyIndexAt(CurrentFrame()));
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Clear all keys")) {
        sState.keys.clear();
        sState.time = 0.0f;
        sState.playing = false;
    }
    ImGui::TextDisabled("%zu keyframe(s). Yellow diamonds on the timeline are keyframes.", sState.keys.size());
}

void DrawFileControls() {
    // ---- Poses ----
    static char sPoseName[64] = "";
    static std::vector<std::string> sPoses = ListJson(DataDir("poses"));
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputText("##posename", sPoseName, sizeof(sPoseName));
    ImGui::SameLine();
    if (ImGui::Button("Save pose")) {
        const std::string name = SanitizeName(sPoseName);
        if (name.empty()) {
            sStatus = "Type a pose name first.";
        } else if (SavePose(name, PoseAt(sState.time))) {
            sStatus = "Saved pose \"" + name + "\".";
            sPoses = ListJson(DataDir("poses"));
        } else {
            sStatus = "Could not write the pose file.";
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("##loadpose", "Load pose here...")) {
        for (const std::string& name : sPoses) {
            if (ImGui::Selectable(name.c_str())) {
                Pose loaded = PoseAt(sState.time);
                Pose fromFile;
                if (LoadPose(name, fromFile)) {
                    for (int i = 0; i < kLimbCount; i++) {
                        if (fromFile.enabled[i]) {
                            for (int axis = 0; axis < 3; axis++) {
                                loaded.rot[i][axis] = fromFile.rot[i][axis];
                            }
                        }
                    }
                    UpsertKey(loaded);
                    sStatus = "Loaded pose \"" + name + "\" at frame " + std::to_string(CurrentFrame()) + ".";
                } else {
                    sStatus = "Could not read \"" + name + "\".";
                }
            }
        }
        ImGui::EndCombo();
    }

    // ---- Animations ----
    static char sAnimName[64] = "";
    static std::vector<std::string> sAnims = ListJson(DataDir("animations"));
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputText("##animname", sAnimName, sizeof(sAnimName));
    ImGui::SameLine();
    if (ImGui::Button("Save animation")) {
        const std::string name = SanitizeName(sAnimName);
        if (name.empty()) {
            sStatus = "Type an animation name first.";
        } else if (sState.keys.empty()) {
            sStatus = "There are no keyframes to save.";
        } else if (SaveAnimation(name)) {
            sStatus = "Saved animation \"" + name + "\".";
            sAnims = ListJson(DataDir("animations"));
        } else {
            sStatus = "Could not write the animation file.";
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("##loadanim", "Load animation...")) {
        for (const std::string& name : sAnims) {
            if (ImGui::Selectable(name.c_str())) {
                sStatus = LoadAnimation(name) ? "Loaded animation \"" + name + "\"." : "Could not read \"" + name + "\".";
            }
        }
        ImGui::EndCombo();
    }

    if (!sStatus.empty()) {
        ImGui::TextWrapped("%s", sStatus.c_str());
    }
}


// Track grid: one row per limb with a diamond on every keyframe, a ruler on top and the playhead.
void DrawTrackGrid() {
    const float labelW = 150.0f;
    const float rulerH = 18.0f;
    const float rowH = 18.0f;
    const float width = std::max(ImGui::GetContentRegionAvail().x, 300.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float x0 = origin.x + labelW;
    const float trackW = width - labelW - 10.0f;
    const int total = std::max(TotalLength(), 1);

    ImGui::InvisibleButton("##grid", ImVec2(width, rulerH + rowH * kLimbCount));
    if (ImGui::IsItemActive()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (mouse.x >= x0) {
            const float t = std::clamp((mouse.x - x0) / trackW, 0.0f, 1.0f);
            sState.time = std::round(t * static_cast<float>(total));
            sState.playing = false;
        }
        if (ImGui::IsItemClicked() && mouse.y > origin.y + rulerH) {
            const int row = static_cast<int>((mouse.y - origin.y - rulerH) / rowH);
            if (row >= 0 && row < kLimbCount) {
                sState.selected = kDisplayOrder[row];
            }
        }
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float bottom = origin.y + rulerH + rowH * kLimbCount;
    // ruler
    const int step = total > 200 ? 20 : (total > 80 ? 10 : 5);
    for (int f = 0; f <= total; f += step) {
        const float x = x0 + trackW * static_cast<float>(f) / static_cast<float>(total);
        draw->AddLine(ImVec2(x, origin.y + 8.0f), ImVec2(x, bottom), IM_COL32(70, 70, 78, 255));
        char num[16];
        std::snprintf(num, sizeof(num), "%d", f);
        draw->AddText(ImVec2(x + 2.0f, origin.y), IM_COL32(190, 190, 200, 255), num);
    }
    // rows
    for (int n = 0; n < kLimbCount; n++) {
        const int limb = kDisplayOrder[n];
        const float y = origin.y + rulerH + rowH * static_cast<float>(n);
        const bool selected = limb == sState.selected;
        draw->AddRectFilled(ImVec2(origin.x, y), ImVec2(x0 + trackW, y + rowH - 1.0f),
                            selected ? IM_COL32(60, 70, 100, 255) : IM_COL32(n % 2 ? 34 : 28, n % 2 ? 34 : 28, 38, 255));
        draw->AddText(ImVec2(origin.x + 4.0f, y + 2.0f), IM_COL32(225, 225, 230, 255), kLimbs[limb].name);
        for (const Keyframe& key : sState.keys) {
            const float x = x0 + trackW * static_cast<float>(key.frame) / static_cast<float>(total);
            const float cy = y + rowH * 0.5f;
            draw->AddQuadFilled(ImVec2(x, cy - 5), ImVec2(x + 5, cy), ImVec2(x, cy + 5), ImVec2(x - 5, cy),
                                IM_COL32(255, 200, 40, 255));
        }
    }
    // playhead
    const float px = x0 + trackW * std::clamp(sState.time / static_cast<float>(total), 0.0f, 1.0f);
    draw->AddLine(ImVec2(px, origin.y), ImVec2(px, bottom), IM_COL32(255, 60, 60, 255), 2.0f);
}

// One row per limb with three numeric fields (degrees; the first row is the hips position), like Saturn Studio.
void DrawValueRows() {
    Pose pose = PoseAt(sState.time);
    bool edited = false;
    for (int n = 0; n < kLimbCount; n++) {
        const int limb = kDisplayOrder[n];
        ImGui::PushID(limb);
        float values[3];
        for (int axis = 0; axis < 3; axis++) {
            values[axis] = limb == 0 ? static_cast<float>(pose.rot[0][axis]) : S16ToDeg(pose.rot[limb][axis]);
        }
        ImGui::SetNextItemWidth(230.0f);
        if (ImGui::DragFloat3("##v", values, limb == 0 ? 4.0f : 0.5f, 0.0f, 0.0f, limb == 0 ? "%.0f" : "%.1f")) {
            for (int axis = 0; axis < 3; axis++) {
                pose.rot[limb][axis] = limb == 0 ? static_cast<int16_t>(std::clamp(values[axis], -32768.0f, 32767.0f))
                                                 : DegToS16(values[axis]);
            }
            sState.selected = limb;
            edited = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("R")) {
            for (int axis = 0; axis < 3; axis++) {
                pose.rot[limb][axis] = sState.base.rot[limb][axis];
            }
            edited = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reset this limb to the standing pose");
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(kLimbs[limb].name);
        ImGui::PopID();
    }
    if (edited) {
        sState.playing = false;
        UpsertKey(pose);
    }
}

// The OcarinaAnimator layout: pose values on the right, the timeline with one track per limb at the bottom.
void DrawAnimatorLayout() {
    const ImGuiIO& io = ImGui::GetIO();
    const float menuH = ImGui::GetFrameHeight();
    const float timelineH = std::max(260.0f, io.DisplaySize.y * 0.36f);
    const float panelW = 400.0f;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - panelW, menuH));
    ImGui::SetNextWindowSize(ImVec2(panelW, io.DisplaySize.y - menuH - timelineH));
    if (ImGui::Begin("Pose", nullptr, flags)) {
        if (!sState.active || !sState.haveBase) {
            ImGui::TextDisabled("Settling Link into his standing pose...");
        } else {
            ImGui::TextDisabled("Drag a value to pose Link. It keys the current frame.");
            DrawValueRows();
            ImGui::Separator();
            DrawFileControls();
        }
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(0.0f, io.DisplaySize.y - timelineH));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, timelineH));
    if (ImGui::Begin("Timeline", nullptr, flags)) {
        if (sState.active && sState.haveBase) {
            DrawTransportControls();
            ImGui::BeginChild("tracks", ImVec2(0, 0), false);
            DrawTrackGrid();
            ImGui::EndChild();
        }
    }
    ImGui::End();
}

void DrawPoseTab() {
    Player* player = CurrentPlayer();

    // ---- Start / stop ----
    if (!sState.active) {
        ImGui::TextWrapped("The animator freezes Link in his standing pose so you can pose and animate him "
                           "limb by limb. His movement and the game's own animation stop while it is on.");
        ImGui::BeginDisabled(player == nullptr);
        if (ImGui::Button("Start animator", ImVec2(-1, 0))) {
            sState.active = true;
            sState.haveBase = false;
            sState.captureDelay = 12;
        }
        ImGui::EndDisabled();
        if (player == nullptr) {
            ImGui::TextDisabled("Waiting for the game to start...");
        }
        return;
    }

    if (ImGui::Button("Stop animator (back to the game)")) {
        sState.active = false;
        sState.playing = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Recapture standing pose")) {
        sState.haveBase = false;
        sState.captureDelay = 0;
        sState.active = true;
    }
    if (!sState.haveBase) {
        ImGui::TextDisabled("Settling Link into his standing pose...");
        return;
    }
    ImGui::Separator();

    DrawTransportControls();
    ImGui::Separator();

    const bool onKey = KeyIndexAt(CurrentFrame()) >= 0;

    // ---- Limb list + editor ----
    ImGui::BeginChild("limbs", ImVec2(170, 250), true);
    const char* lastGroup = "";
    for (int n = 0; n < kLimbCount; n++) {
        const int i = kDisplayOrder[n];
        if (std::strcmp(lastGroup, kLimbs[i].group) != 0) {
            lastGroup = kLimbs[i].group;
            ImGui::TextDisabled("%s", lastGroup);
        }
        ImGui::PushID(i);
        if (ImGui::Selectable(kLimbs[i].name, sState.selected == i)) {
            sState.selected = i;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("limbedit", ImVec2(0, 250), true);
    const int sel = sState.selected;
    ImGui::Text("%s", kLimbs[sel].name);
    Pose pose = PoseAt(sState.time);
    bool edited = false;
    if (sel == 0) {
        int pos[3] = { pose.rot[0][0], pose.rot[0][1], pose.rot[0][2] };
        if (ImGui::DragInt3("Position", pos, 4.0f, -32768, 32767)) {
            for (int axis = 0; axis < 3; axis++) {
                pose.rot[0][axis] = static_cast<int16_t>(pos[axis]);
            }
            edited = true;
        }
        ImGui::TextDisabled("Moves the hips. The Y value is the height.");
    } else {
        static const char* kAxes[3] = { "X", "Y", "Z" };
        for (int axis = 0; axis < 3; axis++) {
            float deg = S16ToDeg(pose.rot[sel][axis]);
            ImGui::PushID(axis);
            ImGui::SetNextItemWidth(-90.0f);
            if (ImGui::SliderFloat(kAxes[axis], &deg, -180.0f, 180.0f, "%.1f deg")) {
                pose.rot[sel][axis] = DegToS16(deg);
                edited = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("-5")) {
                pose.rot[sel][axis] = DegToS16(deg - 5.0f);
                edited = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("+5")) {
                pose.rot[sel][axis] = DegToS16(deg + 5.0f);
                edited = true;
            }
            ImGui::PopID();
        }
    }
    if (ImGui::Button("Reset this limb")) {
        for (int axis = 0; axis < 3; axis++) {
            pose.rot[sel][axis] = sState.base.rot[sel][axis];
        }
        edited = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset whole pose")) {
        pose = sState.base;
        for (int i = 0; i < kLimbCount; i++) {
            pose.enabled[i] = true;
        }
        edited = true;
    }
    if (edited) {
        // While playing, editing pauses so the slider does not fight the playback.
        sState.playing = false;
        if (sState.autoKey || onKey) {
            UpsertKey(pose);
        } else {
            // No key here and auto key is off: nothing is stored, the change would be lost on the next tick.
            sStatus = "Turn on Auto key or press \"Add key here\" to keep your changes.";
        }
    }
    ImGui::EndChild();

    ImGui::Separator();

    DrawFileControls();
}

} // namespace OcarinaStudio
