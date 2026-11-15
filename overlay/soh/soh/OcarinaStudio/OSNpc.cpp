#include "soh/OcarinaStudio/OSInternal.h"
#include "soh/OcarinaStudio/OSGame.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <imgui.h>

extern "C" {
#include "functions.h"
}
extern "C" char** ResourceMgr_ListFiles(const char* searchMask, int* resultSize);

/*
 * NPC mode: Link is drawn as another character of the game (Ganondorf, the NPCs, enemies...). You keep moving
 * him with the controller, but he has none of Link's animations: you pick which animation of that character plays.
 *
 * Every skeleton in the game files is named objects/<object>/g<Name>Skel and its animations are in the same
 * folder as objects/<object>/g<Name>...Anim, so both lists are read from the loaded oot.o2r.
 */

namespace {

struct Npc {
    std::string path;    // objects/object_ganon/gGanondorfSkel
    std::string folder;  // objects/object_ganon
    std::string label;   // "Ganondorf  (object_ganon)"
};

struct NpcState {
    bool enabled = false;
    bool loaded = false;      // the skeleton below is set up for the current scene
    bool paused = false;
    float scale = 1.0f;
    float speed = 1.0f;
    int selected = -1;        // index in sNpcs
    int animSelected = -1;    // index in sAnims

    std::vector<Npc> npcs;
    bool listLoaded = false;
    std::vector<std::string> anims; // full paths
    std::string skeletonString;  // "__OTR__..." (must outlive the SkelAnime)
    std::string animString;
    SkelAnime skelAnime{};
    char filter[64] = "";
    char animFilter[64] = "";
} sNpc;

bool EndsWith(const std::string& text, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return text.size() >= n && text.compare(text.size() - n, n, suffix) == 0;
}

std::vector<std::string> List(const char* mask) {
    std::vector<std::string> out;
    int count = 0;
    char** files = ResourceMgr_ListFiles(mask, &count);
    if (files == nullptr) {
        return out;
    }
    for (int i = 0; i < count; i++) {
        out.emplace_back(files[i]);
        std::free(files[i]);
    }
    std::free(files);
    return out;
}

std::string Prettify(const std::string& symbol) {
    std::string name = symbol;
    if (name.size() > 1 && name[0] == 'g') {
        name.erase(0, 1);
    }
    if (EndsWith(name, "Skel")) {
        name.resize(name.size() - 4);
    }
    // "GanondorfFloat" -> "Ganondorf Float"
    std::string out;
    for (size_t i = 0; i < name.size(); i++) {
        if (i > 0 && std::isupper(static_cast<unsigned char>(name[i])) &&
            !std::isupper(static_cast<unsigned char>(name[i - 1]))) {
            out += ' ';
        }
        out += name[i];
    }
    return out;
}

void LoadNpcList() {
    sNpc.listLoaded = true;
    sNpc.npcs.clear();
    for (const std::string& path : List("objects/*Skel")) {
        const size_t slash = path.find_last_of('/');
        if (slash == std::string::npos || !EndsWith(path, "Skel") || path.find("object_link") != std::string::npos ||
            path.find("gameplay_keep") != std::string::npos) {
            continue;
        }
        Npc npc;
        npc.path = path;
        npc.folder = path.substr(0, slash);
        const std::string object = npc.folder.substr(npc.folder.find_last_of('/') + 1);
        npc.label = Prettify(path.substr(slash + 1)) + "  (" + object + ")";
        sNpc.npcs.push_back(std::move(npc));
    }
    std::sort(sNpc.npcs.begin(), sNpc.npcs.end(), [](const Npc& a, const Npc& b) { return a.label < b.label; });
}

bool ContainsNoCase(const std::string& text, const char* filter) {
    std::string a = text, b = filter;
    std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return std::tolower(c); });
    std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return std::tolower(c); });
    return a.find(b) != std::string::npos;
}

void PlayAnimation(int index) {
    if (index < 0 || index >= static_cast<int>(sNpc.anims.size()) || !sNpc.loaded) {
        return;
    }
    sNpc.animSelected = index;
    sNpc.animString = "__OTR__" + sNpc.anims[index];
    Animation_PlayLoop(&sNpc.skelAnime, reinterpret_cast<AnimationHeader*>(const_cast<char*>(sNpc.animString.c_str())));
}

// (Re)creates the skeleton of the selected character. Needs a game scene to exist.
void SetupSkeleton() {
    sNpc.loaded = false;
    if (gPlayState == nullptr || sNpc.selected < 0 || sNpc.selected >= static_cast<int>(sNpc.npcs.size())) {
        return;
    }
    const Npc& npc = sNpc.npcs[sNpc.selected];
    sNpc.skeletonString = "__OTR__" + npc.path;
    std::memset(&sNpc.skelAnime, 0, sizeof(sNpc.skelAnime));
    SkelAnime_InitFlex(gPlayState, &sNpc.skelAnime, reinterpret_cast<FlexSkeletonHeader*>(
                           const_cast<char*>(sNpc.skeletonString.c_str())),
                       nullptr, nullptr, nullptr, 0);
    sNpc.loaded = sNpc.skelAnime.skeleton != nullptr && sNpc.skelAnime.jointTable != nullptr;

    // Animations: same folder, names ending in Anim.
    sNpc.anims.clear();
    for (const std::string& path : List((npc.folder + "/*Anim").c_str())) {
        if (EndsWith(path, "Anim")) {
            sNpc.anims.push_back(path);
        }
    }
    std::sort(sNpc.anims.begin(), sNpc.anims.end());
    sNpc.animSelected = -1;
    if (sNpc.loaded && !sNpc.anims.empty()) {
        PlayAnimation(0);
    }
}

} // namespace

namespace OcarinaStudio {

void NpcOnSceneInit() {
    sNpc.loaded = false; // the skeleton is set up again on the next draw
}

// Once per game tick.
void NpcTick() {
    if (sNpc.enabled && sNpc.loaded && !sNpc.paused && !sNpc.anims.empty() && sNpc.animSelected >= 0) {
        sNpc.skelAnime.playSpeed = sNpc.speed;
        SkelAnime_Update(&sNpc.skelAnime);
    }
}

// Called from Player_Draw: draws the chosen character instead of Link and returns true.
bool NpcDraw(void* playerPointer) {
    if (!sNpc.enabled || gPlayState == nullptr || playerPointer != GET_PLAYER(gPlayState)) {
        return false;
    }
    if (!sNpc.loaded) {
        SetupSkeleton();
    }
    if (!sNpc.loaded) {
        return false; // nothing selected yet: Link stays visible
    }
    Gfx_SetupDL_25Opa(gPlayState->state.gfxCtx);
    Matrix_Push();
    Matrix_Scale(sNpc.scale, sNpc.scale, sNpc.scale, MTXMODE_APPLY);
    SkelAnime_DrawFlexOpa(gPlayState, sNpc.skelAnime.skeleton, sNpc.skelAnime.jointTable, sNpc.skelAnime.dListCount,
                          nullptr, nullptr, nullptr);
    Matrix_Pop();
    return true;
}

void DrawNpcTab() {
    if (!sNpc.listLoaded) {
        LoadNpcList();
    }
    if (ImGui::Checkbox("Play as another character", &sNpc.enabled)) {
        sNpc.loaded = false;
    }
    ImGui::TextDisabled("You keep moving Link, but he is drawn as the character below.");

    ImGui::SeparatorText("Character");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##npcfilter", "Search (ganon, zelda, impa, ...)", sNpc.filter, sizeof(sNpc.filter));
    ImGui::BeginChild("npclist", ImVec2(0, 170.0f), true);
    for (int i = 0; i < static_cast<int>(sNpc.npcs.size()); i++) {
        if (sNpc.filter[0] != '\0' && !ContainsNoCase(sNpc.npcs[i].label, sNpc.filter)) {
            continue;
        }
        ImGui::PushID(i);
        if (ImGui::Selectable(sNpc.npcs[i].label.c_str(), sNpc.selected == i)) {
            sNpc.selected = i;
            sNpc.enabled = true;
            sNpc.loaded = false; // set up on the next draw
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (sNpc.npcs.empty()) {
        ImGui::TextDisabled("No characters found: oot.o2r is not loaded yet.");
    }

    ImGui::SeparatorText("Animation");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##animfilter", "Search animations...", sNpc.animFilter, sizeof(sNpc.animFilter));
    ImGui::BeginChild("npcanims", ImVec2(0, 170.0f), true);
    for (int i = 0; i < static_cast<int>(sNpc.anims.size()); i++) {
        const std::string name = sNpc.anims[i].substr(sNpc.anims[i].find_last_of('/') + 1);
        if (sNpc.animFilter[0] != '\0' && !ContainsNoCase(name, sNpc.animFilter)) {
            continue;
        }
        ImGui::PushID(i);
        if (ImGui::Selectable(name.c_str(), sNpc.animSelected == i)) {
            PlayAnimation(i);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (sNpc.anims.empty() && sNpc.selected >= 0) {
        ImGui::TextDisabled("This character has no animations of its own.");
    }

    ImGui::Checkbox("Pause animation", &sNpc.paused);
    ImGui::SetNextItemWidth(-90.0f);
    ImGui::SliderFloat("Speed", &sNpc.speed, 0.1f, 3.0f, "%.2fx");
    ImGui::SetNextItemWidth(-90.0f);
    ImGui::SliderFloat("Size", &sNpc.scale, 0.2f, 3.0f, "%.2fx");
    if (ImGui::Button("Back to Link")) {
        sNpc.enabled = false;
    }
}

} // namespace OcarinaStudio
