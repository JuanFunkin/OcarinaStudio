#include "soh/OcarinaStudio/OSInternal.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <imgui.h>
#include <ship/Context.h>
#include <stb_image.h>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <zip.h>

/*
 * Texture tool: reads an .o2r (oot.o2r or a mod), exports its textures to PNG, converts edited PNGs back to the
 * game's texture format and writes a mod .o2r with only the changed textures.
 *
 * An .o2r is a zip file. A texture file has a 64 byte header (byte 0 = byte order, bytes 4..7 resource type
 * 'OTEX', bytes 8..11 version) followed by: type, width, height, [flags, hscale, vscale (version 1)],
 * image size, pixel data. Pixels are stored in the N64 formats (RGBA16 is 5551, IA/I are 4 / 8 / 16 bit).
 * Palette textures (CI4 / CI8) are not supported because the palette lives in another file.
 *
 * Files:  OcarinaStudio/textures/export/<path>.png   (written by Export)
 *         OcarinaStudio/textures/import/<path>.png   (read by Import; same layout as export)
 *         mods/<name>.o2r                            (written by Export mod)
 */

namespace {

constexpr size_t kHeaderSize = 64;
constexpr uint32_t kTextureMagic = 0x4F544558; // 'OTEX'

enum TexType : uint32_t {
    kRGBA32 = 1, kRGBA16 = 2, kCI4 = 3, kCI8 = 4, kI4 = 5, kI8 = 6, kIA4 = 7, kIA8 = 8, kIA16 = 9,
};

const char* TypeName(uint32_t type) {
    static const char* kNames[] = { "error", "RGBA32", "RGBA16", "CI4 (palette)", "CI8 (palette)",
                                    "I4",    "I8",     "IA4",    "IA8",           "IA16" };
    return type < 10 ? kNames[type] : "unknown";
}

struct TexInfo {
    std::string name;
    uint32_t type = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t version = 0;
    bool supported = false;
};

struct Pending {
    std::string target;
    std::vector<uint8_t> bytes;
    std::string warning;
};

struct ToolState {
    std::string sourcePath;
    char customPath[260] = "";
    std::vector<TexInfo> textures;
    std::mutex mutex;
    std::atomic<bool> scanning{ false };
    std::atomic<int> scanned{ 0 };
    std::atomic<int> total{ 0 };
    char filter[96] = "";
    int selected = -1;
    std::vector<Pending> pending;
    char modName[64] = "MyTextures";
    std::string status;
    uint32_t generation = 0;
} sTool;

// ---- byte helpers (the byte order is in the file's first header byte) -------------------------

uint32_t Rd32(const uint8_t* p, bool big) {
    return big ? (uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3])
               : (uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0]);
}

void Wr32(std::vector<uint8_t>& out, uint32_t v, bool big) {
    for (int i = 0; i < 4; i++) {
        const int shift = big ? 24 - 8 * i : 8 * i;
        out.push_back(static_cast<uint8_t>(v >> shift));
    }
}

// ---- zip --------------------------------------------------------------------------------------

bool ReadEntry(zip_t* archive, zip_uint64_t index, size_t maxBytes, std::vector<uint8_t>& out) {
    zip_stat_t st;
    if (zip_stat_index(archive, index, 0, &st) != 0) {
        return false;
    }
    size_t size = static_cast<size_t>(st.size);
    if (maxBytes != 0) {
        size = std::min(size, maxBytes);
    }
    zip_file_t* file = zip_fopen_index(archive, index, 0);
    if (file == nullptr) {
        return false;
    }
    out.resize(size);
    const zip_int64_t got = size == 0 ? 0 : zip_fread(file, out.data(), size);
    zip_fclose(file);
    if (got < 0) {
        return false;
    }
    out.resize(static_cast<size_t>(got));
    return true;
}

bool ReadByName(const std::string& archivePath, const std::string& name, std::vector<uint8_t>& out) {
    int err = 0;
    zip_t* archive = zip_open(archivePath.c_str(), ZIP_RDONLY, &err);
    if (archive == nullptr) {
        return false;
    }
    const zip_int64_t index = zip_name_locate(archive, name.c_str(), 0);
    const bool ok = index >= 0 && ReadEntry(archive, static_cast<zip_uint64_t>(index), 0, out);
    zip_close(archive);
    return ok;
}

// Lists every texture of the archive on a worker thread (oot.o2r has tens of thousands of files).
void ScanArchive(std::string path, uint32_t generation) {
    int err = 0;
    zip_t* archive = zip_open(path.c_str(), ZIP_RDONLY, &err);
    std::vector<TexInfo> found;
    if (archive != nullptr) {
        const zip_int64_t count = zip_get_num_entries(archive, 0);
        sTool.total = static_cast<int>(count);
        for (zip_int64_t i = 0; i < count; i++) {
            sTool.scanned = static_cast<int>(i);
            const char* name = zip_get_name(archive, static_cast<zip_uint64_t>(i), 0);
            std::vector<uint8_t> head;
            if (name == nullptr || !ReadEntry(archive, static_cast<zip_uint64_t>(i), kHeaderSize + 28, head) ||
                head.size() < kHeaderSize + 16) {
                continue;
            }
            const bool big = head[0] == 1;
            if (Rd32(head.data() + 4, big) != kTextureMagic) {
                continue;
            }
            TexInfo info;
            info.name = name;
            info.version = Rd32(head.data() + 8, big);
            info.type = Rd32(head.data() + kHeaderSize, big);
            info.width = Rd32(head.data() + kHeaderSize + 4, big);
            info.height = Rd32(head.data() + kHeaderSize + 8, big);
            info.supported = info.type == kRGBA32 || info.type == kRGBA16 || (info.type >= kI4 && info.type <= kIA16);
            found.push_back(std::move(info));
        }
        zip_close(archive);
    }
    std::sort(found.begin(), found.end(), [](const TexInfo& a, const TexInfo& b) { return a.name < b.name; });
    {
        std::lock_guard<std::mutex> lock(sTool.mutex);
        if (generation == sTool.generation) {
            sTool.textures = std::move(found);
        }
    }
    sTool.scanning = false;
}

void OpenArchive(const std::string& path) {
    if (sTool.scanning) {
        sTool.status = "Still reading the previous file...";
        return;
    }
    sTool.sourcePath = path;
    sTool.selected = -1;
    sTool.scanned = 0;
    sTool.total = 0;
    sTool.scanning = true;
    {
        std::lock_guard<std::mutex> lock(sTool.mutex);
        sTool.textures.clear();
        sTool.generation++;
    }
    std::thread(ScanArchive, path, sTool.generation).detach();
    sTool.status = "Reading " + path + "...";
}

// ---- pixel conversion -------------------------------------------------------------------------

// Decodes the texture file body into RGBA8 pixels. Returns false for unsupported formats.
bool DecodeTexture(const std::vector<uint8_t>& file, TexInfo& info, std::vector<uint8_t>& rgba) {
    if (file.size() < kHeaderSize + 20) {
        return false;
    }
    const bool big = file[0] == 1;
    const uint32_t version = Rd32(file.data() + 8, big);
    const uint8_t* body = file.data() + kHeaderSize;
    info.type = Rd32(body, big);
    info.width = Rd32(body + 4, big);
    info.height = Rd32(body + 8, big);
    const size_t dataOffset = kHeaderSize + (version == 0 ? 16 : 28);
    if (file.size() < dataOffset) {
        return false;
    }
    const uint8_t* data = file.data() + dataOffset;
    const size_t avail = file.size() - dataOffset;
    const size_t pixels = static_cast<size_t>(info.width) * info.height;
    rgba.assign(pixels * 4, 255);

    auto need = [&](size_t bytes) { return avail >= bytes; };
    for (size_t i = 0; i < pixels; i++) {
        uint8_t* px = &rgba[i * 4];
        switch (info.type) {
            case kRGBA32:
                if (!need(pixels * 4)) return false;
                std::memcpy(px, data + i * 4, 4);
                break;
            case kRGBA16: {
                if (!need(pixels * 2)) return false;
                const uint16_t v = static_cast<uint16_t>(data[i * 2] << 8 | data[i * 2 + 1]);
                px[0] = static_cast<uint8_t>(((v >> 11) & 31) * 255 / 31);
                px[1] = static_cast<uint8_t>(((v >> 6) & 31) * 255 / 31);
                px[2] = static_cast<uint8_t>(((v >> 1) & 31) * 255 / 31);
                px[3] = (v & 1) ? 255 : 0;
                break;
            }
            case kI8:
                if (!need(pixels)) return false;
                px[0] = px[1] = px[2] = data[i];
                break;
            case kI4: {
                if (!need((pixels + 1) / 2)) return false;
                const uint8_t n = (i & 1) ? (data[i / 2] & 15) : (data[i / 2] >> 4);
                px[0] = px[1] = px[2] = static_cast<uint8_t>(n * 17);
                break;
            }
            case kIA16:
                if (!need(pixels * 2)) return false;
                px[0] = px[1] = px[2] = data[i * 2];
                px[3] = data[i * 2 + 1];
                break;
            case kIA8:
                if (!need(pixels)) return false;
                px[0] = px[1] = px[2] = static_cast<uint8_t>((data[i] >> 4) * 17);
                px[3] = static_cast<uint8_t>((data[i] & 15) * 17);
                break;
            case kIA4: {
                if (!need((pixels + 1) / 2)) return false;
                const uint8_t n = (i & 1) ? (data[i / 2] & 15) : (data[i / 2] >> 4);
                px[0] = px[1] = px[2] = static_cast<uint8_t>((n >> 1) * 255 / 7);
                px[3] = (n & 1) ? 255 : 0;
                break;
            }
            default:
                return false;
        }
    }
    return true;
}

uint8_t Luma(const uint8_t* px) {
    return static_cast<uint8_t>(std::lround(px[0] * 0.299f + px[1] * 0.587f + px[2] * 0.114f));
}

// Packs RGBA8 pixels into the format `type`.
bool EncodePixels(const uint8_t* rgba, size_t pixels, uint32_t type, std::vector<uint8_t>& out) {
    out.clear();
    for (size_t i = 0; i < pixels; i++) {
        const uint8_t* px = rgba + i * 4;
        switch (type) {
            case kRGBA32:
                out.insert(out.end(), px, px + 4);
                break;
            case kRGBA16: {
                const uint16_t v = static_cast<uint16_t>(((px[0] * 31 + 127) / 255) << 11 |
                                                         ((px[1] * 31 + 127) / 255) << 6 |
                                                         ((px[2] * 31 + 127) / 255) << 1 | (px[3] >= 128 ? 1 : 0));
                out.push_back(static_cast<uint8_t>(v >> 8));
                out.push_back(static_cast<uint8_t>(v & 255));
                break;
            }
            case kI8:
                out.push_back(Luma(px));
                break;
            case kI4: {
                const uint8_t n = static_cast<uint8_t>((Luma(px) + 8) / 17);
                if (i & 1) out.back() |= n; else out.push_back(static_cast<uint8_t>(n << 4));
                break;
            }
            case kIA16:
                out.push_back(Luma(px));
                out.push_back(px[3]);
                break;
            case kIA8:
                out.push_back(static_cast<uint8_t>(((Luma(px) + 8) / 17) << 4 | ((px[3] + 8) / 17)));
                break;
            case kIA4: {
                const uint8_t n = static_cast<uint8_t>(((Luma(px) * 7 + 127) / 255) << 1 | (px[3] >= 128 ? 1 : 0));
                if (i & 1) out.back() |= n; else out.push_back(static_cast<uint8_t>(n << 4));
                break;
            }
            default:
                return false;
        }
    }
    return true;
}

// ---- export / import --------------------------------------------------------------------------

std::filesystem::path ExportDir() {
    return std::filesystem::path(OcarinaStudio::DataDir("textures/export"));
}

std::filesystem::path ImportDir() {
    return std::filesystem::path(OcarinaStudio::DataDir("textures/import"));
}

bool ExportOne(zip_t* archive, const std::string& name) {
    const zip_int64_t index = zip_name_locate(archive, name.c_str(), 0);
    std::vector<uint8_t> file, rgba;
    TexInfo info;
    if (index < 0 || !ReadEntry(archive, static_cast<zip_uint64_t>(index), 0, file) ||
        !DecodeTexture(file, info, rgba) || info.width == 0 || info.height == 0) {
        return false;
    }
    const std::filesystem::path path = ExportDir() / (name + ".png");
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return stbi_write_png(path.string().c_str(), static_cast<int>(info.width), static_cast<int>(info.height), 4,
                          rgba.data(), static_cast<int>(info.width) * 4) != 0;
}

void ExportTextures(const std::vector<std::string>& names) {
    int err = 0;
    zip_t* archive = zip_open(sTool.sourcePath.c_str(), ZIP_RDONLY, &err);
    if (archive == nullptr) {
        sTool.status = "Could not open " + sTool.sourcePath;
        return;
    }
    int ok = 0;
    for (const std::string& name : names) {
        ok += ExportOne(archive, name) ? 1 : 0;
    }
    zip_close(archive);
    sTool.status = "Exported " + std::to_string(ok) + " of " + std::to_string(names.size()) + " textures to " +
                   ExportDir().string();
}

// Converts `png` into the texture file for `target` (same header, type and version as the original).
bool ConvertPng(const std::filesystem::path& png, const std::string& target, Pending& out) {
    std::vector<uint8_t> original;
    if (!ReadByName(sTool.sourcePath, target, original) || original.size() < kHeaderSize + 20) {
        sTool.status = "\"" + target + "\" is not in the loaded file.";
        return false;
    }
    TexInfo info;
    std::vector<uint8_t> unused;
    // Only the original's header fields are needed here, so decode failures on the pixels are fine.
    const bool big = original[0] == 1;
    const uint32_t version = Rd32(original.data() + 8, big);
    info.type = Rd32(original.data() + kHeaderSize, big);
    info.width = Rd32(original.data() + kHeaderSize + 4, big);
    info.height = Rd32(original.data() + kHeaderSize + 8, big);
    if (!(info.type == kRGBA32 || info.type == kRGBA16 || (info.type >= kI4 && info.type <= kIA16))) {
        sTool.status = std::string("Texture type ") + TypeName(info.type) + " is not supported.";
        return false;
    }

    int w = 0, h = 0, comp = 0;
    unsigned char* pixels = stbi_load(png.string().c_str(), &w, &h, &comp, 4);
    if (pixels == nullptr) {
        sTool.status = "Could not read " + png.string();
        return false;
    }
    std::vector<uint8_t> packed;
    const bool encoded = EncodePixels(pixels, static_cast<size_t>(w) * h, info.type, packed);
    stbi_image_free(pixels);
    if (!encoded) {
        return false;
    }

    out.target = target;
    out.warning.clear();
    if (static_cast<uint32_t>(w) != info.width || static_cast<uint32_t>(h) != info.height) {
        out.warning = "PNG is " + std::to_string(w) + "x" + std::to_string(h) + ", the original is " +
                      std::to_string(info.width) + "x" + std::to_string(info.height) +
                      ". With a different size there can be scaling and texture errors.";
    }

    // Keep the original header and extra fields, replace size, width, height and pixels.
    const size_t extra = version == 0 ? 0 : 12; // flags, hscale, vscale
    std::vector<uint8_t>& bytes = out.bytes;
    bytes.assign(original.begin(), original.begin() + kHeaderSize);
    Wr32(bytes, info.type, big);
    Wr32(bytes, static_cast<uint32_t>(w), big);
    Wr32(bytes, static_cast<uint32_t>(h), big);
    if (extra != 0) {
        bytes.insert(bytes.end(), original.begin() + kHeaderSize + 12, original.begin() + kHeaderSize + 24);
    }
    Wr32(bytes, static_cast<uint32_t>(packed.size()), big);
    bytes.insert(bytes.end(), packed.begin(), packed.end());
    return true;
}

void AddPending(Pending&& item) {
    for (Pending& existing : sTool.pending) {
        if (existing.target == item.target) {
            existing = std::move(item);
            return;
        }
    }
    sTool.pending.push_back(std::move(item));
}

// Every PNG under the import folder whose relative path (without .png) is a texture of the loaded file.
void ImportFolder() {
    std::vector<std::string> known;
    {
        std::lock_guard<std::mutex> lock(sTool.mutex);
        for (const TexInfo& t : sTool.textures) {
            known.push_back(t.name);
        }
    }
    int added = 0, skipped = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(ImportDir(), ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".png") {
            continue;
        }
        std::string rel = std::filesystem::relative(entry.path(), ImportDir(), ec).generic_string();
        rel.resize(rel.size() - 4);
        if (std::find(known.begin(), known.end(), rel) == known.end()) {
            skipped++;
            continue;
        }
        Pending item;
        if (ConvertPng(entry.path(), rel, item)) {
            AddPending(std::move(item));
            added++;
        } else {
            skipped++;
        }
    }
    sTool.status = "Added " + std::to_string(added) + " textures from the import folder, skipped " +
                   std::to_string(skipped) + ".";
}

void WriteMod() {
    const std::string name = OcarinaStudio::SanitizeName(sTool.modName);
    if (name.empty() || sTool.pending.empty()) {
        sTool.status = "Give the mod a name and add at least one texture.";
        return;
    }
    const std::filesystem::path dir = Ship::Context::GetPathRelativeToAppDirectory("mods");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string path = (dir / (name + ".o2r")).string();

    int err = 0;
    zip_t* archive = zip_open(path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (archive == nullptr) {
        sTool.status = "Could not create " + path;
        return;
    }
    for (const Pending& item : sTool.pending) {
        zip_source_t* source = zip_source_buffer(archive, item.bytes.data(), item.bytes.size(), 0);
        if (source == nullptr || zip_file_add(archive, item.target.c_str(), source, ZIP_FL_OVERWRITE) < 0) {
            zip_source_free(source);
            zip_discard(archive);
            sTool.status = "Failed to add " + item.target;
            return;
        }
    }
    if (zip_close(archive) != 0) {
        zip_discard(archive);
        sTool.status = "Failed to write " + path;
        return;
    }
    sTool.status = "Wrote " + std::to_string(sTool.pending.size()) + " textures to " + path +
                   ". Restart the game to load it.";
}

bool ContainsNoCase(const std::string& text, const char* filter) {
    std::string a = text, b = filter;
    std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return std::tolower(c); });
    std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return std::tolower(c); });
    return a.find(b) != std::string::npos;
}

} // namespace

namespace OcarinaStudio {

void DrawTexturesTab() {
    // 1. Source file
    ImGui::SeparatorText("1. Open an .o2r");
    const std::string ootPath = Ship::Context::GetPathRelativeToAppDirectory("oot.o2r");
    if (ImGui::Button("oot.o2r")) {
        OpenArchive(ootPath);
    }
    ImGui::SameLine();
    if (ImGui::BeginCombo("##mods", "A mod from the mods folder...")) {
        std::error_code ec;
        for (const auto& entry :
             std::filesystem::directory_iterator(Ship::Context::GetPathRelativeToAppDirectory("mods"), ec)) {
            if (entry.path().extension() == ".o2r" && ImGui::Selectable(entry.path().filename().string().c_str())) {
                OpenArchive(entry.path().string());
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(-60.0f);
    ImGui::InputTextWithHint("##custompath", "...or type the full path of an .o2r", sTool.customPath,
                             sizeof(sTool.customPath));
    ImGui::SameLine();
    if (ImGui::Button("Open") && sTool.customPath[0] != '\0') {
        OpenArchive(sTool.customPath);
    }

    if (sTool.scanning) {
        ImGui::TextDisabled("Reading textures... %d / %d", sTool.scanned.load(), sTool.total.load());
    } else if (!sTool.sourcePath.empty()) {
        ImGui::TextDisabled("%s", sTool.sourcePath.c_str());
    }

    // 2. List
    ImGui::SeparatorText("2. Textures");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##texfilter", "Filter (e.g. link_child, eyes, gameplay_keep, spot00)...", sTool.filter,
                             sizeof(sTool.filter));

    std::vector<std::string> shown;
    {
        std::lock_guard<std::mutex> lock(sTool.mutex);
        ImGui::BeginChild("texlist", ImVec2(0, 200.0f), true);
        ImGuiListClipper clipper;
        std::vector<int> visible;
        for (int i = 0; i < static_cast<int>(sTool.textures.size()); i++) {
            if (sTool.filter[0] == '\0' || ContainsNoCase(sTool.textures[i].name, sTool.filter)) {
                visible.push_back(i);
            }
        }
        clipper.Begin(static_cast<int>(visible.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                const int i = visible[row];
                const TexInfo& t = sTool.textures[i];
                ImGui::PushID(i);
                const std::string label = t.name + "   " + std::to_string(t.width) + "x" + std::to_string(t.height) +
                                          " " + TypeName(t.type);
                if (ImGui::Selectable(label.c_str(), sTool.selected == i)) {
                    sTool.selected = i;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        for (int i : visible) {
            shown.push_back(sTool.textures[i].name);
        }
    }
    ImGui::TextDisabled("%zu shown", shown.size());

    std::string selectedName;
    bool selectedSupported = false;
    {
        std::lock_guard<std::mutex> lock(sTool.mutex);
        if (sTool.selected >= 0 && sTool.selected < static_cast<int>(sTool.textures.size())) {
            selectedName = sTool.textures[sTool.selected].name;
            selectedSupported = sTool.textures[sTool.selected].supported;
        }
    }

    ImGui::BeginDisabled(selectedName.empty());
    if (ImGui::Button("Export selected to PNG")) {
        ExportTextures({ selectedName });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(shown.empty());
    if (ImGui::Button("Export all shown to PNG")) {
        ExportTextures(shown);
    }
    ImGui::EndDisabled();

    // 3. Import
    ImGui::SeparatorText("3. Import edited PNGs");
    ImGui::TextWrapped("Put edited PNGs in the import folder with the same layout as the export folder "
                       "(textures/import/<path>.png), or pick one PNG for the selected texture.");
    ImGui::TextDisabled("%s", ImportDir().string().c_str());
    if (ImGui::Button("Add all PNGs from the import folder")) {
        ImportFolder();
    }

    static std::string sPickedPng;
    if (ImGui::BeginCombo("##pickpng", sPickedPng.empty() ? "Pick one PNG for the selected texture..."
                                                         : sPickedPng.c_str())) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(ImportDir(), ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".png") {
                const std::string rel = std::filesystem::relative(entry.path(), ImportDir(), ec).generic_string();
                if (ImGui::Selectable(rel.c_str())) {
                    sPickedPng = rel;
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(selectedName.empty() || !selectedSupported || sPickedPng.empty());
    if (ImGui::Button("Use this PNG for the selected texture")) {
        Pending item;
        if (ConvertPng(ImportDir() / sPickedPng, selectedName, item)) {
            AddPending(std::move(item));
            sTool.status = "Added " + selectedName;
        }
    }
    ImGui::EndDisabled();
    if (!selectedName.empty() && !selectedSupported) {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Palette textures (CI4 / CI8) are not supported.");
    }

    // 4. Pending + export mod
    ImGui::SeparatorText("4. Export mod");
    ImGui::BeginChild("pending", ImVec2(0, 110.0f), true);
    int removeIndex = -1;
    for (int i = 0; i < static_cast<int>(sTool.pending.size()); i++) {
        ImGui::PushID(i);
        if (ImGui::SmallButton("x")) {
            removeIndex = i;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(sTool.pending[i].target.c_str());
        if (!sTool.pending[i].warning.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "  Warning: %s", sTool.pending[i].warning.c_str());
        }
        ImGui::PopID();
    }
    if (sTool.pending.empty()) {
        ImGui::TextDisabled("Nothing added yet.");
    }
    ImGui::EndChild();
    if (removeIndex >= 0) {
        sTool.pending.erase(sTool.pending.begin() + removeIndex);
    }
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputText("Mod name", sTool.modName, sizeof(sTool.modName));
    ImGui::SameLine();
    if (ImGui::Button("Export .o2r")) {
        WriteMod();
    }
    if (!sTool.status.empty()) {
        ImGui::TextWrapped("%s", sTool.status.c_str());
    }
}

} // namespace OcarinaStudio
