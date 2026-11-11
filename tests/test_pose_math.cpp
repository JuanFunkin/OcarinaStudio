// Standalone unit tests for OSPoseMath.h.
// Build: g++ -std=c++17 -I<nlohmann include dir> -I../overlay/soh/soh/OcarinaStudio test_pose_math.cpp -o test_pose_math
#include "OSPoseMath.h"
#include <cstdio>

using namespace OcarinaStudio::PoseMath;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static Pose MakePose(int limb, int16_t x) {
    Pose p; p.enabled[limb] = true; p.rot[limb][0] = x; return p;
}

int main() {
    CHECK(DegToS16(180.0f) == -32768 || DegToS16(180.0f) == 32767);
    CHECK(std::abs(S16ToDeg(DegToS16(90.0f)) - 90.0f) < 0.01f);
    // shortest path across the wrap point
    CHECK(LerpAngle(32000, -32000, 0.5f) > 32000 || LerpAngle(32000, -32000, 0.5f) < -32000);
    CHECK(LerpAngle(0, 1000, 0.5f) == 500);
    // blend endpoints
    Pose a = MakePose(3, 0), b = MakePose(3, 1000);
    CHECK(Blend(a, b, 0.0f).rot[3][0] == 0);
    CHECK(Blend(a, b, 1.0f).rot[3][0] == 1000);
    CHECK(Blend(a, b, 0.5f).rot[3][0] == 500);
    // limb only in one pose is kept, not swung to zero
    Pose c = MakePose(4, 700);
    Pose m = Blend(a, c, 0.5f);
    CHECK(m.enabled[3] && m.rot[3][0] == 0 && m.enabled[4] && m.rot[4][0] == 700);
    // root is linear, not wrapped
    Pose r1 = MakePose(0, -30000), r2 = MakePose(0, 30000);
    CHECK(Blend(r1, r2, 0.5f).rot[0][0] == 0);
    // easing
    CHECK(ApplyEasing(Easing::Smooth, 0.5f) == 0.5f);
    CHECK(ApplyEasing(Easing::Smooth, 0.25f) < 0.25f);
    CHECK(ApplyEasing(Easing::Linear, 2.0f) == 1.0f);
    // sampling
    std::vector<Keyframe> keys(3);
    keys[0].frame = 0; keys[0].pose = MakePose(3, 0);
    keys[1].frame = 10; keys[1].pose = MakePose(3, 1000);
    keys[2].frame = 30; keys[2].pose = MakePose(3, 0);
    CHECK(Sample(keys, -5, Easing::Linear).rot[3][0] == 0);
    CHECK(Sample(keys, 5, Easing::Linear).rot[3][0] == 500);
    CHECK(Sample(keys, 10, Easing::Linear).rot[3][0] == 1000);
    CHECK(Sample(keys, 20, Easing::Linear).rot[3][0] == 500);
    CHECK(Sample(keys, 99, Easing::Linear).rot[3][0] == 0);
    // time advance
    bool playing = true;
    CHECK(AdvanceTime(25, 10, 30, true, playing) == 5 && playing);
    CHECK(AdvanceTime(25, 10, 30, false, playing) == 30 && !playing);
    playing = true;
    CHECK(AdvanceTime(2, -5, 30, true, playing) == 27 && playing);
    CHECK(AdvanceTime(0, 1, 0, true, playing) == 0 && !playing);
    // json round trip
    std::vector<Keyframe> out; bool loop = false; Easing e = Easing::Linear;
    CHECK(AnimationFromJson(AnimationToJson(keys, true, Easing::Smooth), out, loop, e));
    CHECK(out.size() == 3 && loop && e == Easing::Smooth && out[1].frame == 10 && out[1].pose.rot[3][0] == 1000);
    // unsorted input gets sorted; invalid input rejected without touching outputs
    nlohmann::json j = AnimationToJson(keys, true, Easing::Linear);
    std::swap(j["keyframes"][0], j["keyframes"][2]);
    CHECK(AnimationFromJson(j, out, loop, e) && out[0].frame == 0 && out[2].frame == 30);
    size_t n = out.size();
    CHECK(!AnimationFromJson(nlohmann::json::parse("{\"keyframes\":[{\"limbs\":[]}]}"), out, loop, e) && out.size() == n);
    Pose pp;
    CHECK(!PoseFromJson(nlohmann::json::parse("[1,2]"), pp));
    CHECK(PoseFromJson(PoseToJson(MakePose(5, -123)), pp) && pp.rot[5][0] == -123 && pp.enabled[5] && !pp.enabled[6]);

    std::printf(failures ? "%d FAILED\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}
