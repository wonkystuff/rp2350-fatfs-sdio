// SPDX-License-Identifier: MIT
// Copyright (c) 2026 wonkystuff
// Minimal sparse filesystem fixtures exercise the real, unmodified FatFs parser.
// Only the sector-device boundary is fake; no SDK, formatting tool or card needed.
#include "ff.h"
#include "diskio.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace
{
using Sector = std::array<BYTE, 512>;
std::map<LBA_t, Sector> sectors;
LBA_t capacity = 0, failed_sector = static_cast<LBA_t>(-1);
unsigned reads = 0;
constexpr char kName[] = "Long Loop.wav";
constexpr unsigned kFileSize = 900;

void check(bool condition, const char *message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void put16(BYTE *p, unsigned value) { p[0] = value; p[1] = value >> 8; }
void put32(BYTE *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<BYTE>(value >> (8 * i));
}
void put64(BYTE *p, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i) p[i] = static_cast<BYTE>(value >> (8 * i));
}
BYTE payload(unsigned offset)
{
    constexpr char line[] = "loop.wav\n";
    return offset < sizeof(line) - 1 ? static_cast<BYTE>(line[offset]) : static_cast<BYTE>(offset * 17);
}
void reset()
{
    check(f_mount(nullptr, "0:", 0) == FR_OK, "unregister previous filesystem");
    sectors.clear(); reads = 0; failed_sector = static_cast<LBA_t>(-1);
}
void fileData(LBA_t first)
{
    for (unsigned i = 0; i < kFileSize; ++i) sectors[first + i / 512][i % 512] = payload(i);
}

LBA_t fatFixture(bool fat32)
{
    reset();
    capacity = fat32 ? 70000 : 8192;
    auto &boot = sectors[0];
    boot[0] = 0xeb; boot[1] = 0x58; boot[2] = 0x90;
    put16(boot.data() + 11, 512); boot[13] = 1;
    const unsigned reserved = fat32 ? 32 : 1;
    const unsigned fat_size = fat32 ? 600 : 32;
    put16(boot.data() + 14, reserved); boot[16] = 1;
    put16(boot.data() + 17, fat32 ? 0 : 32);
    if (fat32) put32(boot.data() + 32, capacity);
    else put16(boot.data() + 19, capacity);
    boot[21] = 0xf8;
    if (fat32)
    {
        put32(boot.data() + 36, fat_size); put32(boot.data() + 44, 2);
        std::memcpy(boot.data() + 82, "FAT32   ", 8);
    }
    else put16(boot.data() + 22, fat_size);
    put16(boot.data() + 510, 0xaa55);
    const LBA_t root = reserved + fat_size;
    const LBA_t first_data = root + (fat32 ? 1 : 2);
    const unsigned file_cluster = fat32 ? 3 : 2;
    auto &fat = sectors[reserved];
    if (fat32)
    {
        put32(fat.data(), 0x0ffffff8); put32(fat.data() + 4, 0xffffffff);
        put32(fat.data() + 8, 0x0fffffff);
        put32(fat.data() + 12, 4); put32(fat.data() + 16, 0x0fffffff);
    }
    else
    {
        put16(fat.data(), 0xfff8); put16(fat.data() + 2, 0xffff);
        put16(fat.data() + 4, 3); put16(fat.data() + 6, 0xffff);
    }
    auto &directory = sectors[root];
    constexpr char short_name[] = "LONGLO~1WAV";
    BYTE checksum = 0;
    for (unsigned i = 0; i < 11; ++i)
        checksum = static_cast<BYTE>(((checksum & 1) << 7) + (checksum >> 1) + short_name[i]);
    directory[0] = 0x41; directory[11] = 0x0f; directory[13] = checksum;
    constexpr unsigned positions[] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    static_assert(sizeof(kName) - 1 == 13);
    for (unsigned i = 0; i < 13; ++i) put16(directory.data() + positions[i], kName[i]);
    std::memcpy(directory.data() + 32, short_name, 11); directory[43] = AM_ARC;
    put16(directory.data() + 58, file_cluster); put32(directory.data() + 60, kFileSize);
    fileData(first_data);
    return first_data;
}

LBA_t exfatFixture()
{
    reset(); capacity = 288;
    auto &boot = sectors[0];
    boot[0] = 0xeb; boot[1] = 0x76; boot[2] = 0x90;
    std::memcpy(boot.data() + 3, "EXFAT   ", 8);
    put64(boot.data() + 72, capacity); put32(boot.data() + 80, 24);
    put32(boot.data() + 84, 2); put32(boot.data() + 88, 32);
    put32(boot.data() + 92, 256); put32(boot.data() + 96, 2);
    put16(boot.data() + 104, 0x100); boot[108] = 9; boot[110] = 1;
    put16(boot.data() + 510, 0xaa55);
    put32(sectors[24].data() + 8, 0xffffffff); // Root directory cluster.
    put32(sectors[24].data() + 12, 0xffffffff); // Contiguous bitmap cluster.
    auto &directory = sectors[32];
    directory[0] = 0x81; put32(directory.data() + 20, 3); put64(directory.data() + 24, 32);
    sectors[33][0] = 0x0f; // Root, bitmap and two contiguous data clusters.
    BYTE *entry = directory.data() + 32;
    entry[0] = 0x85; entry[1] = 2; put16(entry + 4, AM_ARC);
    entry[32] = 0xc0; entry[33] = 3; entry[35] = sizeof(kName) - 1;
    uint16_t hash = 0;
    for (unsigned i = 0; i < sizeof(kName) - 1; ++i)
    {
        const unsigned upper = kName[i] >= 'a' && kName[i] <= 'z' ? kName[i] - 'a' + 'A' : kName[i];
        hash = static_cast<uint16_t>((hash >> 1) | (hash << 15)); hash += upper;
        hash = static_cast<uint16_t>((hash >> 1) | (hash << 15));
    }
    put16(entry + 36, hash); put64(entry + 40, kFileSize);
    put32(entry + 52, 4); put64(entry + 56, kFileSize);
    entry[64] = 0xc1;
    for (unsigned i = 0; i < sizeof(kName) - 1; ++i) put16(entry + 66 + 2 * i, kName[i]);
    uint16_t checksum = 0;
    for (unsigned i = 0; i < 96; ++i)
        if (i != 2 && i != 3)
            checksum = static_cast<uint16_t>(((checksum >> 1) | (checksum << 15)) + entry[i]);
    put16(entry + 2, checksum);
    fileData(34);
    return 34;
}

void verifyFile(BYTE expected_type, LBA_t data_sector)
{
    FATFS fs{};
    const auto mounted = f_mount(&fs, "0:", 1);
    if (mounted != FR_OK || fs.fs_type != expected_type)
        std::fprintf(stderr, "mount result=%u type=%u expected=%u\n", mounted, fs.fs_type, expected_type);
    check(mounted == FR_OK && fs.fs_type == expected_type, "mount supported filesystem");
    DIR directory{}; FILINFO info{};
    check(f_opendir(&directory, "0:/") == FR_OK && f_readdir(&directory, &info) == FR_OK &&
          std::strcmp(info.fname, kName) == 0 && info.fsize == kFileSize, "list long filename and size");
    check(f_readdir(&directory, &info) == FR_OK && info.fname[0] == 0, "directory reaches its end");
    check(f_closedir(&directory) == FR_OK, "close directory");
    FIL file{}; std::array<BYTE, 1024> data{}; UINT count = 0;
    check(f_open(&file, "0:/Long Loop.wav", FA_READ) == FR_OK, "open long filename");
    check(f_read(&file, data.data(), data.size(), &count) == FR_OK && count == kFileSize, "read file across sector/cluster boundary to EOF");
    for (unsigned i = 0; i < count; ++i) check(data[i] == payload(i), "read content matches fixture");
    check(f_lseek(&file, 509) == FR_OK && f_read(&file, data.data(), 10, &count) == FR_OK && count == 10, "seek and read across boundary");
    for (unsigned i = 0; i < count; ++i) check(data[i] == payload(509 + i), "seek content matches fixture");
    char line[32]{};
    check(f_lseek(&file, 0) == FR_OK && f_gets(line, sizeof(line), &file) &&
          std::strcmp(line, "loop.wav\n") == 0, "read set-style text line");
    check(f_close(&file) == FR_OK, "close file");
    check(f_open(&file, "0:/missing.wav", FA_READ) == FR_NO_FILE, "missing files are rejected");
    check(f_open(&file, "0:/Long Loop.wav", FA_READ) == FR_OK, "reopen file for device failure");
    failed_sector = data_sector + 1;
    check(f_read(&file, data.data(), data.size(), &count) == FR_DISK_ERR, "sector read failures reach filesystem caller");
    failed_sector = static_cast<LBA_t>(-1);
    f_close(&file);
    check(f_mount(nullptr, "0:", 0) == FR_OK, "unmount filesystem before its owner expires");
}
}

extern "C" DSTATUS disk_initialize(BYTE drive) { return drive == 0 ? 0 : STA_NOINIT; }
extern "C" DSTATUS disk_status(BYTE drive) { return drive == 0 ? 0 : STA_NOINIT; }
extern "C" DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t sector, UINT count)
{
    check(++reads < 1000, "filesystem access remains bounded for these fixtures");
    if (drive || !count || sector >= capacity || count > capacity - sector) return RES_PARERR;
    if (failed_sector >= sector && failed_sector - sector < count) return RES_ERROR;
    for (UINT i = 0; i < count; ++i)
    {
        const auto found = sectors.find(sector + i);
        if (found == sectors.end()) std::memset(buffer + i * 512, 0, 512);
        else std::memcpy(buffer + i * 512, found->second.data(), 512);
    }
    return RES_OK;
}
extern "C" DRESULT disk_ioctl(BYTE, BYTE, void *) { return RES_PARERR; }

int main()
{
    auto data_sector = fatFixture(false); verifyFile(FS_FAT16, data_sector);
    data_sector = fatFixture(true); verifyFile(FS_FAT32, data_sector);
    data_sector = exfatFixture(); verifyFile(FS_EXFAT, data_sector);
    FATFS fs{};
    fatFixture(true); put32(sectors[0].data() + 36, 0x200000);
    check(f_mount(&fs, "0:", 1) == FR_NO_FILESYSTEM, "reject excessive FAT size in BPB");
    exfatFixture(); put32(sectors[0].data() + 92, 0);
    check(f_mount(&fs, "0:", 1) == FR_NO_FILESYSTEM, "reject zero exFAT cluster count");
    exfatFixture(); put32(sectors[0].data() + 92, 1);
    check(f_mount(&fs, "0:", 1) == FR_NO_FILESYSTEM, "reject undersized exFAT cluster count");
    reset(); capacity = 8192;
    check(f_mount(&fs, "0:", 1) == FR_NO_FILESYSTEM, "reject blank media");
    check(f_mount(nullptr, "0:", 0) == FR_OK, "unregister final fixture");
    std::puts("FatFs filesystem scenarios passed");
}
