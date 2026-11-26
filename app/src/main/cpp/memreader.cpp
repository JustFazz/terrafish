#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cinttypes>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/select.h>
#include <cctype>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

struct Offsets {
    uint64_t mainTypeInfo = 0x0;       // RVA of Terraria.Main_TypeInfo in libil2cpp.so (0 = auto heap scan)
    uint64_t staticFields = 0xB8;      // Il2CppClass::static_fields
    uint64_t projectileField = 0x168;  // Main.projectile static field offset
    uint64_t projActive = 0x20;        // Entity.active (bool)
    uint64_t projAiStyle = 0x94;       // Projectile.aiStyle (int32, 61 = Fishing Bobber)
    uint64_t projAiArray = 0xB8;       // Projectile.ai (float[])
    uint64_t projLocalAiArray = 0xC0;  // Projectile.localAI (float[])
};

struct MemoryRegion {
    uint64_t start;
    uint64_t end;
};

static Offsets gOffsets;
static uint64_t gCachedBobberAddr = 0;

bool readMemory(pid_t pid, uint64_t address, void* buffer, size_t size) {
    if (address < 0x10000 || size == 0) return false;
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
                if (strstr(line, "[anon:") != nullptr || strchr(line, '/') == nullptr) {
                    uint64_t len = end - start;
                    if (len >= 0x1000 && len <= 64ULL * 1024ULL * 1024ULL) {
                        regions.push_back({start, end});
                    }
                }
            }
        }
    }
    fclose(fp);
    return regions;
}

bool validateAndReadBobber(pid_t pid, uint64_t projPtr, float& outAi0, float& outAi1, int& outItemId) {
    if (projPtr < 0x10000000ULL || (projPtr & 0x7) != 0) return false;

    uint8_t active = 0;
    int32_t aiStyle = 0;
    if (!readVal(pid, projPtr + gOffsets.projActive, active) || active != 1) return false;
    if (!readVal(pid, projPtr + gOffsets.projAiStyle, aiStyle) || aiStyle != 61) return false;

    uint64_t aiArrPtr = 0;
    uint64_t localAiArrPtr = 0;
    if (!readVal(pid, projPtr + gOffsets.projAiArray, aiArrPtr)) return false;
    if (!readVal(pid, projPtr + gOffsets.projLocalAiArray, localAiArrPtr)) return false;

    if (aiArrPtr < 0x10000000ULL || localAiArrPtr < 0x10000000ULL) return false;

    // IL2CPP Array layout on ARM64:
    // +0x18: max_length (uint64_t)
    // +0x20: elements[0]
    uint64_t aiLen = 0, localAiLen = 0;
    if (!readVal(pid, aiArrPtr + 0x18, aiLen) || aiLen < 2 || aiLen > 16) return false;
    if (!readVal(pid, localAiArrPtr + 0x18, localAiLen) || localAiLen < 2 || localAiLen > 16) return false;

    float ai[2] = {0.0f, 0.0f};
    float localAi[2] = {0.0f, 0.0f};
    if (!readMemory(pid, aiArrPtr + 0x20, ai, sizeof(ai))) return false;
    if (!readMemory(pid, localAiArrPtr + 0x20, localAi, sizeof(localAi))) return false;

    // Sanity check bobber state values:
    // ai[0] is state (0.0 = floating, 1.0 = reeling, 2.0 = broken)
    if (ai[0] < -1.0f || ai[0] > 5.0f) return false;
    // ai[1] is bite timer (0 when idle, negative when biting, positive while reeling)
    if (ai[1] < -1000.0f || ai[1] > 1000.0f) return false;

    outAi0 = ai[0];
    outAi1 = ai[1];
    outItemId = static_cast<int>(localAi[1]);
    return true;
}

// Mode 1: Direct O(1) IL2CPP pointer chain lookup via Main.projectile[]
bool findBobberViaPointerChain(pid_t pid, uint64_t il2cppBase, float& ai0, float& ai1, int& itemId) {
    if (gOffsets.mainTypeInfo == 0 || il2cppBase == 0) return false;

    uint64_t mainClassPtr = 0;
    if (!readVal(pid, il2cppBase + gOffsets.mainTypeInfo, mainClassPtr) || mainClassPtr == 0) return false;

    uint64_t staticFieldsPtr = 0;
    if (!readVal(pid, mainClassPtr + gOffsets.staticFields, staticFieldsPtr) || staticFieldsPtr == 0) return false;

    uint64_t projArrayPtr = 0;
    if (!readVal(pid, staticFieldsPtr + gOffsets.projectileField, projArrayPtr) || projArrayPtr == 0) return false;

    uint64_t arrayLen = 0;
    if (!readVal(pid, projArrayPtr + 0x18, arrayLen) || arrayLen == 0 || arrayLen > 2000) return false;

    size_t scanCount = std::min<size_t>(static_cast<size_t>(arrayLen), 1001);
    std::vector<uint64_t> elements(scanCount);
    if (!readMemory(pid, projArrayPtr + 0x20, elements.data(), scanCount * sizeof(uint64_t))) return false;

    for (size_t i = 0; i < scanCount; ++i) {
        if (validateAndReadBobber(pid, elements[i], ai0, ai1, itemId)) {
            gCachedBobberAddr = elements[i];
            return true;
        }
    }
    return false;
}

// Mode 2: Heuristic Heap Scanner for Projectile with aiStyle == 61
bool findBobberViaHeapScan(pid_t pid, float& ai0, float& ai1, int& itemId) {
    if (gCachedBobberAddr != 0) {
        if (validateAndReadBobber(pid, gCachedBobberAddr, ai0, ai1, itemId)) {
            return true;
        }
        gCachedBobberAddr = 0;
    }

    auto regions = getWritableHeapRegions(pid);
    const size_t chunkSize = 256 * 1024;
    std::vector<uint8_t> buf(chunkSize);

    for (const auto& reg : regions) {
        for (uint64_t addr = reg.start; addr < reg.end; addr += chunkSize) {
            size_t toRead = std::min<size_t>(chunkSize, static_cast<size_t>(reg.end - addr));
            if (!readMemory(pid, addr, buf.data(), toRead)) continue;

            for (size_t off = static_cast<size_t>(gOffsets.projAiStyle); off + 0x40 < toRead; off += 8) {
                int32_t candidateStyle = *reinterpret_cast<int32_t*>(&buf[off]);
                if (candidateStyle == 61) {
                    uint64_t candidateProj = addr + off - gOffsets.projAiStyle;
                    if (validateAndReadBobber(pid, candidateProj, ai0, ai1, itemId)) {
                        gCachedBobberAddr = candidateProj;
                        return true;
                    }
                }
            }
        }
    }
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
    if (argc >= 3) gOffsets.mainTypeInfo = strtoull(argv[2], nullptr, 16);
    if (argc >= 4) gOffsets.projectileField = strtoull(argv[3], nullptr, 16);
    if (argc >= 5) gOffsets.projAiStyle = strtoull(argv[4], nullptr, 16);
    if (argc >= 6) gOffsets.projAiArray = strtoull(argv[5], nullptr, 16);
    if (argc >= 7) gOffsets.projLocalAiArray = strtoull(argv[6], nullptr, 16);

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

        if (gOffsets.mainTypeInfo != 0 && il2cppBase != 0) {
            found = findBobberViaPointerChain(pid, il2cppBase, ai0, ai1, itemId);
        } else {
            if (gCachedBobberAddr != 0 || scanCooldown <= 0) {
                found = findBobberViaHeapScan(pid, ai0, ai1, itemId);
                scanCooldown = found ? 0 : 20;
            } else {
                scanCooldown--;
            }
        }

        printf("STATUS|%d|%" PRIx64 "|%d|%.2f|%.2f|%d\n",
               pid, il2cppBase, found ? 1 : 0, ai0, ai1, itemId);

        usleep(25000); // 40 Hz polling
    }

    return 0;
}
