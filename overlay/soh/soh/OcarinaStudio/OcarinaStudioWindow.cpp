#include "soh/OcarinaStudio/OcarinaStudioWindow.h"
#include "soh/OcarinaStudio/OSInternal.h"

#include <imgui.h>

namespace {

struct Tool {
    const char* title;
    void (*draw)();
    bool open;
    ImVec2 size;
};

Tool sTools[] = {
    { "Stage", OcarinaStudio::DrawStageTab, false, ImVec2(420, 380) },
    { "Camera", OcarinaStudio::DrawCameraTab, false, ImVec2(420, 420) },
    { "Link colors", OcarinaStudio::DrawLinkTab, false, ImVec2(420, 460) },
    { "Eyes", OcarinaStudio::DrawEyesTab, false, ImVec2(440, 480) },
    { "Models", OcarinaStudio::DrawModelsTab, false, ImVec2(420, 360) },
    { "Pose and animation", OcarinaStudio::DrawPoseTab, false, ImVec2(480, 700) },
    { "Warp to entrance", OcarinaStudio::DrawScenesTab, false, ImVec2(380, 320) },
};

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
        if (ImGui::BeginMenu("Game")) {
            OcarinaStudio::DrawGameMenuItems();
            ImGui::Separator();
            if (ImGui::MenuItem("Warp to entrance index...")) {
                sTools[6].open = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Stage")) {
            ImGui::MenuItem("Stage options...", nullptr, &sTools[0].open);
            ImGui::Separator();
            OcarinaStudio::DrawStageQuickItems();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Camera")) {
            ImGui::MenuItem("Camera options...", nullptr, &sTools[1].open);
            ImGui::Separator();
            OcarinaStudio::DrawCameraQuickItems();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Link")) {
            ImGui::MenuItem("Colors and palettes...", nullptr, &sTools[2].open);
            ImGui::MenuItem("Eyes...", nullptr, &sTools[3].open);
            ImGui::MenuItem("Models...", nullptr, &sTools[4].open);
            ImGui::MenuItem("Pose and animation...", nullptr, &sTools[5].open);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Close all windows")) {
                for (Tool& tool : sTools) {
                    tool.open = false;
                }
            }
            ImGui::TextDisabled("F10 hides / shows the whole interface");
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

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
