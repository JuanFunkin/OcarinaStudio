#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include "BinaryWrapper.h"

/*
 * O2R writer (OcarinaStudio replacement).
 *
 * Torch's own writer builds the whole archive in memory with a miniz that has no ZIP64 support: it stops after
 * 65535 entries ("write error") and cannot grow past 4 GB. This one streams every entry straight to a temporary
 * file and writes a ZIP64 end-of-central-directory record when there are more than 65535 entries; the game's
 * reader (libzip) handles that fine.
 */
class ZWrapper : public BinaryWrapper {
public:
    explicit ZWrapper(const std::string& path);
    ~ZWrapper() override;

    int32_t CreateArchive(void) override;
    bool AddFile(const std::string& path, std::vector<char> data) override;
    int32_t Close(void) override;

private:
    struct Entry {
        std::string name;
        uint32_t crc;
        uint32_t compressedSize;
        uint32_t size;
        uint16_t method;
        uint32_t offset;
    };

    void Write(const void* data, size_t size);
    void Check(const std::string& what);

    std::string mTempPath;
    std::ofstream mOut;
    std::vector<Entry> mEntries;
    uint64_t mPosition = 0;
    bool mOpen = false;
    bool mFinalized = false;
};
