#include "soh/OcarinaStudio/OcarinaStudioWindow.h"
#include "soh/OcarinaStudio/OSInternal.h"

#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>

namespace {

enum ToolId { kStage, kCamera, kLink, kEyes, kModels, kWorlds, kEntrance, kSettings, kTextures, kToolCount };

struct Tool {
    const char* title;
    void (*draw)();
    bool open;
    ImVec2 size;
};

Tool sTools[kToolCount] = {
    { "Stage", OcarinaStudio::DrawStageTab, false, ImVec2(420, 380) },
    { "Camera options", OcarinaStudio::DrawCameraTab, false, ImVec2(420, 420) },
    { "Customize Link", OcarinaStudio::DrawLinkTab, false, ImVec2(420, 460) },
    { "Custom eyes", OcarinaStudio::DrawEyesTab, false, ImVec2(440, 480) },
    { "Models", OcarinaStudio::DrawModelsTab, false, ImVec2(420, 360) },
    { "Game - Worlds", OcarinaStudio::DrawWorldsTab, false, ImVec2(380, 520) },
    { "Warp to entrance", OcarinaStudio::DrawScenesTab, false, ImVec2(380, 320) },
    { "Settings", OcarinaStudio::DrawSettingsTab, false, ImVec2(340, 420) },
    { "Texture tool", OcarinaStudio::DrawTexturesTab, false, ImVec2(520, 700) },
};

void ChromaKeyMenu() {
    bool on = CVarGetInteger(OcarinaStudio::Cvar::StageMode, 0) != 0;
    if (ImGui::MenuItem("Green screen", nullptr, &on)) {
        CVarSetInteger(OcarinaStudio::Cvar::StageMode, on ? 1 : 0);
        CVarSave();
    }
    int rgb[3] = { CVarGetInteger(OcarinaStudio::Cvar::StageR, 0), CVarGetInteger(OcarinaStudio::Cvar::StageG, 255),
                   CVarGetInteger(OcarinaStudio::Cvar::StageB, 0) };
    float color[3] = { rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f };
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::ColorEdit3("Color", color)) {
        CVarSetInteger(OcarinaStudio::Cvar::StageR, static_cast<int>(color[0] * 255.0f + 0.5f));
        CVarSetInteger(OcarinaStudio::Cvar::StageG, static_cast<int>(color[1] * 255.0f + 0.5f));
        CVarSetInteger(OcarinaStudio::Cvar::StageB, static_cast<int>(color[2] * 255.0f + 0.5f));
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        CVarSave();
    }
    ImGui::Separator();
    bool hud = CVarGetInteger(OcarinaStudio::Cvar::HideHud, 0) != 0;
    if (ImGui::MenuItem("Hide game HUD", nullptr, &hud)) {
        CVarSetInteger(OcarinaStudio::Cvar::HideHud, hud ? 1 : 0);
        CVarSave();
    }
    bool msgs = CVarGetInteger(OcarinaStudio::Cvar::HideMessages, 0) != 0;
    if (ImGui::MenuItem("Hide message boxes", nullptr, &msgs)) {
        CVarSetInteger(OcarinaStudio::Cvar::HideMessages, msgs ? 1 : 0);
        CVarSave();
    }
    bool lock = CVarGetInteger(OcarinaStudio::Cvar::StageLockLink, 0) != 0;
    if (ImGui::MenuItem("Lock Link on the stage", nullptr, &lock)) {
        CVarSetInteger(OcarinaStudio::Cvar::StageLockLink, lock ? 1 : 0);
        CVarSave();
    }
    bool mute = CVarGetInteger(OcarinaStudio::Cvar::MuteStage, 0) != 0;
    if (ImGui::MenuItem("Mute music and voices", nullptr, &mute)) {
        CVarSetInteger(OcarinaStudio::Cvar::MuteStage, mute ? 1 : 0);
        CVarSave();
    }
    bool block = CVarGetInteger(OcarinaStudio::Cvar::BlockWarps, 1) != 0;
    if (ImGui::MenuItem("Block exits on the stage", nullptr, &block)) {
        CVarSetInteger(OcarinaStudio::Cvar::BlockWarps, block ? 1 : 0);
        CVarSave();
    }
}

void CameraMenu() {
    OcarinaStudio::DrawCameraQuickItems();
    ImGui::Separator();
    OcarinaStudio::SliderFloatCvar("FOV", OcarinaStudio::Cvar::CameraFov, 10.0f, 120.0f, 60.0f, "%.0f");
    OcarinaStudio::SliderFloatCvar("Move speed", OcarinaStudio::Cvar::CameraSpeed, 1.0f, 60.0f, 12.0f, "%.1f");
    OcarinaStudio::SliderFloatCvar("Rotate speed", OcarinaStudio::Cvar::CameraSensitivity, 0.001f, 0.015f, 0.004f,
                                   "%.4f");
    ImGui::Separator();
    ImGui::MenuItem("More camera options...", nullptr, &sTools[kCamera].open);
}

} // namespace

void OcarinaStudioWindow::InitElement() {
}

void OcarinaStudioWindow::DrawElement() {
}

// Replaces GuiWindow::Draw: there is no outer window, only the menu bar and the tool windows.
void OcarinaStudioWindow::Draw() {
    if (!IsVisible()) {
        return;
    }

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Menu")) {
            ImGui::TextDisabled("F10 hides / shows the whole interface");
            ImGui::Separator();
            bool animator = OcarinaStudio::IsAnimatorActive();
            if (ImGui::MenuItem("Animator", nullptr, &animator)) {
                OcarinaStudio::SetAnimatorActive(animator);
            }
            ImGui::MenuItem("Stage options", nullptr, &sTools[kStage].open);
            ImGui::MenuItem("Warp to entrance index", nullptr, &sTools[kEntrance].open);
            ImGui::Separator();
            ImGui::MenuItem("Settings", nullptr, &sTools[kSettings].open);
            if (ImGui::MenuItem("Close all windows")) {
                for (Tool& tool : sTools) {
                    tool.open = false;
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Camera")) {
            CameraMenu();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Appearance")) {
            ImGui::MenuItem("Customize...", nullptr, &sTools[kLink].open);
            ImGui::MenuItem("Custom eyes...", nullptr, &sTools[kEyes].open);
            ImGui::MenuItem("Models...", nullptr, &sTools[kModels].open);
            ImGui::MenuItem("Texture tool...", nullptr, &sTools[kTextures].open);
            ImGui::Separator();
            const int age = OcarinaStudio::GetLinkAge();
            if (ImGui::MenuItem("Child Link", nullptr, age == 1)) {
                OcarinaStudio::SetLinkAge(1);
            }
            if (ImGui::MenuItem("Adult Link", nullptr, age == 0)) {
                OcarinaStudio::SetLinkAge(0);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Game")) {
            ImGui::MenuItem("Worlds...", nullptr, &sTools[kWorlds].open);
            if (ImGui::MenuItem("Reload current world")) {
                OcarinaStudio::ReloadCurrentScene();
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("FPS")) {
                OcarinaStudio::DrawFpsMenuItems();
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("CHROMA KEY")) {
            ChromaKeyMenu();
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    OcarinaStudio::DrawAnimatorLayout();

    for (Tool& tool : sTools) {
        if (!tool.open) {
            continue;
        }
        ImGui::SetNextWindowSize(tool.size, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(tool.title, &tool.open)) {
            tool.draw();
        }
        ImGui::End();
    }
}
