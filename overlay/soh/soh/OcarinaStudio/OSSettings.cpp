#include "soh/OcarinaStudio/OSInternal.h"
#include "soh/OcarinaStudio/OSGame.h"

#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/cvar_prefixes.h"
#include <ship/Context.h>
#include <ship/window/Window.h>

/*
 * Settings window: frame rate, audio and a few graphics options, like Saturn 64's Settings window.
 *
 * OoT's logic runs at 20 ticks per second. The FPS setting only controls the interpolated picture, so
 * 30 or 60 FPS is purely visual (the same setting Ship of Harkinian has).
 */

namespace {

void SetFps(int fps) {
    CVarSetInteger(CVAR_SETTING("InterpolationFPS"), fps);
    CVarSave();
}

// A 0..100 percent volume slider stored in a CVar. `player` < 0 means "no per-player volume to update".
void VolumeSlider(const char* label, const char* cvar, int defaultValue, int player) {
    int value = CVarGetInteger(cvar, defaultValue);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::SliderInt(label, &value, 0, 100, "%d %%")) {
        CVarSetInteger(cvar, value);
        if (player >= 0) {
            Audio_SetGameVolume(player, static_cast<float>(value) / 100.0f);
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        CVarSave();
    }
}

} // namespace

namespace OcarinaStudio {

void DrawFpsMenuItems() {
    const int fps = CVarGetInteger(CVAR_SETTING("InterpolationFPS"), 30);
    const int presets[] = { 20, 30, 60, 120 };
    for (int preset : presets) {
        char label[32];
        std::snprintf(label, sizeof(label), preset == 20 ? "%d FPS (original)" : "%d FPS", preset);
        if (ImGui::MenuItem(label, nullptr, fps == preset)) {
            SetFps(preset);
        }
    }
}

void DrawSettingsTab() {
    ImGui::SeparatorText("Frame rate");
    int fps = CVarGetInteger(CVAR_SETTING("InterpolationFPS"), 30);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::SliderInt("FPS", &fps, 20, 240)) {
        CVarSetInteger(CVAR_SETTING("InterpolationFPS"), fps);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        CVarSave();
    }
    if (ImGui::Button("20")) {
        SetFps(20);
    }
    ImGui::SameLine();
    if (ImGui::Button("30")) {
        SetFps(30);
    }
    ImGui::SameLine();
    if (ImGui::Button("60")) {
        SetFps(60);
    }
    ImGui::TextDisabled("The game logic always runs at 20 ticks per second; this only smooths the picture.");

    bool match = CVarGetInteger(CVAR_SETTING("MatchRefreshRate"), 0) != 0;
    if (ImGui::Checkbox("Match monitor refresh rate", &match)) {
        CVarSetInteger(CVAR_SETTING("MatchRefreshRate"), match ? 1 : 0);
        CVarSave();
    }

    ImGui::SeparatorText("Audio");
    VolumeSlider("Master", CVAR_SETTING("Volume.Master"), 40, -1);
    VolumeSlider("Music", CVAR_SETTING("Volume.MainMusic"), 100, SEQ_PLAYER_BGM_MAIN);
    VolumeSlider("Sub music", CVAR_SETTING("Volume.SubMusic"), 100, SEQ_PLAYER_BGM_SUB);
    VolumeSlider("Fanfares", CVAR_SETTING("Volume.Fanfare"), 100, SEQ_PLAYER_FANFARE);
    VolumeSlider("Sound effects", CVAR_SETTING("Volume.SFX"), 100, SEQ_PLAYER_SFX);

    ImGui::SeparatorText("Window");
    if (ImGui::Button("Toggle fullscreen")) {
        Ship::Context::GetRawInstance()->GetWindow()->ToggleFullscreen();
    }
    bool vsync = CVarGetInteger(CVAR_SETTING("VsyncEnabled"), 1) != 0;
    if (ImGui::Checkbox("VSync", &vsync)) {
        CVarSetInteger(CVAR_SETTING("VsyncEnabled"), vsync ? 1 : 0);
        CVarSave();
    }
}

} // namespace OcarinaStudio
