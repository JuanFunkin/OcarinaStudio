#include "soh/OcarinaStudio/OcarinaStudio.h"
#include "soh/OcarinaStudio/OSInternal.h"

#include <algorithm>
#include <cmath>

#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>

/*
 * Machinima camera.
 *
 * The game camera keeps running (so cutscenes, targeting, etc. stay consistent). OcarinaStudio only
 * replaces the *output* handed to the view, which means:
 *   - Frozen: the view stays exactly where it was when the camera was frozen.
 *   - Free:   the view is driven by the keys / mouse below.
 *
 * Free camera controls:
 *   I / K  forward / back        J / L  strafe left / right
 *   U / O  down / up             Hold right mouse button and drag to look around
 *   Shift  faster                Ctrl   slower
 */

namespace {

struct CameraState {
    bool captured = false;
    float pos[3] = { 0.0f, 0.0f, 0.0f };
    float yaw = 0.0f;
    float pitch = 0.0f;
    float fov = 60.0f;
    bool rightMouseWasDown = false;
    float lastMouse[2] = { 0.0f, 0.0f };
};

CameraState sCam;

constexpr float kMaxPitch = 1.55f; // just under 90 degrees
constexpr float kLookDistance = 100.0f;

void Forward(float yaw, float pitch, float out[3]) {
    out[0] = sinf(yaw) * cosf(pitch);
    out[1] = sinf(pitch);
    out[2] = cosf(yaw) * cosf(pitch);
}

void Normalize(float v[3]) {
    float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 0.00001f) {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
}

void Cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

void Capture(const float* eye, const float* at, float fovy) {
    float dir[3] = { at[0] - eye[0], at[1] - eye[1], at[2] - eye[2] };
    float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    sCam.pos[0] = eye[0];
    sCam.pos[1] = eye[1];
    sCam.pos[2] = eye[2];
    if (len > 0.00001f) {
        sCam.yaw = atan2f(dir[0], dir[2]);
        sCam.pitch = std::clamp(asinf(dir[1] / len), -kMaxPitch, kMaxPitch);
    }
    sCam.fov = fovy;
    sCam.captured = true;
}

void UpdateFreeCamera() {
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();

    // --- Look: right mouse button drag. We compare absolute positions between game ticks because
    // the game updates less often than the screen is redrawn.
    const float sensitivity = CVarGetFloat(OcarinaStudio::Cvar::CameraSensitivity, 0.004f);
    bool rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right) && !io.WantCaptureMouse;
    ImVec2 mouse = ImGui::GetMousePos();
    if (rightDown && sCam.rightMouseWasDown) {
        float dx = mouse.x - sCam.lastMouse[0];
        float dy = mouse.y - sCam.lastMouse[1];
        sCam.yaw -= dx * sensitivity;
        sCam.pitch = std::clamp(sCam.pitch - dy * sensitivity, -kMaxPitch, kMaxPitch);
    }
    sCam.rightMouseWasDown = rightDown;
    sCam.lastMouse[0] = mouse.x;
    sCam.lastMouse[1] = mouse.y;

    // --- Move
    if (io.WantCaptureKeyboard) {
        return;
    }
    float speed = CVarGetFloat(OcarinaStudio::Cvar::CameraSpeed, 12.0f);
    if (io.KeyShift) {
        speed *= 4.0f;
    }
    if (io.KeyCtrl) {
        speed *= 0.25f;
    }

    float fwd[3];
    Forward(sCam.yaw, sCam.pitch, fwd);
    const float worldUp[3] = { 0.0f, 1.0f, 0.0f };
    float right[3];
    Cross(fwd, worldUp, right);
    right[1] = 0.0f;
    Normalize(right);

    auto move = [&](const float dir[3], float amount) {
        sCam.pos[0] += dir[0] * amount;
        sCam.pos[1] += dir[1] * amount;
        sCam.pos[2] += dir[2] * amount;
    };

    if (ImGui::IsKeyDown(ImGuiKey_I)) {
        move(fwd, speed);
    }
    if (ImGui::IsKeyDown(ImGuiKey_K)) {
        move(fwd, -speed);
    }
    if (ImGui::IsKeyDown(ImGuiKey_L)) {
        move(right, speed);
    }
    if (ImGui::IsKeyDown(ImGuiKey_J)) {
        move(right, -speed);
    }
    if (ImGui::IsKeyDown(ImGuiKey_O)) {
        sCam.pos[1] += speed;
    }
    if (ImGui::IsKeyDown(ImGuiKey_U)) {
        sCam.pos[1] -= speed;
    }
}

} // namespace

extern "C" void OcarinaStudio_OverrideView(struct PlayState* play, float* eye, float* at, float* up, float* fovy) {
    (void)play;
    const int mode = CVarGetInteger(OcarinaStudio::Cvar::CameraMode, 0);
    if (mode == 0) {
        sCam.captured = false;
        sCam.rightMouseWasDown = false;
        return;
    }

    if (!sCam.captured) {
        Capture(eye, at, *fovy);
    }

    if (mode == 2) {
        UpdateFreeCamera();
    }

    float fwd[3];
    Forward(sCam.yaw, sCam.pitch, fwd);

    eye[0] = sCam.pos[0];
    eye[1] = sCam.pos[1];
    eye[2] = sCam.pos[2];
    at[0] = sCam.pos[0] + fwd[0] * kLookDistance;
    at[1] = sCam.pos[1] + fwd[1] * kLookDistance;
    at[2] = sCam.pos[2] + fwd[2] * kLookDistance;

    // Up vector with optional roll.
    const float worldUp[3] = { 0.0f, 1.0f, 0.0f };
    float right[3];
    Cross(fwd, worldUp, right);
    Normalize(right);
    float trueUp[3];
    Cross(right, fwd, trueUp);
    Normalize(trueUp);
    const float roll = CVarGetFloat(OcarinaStudio::Cvar::CameraRoll, 0.0f) * 0.01745329252f;
    const float c = cosf(roll);
    const float s = sinf(roll);
    up[0] = trueUp[0] * c + right[0] * s;
    up[1] = trueUp[1] * c + right[1] * s;
    up[2] = trueUp[2] * c + right[2] * s;

    *fovy = CVarGetFloat(OcarinaStudio::Cvar::CameraFov, sCam.fov);
}

namespace OcarinaStudio {

void RecaptureCamera() {
    sCam.captured = false;
}

void DrawCameraQuickItems() {
    const int mode = CVarGetInteger(Cvar::CameraMode, 0);
    const char* names[3] = { "Game camera", "Frozen camera", "Free camera" };
    for (int i = 0; i < 3; i++) {
        if (ImGui::MenuItem(names[i], nullptr, mode == i)) {
            CVarSetInteger(Cvar::CameraMode, i);
            CVarSave();
        }
    }
}

void DrawCameraTab() {
    int mode = CVarGetInteger(Cvar::CameraMode, 0);
    bool changed = false;
    changed |= ImGui::RadioButton("Game camera", &mode, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Frozen", &mode, 1);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Free camera", &mode, 2);
    if (changed) {
        CVarSetInteger(Cvar::CameraMode, mode);
        CVarSave();
    }

    ImGui::TextWrapped("Frozen keeps the current shot. Free camera lets you fly around: I/K forward and back, "
                       "J/L strafe, U/O down and up, hold the right mouse button to look. Shift is faster, "
                       "Ctrl is slower.");
    ImGui::Separator();

    SliderFloatCvar("Move speed", Cvar::CameraSpeed, 1.0f, 60.0f, 12.0f, "%.1f");
    SliderFloatCvar("Look sensitivity", Cvar::CameraSensitivity, 0.001f, 0.015f, 0.004f, "%.4f");
    SliderFloatCvar("Roll (degrees)", Cvar::CameraRoll, -180.0f, 180.0f, 0.0f, "%.0f");
    SliderFloatCvar("Field of view", Cvar::CameraFov, 10.0f, 120.0f, 60.0f, "%.0f");

    if (ImGui::Button("Reset FOV and roll")) {
        CVarClear(Cvar::CameraFov);
        CVarClear(Cvar::CameraRoll);
        CVarSave();
    }
    ImGui::SameLine();
    if (ImGui::Button("Re-capture game camera")) {
        RecaptureCamera();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Snap the frozen / free camera back to where the game camera currently is.");
    }
}

} // namespace OcarinaStudio
