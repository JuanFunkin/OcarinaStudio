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

/*
 * Pose and animation editor for Link.
 *
 * Link's body is a skeleton of 21 limbs. The game fills `skelAnime.jointTable` every tick from the
 * current animation; right after that, at the end of Player_Update, we overwrite the limbs the user
 * is editing, so the edit is what gets drawn.
 *
 *   jointTable[0]          root translation (x, y, z)
 *   jointTable[limb - 1]   rotation of that limb (PLAYER_LIMB_WAIST == 2 -> jointTable[1])
 *
 * Rotations are s16 angles (65536 = 360 degrees).
 *
 * A pose can be saved to / loaded from <app dir>/OcarinaStudio/poses/*.json and a keyframe animation
 * from <app dir>/OcarinaStudio/animations/*.json. Animations play at the game rate (20 ticks/second).
 */

namespace {

constexpr int kLimbCount = 21; // PLAYER_LIMB_ROOT .. PLAYER_LIMB_TORSO
constexpr float kDegToS16 = 65536.0f / 360.0f;

struct LimbInfo {
    const char* name;
    int limb; // PLAYER_LIMB_* value
};

// Index in this table == slot in Pose::rot.
const LimbInfo kLimbs[kLimbCount] = {
    { "Root (position offset)", 1 }, { "Waist", 2 },       { "Lower body", 3 },  { "Right thigh", 4 },
    { "Right shin", 5 },             { "Right foot", 6 },  { "Left thigh", 7 },  { "Left shin", 8 },
    { "Left foot", 9 },              { "Upper body", 10 }, { "Head", 11 },       { "Hat", 12 },
    { "Collar", 13 },                { "Left shoulder", 14 }, { "Left forearm", 15 }, { "Left hand", 16 },
    { "Right shoulder", 17 },        { "Right forearm", 18 }, { "Right hand", 19 }, { "Sheath", 20 },
    { "Torso", 21 },
};

struct Pose {
    int16_t rot[kLimbCount][3] = {};
    bool enabled[kLimbCount] = {};
};

struct Keyframe {
    int frame = 0; // game ticks from the start of the animation
    Pose pose;
};

enum class Mode { Off, HoldPose, PlayAnimation };

struct PoseState {
    Mode mode = Mode::Off;
    Pose pose; // the pose being edited / held
    bool lockMovement = false;

    std::vector<Keyframe> keys;
    bool playing = false;
    bool loop = true;
    float speed = 1.0f;
    float time = 0.0f; // current playback time, in ticks
} sState;

std::string sStatus;

int16_t DegToS16(float deg) {
    return static_cast<int16_t>(static_cast<int32_t>(std::lround(deg * kDegToS16)));
}

float S16ToDeg(int16_t value) {
    return static_cast<float>(value) / kDegToS16;
}

// Interpolates two angles along the shortest path (s16 subtraction wraps correctly).
int16_t LerpAngle(int16_t a, int16_t b, float t) {
    const int16_t diff = static_cast<int16_t>(b - a);
    return static_cast<int16_t>(a + static_cast<int16_t>(std::lround(diff * t)));
}

Pose Blend(const Pose& a, const Pose& b, float t) {
    Pose out = a;
    for (int i = 0; i < kLimbCount; i++) {
        for (int axis = 0; axis < 3; axis++) {
            if (i == 0) {
                // Root translation is a position, not an angle.
                out.rot[i][axis] = static_cast<int16_t>(std::lround(a.rot[i][axis] + (b.rot[i][axis] - a.rot[i][axis]) * t));
            } else {
                out.rot[i][axis] = LerpAngle(a.rot[i][axis], b.rot[i][axis], t);
            }
        }
    }
    return out;
}

float AnimationLength() {
    return sState.keys.empty() ? 0.0f : static_cast<float>(sState.keys.back().frame);
}

void SortKeys() {
    std::stable_sort(sState.keys.begin(), sState.keys.end(),
                     [](const Keyframe& a, const Keyframe& b) { return a.frame < b.frame; });
}

// The pose the animation has at the given time.
Pose SampleAnimation(float time) {
    const auto& keys = sState.keys;
    if (keys.empty()) {
        return sState.pose;
    }
    if (time <= keys.front().frame || keys.size() == 1) {
        return keys.front().pose;
    }
    if (time >= keys.back().frame) {
        return keys.back().pose;
    }
    for (size_t i = 0; i + 1 < keys.size(); i++) {
        const Keyframe& a = keys[i];
        const Keyframe& b = keys[i + 1];
        if (time >= a.frame && time <= b.frame) {
            const float span = static_cast<float>(b.frame - a.frame);
            const float t = span > 0.0f ? (time - a.frame) / span : 0.0f;
            return Blend(a.pose, b.pose, t);
        }
    }
    return keys.back().pose;
}

// Copies the pose onto the player's joints (only the limbs marked as enabled).
void WritePose(Player* player, const Pose& pose) {
    Vec3s* joints = player->skelAnime.jointTable;
    for (int i = 0; i < kLimbCount; i++) {
        if (!pose.enabled[i]) {
            continue;
        }
        joints[i].x = pose.rot[i][0];
        joints[i].y = pose.rot[i][1];
        joints[i].z = pose.rot[i][2];
    }
}

// Reads the joints the game is using right now into a pose with every limb enabled.
Pose ReadCurrentPose(Player* player) {
    Pose pose;
    Vec3s* joints = player->skelAnime.jointTable;
    for (int i = 0; i < kLimbCount; i++) {
        pose.rot[i][0] = joints[i].x;
        pose.rot[i][1] = joints[i].y;
        pose.rot[i][2] = joints[i].z;
        pose.enabled[i] = true;
    }
    return pose;
}

// ---- JSON -------------------------------------------------------------------------------------

nlohmann::json PoseToJson(const Pose& pose) {
    nlohmann::json j;
    for (int i = 0; i < kLimbCount; i++) {
        j["limbs"].push_back({ { "enabled", pose.enabled[i] }, { "rot", { pose.rot[i][0], pose.rot[i][1], pose.rot[i][2] } } });
    }
    return j;
}

bool PoseFromJson(const nlohmann::json& j, Pose& out) {
    if (!j.is_object() || !j.contains("limbs") || !j["limbs"].is_array() || j["limbs"].size() != kLimbCount) {
        return false;
    }
    for (int i = 0; i < kLimbCount; i++) {
        const auto& limb = j["limbs"][i];
        if (!limb.is_object() || !limb.contains("rot") || !limb["rot"].is_array() || limb["rot"].size() != 3) {
            return false;
        }
        out.enabled[i] = limb.value("enabled", true);
        for (int axis = 0; axis < 3; axis++) {
            out.rot[i][axis] = static_cast<int16_t>(limb["rot"][axis].get<int>());
        }
    }
    return true;
}

std::string PoseDir() {
    return OcarinaStudio::DataDir("poses");
}

std::string AnimDir() {
    return OcarinaStudio::DataDir("animations");
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

bool SavePose(const std::string& name) {
    std::ofstream file(std::filesystem::path(PoseDir()) / (name + ".json"));
    if (!file.is_open()) {
        return false;
    }
    file << PoseToJson(sState.pose).dump(2);
    return true;
}

bool LoadPose(const std::string& name) {
    std::ifstream file(std::filesystem::path(PoseDir()) / (name + ".json"));
    if (!file.is_open()) {
        return false;
    }
    nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
    Pose pose;
    if (j.is_discarded() || !PoseFromJson(j, pose)) {
        return false;
    }
    sState.pose = pose;
    return true;
}

bool SaveAnimation(const std::string& name) {
    nlohmann::json j;
    j["loop"] = sState.loop;
    j["keyframes"] = nlohmann::json::array();
    for (const Keyframe& key : sState.keys) {
        nlohmann::json k = PoseToJson(key.pose);
        k["frame"] = key.frame;
        j["keyframes"].push_back(k);
    }
    std::ofstream file(std::filesystem::path(AnimDir()) / (name + ".json"));
    if (!file.is_open()) {
        return false;
    }
    file << j.dump(2);
    return true;
}

bool LoadAnimation(const std::string& name) {
    std::ifstream file(std::filesystem::path(AnimDir()) / (name + ".json"));
    if (!file.is_open()) {
        return false;
    }
    nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("keyframes") || !j["keyframes"].is_array()) {
        return false;
    }
    std::vector<Keyframe> keys;
    for (const auto& k : j["keyframes"]) {
        Keyframe key;
        if (!PoseFromJson(k, key.pose)) {
            return false;
        }
        key.frame = std::max(0, k.value("frame", 0));
        keys.push_back(key);
    }
    sState.keys = keys;
    sState.loop = j.value("loop", true);
    sState.time = 0.0f;
    SortKeys();
    return true;
}

Player* CurrentPlayer() {
    return gPlayState != nullptr ? GET_PLAYER(gPlayState) : nullptr;
}

} // namespace

namespace OcarinaStudio {

// Runs once per game tick, right after Player_Update.
void ApplyPose() {
    Player* player = CurrentPlayer();
    if (player == nullptr || sState.mode == Mode::Off || player->skelAnime.jointTable == nullptr) {
        return;
    }

    if (sState.lockMovement) {
        player->linearVelocity = 0.0f;
        player->actor.speedXZ = 0.0f;
        player->actor.velocity.x = 0.0f;
        player->actor.velocity.z = 0.0f;
    }

    if (sState.mode == Mode::HoldPose) {
        WritePose(player, sState.pose);
        return;
    }

    // PlayAnimation
    if (sState.keys.empty()) {
        return;
    }
    if (sState.playing) {
        sState.time += sState.speed;
        const float length = AnimationLength();
        if (sState.time > length) {
            if (sState.loop && length > 0.0f) {
                sState.time = std::fmod(sState.time, length);
            } else {
                sState.time = length;
                sState.playing = false;
            }
        }
        if (sState.time < 0.0f) {
            sState.time = sState.loop && length > 0.0f ? length + std::fmod(sState.time, length) : 0.0f;
        }
    }
    WritePose(player, SampleAnimation(sState.time));
}

void DrawPoseTab() {
    Player* player = CurrentPlayer();
    if (player == nullptr) {
        ImGui::TextDisabled("Load a save file first.");
    }

    // ---- Mode ----
    int mode = static_cast<int>(sState.mode);
    bool changed = false;
    changed |= ImGui::RadioButton("Off (game animation)", &mode, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Hold pose", &mode, 1);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Play animation", &mode, 2);
    if (changed) {
        sState.mode = static_cast<Mode>(mode);
    }
    ImGui::Checkbox("Lock Link in place", &sState.lockMovement);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Stops Link's movement while a pose or animation is active.");
    }

    ImGui::Separator();

    // ---- Pose editor ----
    ImGui::Text("Pose");
    ImGui::BeginDisabled(player == nullptr);
    if (ImGui::Button("Capture current pose")) {
        sState.pose = ReadCurrentPose(player);
        sStatus = "Captured Link's current pose.";
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Reset pose")) {
        sState.pose = Pose();
    }
    ImGui::SameLine();
    if (ImGui::Button("Enable all")) {
        std::fill(std::begin(sState.pose.enabled), std::end(sState.pose.enabled), true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Disable all")) {
        std::fill(std::begin(sState.pose.enabled), std::end(sState.pose.enabled), false);
    }
    ImGui::TextDisabled("Tick a limb to take control of it. Unticked limbs keep playing the game's animation.");

    ImGui::BeginChild("limbs", ImVec2(0, 240), true);
    for (int i = 0; i < kLimbCount; i++) {
        ImGui::PushID(i);
        ImGui::Checkbox("##on", &sState.pose.enabled[i]);
        ImGui::SameLine();
        if (ImGui::TreeNode(kLimbs[i].name)) {
            if (i == 0) {
                // Root position offsets are raw units (about 1/100 of a game unit per step).
                int pos[3] = { sState.pose.rot[0][0], sState.pose.rot[0][1], sState.pose.rot[0][2] };
                if (ImGui::DragInt3("Position", pos, 4.0f, -32768, 32767)) {
                    for (int axis = 0; axis < 3; axis++) {
                        sState.pose.rot[0][axis] = static_cast<int16_t>(pos[axis]);
                    }
                    sState.pose.enabled[0] = true;
                }
            } else {
                static const char* kAxes[3] = { "X", "Y", "Z" };
                for (int axis = 0; axis < 3; axis++) {
                    float deg = S16ToDeg(sState.pose.rot[i][axis]);
                    if (ImGui::SliderFloat(kAxes[axis], &deg, -180.0f, 180.0f, "%.1f deg")) {
                        sState.pose.rot[i][axis] = DegToS16(deg);
                        sState.pose.enabled[i] = true;
                    }
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    // ---- Save / load pose ----
    static char sPoseName[64] = "";
    static std::vector<std::string> sPoses = ListJson(PoseDir());
    ImGui::InputText("Pose name", sPoseName, sizeof(sPoseName));
    ImGui::SameLine();
    if (ImGui::Button("Save pose")) {
        std::string name = SanitizeName(sPoseName);
        if (name.empty()) {
            sStatus = "Type a pose name first.";
        } else if (SavePose(name)) {
            sStatus = "Saved pose \"" + name + "\".";
            sPoses = ListJson(PoseDir());
        } else {
            sStatus = "Could not write the pose file.";
        }
    }
    if (ImGui::BeginCombo("Saved poses", "Load a pose...")) {
        for (const std::string& name : sPoses) {
            if (ImGui::Selectable(name.c_str())) {
                sStatus = LoadPose(name) ? "Loaded pose \"" + name + "\"." : "Could not read \"" + name + "\".";
                sState.mode = Mode::HoldPose;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();

    // ---- Animation ----
    ImGui::Text("Animation (20 ticks = 1 second)");
    if (ImGui::Button("Add keyframe from pose")) {
        Keyframe key;
        key.pose = sState.pose;
        key.frame = sState.keys.empty() ? 0 : sState.keys.back().frame + 20;
        sState.keys.push_back(key);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear keyframes")) {
        sState.keys.clear();
        sState.playing = false;
        sState.time = 0.0f;
    }

    int removeIndex = -1;
    bool resort = false;
    ImGui::BeginChild("keys", ImVec2(0, 120), true);
    for (size_t i = 0; i < sState.keys.size(); i++) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::InputInt("##frame", &sState.keys[i].frame, 1, 10)) {
            sState.keys[i].frame = std::max(0, sState.keys[i].frame);
            resort = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Use")) {
            sState.pose = sState.keys[i].pose;
            sState.mode = Mode::HoldPose;
        }
        ImGui::SameLine();
        if (ImGui::Button("Update")) {
            sState.keys[i].pose = sState.pose;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            removeIndex = static_cast<int>(i);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (removeIndex >= 0) {
        sState.keys.erase(sState.keys.begin() + removeIndex);
    }
    if (resort) {
        SortKeys();
    }

    const float length = AnimationLength();
    if (sState.keys.size() < 2) {
        ImGui::TextDisabled("Add at least two keyframes to make an animation.");
    }
    ImGui::BeginDisabled(sState.keys.size() < 2);
    if (ImGui::Button(sState.playing ? "Pause" : "Play")) {
        sState.playing = !sState.playing;
        sState.mode = Mode::PlayAnimation;
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        sState.playing = false;
        sState.time = 0.0f;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &sState.loop);
    ImGui::SliderFloat("Speed", &sState.speed, 0.1f, 3.0f, "%.2fx");
    if (ImGui::SliderFloat("Time", &sState.time, 0.0f, std::max(length, 1.0f), "%.1f ticks")) {
        sState.mode = Mode::PlayAnimation;
    }
    ImGui::EndDisabled();

    // ---- Save / load animation ----
    static char sAnimName[64] = "";
    static std::vector<std::string> sAnims = ListJson(AnimDir());
    ImGui::InputText("Animation name", sAnimName, sizeof(sAnimName));
    ImGui::SameLine();
    if (ImGui::Button("Save animation")) {
        std::string name = SanitizeName(sAnimName);
        if (name.empty()) {
            sStatus = "Type an animation name first.";
        } else if (sState.keys.empty()) {
            sStatus = "There are no keyframes to save.";
        } else if (SaveAnimation(name)) {
            sStatus = "Saved animation \"" + name + "\".";
            sAnims = ListJson(AnimDir());
        } else {
            sStatus = "Could not write the animation file.";
        }
    }
    if (ImGui::BeginCombo("Saved animations", "Load an animation...")) {
        for (const std::string& name : sAnims) {
            if (ImGui::Selectable(name.c_str())) {
                sStatus = LoadAnimation(name) ? "Loaded animation \"" + name + "\"." : "Could not read \"" + name + "\".";
                sState.mode = Mode::PlayAnimation;
            }
        }
        ImGui::EndCombo();
    }

    if (!sStatus.empty()) {
        ImGui::TextWrapped("%s", sStatus.c_str());
    }
}

} // namespace OcarinaStudio
