#include "ZWrapper.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "spdlog/spdlog.h"
#include <Companion.h>
#include <miniz/zip_file.hpp>

namespace fs = std::filesystem;

namespace {

void Put16(std::string& out, uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void Put32(std::string& out, uint32_t v) {
    for (int i = 0; i < 4; i++) {
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
}

void Put64(std::string& out, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
}

constexpr uint16_t kUtf8Names = 0x0800;
constexpr uint16_t kDosDate1980 = (1 << 5) | 1; // 1980-01-01, time 00:00

} // namespace

ZWrapper::ZWrapper(const std::string& path) {
    this->mPath = path;
    this->mTempPath = path + ".tmp";
}

ZWrapper::~ZWrapper() {
    if (this->mOpen) {
        this->mOut.close();
    }
    if (!this->mFinalized) {
        std::error_code ec;
        fs::remove(this->mTempPath, ec);
    }
}

void ZWrapper::Write(const void* data, size_t size) {
    this->mOut.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    this->mPosition += size;
}

void ZWrapper::Check(const std::string& what) {
    if (!this->mOut.good()) {
        throw std::runtime_error("[ZWrapper] Could not write " + what + " to '" + this->mTempPath +
                                 "' (disk full or the folder is not writable?)");
    }
}

int32_t ZWrapper::CreateArchive(void) {
    const fs::path parent = fs::path(this->mTempPath).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
    }
    this->mOut.open(this->mTempPath, std::ios::binary | std::ios::trunc);
    if (!this->mOut.is_open()) {
        throw std::runtime_error("[ZWrapper] Could not create '" + this->mTempPath + "'");
    }
    this->mOpen = true;
    this->mPosition = 0;
    this->mEntries.clear();
    SPDLOG_INFO("Loaded ZIP (O2R) archive: {}", this->mPath.c_str());
    return 0;
}

bool ZWrapper::AddFile(const std::string& path, std::vector<char> data) {
    if (Companion::Instance != nullptr && Companion::Instance->IsDebug()) {
        SPDLOG_INFO("Creating debug file: debug/{}", path);
        std::string dpath = "debug/" + path;
        if (!fs::exists(fs::path(dpath).parent_path())) {
            fs::create_directories(fs::path(dpath).parent_path());
        }
        std::ofstream stream(dpath, std::ios::binary);
        stream.write(data.data(), data.size());
        stream.close();
    }

    std::lock_guard<std::mutex> guard(this->mMutex);

    if (!this->mOpen) {
        throw std::runtime_error("[ZWrapper] Cannot add '" + path + "': the archive is not open");
    }
    if (path.size() > 0xFFFF || data.size() > 0xFFFFFFFFull || this->mPosition > 0xF0000000ull) {
        throw std::runtime_error("[ZWrapper] '" + path + "' does not fit in the archive (name, size or total size)");
    }

    Entry entry;
    entry.name = path;
    entry.size = static_cast<uint32_t>(data.size());
    entry.crc = static_cast<uint32_t>(mz_crc32(mz_crc32(0, nullptr, 0), reinterpret_cast<const unsigned char*>(data.data()),
                                              data.size()));
    entry.method = 0;
    entry.compressedSize = entry.size;
    entry.offset = static_cast<uint32_t>(this->mPosition);

    // Raw deflate (negative window bits = no zlib header), as zip wants it.
    void* packed = nullptr;
    size_t packedSize = 0;
    if (!data.empty()) {
        const int flags = static_cast<int>(tdefl_create_comp_flags_from_zip_params(MZ_BEST_COMPRESSION, -15, MZ_DEFAULT_STRATEGY));
        packed = tdefl_compress_mem_to_heap(data.data(), data.size(), &packedSize, flags);
        if (packed != nullptr && packedSize < data.size()) {
            entry.method = 8;
            entry.compressedSize = static_cast<uint32_t>(packedSize);
        }
    }

    std::string header;
    Put32(header, 0x04034b50);
    Put16(header, entry.method == 0 ? 10 : 20); // version needed
    Put16(header, kUtf8Names);
    Put16(header, entry.method);
    Put16(header, 0);            // time
    Put16(header, kDosDate1980); // date
    Put32(header, entry.crc);
    Put32(header, entry.compressedSize);
    Put32(header, entry.size);
    Put16(header, static_cast<uint16_t>(entry.name.size()));
    Put16(header, 0); // extra length
    header += entry.name;
    this->Write(header.data(), header.size());
    if (entry.method == 8) {
        this->Write(packed, entry.compressedSize);
    } else if (!data.empty()) {
        this->Write(data.data(), data.size());
    }
    if (packed != nullptr) {
        mz_free(packed);
    }

    try {
        this->Check("'" + path + "'");
    } catch (const std::exception& e) {
        SPDLOG_ERROR("{} ({} bytes)", e.what(), data.size());
        throw;
    }
    this->mEntries.push_back(std::move(entry));
    return true;
}

int32_t ZWrapper::Close(void) {
    if (!this->mOpen) {
        return -1;
    }

    const uint64_t centralOffset = this->mPosition;
    std::string central;
    for (const Entry& e : this->mEntries) {
        Put32(central, 0x02014b50);
        Put16(central, 20); // made by
        Put16(central, e.method == 0 ? 10 : 20);
        Put16(central, kUtf8Names);
        Put16(central, e.method);
        Put16(central, 0);
        Put16(central, kDosDate1980);
        Put32(central, e.crc);
        Put32(central, e.compressedSize);
        Put32(central, e.size);
        Put16(central, static_cast<uint16_t>(e.name.size()));
        Put16(central, 0); // extra
        Put16(central, 0); // comment
        Put16(central, 0); // disk
        Put16(central, 0); // internal attributes
        Put32(central, 0); // external attributes
        Put32(central, e.offset);
        central += e.name;
        if (central.size() > (1u << 22)) {
            this->Write(central.data(), central.size());
            central.clear();
        }
    }
    this->Write(central.data(), central.size());
    const uint64_t centralSize = this->mPosition - centralOffset;
    const uint64_t count = this->mEntries.size();

    std::string tail;
    if (count >= 0xFFFF || centralOffset >= 0xFFFFFFFFull || centralSize >= 0xFFFFFFFFull) {
        const uint64_t zip64Offset = this->mPosition;
        Put32(tail, 0x06064b50); // ZIP64 end of central directory
        Put64(tail, 44);
        Put16(tail, 45);
        Put16(tail, 45);
        Put32(tail, 0);
        Put32(tail, 0);
        Put64(tail, count);
        Put64(tail, count);
        Put64(tail, centralSize);
        Put64(tail, centralOffset);
        Put32(tail, 0x07064b50); // ZIP64 locator
        Put32(tail, 0);
        Put64(tail, zip64Offset);
        Put32(tail, 1);
    }
    Put32(tail, 0x06054b50); // end of central directory
    Put16(tail, 0);
    Put16(tail, 0);
    Put16(tail, static_cast<uint16_t>(std::min<uint64_t>(count, 0xFFFF)));
    Put16(tail, static_cast<uint16_t>(std::min<uint64_t>(count, 0xFFFF)));
    Put32(tail, static_cast<uint32_t>(std::min<uint64_t>(centralSize, 0xFFFFFFFFull)));
    Put32(tail, static_cast<uint32_t>(std::min<uint64_t>(centralOffset, 0xFFFFFFFFull)));
    Put16(tail, 0);
    this->Write(tail.data(), tail.size());

    this->mOut.flush();
    this->Check("the archive");
    this->mOut.close();
    this->mOpen = false;

    std::error_code ec;
    fs::remove(this->mPath, ec);
    fs::rename(this->mTempPath, this->mPath, ec);
    if (ec) {
        throw std::runtime_error("[ZWrapper] Could not move '" + this->mTempPath + "' to '" + this->mPath +
                                 "': " + ec.message());
    }
    this->mFinalized = true;
    SPDLOG_INFO("Wrote {} entries to {}", count, this->mPath);
    return 0;
}
