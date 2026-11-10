#include "soh/OcarinaStudio/OSInternal.h"
#include "soh/OcarinaStudio/OSGame.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <vector>

#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>

extern "C" void gfx_texture_cache_clear();

/*
 * Model packs.
 *
 * A model pack is a Ship of Harkinian mod archive (.o2r) that replaces Link's model, textures or
 * animations. Put them in <app dir>/OcarinaStudio/models/. Packs can be turned on and off while the
 * game is running; the enabled set is remembered between sessions.
 */

namespace {

std::string ModelDir() {
    return OcarinaStudio::DataDir("models");
}

std::vector<std::string> ListPacks() {
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(ModelDir(), ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        if (ext == ".o2r") {
            files.push_back(entry.path().filename().string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<std::string> sEnabled; // file names, in load order (later packs win)
std::string sStatus;

void SaveEnabled() {
    std::ostringstream out;
    for (const std::string& name : sEnabled) {
        out << name << '|';
    }
    CVarSetString(OcarinaStudio::Cvar::EnabledModels, out.str().c_str());
    CVarSave();
}

std::vector<std::string> LoadEnabled() {
    std::vector<std::string> names;
    std::stringstream stream(CVarGetString(OcarinaStudio::Cvar::EnabledModels, ""));
    std::string item;
    while (std::getline(stream, item, '|')) {
        if (!item.empty()) {
            names.push_back(item);
        }
    }
    return names;
}

std::string FullPath(const std::string& name) {
    return (std::filesystem::path(ModelDir()) / name).string();
}

bool sRefreshPending = false;

// Makes the game forget Link's cached resources so the new archive set takes effect.
// Only Link's objects are evicted on purpose: the game keeps raw pointers into other resources (the
// current scene, collision, ...) and freeing those while they are in use would crash.
void EvictLinkResources() {
    auto resourceManager = Ship::Context::GetRawInstance()->GetResourceManager();
    resourceManager->UnloadResources("*object_link_boy*");
    resourceManager->UnloadResources("*object_link_child*");
    gfx_texture_cache_clear();
}

// Called after the set of enabled packs changed. Link's skeleton is referenced by a raw pointer while
// he is alive, so with a game running we reload the scene first and evict from OnSceneInit(), when
// the old Link no longer exists and the new one has not been created yet.
void RefreshResources() {
    if (gPlayState == nullptr) {
        EvictLinkResources();
        OcarinaStudio::ReapplyEyeAssignments();
        return;
    }
    sRefreshPending = true;
    OcarinaStudio::ReloadCurrentScene();
}

bool IsEnabled(const std::string& name) {
    return std::find(sEnabled.begin(), sEnabled.end(), name) != sEnabled.end();
}

void Enable(const std::string& name) {
    if (IsEnabled(name)) {
        return;
    }
    auto archives = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
    if (archives->AddArchive(FullPath(name)) == nullptr) {
        sStatus = "Could not open \"" + name + "\".";
        return;
    }
    sEnabled.push_back(name);
    SaveEnabled();
    RefreshResources();
    sStatus = "Enabled \"" + name + "\".";
}

void Disable(const std::string& name) {
    auto archives = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
    archives->RemoveArchive(FullPath(name));
    sEnabled.erase(std::remove(sEnabled.begin(), sEnabled.end(), name), sEnabled.end());
    SaveEnabled();
    RefreshResources();
    sStatus = "Disabled \"" + name + "\".";
}

} // namespace

namespace OcarinaStudio {

// Game hook: a scene is being initialised. Finishes a pending model refresh (see RefreshResources).
void OnSceneInit() {
    if (!sRefreshPending) {
        return;
    }
    sRefreshPending = false;
    EvictLinkResources();
    ReapplyEyeAssignments(); // the eviction also dropped any custom eye textures
}

void MountEnabledModels() {
    sEnabled.clear();
    auto archives = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
    bool any = false;
    for (const std::string& name : LoadEnabled()) {
        if (std::filesystem::exists(FullPath(name)) && archives->AddArchive(FullPath(name)) != nullptr) {
            sEnabled.push_back(name);
            any = true;
        }
    }
    if (any) {
        RefreshResources();
    }
}

void DrawModelsTab() {
    ImGui::TextWrapped("Swap Link's model. Put model packs (.o2r files) in the folder below, then enable them. "
                       "Enabled packs load in the order they were turned on; a later pack overrides an earlier one.");
    ImGui::TextDisabled("%s", ModelDir().c_str());

    static std::vector<std::string> sPacks = ListPacks();
    if (ImGui::Button("Refresh list")) {
        sPacks = ListPacks();
    }

    if (sPacks.empty()) {
        ImGui::TextDisabled("No .o2r model packs found yet.");
    }
    for (const std::string& name : sPacks) {
        ImGui::PushID(name.c_str());
        bool enabled = IsEnabled(name);
        if (ImGui::Checkbox(name.c_str(), &enabled)) {
            if (enabled) {
                Enable(name);
            } else {
                Disable(name);
            }
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button("Disable all")) {
        std::vector<std::string> copy = sEnabled;
        for (const std::string& name : copy) {
            Disable(name);
        }
    }
    if (!sStatus.empty()) {
        ImGui::TextWrapped("%s", sStatus.c_str());
    }
    ImGui::TextDisabled("Changing packs reloads the current scene so Link is rebuilt with the new model.");
}

} // namespace OcarinaStudio
