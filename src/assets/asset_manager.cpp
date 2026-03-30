#include "asset_manager.hpp"
#include <cstdio>
#include <cstring>

namespace ao {

#ifdef __SWITCH__
static constexpr const char* USER_BASE  = "sdmc:/switch/ferris-ao/base";
static constexpr const char* ROMFS_BASE = "romfs:";
#else
// Desktop / emulator fallback — look in ./base/ then ./romfs/
static constexpr const char* USER_BASE  = "base";
static constexpr const char* ROMFS_BASE = "romfs";
#endif

const char* AssetManager::user_base()  { return USER_BASE;  }
const char* AssetManager::romfs_base() { return ROMFS_BASE; }

static bool file_exists(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (f) { std::fclose(f); return true; }
    return false;
}

bool AssetManager::resolve(const char* relative,
                            char* out_path, int out_cap) {
    // 1. User sdmc base
    int n = std::snprintf(out_path, out_cap, "%s/%s", USER_BASE, relative);
    if (n > 0 && n < out_cap && file_exists(out_path)) return true;

    // 2. RomFS
    n = std::snprintf(out_path, out_cap, "%s/%s", ROMFS_BASE, relative);
    if (n > 0 && n < out_cap && file_exists(out_path)) return true;

    out_path[0] = '\0';
    return false;
}

} // namespace ao
