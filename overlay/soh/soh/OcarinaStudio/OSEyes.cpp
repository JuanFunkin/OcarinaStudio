#include "soh/OcarinaStudio/OSInternal.h"
#include "soh/OcarinaStudio/OSGame.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <vector>

#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <fast/resource/type/Texture.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <stb_image.h>

extern "C" void gfx_texture_cache_clear();

/*
 * Link's eyes.
 *
 *  - Expression: force one of the eight vanilla eye textures (and one of four mouths).
 *  - Custom eyes: drop PNG files into <app dir>/OcarinaStudio/eyes/ and assign them to an eye slot.
 *
 * A custom eye replaces the in-memory texture resource of that slot (Fast::Texture), converted to the
 * same format and size as the original. Nothing on disk is modified; "Restore" evicts the replacement
 * and the game reloads the original texture.
 */

namespace {

struct EyeSlot {
    const char* label;
    const char* resourceSuffix; // e.g. "EyesOpenTex"
};

const EyeSlot kEyeSlots[] = {
    { "Open", "EyesOpenTex" },        { "Half closed", "EyesHalfTex" }, { "Closed", "EyesClosedfTex" },
    { "Look left", "EyesRollLeftTex" }, { "Look right", "EyesRollRightTex" }, { "Shocked", "EyesShockTex" },
    { "Unused 1", "EyesUnk1Tex" },    { "Unused 2", "EyesUnk2Tex" },
};
constexpr int kEyeSlotCount = 8;

enum Age { AGE_ADULT = 0, AGE_CHILD = 1 };

std::string ResourcePath(int age, int slot) {
    if (age == AGE_ADULT) {
        return std::string("objects/object_link_boy/gLinkAdult") + kEyeSlots[slot].resourceSuffix;
    }
    return std::string("objects/object_link_child/gLinkChild") + kEyeSlots[slot].resourceSuffix;
}

struct EyeAssignment {
    int age;
    int slot;
    std::string file;
};

std::vector<EyeAssignment> sAssignments;
std::string sStatus;

std::string EyeDir() {
    return OcarinaStudio::DataDir("eyes");
}

void SaveAssignments() {
    std::ostringstream out;
    for (const EyeAssignment& a : sAssignments) {
        out << a.age << ':' << a.slot << ':' << a.file << ';';
    }
    CVarSetString(OcarinaStudio::Cvar::EyeAssignments, out.str().c_str());
    CVarSave();
}

void LoadAssignments() {
    sAssignments.clear();
    std::string raw = CVarGetString(OcarinaStudio::Cvar::EyeAssignments, "");
    std::stringstream stream(raw);
    std::string item;
    while (std::getline(stream, item, ';')) {
        size_t first = item.find(':');
        size_t second = item.find(':', first == std::string::npos ? 0 : first + 1);
        if (first == std::string::npos || second == std::string::npos) {
            continue;
        }
        EyeAssignment a;
        a.age = std::atoi(item.substr(0, first).c_str());
        a.slot = std::atoi(item.substr(first + 1, second - first - 1).c_str());
        a.file = item.substr(second + 1);
        if ((a.age == AGE_ADULT || a.age == AGE_CHILD) && a.slot >= 0 && a.slot < kEyeSlotCount && !a.file.empty()) {
            sAssignments.push_back(a);
        }
    }
}

// Converts a PNG into the original texture's format and installs it over the resource.
bool InstallEye(const std::string& pngFile, int age, int slot) {
    auto resourceManager = Ship::Context::GetRawInstance()->GetResourceManager();
    const std::string path = ResourcePath(age, slot);

    // Always start from the original resource, never from a previous replacement.
    resourceManager->UnloadResource(path);
    auto original = std::static_pointer_cast<Fast::Texture>(resourceManager->LoadResource(path));
    if (original == nullptr) {
        sStatus = "Could not find the original eye texture (is the game data loaded?).";
        return false;
    }

    const std::filesystem::path pngPath = std::filesystem::path(EyeDir()) / pngFile;
    int srcW = 0;
    int srcH = 0;
    int comp = 0;
    unsigned char* src = stbi_load(pngPath.string().c_str(), &srcW, &srcH, &comp, 4);
    if (src == nullptr || srcW <= 0 || srcH <= 0) {
        sStatus = "Could not read the PNG \"" + pngFile + "\".";
        return false;
    }

    const int dstW = original->Width;
    const int dstH = original->Height;
    const uint32_t bytes = original->ImageDataSize;
    const bool isRgba16 = original->Type == Fast::TextureType::RGBA16bpp;
    const bool isRgba32 = original->Type == Fast::TextureType::RGBA32bpp;
    if ((!isRgba16 && !isRgba32) || dstW <= 0 || dstH <= 0 ||
        bytes < static_cast<uint32_t>(dstW * dstH * (isRgba16 ? 2 : 4))) {
        stbi_image_free(src);
        sStatus = "Unsupported eye texture format.";
        return false;
    }

    uint8_t* data = new uint8_t[bytes];
    std::memset(data, 0, bytes);
    for (int y = 0; y < dstH; y++) {
        const int sy = std::min(srcH - 1, y * srcH / dstH);
        for (int x = 0; x < dstW; x++) {
            const int sx = std::min(srcW - 1, x * srcW / dstW);
            const unsigned char* px = src + (sy * srcW + sx) * 4;
            const int index = y * dstW + x;
            if (isRgba16) {
                const uint16_t value = static_cast<uint16_t>(((px[0] >> 3) << 11) | ((px[1] >> 3) << 6) |
                                                              ((px[2] >> 3) << 1) | (px[3] >= 128 ? 1 : 0));
                data[index * 2] = static_cast<uint8_t>(value >> 8); // N64 textures are big endian
                data[index * 2 + 1] = static_cast<uint8_t>(value & 0xFF);
            } else {
                std::memcpy(data + index * 4, px, 4);
            }
        }
    }
    stbi_image_free(src);

    auto texture = std::make_shared<Fast::Texture>(original->GetInitData());
    texture->Type = original->Type;
    texture->Width = original->Width;
    texture->Height = original->Height;
    texture->Flags = original->Flags;
    texture->HByteScale = original->HByteScale;
    texture->VPixelScale = original->VPixelScale;
    texture->ImageDataSize = bytes;
    texture->ImageData = data; // owned by the texture (mImageBuffer stays empty)

    resourceManager->CacheExternalResource(path, texture);
    gfx_texture_cache_clear();
    return true;
}

void RestoreEye(int age, int slot) {
    Ship::Context::GetRawInstance()->GetResourceManager()->UnloadResource(ResourcePath(age, slot));
    gfx_texture_cache_clear();
}

void RemoveAssignment(int age, int slot) {
    sAssignments.erase(std::remove_if(sAssignments.begin(), sAssignments.end(),
                                      [&](const EyeAssignment& a) { return a.age == age && a.slot == slot; }),
                       sAssignments.end());
}

void Assign(const std::string& file, int age, int slot) {
    if (InstallEye(file, age, slot)) {
        RemoveAssignment(age, slot);
        sAssignments.push_back({ age, slot, file });
        SaveAssignments();
        sStatus = "Applied \"" + file + "\" to " + (age == AGE_ADULT ? "adult" : "child") + " Link (" +
                  kEyeSlots[slot].label + ").";
    }
}

std::vector<std::string> ListPngs() {
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(EyeDir(), ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        if (ext == ".png") {
            files.push_back(entry.path().filename().string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

// ---- Eye sets: a folder inside eyes/ holding one image per slot -------------------------------------

std::vector<std::string> ListEyeSets() {
    std::vector<std::string> sets;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(EyeDir(), ec)) {
        if (entry.is_directory()) {
            sets.push_back(entry.path().filename().string());
        }
    }
    std::sort(sets.begin(), sets.end());
    return sets;
}

// Maps a file name (without extension) to an eye slot, or -1.
int SlotForName(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
    name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char c) { return c == ' ' || c == '_' || c == '-'; }),
               name.end());
    static const struct {
        const char* name;
        int slot;
    } kNames[] = { { "open", 0 },      { "half", 1 },       { "halfclosed", 1 }, { "closed", 2 },    { "close", 2 },
                   { "left", 3 },      { "lookleft", 3 },   { "right", 4 },      { "lookright", 4 }, { "shock", 5 },
                   { "shocked", 5 },   { "unk1", 6 },       { "unused1", 6 },    { "unk2", 7 },      { "unused2", 7 } };
    for (const auto& entry : kNames) {
        if (name == entry.name) {
            return entry.slot;
        }
    }
    return -1;
}

// Applies every recognised image of the set (open.png, half.png, closed.png, left.png, right.png, shock.png).
// A set holding a single image of any name is used for all slots. Returns how many slots were set.
int ApplySet(const std::string& set, int ageMode) {
    std::vector<std::pair<int, std::string>> found;
    std::vector<std::string> others;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(EyeDir()) / set, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        if (ext != ".png") {
            continue;
        }
        const std::string relative = set + "/" + entry.path().filename().string();
        const int slot = SlotForName(entry.path().stem().string());
        if (slot >= 0) {
            found.emplace_back(slot, relative);
        } else {
            others.push_back(relative);
        }
    }
    if (found.empty() && others.size() == 1) {
        for (int slot = 0; slot < kEyeSlotCount; slot++) {
            found.emplace_back(slot, others[0]);
        }
    }
    for (const auto& [slot, relative] : found) {
        if (ageMode == 0 || ageMode == 2) {
            Assign(relative, AGE_ADULT, slot);
        }
        if (ageMode == 1 || ageMode == 2) {
            Assign(relative, AGE_CHILD, slot);
        }
    }
    return static_cast<int>(found.size());
}

} // namespace

namespace OcarinaStudio {

void ReapplyEyeAssignments() {
    LoadAssignments();
    std::vector<EyeAssignment> toApply = sAssignments;
    sAssignments.clear();
    for (const EyeAssignment& a : toApply) {
        if (InstallEye(a.file, a.age, a.slot)) {
            sAssignments.push_back(a);
        }
    }
}

// Runs every Player update: forces the chosen expression by writing the face joint.
void ApplyEyeOverride() {
    const int eye = CVarGetInteger(Cvar::ForceEye, -1);
    const int mouth = CVarGetInteger(Cvar::ForceMouth, -1);
    if ((eye < 0 && mouth < 0) || gPlayState == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr || player->skelAnime.jointTable == nullptr) {
        return;
    }
    // Player_DrawImpl reads the low nibble as (eye + 1) and the high nibble as (mouth + 1).
    const int eyeBits = eye >= 0 ? (std::min(eye, 7) + 1) : 0;
    const int mouthBits = mouth >= 0 ? (std::min(mouth, 3) + 1) : 0;
    player->skelAnime.jointTable[22].x = static_cast<s16>(eyeBits | (mouthBits << 4));
}

void DrawEyesTab() {
    static const char* kEyeNames[] = { "Default (animation)", "Open", "Half closed", "Closed", "Look left",
                                       "Look right", "Shocked", "Unused 1", "Unused 2" };
    static const char* kMouthNames[] = { "Default (animation)", "Mouth 1", "Mouth 2", "Mouth 3", "Mouth 4" };

    ImGui::Text("Expression");
    int eye = CVarGetInteger(Cvar::ForceEye, -1) + 1;
    if (ImGui::Combo("Eyes", &eye, kEyeNames, IM_ARRAYSIZE(kEyeNames))) {
        CVarSetInteger(Cvar::ForceEye, eye - 1);
        CVarSave();
    }
    int mouth = CVarGetInteger(Cvar::ForceMouth, -1) + 1;
    if (ImGui::Combo("Mouth", &mouth, kMouthNames, IM_ARRAYSIZE(kMouthNames))) {
        CVarSetInteger(Cvar::ForceMouth, mouth - 1);
        CVarSave();
    }

    ImGui::Separator();
    ImGui::Text("Eye sets");
    ImGui::TextWrapped("Each folder inside the eyes folder is a set. Name the images open, half, closed, left, "
                       "right and shock (.png). A folder with one image uses it for every slot.");
    static std::vector<std::string> sSets = ListEyeSets();
    static int sSetAge = 2;
    if (ImGui::Button("Refresh sets")) {
        sSets = ListEyeSets();
    }
    ImGui::SameLine();
    ImGui::RadioButton("Adult##set", &sSetAge, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Child##set", &sSetAge, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Both##set", &sSetAge, 2);
    ImGui::BeginChild("eyesets", ImVec2(0, 110), true);
    if (sSets.empty()) {
        ImGui::TextDisabled("No set folders yet.");
    }
    for (const std::string& set : sSets) {
        if (ImGui::Selectable(set.c_str())) {
            const int count = ApplySet(set, sSetAge);
            sStatus = count > 0 ? "Applied \"" + set + "\" (" + std::to_string(count) + " slots)."
                                : "No recognised images in \"" + set + "\".";
        }
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Text("Single image");
    ImGui::TextWrapped("Put PNG images in the folder below. They are resized to the size of the original eye "
                       "texture. Pick a slot and press Apply.");
    ImGui::TextDisabled("%s", EyeDir().c_str());

    static std::vector<std::string> sFiles = ListPngs();
    static int sSelectedFile = 0;
    static int sSelectedSlot = 0;
    static int sSelectedAge = 2; // 0 adult, 1 child, 2 both

    if (ImGui::Button("Refresh list")) {
        sFiles = ListPngs();
        sSelectedFile = 0;
    }

    if (sFiles.empty()) {
        ImGui::TextDisabled("No PNG files found yet.");
    } else {
        sSelectedFile = std::clamp(sSelectedFile, 0, static_cast<int>(sFiles.size()) - 1);
        if (ImGui::BeginCombo("Image", sFiles[sSelectedFile].c_str())) {
            for (int i = 0; i < static_cast<int>(sFiles.size()); i++) {
                if (ImGui::Selectable(sFiles[i].c_str(), i == sSelectedFile)) {
                    sSelectedFile = i;
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::BeginCombo("Slot", kEyeSlots[sSelectedSlot].label)) {
            for (int i = 0; i < kEyeSlotCount; i++) {
                if (ImGui::Selectable(kEyeSlots[i].label, i == sSelectedSlot)) {
                    sSelectedSlot = i;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::RadioButton("Adult", &sSelectedAge, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Child", &sSelectedAge, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Both", &sSelectedAge, 2);

        if (ImGui::Button("Apply to slot")) {
            if (sSelectedAge == 0 || sSelectedAge == 2) {
                Assign(sFiles[sSelectedFile], AGE_ADULT, sSelectedSlot);
            }
            if (sSelectedAge == 1 || sSelectedAge == 2) {
                Assign(sFiles[sSelectedFile], AGE_CHILD, sSelectedSlot);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply to ALL slots")) {
            for (int slot = 0; slot < kEyeSlotCount; slot++) {
                if (sSelectedAge == 0 || sSelectedAge == 2) {
                    Assign(sFiles[sSelectedFile], AGE_ADULT, slot);
                }
                if (sSelectedAge == 1 || sSelectedAge == 2) {
                    Assign(sFiles[sSelectedFile], AGE_CHILD, slot);
                }
            }
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Restore all vanilla eyes")) {
        for (const EyeAssignment& a : sAssignments) {
            RestoreEye(a.age, a.slot);
        }
        sAssignments.clear();
        SaveAssignments();
        sStatus = "Restored the original eyes.";
    }

    for (const EyeAssignment& a : sAssignments) {
        ImGui::BulletText("%s Link, %s: %s", a.age == AGE_ADULT ? "Adult" : "Child", kEyeSlots[a.slot].label,
                          a.file.c_str());
    }
    if (!sStatus.empty()) {
        ImGui::TextWrapped("%s", sStatus.c_str());
    }
}

} // namespace OcarinaStudio
