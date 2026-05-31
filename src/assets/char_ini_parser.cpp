#include "char_ini_parser.hpp"
#include "asset_manager.hpp"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace ao {

// Minimal Windows INI parser for AO2 char.ini.
// Handles [Section], key = value, and ignores comments (;/#).
// Shared by the file path, in-memory, and asset-streaming entry points.

static void trim(char* s) {
    // Leading
    int start = 0;
    while (s[start] == ' ' || s[start] == '\t') ++start;
    if (start > 0) std::memmove(s, s + start, std::strlen(s + start) + 1);
    // Trailing
    int len = (int)std::strlen(s);
    while (len > 0 && (s[len-1]==' ' || s[len-1]=='\t' ||
                        s[len-1]=='\r'|| s[len-1]=='\n'))
        s[--len] = '\0';
}

// Case-insensitive section/key match — char.ini casing varies wildly across
// the ecosystem ([Options] vs [options], "showname" vs "Showname").
static bool ieq(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b)
        if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b))
            return false;
    return *a == *b;
}

// Handle one already-trimmed, non-comment line. `section` is the current
// section name; `emoji` accumulates into `out`.
static void handle_line(char* line, char* section, CharDef& out) {
    if (line[0] == '[') {
        char* end = std::strchr(line + 1, ']');
        if (end) {
            int len = (int)(end - line - 1);
            if (len > 63) len = 63;
            std::strncpy(section, line + 1, len);
            section[len] = '\0';
        }
        return;
    }

    char* eq = std::strchr(line, '=');
    if (!eq) return;
    *eq = '\0';
    char* key = line;
    char* val = eq + 1;
    trim(key);
    trim(val);

    if (ieq(section, "Options")) {
        if (ieq(key, "name"))     std::strncpy(out.name,     val, sizeof(out.name)     - 1);
        if (ieq(key, "showname")) std::strncpy(out.showname, val, sizeof(out.showname) - 1);
        if (ieq(key, "blips"))    std::strncpy(out.blips,    val, sizeof(out.blips)    - 1);
        if (ieq(key, "gender"))   { if (!out.blips[0]) std::strncpy(out.blips, val, sizeof(out.blips) - 1); }
        if (ieq(key, "side"))     std::strncpy(out.side,     val, sizeof(out.side)     - 1);
    } else if (ieq(section, "Emotions")) {
        if (ieq(key, "number")) return; // count hint — we size by index instead
        // Emotion entry: "1 = Thinking#-#normal#0" or "1 = Thinking#preanim#normal#0"
        int idx = std::atoi(key) - 1; // 1-based → 0-based
        if (idx < 0 || idx >= MAX_EMOTIONS) return;
        if (idx >= out.emotion_count) out.emotion_count = idx + 1;

        EmotionEntry& em = out.emotions[idx];
        // Split val on '#'
        char parts[4][64] = {};
        char tmp[256];
        std::strncpy(tmp, val, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        char* tok = tmp;
        for (int i = 0; i < 4 && *tok; ) {
            char* sep = std::strchr(tok, '#');
            int plen;
            if (sep) { plen = (int)(sep - tok); *sep = '\0'; }
            else      { plen = (int)std::strlen(tok); }
            if (plen > 63) plen = 63;
            std::strncpy(parts[i], tok, plen);
            ++i;
            tok = sep ? sep + 1 : tok + plen;
            if (!sep) break;
        }
        // parts[0]=name, parts[1]=pre_anim ("-" means none),
        // parts[2]=anim_base, parts[3]=desk_mod
        std::strncpy(em.name,      parts[0], sizeof(em.name)      - 1);
        std::strncpy(em.idle_anim, parts[2], sizeof(em.idle_anim) - 1);
        std::strncpy(em.talk_anim, parts[2], sizeof(em.talk_anim) - 1);
        em.desk_mod = parts[3][0] ? std::atoi(parts[3]) : 1;
        em.has_pre  = (parts[1][0] != '\0' && std::strcmp(parts[1], "-") != 0);
        if (em.has_pre) std::strncpy(em.pre_anim, parts[1], sizeof(em.pre_anim) - 1);
    }
}

bool parse_char_ini_bytes(const uint8_t* data, int size, CharDef& out) {
    if (!data || size <= 0) return false;
    out = CharDef{};   // value-init (CharDef has default member initializers)

    char section[64] = {};
    char line[512];
    int  i = 0;
    while (i < size) {
        // Read one line into `line`
        int n = 0;
        while (i < size && data[i] != '\n') {
            if (n < (int)sizeof(line) - 1) line[n++] = (char)data[i];
            ++i;
        }
        line[n] = '\0';
        if (i < size) ++i; // skip '\n'

        trim(line);
        if (!line[0] || line[0] == ';' || line[0] == '#') continue;
        handle_line(line, section, out);
    }
    return true;
}

bool parse_char_ini(const char* path, CharDef& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 1 << 20) { std::fclose(f); return false; }
    uint8_t* buf = (uint8_t*)SDL_malloc((size_t)sz);
    if (!buf) { std::fclose(f); return false; }
    bool ok = ((long)std::fread(buf, 1, (size_t)sz, f) == sz)
              && parse_char_ini_bytes(buf, (int)sz, out);
    std::fclose(f);
    SDL_free(buf);
    return ok;
}

bool load_char_ini(const char* folder, CharDef& out) {
    if (!folder || !folder[0]) return false;
    // Lowercase the folder to match the lowercase-tree CDN convention (the
    // HTTP tier lowercases anyway; doing it here keeps the local/prefetch
    // cache keys consistent with how CharSelectScreen builds icon paths).
    char lc[64];
    int i = 0;
    for (; folder[i] && i < (int)sizeof(lc) - 1; ++i)
        lc[i] = (char)std::tolower((unsigned char)folder[i]);
    lc[i] = '\0';

    char rel[160];
    std::snprintf(rel, sizeof(rel), "characters/%s/char.ini", lc);

    int size = 0;
    uint8_t* data = AssetManager::fetch_bytes(rel, &size);
    if (!data) return false;
    bool ok = parse_char_ini_bytes(data, size, out);
    SDL_free(data);
    return ok;
}

} // namespace ao
