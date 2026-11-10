#include "soh/OcarinaStudio/OSInternal.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <nlohmann/json.hpp>

/*
 * Link color palettes.
 *
 * Link's colors are driven by Ship of Harkinian's own cosmetics CVars (the same ones the Cosmetics
 * Editor uses), so changing a color here changes the real game. Palettes are small JSON files in
 * <app dir>/OcarinaStudio/palettes/ that can be saved, loaded and shared.
 */

namespace {

struct LinkColor {
    const char* id;    // Cosmetics editor id, e.g. "Link.KokiriTunic"
    const char* label; // Shown in the UI
    uint8_t def[3];    // Vanilla color, used for the reset button and for unset values
};

const LinkColor kLinkColors[] = {
    { "Link.KokiriTunic", "Kokiri tunic", { 30, 105, 27 } }, { "Link.GoronTunic", "Goron tunic", { 100, 20, 0 } },
    { "Link.ZoraTunic", "Zora tunic", { 0, 60, 100 } },      { "Link.Hair", "Hair", { 255, 173, 27 } },
    { "Link.Linen", "Linen (undershirt)", { 255, 255, 255 } }, { "Link.Boots", "Boots", { 93, 44, 18 } },
};

std::string CosmeticCvar(const LinkColor& color, const char* suffix) {
    return std::string(CVAR_PREFIX_COSMETIC) + "." + color.id + suffix;
}

Color_RGB8 GetColor(const LinkColor& color) {
    Color_RGB8 fallback = { color.def[0], color.def[1], color.def[2] };
    if (!CVarGetInteger(CosmeticCvar(color, ".Changed").c_str(), 0)) {
        return fallback;
    }
    return CVarGetColor24(CosmeticCvar(color, ".Value").c_str(), fallback);
}

void SetColor(const LinkColor& color, uint8_t r, uint8_t g, uint8_t b) {
    Color_RGBA8 value = { r, g, b, 255 };
    CVarSetColor(CosmeticCvar(color, ".Value").c_str(), value);
    CVarSetInteger(CosmeticCvar(color, ".Changed").c_str(), 1);
}

void ResetColor(const LinkColor& color) {
    CVarClear(CosmeticCvar(color, ".Value").c_str());
    CVarClear(CosmeticCvar(color, ".Changed").c_str());
}

std::string PaletteDir() {
    return OcarinaStudio::DataDir("palettes");
}

std::vector<std::string> ListPalettes() {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(PaletteDir(), ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool SavePalette(const std::string& name) {
    nlohmann::json j;
    j["name"] = name;
    nlohmann::json colors = nlohmann::json::object();
    for (const LinkColor& color : kLinkColors) {
        Color_RGB8 c = GetColor(color);
        colors[color.id] = { c.r, c.g, c.b };
    }
    j["colors"] = colors;

    std::ofstream file(std::filesystem::path(PaletteDir()) / (name + ".json"));
    if (!file.is_open()) {
        return false;
    }
    file << j.dump(2);
    return true;
}

bool LoadPalette(const std::string& name) {
    std::ifstream file(std::filesystem::path(PaletteDir()) / (name + ".json"));
    if (!file.is_open()) {
        return false;
    }
    nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
    if (j.is_discarded() || !j.contains("colors") || !j["colors"].is_object()) {
        return false;
    }
    for (const LinkColor& color : kLinkColors) {
        if (!j["colors"].contains(color.id)) {
            continue;
        }
        const auto& arr = j["colors"][color.id];
        if (arr.is_array() && arr.size() == 3) {
            SetColor(color, static_cast<uint8_t>(arr[0].get<int>()), static_cast<uint8_t>(arr[1].get<int>()),
                     static_cast<uint8_t>(arr[2].get<int>()));
        }
    }
    CVarSave();
    return true;
}

void DeletePalette(const std::string& name) {
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(PaletteDir()) / (name + ".json"), ec);
}

} // namespace

namespace OcarinaStudio {

void DrawLinkTab() {
    ImGui::TextWrapped("Change Link's colors. Changes apply to the game immediately.");
    ImGui::Separator();

    for (const LinkColor& color : kLinkColors) {
        Color_RGB8 c = GetColor(color);
        float rgb[3] = { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f };
        if (ImGui::ColorEdit3(color.label, rgb)) {
            SetColor(color, static_cast<uint8_t>(rgb[0] * 255.0f + 0.5f), static_cast<uint8_t>(rgb[1] * 255.0f + 0.5f),
                     static_cast<uint8_t>(rgb[2] * 255.0f + 0.5f));
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            CVarSave();
        }
    }

    if (ImGui::Button("Reset all to vanilla")) {
        for (const LinkColor& color : kLinkColors) {
            ResetColor(color);
        }
        CVarSave();
    }

    ImGui::Separator();
    ImGui::Text("Palettes");

    static char sNameBuffer[64] = "";
    static std::vector<std::string> sPalettes = ListPalettes();
    static std::string sStatus;

    ImGui::InputText("Name", sNameBuffer, sizeof(sNameBuffer));
    ImGui::SameLine();
    if (ImGui::Button("Save palette")) {
        std::string name = SanitizeName(sNameBuffer);
        if (name.empty()) {
            sStatus = "Type a palette name first.";
        } else if (SavePalette(name)) {
            sStatus = "Saved \"" + name + "\".";
            sPalettes = ListPalettes();
        } else {
            sStatus = "Could not write the palette file.";
        }
    }

    if (ImGui::Button("Refresh list")) {
        sPalettes = ListPalettes();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", PaletteDir().c_str());

    if (sPalettes.empty()) {
        ImGui::TextDisabled("No saved palettes yet.");
    }
    for (const std::string& name : sPalettes) {
        ImGui::PushID(name.c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(name.c_str());
        ImGui::SameLine(260.0f);
        if (ImGui::Button("Load")) {
            sStatus = LoadPalette(name) ? "Loaded \"" + name + "\"." : "Could not read \"" + name + "\".";
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            DeletePalette(name);
            sPalettes = ListPalettes();
            sStatus = "Deleted \"" + name + "\".";
            ImGui::PopID();
            break; // the list changed
        }
        ImGui::PopID();
    }

    if (!sStatus.empty()) {
        ImGui::TextWrapped("%s", sStatus.c_str());
    }
}

} // namespace OcarinaStudio
