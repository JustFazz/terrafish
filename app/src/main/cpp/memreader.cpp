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
    uint64_t projActive = 0x14;        // Entity.active (ARM64)
    uint64_t projAiStyle = 0x0;        // Auto-detected saat runtime (0x60..0x280)
    uint64_t projAiArray = 0x0;        // Auto-detected saat runtime
    uint64_t projLocalAiArray = 0x0;   // Auto-detected saat runtime
};

struct MemoryRegion {
    uint64_t start;
    uint64_t end;
};

static Offsets gOffsets;
static uint64_t gCachedBobberAddr = 0;
static bool gLayoutDetected = false;
static int gLastRegionCount = 0;
static int gLastStyleHits = 0;

inline uint64_t stripTag(uint64_t ptr) {
    return ptr & 0x00FFFFFFFFFFFFFFULL;
}

inline bool isValidUserPtr(uint64_t ptr) {
    uint64_t clean = stripTag(ptr);
    return clean >= 0x10000000ULL && clean <= 0x0000FFFFFFFFFFFFULL && (clean & 0x3) == 0;
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

bool readFloatArrayPair(pid_t pid, uint64_t aiArrPtr, uint64_t localAiArrPtr,
                        float& outAi0, float& outAi1, int& outItemId) {
    if (!isValidUserPtr(aiArrPtr) || !isValidUserPtr(localAiArrPtr)) return false;
    if (stripTag(aiArrPtr) == stripTag(localAiArrPtr)) return false;

    uint8_t aiBuf[0x30] = {0};
    uint8_t localBuf[0x30] = {0};
    if (!readMemory(pid, aiArrPtr, aiBuf, sizeof(aiBuf))) return false;
    if (!readMemory(pid, localAiArrPtr, localBuf, sizeof(localBuf))) return false;

    uint64_t aiKlass = *reinterpret_cast<uint64_t*>(&aiBuf[0x00]);
    uint64_t aiMonitor = *reinterpret_cast<uint64_t*>(&aiBuf[0x08]);
    uint64_t localKlass = *reinterpret_cast<uint64_t*>(&localBuf[0x00]);
    uint64_t localMonitor = *reinterpret_cast<uint64_t*>(&localBuf[0x08]);

    // ai[] dan localAI[] sama-sama bertipe float[] (System.Single[]) sehingga klass pointer identik
    if (!isValidUserPtr(aiKlass) || stripTag(aiKlass) != stripTag(localKlass)) return false;
    if (aiMonitor != 0 || localMonitor != 0) return false;

    uint64_t aiLen = *reinterpret_cast<uint64_t*>(&aiBuf[0x18]);
    uint64_t localAiLen = *reinterpret_cast<uint64_t*>(&localBuf[0x18]);
    if (aiLen < 2 || aiLen > 4 || localAiLen != aiLen) return false;

    float ai0 = *reinterpret_cast<float*>(&aiBuf[0x20]);
    float ai1 = *reinterpret_cast<float*>(&aiBuf[0x24]);
    float localAi1 = *reinterpret_cast<float*>(&localBuf[0x24]);

    if (std::isnan(ai0) || std::isnan(ai1) || std::isnan(localAi1)) return false;
    if (ai0 < -1.0f || ai0 > 5.0f) return false;
    if (ai1 < -1000.0f || ai1 > 1000.0f) return false;
    if (localAi1 < 0.0f || localAi1 > 10000.0f) return false;

    outAi0 = ai0;
    outAi1 = ai1;
    outItemId = static_cast<int>(localAi1);
    return true;
}

bool validateWithKnownLayout(pid_t pid, uint64_t projPtr, float& outAi0, float& outAi1, int& outItemId) {
    if (!gLayoutDetected || !isValidUserPtr(projPtr)) return false;

    uint8_t active = 0;
    int32_t aiStyle = 0;
    if (!readVal(pid, projPtr + gOffsets.projActive, active) || active != 1) return false;
    if (!readVal(pid, projPtr + gOffsets.projAiStyle, aiStyle) || aiStyle != 61) return false;

    uint64_t aiArrPtr = 0;
    uint64_t localAiArrPtr = 0;
    if (!readVal(pid, projPtr + gOffsets.projAiArray, aiArrPtr)) return false;
    if (!readVal(pid, projPtr + gOffsets.projLocalAiArray, localAiArrPtr)) return false;

    return readFloatArrayPair(pid, aiArrPtr, localAiArrPtr, outAi0, outAi1, outItemId);
}

bool tryAutoDiscoverFromStyleAddr(pid_t pid, uint64_t styleAddr, float& outAi0, float& outAi1, int& outItemId) {
    // Jangkauan offset aiStyle pada Terraria 1.4.4 64-bit: 0x60..0x280
    for (uint64_t styleOff = 0x60; styleOff <= 0x280; styleOff += 4) {
        uint64_t projPtr = styleAddr - styleOff;
        if ((projPtr & 0x7) != 0) continue;

        uint8_t obj[0x2A0] = {0};
        if (!readMemory(pid, projPtr, obj, sizeof(obj))) continue;

        uint64_t klass = *reinterpret_cast<uint64_t*>(&obj[0x00]);
        uint64_t monitor = *reinterpret_cast<uint64_t*>(&obj[0x08]);
        int32_t whoAmI = *reinterpret_cast<int32_t*>(&obj[0x10]);
        uint8_t active = obj[0x14];

        if (!isValidUserPtr(klass) || monitor != 0) continue;
        if (whoAmI < 0 || whoAmI > 1000 || active != 1) continue;

        for (uint64_t ptrOff = 0x40; ptrOff <= 0x180; ptrOff += 8) {
            uint64_t p1 = *reinterpret_cast<uint64_t*>(&obj[ptrOff]);
            uint64_t p2 = *reinterpret_cast<uint64_t*>(&obj[ptrOff + 8]);
            if (readFloatArrayPair(pid, p1, p2, outAi0, outAi1, outItemId)) {
                gOffsets.projAiStyle = styleOff;
                gOffsets.projAiArray = ptrOff;
                gOffsets.projLocalAiArray = ptrOff + 8;
                gLayoutDetected = true;
                gCachedBobberAddr = projPtr;
                return true;
            }
        }
    }
    return false;
}

bool findBobberViaHeapScan(pid_t pid, float& ai0, float& ai1, int& itemId) {
    if (gCachedBobberAddr != 0) {
        if (validateWithKnownLayout(pid, gCachedBobberAddr, ai0, ai1, itemId)) {
            return true;
        }
        gCachedBobberAddr = 0;
    }

    auto regions = getWritableHeapRegions(pid);
    gLastRegionCount = static_cast<int>(regions.size());
    int hits = 0;

    const size_t chunkSize = 1024 * 1024; // 1 MB per read
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
                        uint64_t candidateProj = styleAddr - gOffsets.projAiStyle;
                        if (validateWithKnownLayout(pid, candidateProj, ai0, ai1, itemId)) {
                            gCachedBobberAddr = candidateProj;
                            gLastStyleHits = hits;
                            return true;
                        }
                    } else {
                        if (tryAutoDiscoverFromStyleAddr(pid, styleAddr, ai0, ai1, itemId)) {
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
            found = findBobberViaHeapScan(pid, ai0, ai1, itemId);
            scanCooldown = found ? 0 : 12;
        } else {
            scanCooldown--;
        }

        if (found) {
            printf("STATUS|%d|%" PRIx64 " (off:%" PRIx64 "/%" PRIx64 ")|1|%.2f|%.2f|%d\n",
                   pid, il2cppBase, gOffsets.projAiStyle, gOffsets.projAiArray, ai0, ai1, itemId);
        } else {
            printf("STATUS|%d|%" PRIx64 " (R:%d H:%d)|0|0.0|0.0|0\n",
                   pid, il2cppBase, gLastRegionCount, gLastStyleHits);
        }

        usleep(25000);
    }

    return 0;
}
