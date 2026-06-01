#include "settings.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#ifdef __SWITCH__
#include <sys/stat.h>
#endif

namespace ao {

#ifdef __SWITCH__
static constexpr const char* CFG_DIR  = "sdmc:/switch/ferris-ao";
static constexpr const char* CFG_PATH = "sdmc:/switch/ferris-ao/config.ini";
#else
static constexpr const char* CFG_DIR  = ".";
static constexpr const char* CFG_PATH = "config.ini";
#endif

const char* Settings::path() { return CFG_PATH; }

static void strip_eol(char* s) {
    int n = (int)std::strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\t'))
        s[--n] = '\0';
}

static void set_str(char* dst, int cap, const char* val) {
    std::strncpy(dst, val, cap - 1);
    dst[cap - 1] = '\0';
}

void Settings::load() {
    FILE* f = std::fopen(CFG_PATH, "rb");
    if (!f) return;   // first run — keep defaults
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        if (line[0] == ';' || line[0] == '#') continue;
        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = line;
        char* val = eq + 1;
        // trim key trailing space
        int kn = (int)std::strlen(key);
        while (kn > 0 && (key[kn-1] == ' ' || key[kn-1] == '\t')) key[--kn] = '\0';
        while (*val == ' ' || *val == '\t') ++val;
        strip_eol(val);

        if      (!std::strcmp(key, "showname"))     set_str(showname,   sizeof(showname),   val);
        else if (!std::strcmp(key, "theme"))        set_str(theme,      sizeof(theme),      val);
        else if (!std::strcmp(key, "master_url"))   set_str(master_url, sizeof(master_url), val);
        else if (!std::strcmp(key, "last_host"))    set_str(last_host,  sizeof(last_host),  val);
        else if (!std::strcmp(key, "last_port"))    set_str(last_port,  sizeof(last_port),  val);
        else if (!std::strcmp(key, "sfx_volume"))   sfx_volume   = std::atoi(val);
        else if (!std::strcmp(key, "music_volume")) music_volume = std::atoi(val);
    }
    std::fclose(f);

    if (sfx_volume   < 0) sfx_volume   = 0;   if (sfx_volume   > 128) sfx_volume   = 128;
    if (music_volume < 0) music_volume = 0;   if (music_volume > 128) music_volume = 128;
    if (!theme[0]) set_str(theme, sizeof(theme), "default");
}

void Settings::save() {
#ifdef __SWITCH__
    ::mkdir("sdmc:/switch", 0777);
    ::mkdir(CFG_DIR, 0777);
#endif
    FILE* f = std::fopen(CFG_PATH, "wb");
    if (!f) return;   // best-effort
    std::fprintf(f, "; ferris-ao-switch settings\n");
    std::fprintf(f, "showname = %s\n",     showname);
    std::fprintf(f, "theme = %s\n",        theme);
    std::fprintf(f, "master_url = %s\n",   master_url);
    std::fprintf(f, "last_host = %s\n",    last_host);
    std::fprintf(f, "last_port = %s\n",    last_port);
    std::fprintf(f, "sfx_volume = %d\n",   sfx_volume);
    std::fprintf(f, "music_volume = %d\n", music_volume);
    std::fclose(f);
}

} // namespace ao
