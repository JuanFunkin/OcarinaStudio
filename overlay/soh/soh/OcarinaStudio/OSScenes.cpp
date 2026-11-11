#include "soh/OcarinaStudio/OSInternal.h"
#include "soh/OcarinaStudio/OSGame.h"

#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>

namespace {

struct SceneEntry {
    const char* name;
    int entrance;
};

// Entrance indices taken from soh/include/tables/entrance_table.h.
const SceneEntry kScenes[] = {
    { "Link's House", 0x0BB },        { "Kokiri Forest", 0x0EE },   { "Hyrule Field", 0x0CD },
    { "Temple of Time", 0x053 },      { "Chamber of the Sages", 0x06B }, { "Test Room (empty)", 0x024 },
    { "Sasa Test", 0x018 },           { "Depth Test", 0x0B6 },      { "Cutscene Map", 0x0A0 },
};

} // namespace

namespace OcarinaStudio {

bool WarpToEntrance(int entranceIndex) {
    if (gPlayState == nullptr) {
        return false;
    }
    gPlayState->nextEntranceIndex = static_cast<s16>(entranceIndex);
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType = TRANS_TYPE_FADE_BLACK;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK;
    return true;
}

bool ReloadCurrentScene() {
    if (gPlayState == nullptr) {
        return false;
    }
    return WarpToEntrance(gSaveContext.entranceIndex);
}

void DrawStageTab() {
    ImGui::TextWrapped("Hide everything that gets in the way of a clean shot.");
    CheckboxCvar("Hide HUD (hearts, buttons, minimap...)", Cvar::HideHud);
    CheckboxCvar("Hide message boxes", Cvar::HideMessages);

    ImGui::Separator();
    ImGui::TextWrapped("Green screen stage: hides the scene and the sky and fills the background with a flat "
                       "color so Link can be keyed out in your video editor.");
    CheckboxCvar("Green screen stage", Cvar::StageMode);

    int rgb[3] = { CVarGetInteger(Cvar::StageR, 0), CVarGetInteger(Cvar::StageG, 255),
                   CVarGetInteger(Cvar::StageB, 0) };
    float color[3] = { rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f };
    if (ImGui::ColorEdit3("Background color", color)) {
        CVarSetInteger(Cvar::StageR, static_cast<int>(color[0] * 255.0f + 0.5f));
        CVarSetInteger(Cvar::StageG, static_cast<int>(color[1] * 255.0f + 0.5f));
        CVarSetInteger(Cvar::StageB, static_cast<int>(color[2] * 255.0f + 0.5f));
        CVarSave();
    }
    if (ImGui::Button("Green")) {
        CVarSetInteger(Cvar::StageR, 0);
        CVarSetInteger(Cvar::StageG, 255);
        CVarSetInteger(Cvar::StageB, 0);
        CVarSave();
    }
    ImGui::SameLine();
    if (ImGui::Button("Blue")) {
        CVarSetInteger(Cvar::StageR, 0);
        CVarSetInteger(Cvar::StageG, 0);
        CVarSetInteger(Cvar::StageB, 255);
        CVarSave();
    }
    ImGui::SameLine();
    if (ImGui::Button("Magenta")) {
        CVarSetInteger(Cvar::StageR, 255);
        CVarSetInteger(Cvar::StageG, 0);
        CVarSetInteger(Cvar::StageB, 255);
        CVarSave();
    }

    ImGui::Separator();
    if (ImGui::Button("Start clean stage")) {
        // Empty test room + green screen + no HUD / messages.
        CVarSetInteger(Cvar::StageMode, 1);
        CVarSetInteger(Cvar::HideHud, 1);
        CVarSetInteger(Cvar::HideMessages, 1);
        CVarSave();
        WarpToEntrance(0x024);
    }
    ImGui::SameLine();
    if (ImGui::Button("Back to normal view")) {
        CVarSetInteger(Cvar::StageMode, 0);
        CVarSetInteger(Cvar::HideHud, 0);
        CVarSetInteger(Cvar::HideMessages, 0);
        CVarSave();
    }
    if (gPlayState == nullptr) {
        ImGui::TextDisabled("Load a save file first so there is a world to work in.");
    }
}

void DrawGameMenuItems() {
    if (gPlayState == nullptr) {
        ImGui::TextDisabled("Starting the game...");
    }
    ImGui::BeginDisabled(gPlayState == nullptr);
    for (const SceneEntry& scene : kScenes) {
        if (ImGui::MenuItem(scene.name)) {
            WarpToEntrance(scene.entrance);
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Reload current world")) {
        ReloadCurrentScene();
    }
    ImGui::EndDisabled();
}

void DrawStageQuickItems() {
    bool value = CVarGetInteger(Cvar::StageMode, 0) != 0;
    if (ImGui::MenuItem("Green screen", nullptr, &value)) {
        CVarSetInteger(Cvar::StageMode, value ? 1 : 0);
        CVarSave();
    }
    value = CVarGetInteger(Cvar::HideHud, 0) != 0;
    if (ImGui::MenuItem("Hide HUD", nullptr, &value)) {
        CVarSetInteger(Cvar::HideHud, value ? 1 : 0);
        CVarSave();
    }
    value = CVarGetInteger(Cvar::HideMessages, 0) != 0;
    if (ImGui::MenuItem("Hide message boxes", nullptr, &value)) {
        CVarSetInteger(Cvar::HideMessages, value ? 1 : 0);
        CVarSave();
    }
}

void DrawScenesTab() {
    ImGui::TextWrapped("Go to a world. This warps Link to the chosen entrance (a save file must be loaded).");
    if (gPlayState == nullptr) {
        ImGui::TextDisabled("No game running yet. Load a save file first.");
    }
    ImGui::BeginDisabled(gPlayState == nullptr);

    for (const SceneEntry& scene : kScenes) {
        if (ImGui::Button(scene.name)) {
            WarpToEntrance(scene.entrance);
        }
    }

    ImGui::Separator();
    static uint32_t sManualEntrance = 0x0BB;
    ImGui::InputScalar("Entrance index (hex)", ImGuiDataType_U32, &sManualEntrance, nullptr, nullptr, "%X",
                       ImGuiInputTextFlags_CharsHexadecimal);
    if (ImGui::Button("Warp to entrance")) {
        WarpToEntrance(static_cast<int>(sManualEntrance));
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload current scene")) {
        ReloadCurrentScene();
    }
    ImGui::EndDisabled();
}

} // namespace OcarinaStudio
