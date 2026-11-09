// ============================================================================
//  pak.c — загрузка ресурсов из встроенного архива (дописан в конец .exe)
//  Формат: [данные...][индекс][16 байт футер: "TZP1", indexOffset, indexSize, count]
//  Если архив не найден — читаем assets/game.pak или напрямую assets/raw (dev).
// ============================================================================
#include "pak.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <unistd.h>
  #include <limits.h>
#endif

#define PAK_MAGIC "TZP1"
#define PAK_FOOTER 16

typedef struct {
    char name[96];
    uint32_t offset;
    uint32_t size;
} PakEntry;

static FILE *pakFile = NULL;
static PakEntry *entries = NULL;
static int entryCount = 0;
static char baseDir[512] = ".";
static char pakPath[512] = "";
static bool useRawDir = false;
static long pakBase = 0;   // смещение архива внутри файла (.exe + дописанный пак)
static char exePath[512] = "";

static void get_exe_path(char *out, size_t cap) {
#if defined(_WIN32)
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)cap);
    if (n == 0 || n >= cap) snprintf(out, cap, "game.exe");
#else
    ssize_t n = readlink("/proc/self/exe", out, cap - 1);
    if (n <= 0) snprintf(out, cap, "./game");
    else out[n] = 0;
#endif
}

static void dir_of(const char *path, char *out, size_t cap) {
    snprintf(out, cap, "%s", path);
    char *slash = strrchr(out, '/');
    char *bslash = strrchr(out, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    if (slash) slash[1] = 0;
    else snprintf(out, cap, ".");
}

static bool read_index(FILE *f, const char *path) {
    if (fseek(f, 0, SEEK_END) != 0) return false;
    long total = ftell(f);
    if (total < (long)PAK_FOOTER + 1) return false;
    fseek(f, total - PAK_FOOTER, SEEK_SET);
    char magic[4];
    uint32_t idxOff = 0, idxSize = 0, count = 0;
    if (fread(magic, 1, 4, f) != 4) return false;
    if (memcmp(magic, PAK_MAGIC, 4) != 0) return false;
    if (fread(&idxOff, 4, 1, f) != 1) return false;
    if (fread(&idxSize, 4, 1, f) != 1) return false;
    if (fread(&count, 4, 1, f) != 1) return false;
    if (count > 4096 || idxSize > 4 * 1024 * 1024) return false;

    // Архив может быть дописан в конец исполняемого файла: индекс заканчивается
    // ровно там, где начинается футер, поэтому база архива вычисляется точно.
    long base = total - (long)PAK_FOOTER - (long)idxSize - (long)idxOff;
    if (base < 0) return false;
    pakBase = base;

    char *idx = (char *)malloc(idxSize + 1);
    if (!idx) return false;
    fseek(f, base + (long)idxOff, SEEK_SET);
    if (fread(idx, 1, idxSize, f) != idxSize) { free(idx); return false; }
    idx[idxSize] = 0;

    entries = (PakEntry *)calloc(count ? count : 1, sizeof(PakEntry));
    if (!entries) { free(idx); return false; }
    uint32_t p = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (p + 2 > idxSize) break;
        uint16_t nl;
        memcpy(&nl, idx + p, 2); p += 2;
        if (nl >= sizeof(entries[i].name) || p + nl + 8 > idxSize) break;
        memcpy(entries[i].name, idx + p, nl);
        entries[i].name[nl] = 0;
        p += nl;
        memcpy(&entries[i].offset, idx + p, 4); p += 4;
        memcpy(&entries[i].size, idx + p, 4); p += 4;
        entryCount++;
    }
    free(idx);
    (void)path;
    return entryCount > 0;
}

bool PakOpen(void) {
    get_exe_path(exePath, sizeof(exePath));
    dir_of(exePath, baseDir, sizeof(baseDir));

    // 1) ресурсы, дописанные в исполняемый файл
    pakFile = fopen(exePath, "rb");
    if (pakFile) {
        if (read_index(pakFile, exePath)) {
            snprintf(pakPath, sizeof(pakPath), "%s", exePath);
            return true;
        }
        fclose(pakFile);
        pakFile = NULL;
    }
    // 2) отдельный архив рядом с .exe
    const char *candidates[] = { "game.pak", "assets/game.pak", "assets\\game.pak", "../assets/game.pak" };
    for (int i = 0; i < 4; i++) {
        char path[700];
        snprintf(path, sizeof(path), "%s%s", baseDir, candidates[i]);
        pakFile = fopen(path, "rb");
        if (pakFile) {
            if (read_index(pakFile, path)) {
                snprintf(pakPath, sizeof(pakPath), "%s", path);
                return true;
            }
            fclose(pakFile);
            pakFile = NULL;
        }
    }
    // 3) режим разработки: читаем напрямую из assets/raw
    useRawDir = true;
    return true;
}

void PakClose(void) {
    if (pakFile) fclose(pakFile);
    pakFile = NULL;
    free(entries);
    entries = NULL;
    entryCount = 0;
    pakBase = 0;
}

const char *PakGetPath(void) { return pakPath; }

static bool raw_load(const char *name, unsigned char **out, unsigned int *outSize) {
    const char *prefixes[] = { "", "assets/raw/", "../assets/raw/", "../../assets/raw/",
                               "assets/shaders/", "../assets/shaders/", "fonts/", "../fonts/", "assets/", "../assets/" };
    for (int i = 0; i < 10; i++) {
        char path[700];
        snprintf(path, sizeof(path), "%s%s%s", baseDir, prefixes[i], name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0) { fclose(f); continue; }
        unsigned char *buf = (unsigned char *)malloc((size_t)sz);
        if (!buf) { fclose(f); return false; }
        size_t rd = fread(buf, 1, (size_t)sz, f);
        fclose(f);
        if (rd != (size_t)sz) { free(buf); return false; }
        *out = buf;
        *outSize = (unsigned int)sz;
        return true;
    }
    return false;
}

bool PakLoad(const char *name, unsigned char **out, unsigned int *outSize) {
    *out = NULL;
    *outSize = 0;
    if (useRawDir || entryCount == 0) return raw_load(name, out, outSize);
    for (int i = 0; i < entryCount; i++) {
        const char *base = strrchr(entries[i].name, '/');
        base = base ? base + 1 : entries[i].name;
        if (strcmp(entries[i].name, name) == 0 || strcmp(base, name) == 0) {
            unsigned char *buf = (unsigned char *)malloc(entries[i].size ? entries[i].size : 1);
            if (!buf) return false;
            if (fseek(pakFile, pakBase + (long)entries[i].offset, SEEK_SET) != 0) { free(buf); return false; }
            if (fread(buf, 1, entries[i].size, pakFile) != entries[i].size) { free(buf); return false; }
            *out = buf;
            *outSize = entries[i].size;
            return true;
        }
    }
    return raw_load(name, out, outSize);
}

int PakCount(void) { return entryCount; }
const char *PakNameAt(int i) { return (i >= 0 && i < entryCount) ? entries[i].name : ""; }
