#pragma once

/*
 * OcarinaStudio - pose and animation math.
 *
 * Header-only and free of ImGui / game dependencies on purpose, so it can be unit tested on its own
 * (see tests/). The editor in OSPose.cpp is a thin UI layer on top of this.
 *
 * Link's body has 21 limbs. Slot 0 is the root *position offset*; slots 1..20 are limb rotations.
 * Rotations are s16 angles (65536 == 360 degrees).
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace OcarinaStudio::PoseMath {

constexpr int kLimbCount = 21;
constexpr float kDegToS16 = 65536.0f / 360.0f;
constexpr int kTicksPerSecond = 20; // the game logic runs at 20 Hz

struct Pose {
    int16_t rot[kLimbCount][3] = {};
    bool enabled[kLimbCount] = {};
};

struct Keyframe {
    int frame = 0; // game ticks from the start of the animation
    Pose pose;
};

enum class Easing { Linear = 0, Smooth = 1 };

inline int16_t DegToS16(float deg) {
    const long v = std::lround(deg * kDegToS16);
    return static_cast<int16_t>(std::clamp<long>(v, -32768, 32767));
}

inline float S16ToDeg(int16_t value) {
    return static_cast<float>(value) / kDegToS16;
}

// Interpolates two angles along the shortest path (s16 subtraction wraps correctly).
inline int16_t LerpAngle(int16_t a, int16_t b, float t) {
    const int16_t diff = static_cast<int16_t>(b - a);
    return static_cast<int16_t>(a + static_cast<int16_t>(std::lround(diff * t)));
}

// Plain linear interpolation, used for the root position (which is not an angle).
inline int16_t LerpValue(int16_t a, int16_t b, float t) {
    const long v = std::lround(a + (b - a) * t);
    return static_cast<int16_t>(std::clamp<long>(v, -32768, 32767));
}

inline float ApplyEasing(Easing easing, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return easing == Easing::Smooth ? t * t * (3.0f - 2.0f * t) : t;
}

/*
 * Blends two poses.
 *   t <= 0 gives exactly `a`, t >= 1 gives exactly `b`.
 *   Limbs ticked in both poses are interpolated.
 *   A limb ticked in only one of them keeps that pose's value for the whole segment, so a limb that is
 *   not part of the next keyframe never swings towards zero.
 */
inline Pose Blend(const Pose& a, const Pose& b, float t) {
    if (t <= 0.0f) {
        return a;
    }
    if (t >= 1.0f) {
        return b;
    }
    Pose out;
    for (int i = 0; i < kLimbCount; i++) {
        if (a.enabled[i] && b.enabled[i]) {
            out.enabled[i] = true;
            for (int axis = 0; axis < 3; axis++) {
                out.rot[i][axis] = i == 0 ? LerpValue(a.rot[i][axis], b.rot[i][axis], t)
                                          : LerpAngle(a.rot[i][axis], b.rot[i][axis], t);
            }
        } else if (a.enabled[i] || b.enabled[i]) {
            const Pose& src = a.enabled[i] ? a : b;
            out.enabled[i] = true;
            for (int axis = 0; axis < 3; axis++) {
                out.rot[i][axis] = src.rot[i][axis];
            }
        }
    }
    return out;
}

inline void SortKeys(std::vector<Keyframe>& keys) {
    std::stable_sort(keys.begin(), keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.frame < b.frame; });
}

// Length of the animation in ticks (the frame of the last keyframe). Keys must be sorted.
inline float AnimationLength(const std::vector<Keyframe>& keys) {
    return keys.empty() ? 0.0f : static_cast<float>(keys.back().frame);
}

// The pose the animation has at `time` (in ticks). Keys must be sorted. Requires at least one key.
inline Pose Sample(const std::vector<Keyframe>& keys, float time, Easing easing) {
    if (keys.size() == 1 || time <= static_cast<float>(keys.front().frame)) {
        return keys.front().pose;
    }
    for (size_t i = 0; i + 1 < keys.size(); i++) {
        const Keyframe& a = keys[i];
        const Keyframe& b = keys[i + 1];
        if (time < static_cast<float>(b.frame)) {
            if (time <= static_cast<float>(a.frame)) {
                return a.pose;
            }
            const float t = (time - static_cast<float>(a.frame)) / static_cast<float>(b.frame - a.frame);
            return Blend(a.pose, b.pose, ApplyEasing(easing, t));
        }
    }
    return keys.back().pose;
}

// Moves the playback time forward by `speed` ticks, looping or stopping at the end.
inline float AdvanceTime(float time, float speed, float length, bool loop, bool& playing) {
    if (length <= 0.0f) {
        playing = false;
        return 0.0f;
    }
    time += speed;
    if (time > length) {
        if (loop) {
            time = std::fmod(time, length);
        } else {
            time = length;
            playing = false;
        }
    } else if (time < 0.0f) {
        if (loop) {
            time = length + std::fmod(time, length);
        } else {
            time = 0.0f;
            playing = false;
        }
    }
    return time;
}

// ---- JSON ---------------------------------------------------------------------------------------

inline nlohmann::json PoseToJson(const Pose& pose) {
    nlohmann::json j;
    j["limbs"] = nlohmann::json::array();
    for (int i = 0; i < kLimbCount; i++) {
        j["limbs"].push_back(
            { { "enabled", pose.enabled[i] }, { "rot", { pose.rot[i][0], pose.rot[i][1], pose.rot[i][2] } } });
    }
    return j;
}

// Returns false (and leaves `out` untouched) if the JSON is not a valid pose.
inline bool PoseFromJson(const nlohmann::json& j, Pose& out) {
    try {
        if (!j.is_object() || !j.contains("limbs") || !j["limbs"].is_array() || j["limbs"].size() != kLimbCount) {
            return false;
        }
        Pose pose;
        for (int i = 0; i < kLimbCount; i++) {
            const auto& limb = j["limbs"][i];
            if (!limb.is_object() || !limb.contains("rot") || !limb["rot"].is_array() || limb["rot"].size() != 3) {
                return false;
            }
            pose.enabled[i] = limb.value("enabled", true);
            for (int axis = 0; axis < 3; axis++) {
                pose.rot[i][axis] = static_cast<int16_t>(std::clamp(limb["rot"][axis].get<int>(), -32768, 32767));
            }
        }
        out = pose;
        return true;
    } catch (...) {
        return false;
    }
}

inline nlohmann::json AnimationToJson(const std::vector<Keyframe>& keys, bool loop, Easing easing) {
    nlohmann::json j;
    j["version"] = 1;
    j["loop"] = loop;
    j["easing"] = easing == Easing::Smooth ? "smooth" : "linear";
    j["keyframes"] = nlohmann::json::array();
    for (const Keyframe& key : keys) {
        nlohmann::json k = PoseToJson(key.pose);
        k["frame"] = key.frame;
        j["keyframes"].push_back(k);
    }
    return j;
}

// Returns false (and leaves the outputs untouched) if the JSON is not a valid animation.
inline bool AnimationFromJson(const nlohmann::json& j, std::vector<Keyframe>& keys, bool& loop, Easing& easing) {
    try {
        if (!j.is_object() || !j.contains("keyframes") || !j["keyframes"].is_array()) {
            return false;
        }
        std::vector<Keyframe> parsed;
        for (const auto& k : j["keyframes"]) {
            Keyframe key;
            if (!PoseFromJson(k, key.pose)) {
                return false;
            }
            key.frame = std::max(0, k.value("frame", 0));
            parsed.push_back(key);
        }
        SortKeys(parsed);
        keys = std::move(parsed);
        loop = j.value("loop", true);
        easing = j.value("easing", std::string("linear")) == "smooth" ? Easing::Smooth : Easing::Linear;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace OcarinaStudio::PoseMath
