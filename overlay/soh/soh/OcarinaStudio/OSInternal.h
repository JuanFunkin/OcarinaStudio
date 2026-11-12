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
inline constexpr const char* BlockWarps = "gOcarinaStudio.BlockWarps";
inline constexpr const char* StageLockLink = "gOcarinaStudio.StageLockLink"; // Link stands still on the stage
inline constexpr const char* MuteStage = "gOcarinaStudio.MuteStage";         // no music / voices on the stage
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

// Feature windows (each lives in its own source file).
void DrawStageTab();
void DrawCameraTab();
void DrawLinkTab();
void DrawEyesTab();
void DrawModelsTab();
void DrawScenesTab();   // warp by entrance index
void DrawTexturesTab(); // export textures to PNG, import edited PNGs, build a mod .o2r
void DrawWorldsTab();   // every world, pick one and press Warp
void DrawSettingsTab();
void DrawFpsMenuItems();
void DrawStageQuickItems();
void DrawCameraQuickItems();

// Animator (OSPose.cpp)
void DrawAnimatorLayout();            // right panel + timeline + mixtape, drawn while the animator is on
bool IsAnimatorActive();
void SetAnimatorActive(bool active);
bool IsAnimatorMode();                // started through OcarinaAnimator (OCARINA_ANIMATOR is set)
void SetViewMargins(float right, float bottom, float aspect); // keeps Link out from under the panels

// Worlds
void WarpFromMenu(int entrance);      // turns the green screen off and warps
void SetLinkAge(int age);             // 0 = adult, 1 = child (reloads the world)
int GetLinkAge();
inline constexpr int kStageEntrance = 0x0BB; // Link's House: the empty green screen stage

// Camera
void RecaptureCamera();

// Per-tick work (called from game hooks).
void OnGameTick();
void ApplyEyeOverride();
void ApplyPose();            // once per tick, at the end of Player_Update
void PrePlayerDraw(void* player); // right before Link is drawn
void MountEnabledModels();
void ReapplyEyeAssignments();
void OnSceneInit();
void PoseOnSceneInit();
void StageOnSceneInit();
void BlockStrayTransitions();

// Scenes
bool WarpToEntrance(int entranceIndex);
bool ReloadCurrentScene();

} // namespace OcarinaStudio
