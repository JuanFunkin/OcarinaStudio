#include "soh/OcarinaStudio/OSInternal.h"
#include "soh/OcarinaStudio/OSGame.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "soh/OcarinaStudio/OSPoseMath.h"

extern "C" {
#include "functions.h"
}
extern "C" char** ResourceMgr_ListFiles(const char* searchMask, int* resultSize);

/*
 * Animator: poses and keyframe animations for Link, laid out like Saturn Studio.
 *
 * While the animator is on, Link is frozen on a "base" pose (his standing pose, captured when it starts) and
 * every limb is driven by the animator, so none of the game's own animation shows. The pose is written right
 * BEFORE Link is drawn (the game reloads his animation after the actors update, which would erase it
 * otherwise). His controller input is disabled and the game's head / upper body "look at" rotation is cleared.
 *
 * Every limb has its own track: the diamond next to a value keys that limb at the current frame, and
 * dragging a value keys it too (auto key). Between two keys the value is interpolated.
 *
 * Player jointTable slots: slot 0 is the root position, slot N is the rotation of PLAYER_LIMB_N, so
 * slot 1 is the ROOT limb (its yaw turns the whole actor), slot 2 the waist, ... slot 21 the torso.
 * Rotations are s16 angles (65536 = 360 degrees).
 *
 * Poses go to <app dir>/OcarinaStudio/poses/*.json, animations to .../animations/*.json (20 ticks per second).
 */

namespace {

using namespace OcarinaStudio::PoseMath;

struct LimbInfo {
    const char* name;
    const char* group;
};

// Index in this table == slot in Player::skelAnime.jointTable == slot in Pose::rot.
const LimbInfo kLimbs[kLimbCount] = {
    { "Position (hips)", "Body" },    { "Body turn", "Body" },          { "Waist", "Body" },
    { "Lower body", "Body" },         { "Right thigh", "Right leg" },   { "Right shin", "Right leg" },
    { "Right foot", "Right leg" },    { "Left thigh", "Left leg" },     { "Left shin", "Left leg" },
    { "Left foot", "Left leg" },      { "Upper body", "Body" },         { "Head", "Head" },
    { "Hat", "Head" },                { "Collar", "Left arm" },         { "Left shoulder", "Left arm" },
    { "Left forearm", "Left arm" },   { "Left hand", "Left arm" },      { "Right shoulder", "Right arm" },
    { "Right forearm", "Right arm" }, { "Right hand", "Right arm" },    { "Sheath", "Items" },
    { "Torso", "Body" },
};

// Order shown in the panel and in the timeline.
const int kDisplayOrder[kLimbCount] = { 0, 1, 2, 3, 10, 21, 11, 12, 13, 14, 15, 16, 17, 18, 19, 7, 8, 9, 4, 5, 6, 20 };

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
    int selected = 11;       // selected limb (head)
} sState;

std::string sStatus;
bool sMixtapeOpen = false;

// The joints the game computed for the current tick, copied right before we overwrite them at draw time.
int16_t sGameJoints[kLimbCount][3] = {};
bool sSnapshotTaken = false; // taken this tick already
bool sHaveSnapshot = false;  // at least one snapshot exists since the scene loaded

// ---- Animation Mixtape: Link's original animations, baked into keyframes ---------------------------

struct Mixtape {
    std::vector<std::string> names;  // full archive paths (without the __OTR__ prefix)
    bool loaded = false;
    char filter[64] = "";
    int selected = -1;
    int step = 2;                    // one keyframe every `step` frames when baking
    bool baking = false;
    bool playWhenDone = false;
    int ticks = 0;
    int endFrame = 0;
    std::string path;                // "__OTR__..." path of the animation being played (must outlive the SkelAnime)
    std::vector<Keyframe> recorded;

    std::vector<std::string> custom; // saved animations (animations/*.json)
    bool customLoaded = false;
    int customSelected = -1;
} sMix;

constexpr const char* kAnimPrefix = "objects/gameplay_keep/gPlayerAnim_";

void LoadMixtapeList() {
    sMix.loaded = true;
    sMix.names.clear();
    int count = 0;
    char** files = ResourceMgr_ListFiles("objects/gameplay_keep/gPlayerAnim_*", &count);
    if (files == nullptr) {
        return;
    }
    for (int i = 0; i < count; i++) {
        sMix.names.emplace_back(files[i]);
        std::free(files[i]);
    }
    std::free(files);
    std::sort(sMix.names.begin(), sMix.names.end());
}

std::string ShortAnimName(const std::string& path) {
    return path.size() > std::strlen(kAnimPrefix) ? path.substr(std::strlen(kAnimPrefix)) : path;
}

bool ContainsNoCase(const std::string& text, const char* filter) {
    std::string a = text, b = filter;
    std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return std::tolower(c); });
    std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return std::tolower(c); });
    return a.find(b) != std::string::npos;
}

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

bool LimbKeyedAt(int limb, int frame) {
    const int index = KeyIndexAt(frame);
    return index >= 0 && sState.keys[index].pose.enabled[limb];
}

// The pose shown at `time`: the animation sampled there, with the base pose under every limb that has no key.
Pose PoseAt(float time) {
    Pose out = sState.base;
    const Pose sampled = Sample(sState.keys, time, sState.easing);
    for (int i = 0; i < kLimbCount; i++) {
        out.enabled[i] = true;
        if (sampled.enabled[i]) {
            for (int axis = 0; axis < 3; axis++) {
                out.rot[i][axis] = sampled.rot[i][axis];
            }
        }
    }
    return out;
}

// Keys `limb` at the current frame with the given values.
void SetLimbKey(int limb, const int16_t rot[3]) {
    const int frame = CurrentFrame();
    int index = KeyIndexAt(frame);
    if (index < 0) {
        Keyframe key;
        key.frame = frame;
        sState.keys.push_back(key);
        SortKeys(sState.keys);
        index = KeyIndexAt(frame);
    }
    Pose& pose = sState.keys[index].pose;
    pose.enabled[limb] = true;
    for (int axis = 0; axis < 3; axis++) {
        pose.rot[limb][axis] = rot[axis];
    }
}

void PruneEmptyKeys() {
    sState.keys.erase(std::remove_if(sState.keys.begin(), sState.keys.end(),
                                     [](const Keyframe& key) {
                                         for (int i = 0; i < kLimbCount; i++) {
                                             if (key.pose.enabled[i]) {
                                                 return false;
                                             }
                                         }
                                         return true;
                                     }),
                      sState.keys.end());
}

void RemoveLimbKey(int limb) {
    const int index = KeyIndexAt(CurrentFrame());
    if (index >= 0) {
        sState.keys[index].pose.enabled[limb] = false;
        PruneEmptyKeys();
    }
}

// Keys every limb at the current frame with what is shown now.
void KeyAllLimbs() {
    const Pose shown = PoseAt(sState.time);
    for (int i = 0; i < kLimbCount; i++) {
        SetLimbKey(i, shown.rot[i]);
    }
}

Pose PoseFromSnapshot() {
    Pose pose;
    for (int i = 0; i < kLimbCount; i++) {
        pose.enabled[i] = true;
        for (int axis = 0; axis < 3; axis++) {
            pose.rot[i][axis] = sGameJoints[i][axis];
        }
    }
    pose.rot[1][1] = 0; // the game already moved this into the actor's facing
    return pose;
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

// ---- baking a game animation ------------------------------------------------------------------

void StartBake(Player* player, const std::string& fullPath, bool playWhenDone) {
    if (player == nullptr || gPlayState == nullptr) {
        return;
    }
    sMix.path = "__OTR__" + fullPath;
    auto* animation = reinterpret_cast<LinkAnimationHeader*>(const_cast<char*>(sMix.path.c_str()));
    sMix.endFrame = static_cast<int>(Animation_GetLastFrame(animation));
    LinkAnimation_Change(gPlayState, &player->skelAnime, animation, 1.0f, 0.0f, static_cast<f32>(sMix.endFrame),
                         ANIMMODE_ONCE, 0.0f);
    sMix.baking = true;
    sMix.playWhenDone = playWhenDone;
    sMix.ticks = 0;
    sMix.recorded.clear();
    sState.playing = false;
}

// Called every tick while baking. The joints snapshot we read at this point is the frame the game loaded last
// tick, so the first tick records nothing.
void BakeTick() {
    const int frame = sMix.ticks - 1;
    sMix.ticks++;
    if (frame >= 0 && (frame % std::max(sMix.step, 1) == 0 || frame >= sMix.endFrame)) {
        Keyframe key;
        key.frame = frame;
        key.pose = PoseFromSnapshot();
        sMix.recorded.push_back(key);
    }
    if (frame >= sMix.endFrame || sMix.ticks > sMix.endFrame + 12) {
        sMix.baking = false;
        if (!sMix.recorded.empty()) {
            sState.keys = sMix.recorded;
            SortKeys(sState.keys);
            sState.length = std::max(sMix.endFrame, 1);
            sState.time = 0.0f;
            sState.playing = sMix.playWhenDone;
            sStatus = "Loaded " + std::to_string(sState.keys.size()) + " keyframes.";
        }
    }
}

} // namespace

namespace OcarinaStudio {

bool IsAnimatorActive() {
    return sState.active;
}

void SetAnimatorActive(bool active) {
    sState.active = active;
    sState.playing = false;
    if (active) {
        sState.haveBase = false;
        sState.captureDelay = 12;
        // Frozen camera so Link stays framed (the panels shift the view, see SetViewMargins).
        if (CVarGetInteger(Cvar::CameraMode, 0) == 0) {
            CVarSetInteger(Cvar::CameraMode, 1);
        }
    } else {
        SetViewMargins(0.0f, 0.0f, 1.0f);
    }
}

void PoseOnSceneInit() {
    // A new Player object exists after a scene change: capture the standing pose again.
    sState.haveBase = false;
    sState.captureDelay = 12;
    sHaveSnapshot = false;
    sSnapshotTaken = false;
    sMix.baking = false;
}

// Once per game tick, at the end of Player_Update.
void ApplyPose() {
    sSnapshotTaken = false; // the next draw takes a fresh copy of the game's joints

    Player* player = CurrentPlayer();
    if (player == nullptr || player->skelAnime.jointTable == nullptr) {
        return;
    }

    // OcarinaAnimator: start as soon as Link exists.
    static bool sAutoStarted = false;
    if (!sAutoStarted && IsAnimatorMode()) {
        sAutoStarted = true;
        SetAnimatorActive(true);
    }

    // On the green screen stage Link stands still (the stage has invisible walls).
    const bool stageLock = CVarGetInteger(Cvar::StageMode, 0) != 0 && CVarGetInteger(Cvar::StageLockLink, 1) != 0;
    if (!sState.active) {
        SetLock(player, stageLock);
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
        if (!sHaveSnapshot) {
            return;
        }
        sState.base = PoseFromSnapshot();
        sState.baseYaw = player->actor.shape.rot.y;
        sState.haveBase = true;
    }

    if (sMix.baking) {
        BakeTick();
        return;
    }

    if (sState.playing) {
        sState.time =
            AdvanceTime(sState.time, sState.speed, static_cast<float>(TotalLength()), sState.loop, sState.playing);
    }

    // The body turn (slot 1) is the game's "yaw from animation": apply it to the actor's facing here.
    const int16_t facing = static_cast<int16_t>(sState.baseYaw + PoseAt(sState.time).rot[1][1]);
    player->actor.shape.rot.y = facing;
    player->actor.world.rot.y = facing;
    player->yaw = facing;
}

// Right before Link is drawn: the game has already reloaded his animation for this tick.
void PrePlayerDraw(void* playerPointer) {
    Player* player = static_cast<Player*>(playerPointer);
    if (gPlayState != nullptr && player == GET_PLAYER(gPlayState)) {
        ApplyEyeOverride(); // the face is chosen from the joint table at draw time
    }
    if (!sState.active || gPlayState == nullptr || player != GET_PLAYER(gPlayState)) {
        return;
    }
    Vec3s* joints = player->skelAnime.jointTable;
    if (joints == nullptr) {
        return;
    }
    if (!sSnapshotTaken) {
        for (int i = 0; i < kLimbCount; i++) {
            sGameJoints[i][0] = joints[i].x;
            sGameJoints[i][1] = joints[i].y;
            sGameJoints[i][2] = joints[i].z;
        }
        sSnapshotTaken = true;
        sHaveSnapshot = true;
    }
    if (!sState.haveBase || sMix.baking) {
        return;
    }
    const Pose pose = PoseAt(sState.time);
    for (int i = 0; i < kLimbCount; i++) {
        joints[i].x = pose.rot[i][0];
        joints[i].y = pose.rot[i][1];
        joints[i].z = pose.rot[i][2];
    }
    joints[1].y = 0; // already applied to the actor's facing
}

} // namespace OcarinaStudio

namespace {

// ---- widgets ----------------------------------------------------------------------------------

// A small diamond button. Filled yellow = this limb has a keyframe at the current frame.
bool DiamondButton(const char* id, bool filled) {
    const float size = 16.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float cx = pos.x + size * 0.5f;
    const float cy = pos.y + size * 0.5f;
    const float r = size * 0.38f;
    const ImVec2 a(cx, cy - r), b(cx + r, cy), c(cx, cy + r), d(cx - r, cy);
    const ImU32 color = ImGui::IsItemHovered() ? IM_COL32(255, 230, 120, 255) : IM_COL32(255, 200, 40, 255);
    if (filled) {
        draw->AddQuadFilled(a, b, c, d, color);
    } else {
        draw->AddQuad(a, b, c, d, IM_COL32(170, 170, 180, 255), 1.5f);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", filled ? "Remove this limb's keyframe at this frame" : "Add a keyframe for this limb here");
    }
    return clicked;
}

// Track grid: one row per limb with a diamond on every keyframe of that limb, a ruler and the playhead.
void DrawTrackGrid() {
    const float labelW = 120.0f;
    const float rulerH = 18.0f;
    const float rowH = 17.0f;
    const float width = std::max(ImGui::GetContentRegionAvail().x, 300.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float x0 = origin.x + labelW;
    const float trackW = width - labelW - 12.0f;
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
    const int step = total > 200 ? 20 : (total > 80 ? 10 : 5);
    for (int f = 0; f <= total; f += step) {
        const float x = x0 + trackW * static_cast<float>(f) / static_cast<float>(total);
        draw->AddLine(ImVec2(x, origin.y + 8.0f), ImVec2(x, bottom), IM_COL32(70, 70, 78, 255));
        char num[16];
        std::snprintf(num, sizeof(num), "%d", f);
        draw->AddText(ImVec2(x + 2.0f, origin.y), IM_COL32(190, 190, 200, 255), num);
    }
    for (int n = 0; n < kLimbCount; n++) {
        const int limb = kDisplayOrder[n];
        const float y = origin.y + rulerH + rowH * static_cast<float>(n);
        const bool selected = limb == sState.selected;
        const int shade = n % 2 ? 34 : 28;
        draw->AddRectFilled(ImVec2(origin.x, y), ImVec2(x0 + trackW, y + rowH - 1.0f),
                            selected ? IM_COL32(60, 70, 100, 255) : IM_COL32(shade, shade, 38, 255));
        draw->AddText(ImVec2(origin.x + 4.0f, y + 2.0f), IM_COL32(225, 225, 230, 255), kLimbs[limb].name);
        for (const Keyframe& key : sState.keys) {
            if (!key.pose.enabled[limb]) {
                continue;
            }
            const float x = x0 + trackW * static_cast<float>(key.frame) / static_cast<float>(total);
            const float cy = y + rowH * 0.5f;
            draw->AddQuadFilled(ImVec2(x, cy - 5), ImVec2(x + 5, cy), ImVec2(x, cy + 5), ImVec2(x - 5, cy),
                                IM_COL32(255, 200, 40, 255));
        }
    }
    const float px = x0 + trackW * std::clamp(sState.time / static_cast<float>(total), 0.0f, 1.0f);
    draw->AddLine(ImVec2(px, origin.y), ImVec2(px, bottom), IM_COL32(255, 60, 60, 255), 2.0f);
}

// One row per limb: name, keyframe diamond, three numeric fields (degrees; the first row is the hips position).
void DrawValueRows() {
    const Pose shown = PoseAt(sState.time);
    const int frame = CurrentFrame();
    for (int n = 0; n < kLimbCount; n++) {
        const int limb = kDisplayOrder[n];
        ImGui::PushID(limb);
        const bool keyed = LimbKeyedAt(limb, frame);

        if (limb == sState.selected) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.35f, 1.0f));
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(kLimbs[limb].name);
        if (limb == sState.selected) {
            ImGui::PopStyleColor();
        }
        ImGui::SameLine(118.0f);

        if (DiamondButton("##key", keyed)) {
            sState.selected = limb;
            sState.playing = false;
            if (keyed) {
                RemoveLimbKey(limb);
            } else {
                SetLimbKey(limb, shown.rot[limb]);
            }
        }
        ImGui::SameLine();

        float values[3];
        for (int axis = 0; axis < 3; axis++) {
            values[axis] = limb == 0 ? static_cast<float>(shown.rot[0][axis]) : S16ToDeg(shown.rot[limb][axis]);
        }
        ImGui::SetNextItemWidth(205.0f);
        if (ImGui::DragFloat3("##v", values, limb == 0 ? 4.0f : 0.5f, 0.0f, 0.0f, limb == 0 ? "%.0f" : "%.1f")) {
            int16_t rot[3];
            for (int axis = 0; axis < 3; axis++) {
                rot[axis] = limb == 0 ? static_cast<int16_t>(std::clamp(values[axis], -32768.0f, 32767.0f))
                                      : DegToS16(values[axis]);
            }
            sState.selected = limb;
            sState.playing = false;
            SetLimbKey(limb, rot); // auto key
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("R")) {
            sState.selected = limb;
            sState.playing = false;
            SetLimbKey(limb, sState.base.rot[limb]);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reset this limb to the standing pose (keys it)");
        }
        ImGui::PopID();
    }
}

void DrawFileControls() {
    static char sPoseName[64] = "";
    static std::vector<std::string> sPoses = ListJson(OcarinaStudio::DataDir("poses"));
    ImGui::SeparatorText("Poses");
    ImGui::SetNextItemWidth(150.0f);
    ImGui::InputText("##posename", sPoseName, sizeof(sPoseName));
    ImGui::SameLine();
    if (ImGui::Button("Save pose")) {
        const std::string name = OcarinaStudio::SanitizeName(sPoseName);
        if (name.empty()) {
            sStatus = "Type a pose name first.";
        } else if (SavePose(name, PoseAt(sState.time))) {
            sStatus = "Saved pose \"" + name + "\".";
            sPoses = ListJson(OcarinaStudio::DataDir("poses"));
        } else {
            sStatus = "Could not write the pose file.";
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::BeginCombo("##loadpose", "Load pose here...")) {
        for (const std::string& name : sPoses) {
            if (ImGui::Selectable(name.c_str())) {
                Pose fromFile;
                if (LoadPose(name, fromFile)) {
                    for (int i = 0; i < kLimbCount; i++) {
                        if (fromFile.enabled[i]) {
                            SetLimbKey(i, fromFile.rot[i]);
                        }
                    }
                    sStatus = "Loaded pose \"" + name + "\" at frame " + std::to_string(CurrentFrame()) + ".";
                } else {
                    sStatus = "Could not read \"" + name + "\".";
                }
            }
        }
        ImGui::EndCombo();
    }

    static char sAnimName[64] = "";
    ImGui::SeparatorText("Animations");
    ImGui::SetNextItemWidth(150.0f);
    ImGui::InputText("##animname", sAnimName, sizeof(sAnimName));
    ImGui::SameLine();
    if (ImGui::Button("Save animation")) {
        const std::string name = OcarinaStudio::SanitizeName(sAnimName);
        if (name.empty()) {
            sStatus = "Type an animation name first.";
        } else if (sState.keys.empty()) {
            sStatus = "There are no keyframes to save.";
        } else if (SaveAnimation(name)) {
            sStatus = "Saved animation \"" + name + "\".";
            sMix.customLoaded = false;
        } else {
            sStatus = "Could not write the animation file.";
        }
    }
    if (!sStatus.empty()) {
        ImGui::TextWrapped("%s", sStatus.c_str());
    }
}

// Play / pause, frame, keyframe buttons, loop, speed, easing.
void DrawTransportControls() {
    if (ImGui::Button(sState.playing ? " Pause " : "  Play  ")) {
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
    ImGui::SameLine();
    int frame = CurrentFrame();
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::InputInt("Frame", &frame, 1, 5)) {
        sState.time = static_cast<float>(std::clamp(frame, 0, TotalLength()));
        sState.playing = false;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::InputInt("Length", &sState.length, 5, 20);
    sState.length = std::clamp(sState.length, 1, 6000);
    ImGui::SameLine();
    ImGui::TextDisabled("%.2f s", static_cast<float>(CurrentFrame()) / static_cast<float>(kTicksPerSecond));

    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("Speed", &sState.speed, 0.1f, 3.0f, "%.2fx");
    ImGui::SameLine();
    int easing = static_cast<int>(sState.easing);
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::Combo("Easing", &easing, "Linear\0Smooth\0")) {
        sState.easing = static_cast<Easing>(easing);
    }
    ImGui::SameLine();
    if (ImGui::Button("Key all limbs")) {
        KeyAllLimbs();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Add a keyframe for every limb at this frame");
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete keys here")) {
        const int index = KeyIndexAt(CurrentFrame());
        if (index >= 0) {
            sState.keys.erase(sState.keys.begin() + index);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear all")) {
        sState.keys.clear();
        sState.time = 0.0f;
        sState.playing = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Mixtape")) {
        sMixtapeOpen = !sMixtapeOpen;
    }
    ImGui::SameLine();
    if (ImGui::Button("Close animator")) {
        OcarinaStudio::SetAnimatorActive(false);
    }
}

// Like Saturn's Animation Mixtape: the game's own animations, and your saved ones.
void DrawMixtapeWindow() {
    if (!sMixtapeOpen) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(12.0f, ImGui::GetFrameHeight() + 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330.0f, std::min(480.0f, io.DisplaySize.y - 300.0f)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Animation Mixtape", &sMixtapeOpen)) {
        ImGui::End();
        return;
    }
    const bool busy = sMix.baking || !sState.haveBase || CurrentPlayer() == nullptr;
    if (ImGui::BeginTabBar("mixtabs")) {
        if (ImGui::BeginTabItem("OoT")) {
            if (!sMix.loaded) {
                LoadMixtapeList();
            }
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##mixfilter", "Search...", sMix.filter, sizeof(sMix.filter));
            ImGui::BeginChild("mixlist", ImVec2(0, -80.0f), true);
            for (int i = 0; i < static_cast<int>(sMix.names.size()); i++) {
                if (sMix.filter[0] != '\0' && !ContainsNoCase(sMix.names[i], sMix.filter)) {
                    continue;
                }
                ImGui::PushID(i);
                if (ImGui::Selectable(ShortAnimName(sMix.names[i]).c_str(), sMix.selected == i,
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    sMix.selected = i;
                    if (ImGui::IsMouseDoubleClicked(0) && !busy) {
                        StartBake(CurrentPlayer(), sMix.names[i], true);
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::BeginDisabled(busy || sMix.selected < 0);
            if (ImGui::Button("Play")) {
                StartBake(CurrentPlayer(), sMix.names[sMix.selected], true);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Checkbox("Loop##mix", &sState.loop);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            ImGui::SliderFloat("Speed##mix", &sState.speed, 0.1f, 3.0f, "%.2fx");
            const char* kSteps[] = { "Keys: every frame", "Keys: every 2 frames", "Keys: every 4 frames" };
            int stepIndex = sMix.step == 1 ? 0 : (sMix.step == 2 ? 1 : 2);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##mixstep", &stepIndex, kSteps, 3)) {
                sMix.step = stepIndex == 0 ? 1 : (stepIndex == 2 ? 4 : 2);
            }
            if (sMix.baking) {
                ImGui::TextDisabled("Loading %d / %d...", std::max(sMix.ticks - 1, 0), sMix.endFrame);
            } else {
                ImGui::TextDisabled("Double-click or Play. The animation becomes keyframes you can edit.");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Custom")) {
            if (!sMix.customLoaded) {
                sMix.custom = ListJson(OcarinaStudio::DataDir("animations"));
                sMix.customLoaded = true;
            }
            if (ImGui::Button("Refresh")) {
                sMix.customLoaded = false;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%zu animations", sMix.custom.size());
            ImGui::BeginChild("customlist", ImVec2(0, -80.0f), true);
            if (sMix.custom.empty()) {
                ImGui::TextWrapped("No animations yet. Make one with the timeline and press Save animation, or drop "
                                   ".json files into:\n%s",
                                   OcarinaStudio::DataDir("animations").c_str());
            }
            for (int i = 0; i < static_cast<int>(sMix.custom.size()); i++) {
                ImGui::PushID(i);
                if (ImGui::Selectable(sMix.custom[i].c_str(), sMix.customSelected == i,
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    sMix.customSelected = i;
                    if (ImGui::IsMouseDoubleClicked(0) && LoadAnimation(sMix.custom[i])) {
                        sState.playing = true;
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::BeginDisabled(sMix.customSelected < 0);
            if (ImGui::Button("Play##custom")) {
                if (LoadAnimation(sMix.custom[sMix.customSelected])) {
                    sState.playing = true;
                    sStatus = "Loaded \"" + sMix.custom[sMix.customSelected] + "\".";
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Checkbox("Loop##custom", &sState.loop);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            ImGui::SliderFloat("Speed##custom", &sState.speed, 0.1f, 3.0f, "%.2fx");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

} // namespace

namespace OcarinaStudio {

// Saturn Studio style: values on the right, the timeline across the bottom. The game view is shifted so Link
// stays centered in the part of the screen that is left free.
void DrawAnimatorLayout() {
    if (!sState.active) {
        SetViewMargins(0.0f, 0.0f, 1.0f);
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const float menuH = ImGui::GetFrameHeight();
    const float timelineH = std::clamp(io.DisplaySize.y * 0.30f, 210.0f, 300.0f);
    const float panelW = 390.0f;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

    SetViewMargins(panelW / io.DisplaySize.x, timelineH / io.DisplaySize.y, io.DisplaySize.x / io.DisplaySize.y);

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - panelW, menuH));
    ImGui::SetNextWindowSize(ImVec2(panelW, io.DisplaySize.y - menuH - timelineH));
    if (ImGui::Begin("Pose", nullptr, flags)) {
        if (!sState.haveBase) {
            ImGui::TextDisabled("Settling Link into his standing pose...");
        } else {
            ImGui::TextDisabled("Drag a value, or press the diamond, to key it at the current frame.");
            DrawValueRows();
            DrawFileControls();
        }
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(0.0f, io.DisplaySize.y - timelineH));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - panelW, timelineH));
    if (ImGui::Begin("Timeline", nullptr, flags)) {
        if (sState.haveBase) {
            DrawTransportControls();
            ImGui::BeginChild("tracks", ImVec2(0, 0), false);
            DrawTrackGrid();
            ImGui::EndChild();
        }
    }
    ImGui::End();

    DrawMixtapeWindow();
}

} // namespace OcarinaStudio
