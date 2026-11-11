#include "soh/OcarinaStudio/OcarinaStudio.h"
#include "soh/OcarinaStudio/OSInternal.h"
#include "soh/cvar_prefixes.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>

#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

namespace {

uint8_t ClampByte(int v) {
    return static_cast<uint8_t>(std::clamp(v, 0, 255));
}

} // namespace

// ---------------------------------------------------------------------------------------------
// C bridge used by the patched engine code
// ---------------------------------------------------------------------------------------------

extern "C" int32_t OcarinaStudio_HideHud(void) {
    return CVarGetInteger(OcarinaStudio::Cvar::HideHud, 0);
}

extern "C" int32_t OcarinaStudio_HideMessages(void) {
    return CVarGetInteger(OcarinaStudio::Cvar::HideMessages, 0);
}

extern "C" int32_t OcarinaStudio_HideWorld(void) {
    return CVarGetInteger(OcarinaStudio::Cvar::StageMode, 0);
}

extern "C" void OcarinaStudio_GetClearColor(uint8_t* r, uint8_t* g, uint8_t* b) {
    if (CVarGetInteger(OcarinaStudio::Cvar::StageMode, 0)) {
        // Chroma key green by default.
        *r = ClampByte(CVarGetInteger(OcarinaStudio::Cvar::StageR, 0));
        *g = ClampByte(CVarGetInteger(OcarinaStudio::Cvar::StageG, 255));
        *b = ClampByte(CVarGetInteger(OcarinaStudio::Cvar::StageB, 0));
    } else {
        *r = 0;
        *g = 0;
        *b = 0;
    }
}

// ---------------------------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------------------------

namespace OcarinaStudio {

std::string DataDir(const std::string& subfolder) {
    std::string path = Ship::Context::GetPathRelativeToAppDirectory("OcarinaStudio/" + subfolder);
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return path;
}

std::string SanitizeName(const std::string& in) {
    std::string out;
    for (unsigned char c : in) {
        if (std::isalnum(c) || c == ' ' || c == '-' || c == '_') {
            out.push_back(static_cast<char>(c));
        }
    }
    // Trim spaces on both ends.
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    size_t start = out.find_first_not_of(' ');
    return start == std::string::npos ? std::string() : out.substr(start);
}

bool CheckboxCvar(const char* label, const char* cvar, bool defaultValue) {
    bool value = CVarGetInteger(cvar, defaultValue ? 1 : 0) != 0;
    if (ImGui::Checkbox(label, &value)) {
        CVarSetInteger(cvar, value ? 1 : 0);
        CVarSave();
        return true;
    }
    return false;
}

bool SliderFloatCvar(const char* label, const char* cvar, float min, float max, float defaultValue,
                     const char* format) {
    float value = CVarGetFloat(cvar, defaultValue);
    if (ImGui::SliderFloat(label, &value, min, max, format)) {
        CVarSetFloat(cvar, value);
        return true;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        CVarSave();
        return true;
    }
    return false;
}

bool SliderIntCvar(const char* label, const char* cvar, int min, int max, int defaultValue) {
    int value = CVarGetInteger(cvar, defaultValue);
    if (ImGui::SliderInt(label, &value, min, max)) {
        CVarSetInteger(cvar, value);
        return true;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        CVarSave();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// Per-tick work
// ---------------------------------------------------------------------------------------------

void OnGameTick() {
    BlockStrayTransitions();
    // Deferred start-up work: the resource manager is ready by the first game frame.
    static bool sInitialized = false;
    if (!sInitialized) {
        sInitialized = true;
        // Hair, boots and linen colors need the cosmetics Advanced Mode.
        CVarSetInteger(CVAR_COSMETIC("AdvancedMode"), 1);
        MountEnabledModels();
        ReapplyEyeAssignments();
    }

    // F10 shows / hides the Ocarina Studio window, even while it is hidden.
    static bool sToggleWasDown = false;
    bool down = ImGui::GetCurrentContext() != nullptr && ImGui::IsKeyDown(ImGuiKey_F10);
    if (down && !sToggleWasDown) {
        auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
        auto window = gui->GetGuiWindow("Ocarina Studio");
        if (window != nullptr) {
            window->ToggleVisibility();
        }
    }
    sToggleWasDown = down;
}

static void RegisterOcarinaStudioHooks() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() { OnGameTick(); });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t) {
        OnSceneInit();
        PoseOnSceneInit();
        StageOnSceneInit();
    });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>([]() {
        BlockStrayTransitions();
        ApplyEyeOverride();
        ApplyPose();
    });
}

static RegisterShipInitFunc sOcarinaStudioInit(RegisterOcarinaStudioHooks);

} // namespace OcarinaStudio
