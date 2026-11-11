#pragma once

/*
 * OcarinaStudio - internal declarations shared by the feature modules.
 * This header must not include any game (C) headers; use OSGame.h for that.
 */

#include <cstdint>
#include <string>

namespace OcarinaStudio {

namespace Cvar {
// Stage / machinima
inline constexpr const char* HideHud = "gOcarinaStudio.HideHud";
inline constexpr const char* HideMessages = "gOcarinaStudio.HideMessages";
inline constexpr const char* StageMode = "gOcarinaStudio.StageMode";
inline constexpr const char* StageR = "gOcarinaStudio.StageColor.R";
inline constexpr const char* StageG = "gOcarinaStudio.StageColor.G";
inline constexpr const char* StageB = "gOcarinaStudio.StageColor.B";
// Camera: 0 = game camera, 1 = frozen, 2 = free camera
inline constexpr const char* CameraMode = "gOcarinaStudio.Camera.Mode";
inline constexpr const char* CameraSpeed = "gOcarinaStudio.Camera.Speed";
inline constexpr const char* CameraSensitivity = "gOcarinaStudio.Camera.Sensitivity";
inline constexpr const char* CameraFov = "gOcarinaStudio.Camera.Fov";
inline constexpr const char* CameraRoll = "gOcarinaStudio.Camera.Roll";
// Eyes
inline constexpr const char* ForceEye = "gOcarinaStudio.Eyes.Force";
inline constexpr const char* ForceMouth = "gOcarinaStudio.Eyes.ForceMouth";
inline constexpr const char* EyeAssignments = "gOcarinaStudio.Eyes.Assignments";
// Models
inline constexpr const char* EnabledModels = "gOcarinaStudio.Models.Enabled";
} // namespace Cvar

// Returns <app dir>/OcarinaStudio/<subfolder> (creating it if needed).
std::string DataDir(const std::string& subfolder);

// Keeps letters, digits, space, '-' and '_' so a user supplied name is safe as a file name.
std::string SanitizeName(const std::string& in);

// Small ImGui + CVar helpers. They return true when the value changed (and was saved).
bool CheckboxCvar(const char* label, const char* cvar, bool defaultValue = false);
bool SliderFloatCvar(const char* label, const char* cvar, float min, float max, float defaultValue,
                     const char* format = "%.2f");
bool SliderIntCvar(const char* label, const char* cvar, int min, int max, int defaultValue);

// Feature tabs (each lives in its own source file).
void DrawStageTab();
void DrawCameraTab();
void DrawLinkTab();
void DrawEyesTab();
void DrawModelsTab();
void DrawPoseTab();
void DrawScenesTab();
void DrawGameMenuItems();   // world list for the Game menu
void DrawStageQuickItems(); // quick toggles for the Stage menu
void DrawCameraQuickItems();// quick toggles for the Camera menu

// Camera
void RecaptureCamera();

// Per-tick work (called from game hooks).
void OnGameTick();
void ApplyEyeOverride();
void ApplyPose();
void MountEnabledModels();
void ReapplyEyeAssignments();
void OnSceneInit();
void PoseOnSceneInit();

// Scenes
bool WarpToEntrance(int entranceIndex);
bool ReloadCurrentScene();

} // namespace OcarinaStudio
