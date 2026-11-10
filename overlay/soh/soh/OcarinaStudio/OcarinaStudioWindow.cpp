#include "soh/OcarinaStudio/OcarinaStudioWindow.h"
#include "soh/OcarinaStudio/OSInternal.h"

#include <imgui.h>

void OcarinaStudioWindow::InitElement() {
}

void OcarinaStudioWindow::DrawElement() {
    using namespace OcarinaStudio;

    ImGui::TextDisabled("F10 shows / hides this window");

    if (!ImGui::BeginTabBar("OcarinaStudioTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        return;
    }
    if (ImGui::BeginTabItem("Stage")) {
        DrawStageTab();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Worlds")) {
        DrawScenesTab();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Camera")) {
        DrawCameraTab();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Link")) {
        DrawLinkTab();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Eyes")) {
        DrawEyesTab();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Models")) {
        DrawModelsTab();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Pose")) {
        DrawPoseTab();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}
