#pragma once

namespace ao {

// Resolves asset paths with a two-level priority:
//   1. sdmc:/switch/ferris-ao/base/<relative>   (user-installed AO base pack)
//   2. romfs:/<relative>                         (bundled fallback assets)
//
// Usage:
//   char path[512];
//   if (AssetManager::resolve("characters/phoenix/emotions/normal(a).png", path, sizeof(path)))
//       tex = IMG_Load(path);

class AssetManager {
public:
    // Returns true and fills out_path if the asset exists at either location.
    // Returns false if the file is not found anywhere.
    static bool resolve(const char* relative, char* out_path, int out_cap);

    // Convenience: returns the sdmc user-data base path
    static const char* user_base();   // "sdmc:/switch/ferris-ao/base"
    static const char* romfs_base();  // "romfs:" (or "./" on non-Switch)
};

} // namespace ao
