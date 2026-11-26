#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cinttypes>
#include <cmath>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/select.h>
#include <cctype>
#include <vector>
#include <string>
#include <algorithm>

struct Offsets {
    uint64_t projAiStyle = 0x0;
    uint64_t projAiArray = 0x0;
    uint64_t projLocalAiArray = 0x0;
};

struct MemoryRegion {
    uint64_t start;
    uint64_t end;
};

static Offsets gOffsets;
static uint64_t gCachedBobberAddr = 0;
static bool gLayoutDetected = false;

static float gLastLocalAi0 = -9999.0f;
static float gLastAi1 = -9999.0f;
static int gStaleTicks = 0;

static int gLastRegionCount = 0;
static int gLastStyleHits = 0;
static int gLastProjCandidates = 0;

inline uint64_t stripTag(uint64_t ptr) {
    return ptr & 0x00FFFFFFFFFFFFFFULL;
}

inline bool isValidUserPtr(uint64_t ptr) {
    uint64_t clean = stripTag(ptr);
    return clean >= 0x10000000ULL && clean <= 0x0000FFFFFFFFFFFFULL && (clean & 0x7) == 0;
}

bool readMemory(pid_t pid, uint64_t address, void* buffer, size_t size) {
    address = stripTag(address);
    if (address < 0x10000ULL || address > 0x0000FFFFFFFFFFFFULL || size == 0) return false;

    struct iovec local[1];
    struct iovec remote[1];
    local[0].iov_base = buffer;
    local[0].iov_len = size;
    remote[0].iov_base = reinterpret_cast<void*>(static_cast<uintptr_t>(address));
    remote[0].iov_len = size;

    ssize_t nread = process_vm_readv(pid, local, 1, remote, 1, 0);
    return nread == static_cast<ssize_t>(size);
}

template <typename T>
bool readVal(pid_t pid, uint64_t address, T& out) {
    return readMemory(pid, address, &out, sizeof(T));
}

bool isValidIl2CppObjectHeader(pid_t pid, uint64_t objPtr) {
    if (!isValidUserPtr(objPtr)) return false;
    uint64_t header[2] = {0, 0};
    if (!readMemory(pid, objPtr, header, sizeof(header))) return false;
    if (!isValidUserPtr(header[0]) || header[1] != 0) return false;

    // Verifikasi bahwa pointer klass menunjuk ke struct Il2CppClass yang valid
    uint64_t imagePtr = 0;
    if (!readVal(pid, header[0], imagePtr)) return false;
    return isValidUserPtr(imagePtr);
}

pid_t findPackagePid(const char* packageName) {
    DIR* dir = opendir("/proc");
    if (!dir) return -1;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (!isdigit(static_cast<unsigned char>(entry->d_name[0]))) continue;
        pid_t pid = atoi(entry->d_name);
        char cmdPath[64];
        snprintf(cmdPath, sizeof(cmdPath), "/proc/%d/cmdline", pid);

        FILE* fp = fopen(cmdPath, "r");
        if (fp) {
            char cmdline[256] = {0};
            size_t len = fread(cmdline, 1, sizeof(cmdline) - 1, fp);
            fclose(fp);
            if (len > 0 && strcmp(cmdline, packageName) == 0) {
                closedir(dir);
                return pid;
            }
        }
    }
    closedir(dir);
    return -1;
}

uint64_t getModuleBase(pid_t pid, const char* moduleName) {
    char mapsPath[64];
    snprintf(mapsPath, sizeof(mapsPath), "/proc/%d/maps", pid);
    FILE* fp = fopen(mapsPath, "r");
    if (!fp) return 0;

    char line[512];
    uint64_t baseAddr = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, moduleName)) {
            uint64_t start = 0;
            if (sscanf(line, "%" SCNx64 "-", &start) == 1) {
                baseAddr = start;
                break;
            }
        }
    }
    fclose(fp);
    return baseAddr;
}

std::vector<MemoryRegion> getWritableHeapRegions(pid_t pid) {
    std::vector<MemoryRegion> regions;
    char mapsPath[64];
    snprintf(mapsPath, sizeof(mapsPath), "/proc/%d/maps", pid);
    FILE* fp = fopen(mapsPath, "r");
    if (!fp) return regions;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        uint64_t start = 0, end = 0;
        char perms[8] = {0};
        if (sscanf(line, "%" SCNx64 "-%" SCNx64 " %7s", &start, &end, perms) == 3) {
            if (perms[0] == 'r' && perms[1] == 'w' && perms[3] == 'p') {
                if (strstr(line, "[anon:") != nullptr ||
                    strstr(line, "[heap]") != nullptr ||
                    strchr(line, '/') == nullptr) {
                    uint64_t len = end - start;
                    if (len >= 0x1000 && len <= 512ULL * 1024ULL * 1024ULL) {
                        regions.push_back({start, end});
                    }
                }
            }
        }
    }
    fclose(fp);
    return regions;
}

bool readFloatArray(pid_t pid, uint64_t arrPtr, uint64_t& outKlass, float& e0, float& e1) {
    if (!isValidUserPtr(arrPtr)) return false;

    uint8_t buf[0x28] = {0};
    if (!readMemory(pid, arrPtr, buf, sizeof(buf))) return false;

    uint64_t klass = *reinterpret_cast<uint64_t*>(&buf[0x00]);
    uint64_t monitor = *reinterpret_cast<uint64_t*>(&buf[0x08]);
    uint64_t len = *reinterpret_cast<uint64_t*>(&buf[0x18]);

    if (!isValidUserPtr(klass) || monitor != 0) return false;
    if (len < 2 || len > 8) return false;

    float v0 = *reinterpret_cast<float*>(&buf[0x20]);
    float v1 = *reinterpret_cast<float*>(&buf[0x24]);
    if (std::isnan(v0) || std::isnan(v1) || std::isinf(v0) || std::isinf(v1)) return false;

    outKlass = stripTag(klass);
    e0 = v0;
    e1 = v1;
    return true;
}

bool classifyAiAndLocalAi(float a0, float a1, float b0, float b1,
                          bool& aIsAi, float& outAi0, float& outAi1, float& outLocalAi0, int& outItemId) {
    auto isBobberState = [](float x) {
        return std::fabs(x - 0.0f) < 0.01f || std::fabs(x - 1.0f) < 0.01f || std::fabs(x - 2.0f) < 0.01f;
    };

    bool aValidState = isBobberState(a0) && (a1 >= -500.0f && a1 <= 10000.0f);
    bool bValidState = isBobberState(b0) && (b1 >= -500.0f && b1 <= 10000.0f);

    if (aValidState && b0 >= 0.0f && b0 <= 5000.0f && (!bValidState || b0 > 2.5f || a1 < 0.0f)) {
        aIsAi = true;
        outAi0 = a0;
        outAi1 = a1;
        outLocalAi0 = b0;
        outItemId = static_cast<int>(b1);
        return true;
    }
    if (bValidState && a0 >= 0.0f && a0 <= 5000.0f && (!aValidState || a0 > 2.5f || b1 < 0.0f)) {
        aIsAi = false;
        outAi0 = b0;
        outAi1 = b1;
        outLocalAi0 = a0;
        outItemId = static_cast<int>(a1);
        return true;
    }
    return false;
}

bool readBobberWithKnownOffsets(pid_t pid, uint64_t projPtr,
                                float& outAi0, float& outAi1, float& outLocalAi0, int& outItemId) {
    if (!gLayoutDetected || !isValidUserPtr(projPtr)) return false;

    int32_t aiStyle = 0;
    if (!readVal(pid, projPtr + gOffsets.projAiStyle, aiStyle) || aiStyle != 61) return false;

    uint64_t aiPtr = 0, localAiPtr = 0;
    if (!readVal(pid, projPtr + gOffsets.projAiArray, aiPtr)) return false;
    if (!readVal(pid, projPtr + gOffsets.projLocalAiArray, localAiPtr)) return false;

    uint64_t k1 = 0, k2 = 0;
    float ai0 = 0.0f, ai1 = 0.0f, loc0 = 0.0f, loc1 = 0.0f;
    if (!readFloatArray(pid, aiPtr, k1, ai0, ai1)) return false;
    if (!readFloatArray(pid, localAiPtr, k2, loc0, loc1)) return false;
    if (k1 != k2) return false;

    outAi0 = ai0;
    outAi1 = ai1;
    outLocalAi0 = loc0;
    outItemId = static_cast<int>(loc1);
    return true;
}

bool tryDiscoverFromStyleAddr(pid_t pid, uint64_t styleAddr,
                              float& outAi0, float& outAi1, float& outLocalAi0, int& outItemId) {
    for (uint64_t styleOff = 0x50; styleOff <= 0x380; styleOff += 4) {
        uint64_t projPtr = styleAddr - styleOff;
        if (!isValidIl2CppObjectHeader(pid, projPtr)) continue;

        gLastProjCandidates++;

        uint8_t obj[0x3A0] = {0};
        size_t readLen = std::min<size_t>(sizeof(obj), static_cast<size_t>(styleOff + 0x20));
        if (!readMemory(pid, projPtr, obj, readLen)) continue;

        struct ArrayCand {
            uint64_t fieldOff;
            uint64_t klass;
            float e0;
            float e1;
        };
        std::vector<ArrayCand> arrays;

        for (uint64_t off = 0x30; off + 8 <= readLen; off += 8) {
            uint64_t ptr = *reinterpret_cast<uint64_t*>(&obj[off]);
            uint64_t arrKlass = 0;
            float v0 = 0.0f, v1 = 0.0f;
            if (readFloatArray(pid, ptr, arrKlass, v0, v1)) {
                arrays.push_back({off, arrKlass, v0, v1});
            }
        }

        for (size_t i = 0; i < arrays.size(); ++i) {
            for (size_t j = i + 1; j < arrays.size(); ++j) {
                if (arrays[i].klass != arrays[j].klass) continue;

                bool iIsAi = false;
                float ai0 = 0.0f, ai1 = 0.0f, loc0 = 0.0f;
                int itemId = 0;
                if (classifyAiAndLocalAi(arrays[i].e0, arrays[i].e1,
                                         arrays[j].e0, arrays[j].e1,
                                         iIsAi, ai0, ai1, loc0, itemId)) {
                    // Hanya kunci pelampung yang sedang aktif (localAI[0] > 0 atau sedang digigit)
                    if (loc0 > 0.5f || ai1 < -0.1f) {
                        gOffsets.projAiStyle = styleOff;
                        gOffsets.projAiArray = iIsAi ? arrays[i].fieldOff : arrays[j].fieldOff;
                        gOffsets.projLocalAiArray = iIsAi ? arrays[j].fieldOff : arrays[i].fieldOff;
                        gLayoutDetected = true;
                        gCachedBobberAddr = projPtr;
                        gLastLocalAi0 = loc0;
                        gLastAi1 = ai1;
                        gStaleTicks = 0;

                        outAi0 = ai0;
                        outAi1 = ai1;
                        outLocalAi0 = loc0;
                        outItemId = itemId;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool findActiveBobber(pid_t pid, float& ai0, float& ai1, int& itemId) {
    float loc0 = 0.0f;

    if (gCachedBobberAddr != 0) {
        if (readBobberWithKnownOffsets(pid, gCachedBobberAddr, ai0, ai1, loc0, itemId)) {
            bool changed = (std::fabs(loc0 - gLastLocalAi0) > 0.01f) || (std::fabs(ai1 - gLastAi1) > 0.01f);
            gLastLocalAi0 = loc0;
            gLastAi1 = ai1;

            if (changed || ai1 < -0.1f) {
                gStaleTicks = 0;
                return true;
            }

            // Jika angka berhenti berubah > 14 tick (~350ms), pelampung sudah ditarik/hilang
            gStaleTicks++;
            if (gStaleTicks <= 14) {
                return true;
            }
        }
        gCachedBobberAddr = 0;
        gStaleTicks = 0;
    }

    auto regions = getWritableHeapRegions(pid);
    gLastRegionCount = static_cast<int>(regions.size());
    int hits = 0;
    gLastProjCandidates = 0;

    const size_t chunkSize = 1024 * 1024;
    std::vector<uint8_t> buf(chunkSize);

    for (const auto& reg : regions) {
        for (uint64_t addr = reg.start; addr < reg.end; addr += chunkSize) {
            size_t toRead = std::min<size_t>(chunkSize, static_cast<size_t>(reg.end - addr));
            if (!readMemory(pid, addr, buf.data(), toRead)) continue;

            for (size_t off = 0; off + 4 <= toRead; off += 4) {
                int32_t val = *reinterpret_cast<int32_t*>(&buf[off]);
                if (val == 61) {
                    hits++;
                    uint64_t styleAddr = addr + off;
                    if (gLayoutDetected) {
                        uint64_t candProj = styleAddr - gOffsets.projAiStyle;
                        if (readBobberWithKnownOffsets(pid, candProj, ai0, ai1, loc0, itemId)) {
                            if (loc0 > 0.5f || ai1 < -0.1f) {
                                gCachedBobberAddr = candProj;
                                gLastLocalAi0 = loc0;
                                gLastAi1 = ai1;
                                gStaleTicks = 0;
                                gLastStyleHits = hits;
                                return true;
                            }
                        }
                    } else {
                        if (tryDiscoverFromStyleAddr(pid, styleAddr, ai0, ai1, loc0, itemId)) {
                            gLastStyleHits = hits;
                            return true;
                        }
                    }
                }
            }
        }
    }
    gLastStyleHits = hits;
    return false;
}

void processStdinCommands() {
    fd_set readfds;
    struct timeval tv;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    if (select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv) > 0) {
        char cmd[256];
        if (fgets(cmd, sizeof(cmd), stdin)) {
            int x = 0, y = 0;
            if (sscanf(cmd, "TAP %d %d", &x, &y) == 2) {
                char shellCmd[96];
                snprintf(shellCmd, sizeof(shellCmd), "input tap %d %d", x, y);
                int rc = system(shellCmd);
                (void)rc;
            } else if (strncmp(cmd, "QUIT", 4) == 0) {
                exit(0);
            }
        }
    }
}

int main(int argc, char** argv) {
    if (geteuid() != 0) {
        fprintf(stderr, "ERROR: memreader must run as root (UID 0)\n");
        return 1;
    }

    const char* pkgName = "com.and.games505.TerrariaPaid";
    if (argc >= 2) pkgName = argv[1];

    setvbuf(stdout, nullptr, _IONBF, 0);

    int scanCooldown = 0;
    while (true) {
        processStdinCommands();

        pid_t pid = findPackagePid(pkgName);
        if (pid <= 0) {
            printf("STATUS|0|0|0|0.0|0.0|0\n");
            usleep(500000);
            continue;
        }

        uint64_t il2cppBase = getModuleBase(pid, "libil2cpp.so");
        float ai0 = 0.0f, ai1 = 0.0f;
        int itemId = 0;
        bool found = false;

        if (gCachedBobberAddr != 0 || scanCooldown <= 0) {
            found = findActiveBobber(pid, ai0, ai1, itemId);
            scanCooldown = found ? 0 : 12;
        } else {
            scanCooldown--;
        }

        if (found) {
            printf("STATUS|%d|%" PRIx64 " (off:%" PRIx64 "/%" PRIx64 "/%" PRIx64 ")|1|%.2f|%.2f|%d\n",
                   pid, il2cppBase,
                   gOffsets.projAiStyle, gOffsets.projAiArray, gOffsets.projLocalAiArray,
                   ai0, ai1, itemId);
        } else {
            printf("STATUS|%d|%" PRIx64 " (R:%d H:%d C:%d)|0|0.0|0.0|0\n",
                   pid, il2cppBase, gLastRegionCount, gLastStyleHits, gLastProjCandidates);
        }

        usleep(25000);
    }

    return 0;
}
