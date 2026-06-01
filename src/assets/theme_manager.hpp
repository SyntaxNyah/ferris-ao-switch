#pragma once
#include <SDL2/SDL.h>

namespace ao {

// Runtime layout produced by parsing an AO2 theme's courtroom_design.ini.
// All rects are in 1280×720 screen space (scaled from the theme's base resolution).
// Screens use this struct instead of the hardcoded Layout:: constants so that
// any standard AO2 base-pack theme is automatically applied.
struct ThemeLayout {
    // ── Courtroom viewport ─────────────────────────────────────────────────────
    SDL_Rect viewport;       // Background + character sprite render area

    // ── Chat area ──────────────────────────────────────────────────────────────
    SDL_Rect chatbox;        // Chatbox background image bounds
    SDL_Rect ic_text;        // IC message text display area (inside chatbox)
    SDL_Rect nameplate;      // Showname / character nameplate

    // ── HP bars ────────────────────────────────────────────────────────────────
    SDL_Rect hp_def;
    SDL_Rect hp_pro;

    // ── Side / log panel ───────────────────────────────────────────────────────
    SDL_Rect log;            // OOC log scrollback area
    SDL_Rect music_name;     // Currently playing music name strip

    // ── Button strip ───────────────────────────────────────────────────────────
    SDL_Rect btn_ic;
    SDL_Rect btn_ooc;
    SDL_Rect btn_music;
    SDL_Rect btn_evidence;

    // ── Overlay panels (full-width, slide over courtroom) ──────────────────────
    SDL_Rect panel_ooc;
    SDL_Rect panel_music;
    SDL_Rect panel_evidence;

    // ── Theme sound names (file base names, no path/extension) ─────────────────
    // Pass to ThemeManager::resolve_sfx() to get the playable relative path.
    char sfx_realization[64];
    char sfx_testimony[64];    // RT#testimony
    char sfx_cross[64];        // RT#cross_examination
    char sfx_blink[64];        // IC typewriter tick
    char sfx_objection[64];
    char sfx_holdit[64];
    char sfx_takethat[64];
    char sfx_guilty[64];
    char sfx_notguilty[64];
};

// Loads AO2 desktop-client themes from the base pack and provides a scaled
// ThemeLayout for use by the courtroom screen.
//
// Search order for theme files:
//   1. AssetManager::open_rwops("misc/<name>/courtroom_design.ini")
//      (HTTP CDN or sdmc:/switch/ferris-ao/base/misc/<name>/)
//   2. AssetManager::open_rwops("themes/<name>/courtroom_design.ini")
//      (newer AO2 theme location)
//   3. Built-in defaults (Layout:: constants from renderer.hpp)
//
// Coordinate scaling: theme INI coordinates are authored at a base resolution
// (default 960×540, or whatever the [version] section specifies). All rects
// are linearly scaled to 1280×720 on load.
class ThemeManager {
public:
    ThemeManager();

    // Attempt to load a theme by name. Returns true if theme files were found.
    // On failure (or if called with name == "default" and no files exist),
    // the built-in defaults are used and the method returns false.
    bool load(const char* theme_name);

    // Reset to built-in defaults (hardcoded Layout:: values).
    void reset_to_defaults();

    const ThemeLayout& layout()       const { return layout_; }
    const char*        active_theme() const { return theme_name_; }
    bool               is_default()   const { return is_default_; }

    // Resolve a theme SFX name to a relative asset path (e.g. "sfx-blink" →
    // "misc/default/sounds/sfx-blink.ogg" or "sounds/general/sfx-blink.ogg").
    // Returns false if the name is empty. out_path receives the relative path
    // for AssetManager::open_rwops().
    bool resolve_sfx(const char* sfx_name, char* out_path, int out_cap) const;

    // Resolve a theme image name (e.g. "chatbox") to a relative asset path.
    bool resolve_image(const char* image_name, char* out_path, int out_cap) const;

private:
    friend void design_cb(const char*, const char*, const char*, void*);
    friend void sounds_cb(const char*, const char*, const char*, void*);

    void parse_design(const uint8_t* data, int size);
    void parse_sounds(const uint8_t* data, int size);
    void scale_layout(float sx, float sy);

    ThemeLayout layout_;
    char theme_name_[128];
    char theme_dir_[256];   // "misc/default" or "themes/default"
    bool is_default_ = true;

    // Base resolution as read from [version] or defaulted to 960×540
    int base_w_ = 960;
    int base_h_ = 540;

    // Intermediate (unscaled) values parsed from INI
    struct Raw {
        int vp_x = 0, vp_y = 0, vp_w = 714, vp_h = 480;
        int cb_x = 0, cb_y = 480, cb_w = 714, cb_h = 148;
        int ic_x = 12, ic_y = 494, ic_w = 690, ic_h = 115;
        int np_x = 0, np_y = 480, np_w = 220, np_h = 35;
        int hd_x = 0, hd_y = 0, hd_w = 191, hd_h = 14;
        int hp_x = 0, hp_y = 14, hp_w = 191, hp_h = 14;
        int log_x = 714, log_y = 0, log_w = 246, log_h = 480;
        int mn_x = 714, mn_y = 480, mn_w = 246, mn_h = 20;
    } raw_;
};

} // namespace ao
