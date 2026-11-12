#include "soh/OcarinaStudio/OSInternal.h"
#include "soh/OcarinaStudio/OSGame.h"
#include "soh/OcarinaStudio/OSWorlds.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>

namespace {

} // namespace

namespace OcarinaStudio {

// Exits, doors and loading planes are ignored (the stage has no way out) unless the studio itself asked for the warp.
static bool sWarpAllowed = false;

void BlockStrayTransitions() {
    // Only the green screen stage is sealed; once you warp to a real world, exits work again.
    if (gPlayState != nullptr && !sWarpAllowed && CVarGetInteger(Cvar::StageMode, 0) != 0 &&
        CVarGetInteger(Cvar::BlockWarps, 1) != 0 &&
        gPlayState->transitionTrigger == TRANS_TRIGGER_START) {
        gPlayState->transitionTrigger = TRANS_TRIGGER_OFF;
    }
}

void StageOnSceneInit() {
    sWarpAllowed = false;
}

bool WarpToEntrance(int entranceIndex) {
    if (gPlayState == nullptr) {
        return false;
    }
    sWarpAllowed = true;
    gPlayState->nextEntranceIndex = static_cast<s16>(entranceIndex);
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType = TRANS_TYPE_FADE_BLACK;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK;
    return true;
}

void SetLinkAge(int age) {
    if (gPlayState == nullptr || gSaveContext.linkAge == age) {
        return;
    }
    gSaveContext.linkAge = age;
    WarpToEntrance(kStageEntrance);
}

int GetLinkAge() {
    return gSaveContext.linkAge;
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
    CheckboxCvar("Block exits and warps (Link stays on the stage)", Cvar::BlockWarps);

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

void WarpFromMenu(int entrance) {
    // Going to a real world: the green screen stage turns off.
    CVarSetInteger(Cvar::StageMode, 0);
    CVarSave();
    WarpToEntrance(entrance);
}

static bool IsAscii(const char* text) {
    for (; *text != '\0'; text++) {
        if (static_cast<unsigned char>(*text) > 126) {
            return false;
        }
    }
    return true;
}

// Every world of the game: pick one and press Warp, like Saturn's level select.
void DrawWorldsTab() {
    static char sFilter[64] = "";
    static int sSelected = -1;
    static bool sKeepStage = false;
    static int sAge = -1;

    if (gPlayState == nullptr) {
        ImGui::TextDisabled("Starting the game...");
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##worldfilter", "Search worlds...", sFilter, sizeof(sFilter));

    ImGui::BeginChild("worldlist", ImVec2(0, -118.0f), true);
    const int count = static_cast<int>(sizeof(kAllWorlds) / sizeof(kAllWorlds[0]));
    for (int i = 0; i < count; i++) {
        const StageWorld& world = kAllWorlds[i];
        if (!IsAscii(world.name)) {
            continue;
        }
        if (sFilter[0] != '\0') {
            std::string a = world.name, b = sFilter;
            std::transform(a.begin(), a.end(), a.begin(), [](unsigned char ch) { return std::tolower(ch); });
            std::transform(b.begin(), b.end(), b.begin(), [](unsigned char ch) { return std::tolower(ch); });
            if (a.find(b) == std::string::npos) {
                continue;
            }
        }
        ImGui::PushID(i);
        if (ImGui::Selectable(world.name, sSelected == i, ImGuiSelectableFlags_AllowDoubleClick)) {
            sSelected = i;
            if (ImGui::IsMouseDoubleClicked(0) && gPlayState != nullptr) {
                if (sAge >= 0) {
                    gSaveContext.linkAge = sAge;
                }
                if (sKeepStage) {
                    WarpToEntrance(world.entrance);
                } else {
                    WarpFromMenu(world.entrance);
                }
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::TextUnformatted("Link:");
    ImGui::SameLine();
    ImGui::RadioButton("Keep##age", &sAge, -1);
    ImGui::SameLine();
    ImGui::RadioButton("Child", &sAge, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Adult", &sAge, 0);
    ImGui::Checkbox("Keep the green screen on", &sKeepStage);

    ImGui::BeginDisabled(gPlayState == nullptr || sSelected < 0);
    if (ImGui::Button("Warp", ImVec2(110.0f, 0.0f))) {
        if (sAge >= 0) {
            gSaveContext.linkAge = sAge;
        }
        if (sKeepStage) {
            WarpToEntrance(kAllWorlds[sSelected].entrance);
        } else {
            WarpFromMenu(kAllWorlds[sSelected].entrance);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(gPlayState == nullptr);
    if (ImGui::Button("Reload world")) {
        ReloadCurrentScene();
    }
    ImGui::SameLine();
    if (ImGui::Button("Green screen stage")) {
        CVarSetInteger(Cvar::StageMode, 1);
        CVarSave();
        WarpToEntrance(kStageEntrance);
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
