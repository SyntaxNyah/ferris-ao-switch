#include "theme_manager.hpp"
#include "asset_manager.hpp"
#include "../render/renderer.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace ao {

// ── Defaults ──────────────────────────────────────────────────────────────────

ThemeManager::ThemeManager() {
    theme_name_[0] = '\0';
    theme_dir_[0]  = '\0';
    reset_to_defaults();
}

void ThemeManager::reset_to_defaults() {
    layout_.viewport        = Layout::VIEWPORT;
    layout_.chatbox         = Layout::CHAT_AREA;
    layout_.ic_text         = Layout::CHATBOX;
    layout_.nameplate       = Layout::NAMEPLATE;
    layout_.hp_def          = Layout::HP_DEF;
    layout_.hp_pro          = Layout::HP_PROS;
    layout_.log             = Layout::SIDE_PANEL;
    layout_.music_name      = {Layout::SIDE_PANEL.x, Layout::SIDE_PANEL.y + 60,
                                Layout::SIDE_PANEL.w, 24};
    layout_.btn_ooc         = Layout::BTN_OOC;
    layout_.btn_music       = Layout::BTN_MUSIC;
    layout_.btn_evidence    = Layout::BTN_EVIDENCE;
    layout_.panel_ooc       = Layout::PANEL_OOC;
    layout_.panel_music     = Layout::PANEL_MUSIC;
    layout_.panel_evidence  = Layout::PANEL_EVIDENCE;

    std::strncpy(layout_.sfx_realization, "sfx-realization", sizeof(layout_.sfx_realization) - 1);
    std::strncpy(layout_.sfx_testimony,   "sfx-gallery",     sizeof(layout_.sfx_testimony)   - 1);
    std::strncpy(layout_.sfx_cross,       "sfx-testimony",   sizeof(layout_.sfx_cross)        - 1);
    std::strncpy(layout_.sfx_blink,       "sfx-blink",       sizeof(layout_.sfx_blink)        - 1);
    std::strncpy(layout_.sfx_objection,   "sfx-objection",   sizeof(layout_.sfx_objection)    - 1);
    std::strncpy(layout_.sfx_holdit,      "sfx-holdit",      sizeof(layout_.sfx_holdit)       - 1);
    std::strncpy(layout_.sfx_takethat,    "sfx-takethat",    sizeof(layout_.sfx_takethat)     - 1);
    std::strncpy(layout_.sfx_guilty,      "sfx-guilty",      sizeof(layout_.sfx_guilty)       - 1);
    std::strncpy(layout_.sfx_notguilty,   "sfx-notguilty",   sizeof(layout_.sfx_notguilty)    - 1);

    raw_    = Raw{};
    base_w_ = 960;
    base_h_ = 540;
    is_default_ = true;
}

// ── INI tokeniser ─────────────────────────────────────────────────────────────
// Walks the INI bytes, calling the virtual handler for each section/key/value.
// No heap allocation. Supports ; and # comments, [sections], key = value lines.

static void trim_right(char* s) {
    int len = (int)std::strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t'
                       || s[len-1] == '\r')) s[--len] = '\0';
}

static void parse_ini_bytes(const uint8_t* data, int size,
    void (*cb)(const char* sec, const char* key,
               const char* val, void* ud), void* ud)
{
    char section[64] = {};
    int  i = 0;

    while (i < size) {
        // Skip leading whitespace and blank lines
        while (i < size && (data[i] == ' ' || data[i] == '\t'
                            || data[i] == '\r' || data[i] == '\n')) ++i;
        if (i >= size) break;

        // Comment lines
        if (data[i] == ';' || data[i] == '#') {
            while (i < size && data[i] != '\n') ++i;
            continue;
        }

        // Section header
        if (data[i] == '[') {
            ++i;
            int len = 0;
            while (i < size && data[i] != ']' && data[i] != '\n') {
                if (len < (int)sizeof(section) - 1) section[len++] = (char)data[i];
                ++i;
            }
            section[len] = '\0';
            trim_right(section);
            while (i < size && data[i] != '\n') ++i;
            continue;
        }

        // key = value line
        char key[64] = {}, val[256] = {};
        int kl = 0;
        while (i < size && data[i] != '=' && data[i] != '\n') {
            if (kl < (int)sizeof(key) - 1) key[kl++] = (char)data[i];
            ++i;
        }
        key[kl] = '\0';
        trim_right(key);

        if (i < size && data[i] == '=') {
            ++i;
            // Skip leading whitespace in value
            while (i < size && (data[i] == ' ' || data[i] == '\t')) ++i;
            int vl = 0;
            while (i < size && data[i] != '\n') {
                if (vl < (int)sizeof(val) - 1) val[vl++] = (char)data[i];
                ++i;
            }
            val[vl] = '\0';
            trim_right(val);
        }

        if (key[0] != '\0') cb(section, key, val, ud);
        // Advance past newline
        if (i < size && data[i] == '\n') ++i;
    }
}

// ── Design INI handler ────────────────────────────────────────────────────────

struct DesignCtx { ThemeManager* m; };

static void design_cb(const char* sec, const char* key, const char* val, void* ud) {
    ThemeManager* m = ((DesignCtx*)ud)->m;
    ThemeManager::Raw& r = m->raw_;
    int v = std::atoi(val);

    if (std::strcmp(sec, "version") == 0) {
        if (std::strcmp(key, "width")  == 0) m->base_w_ = v;
        if (std::strcmp(key, "height") == 0) m->base_h_ = v;
        return;
    }
    if (std::strcmp(sec, "Viewport") == 0) {
        if (std::strcmp(key, "x") == 0)      r.vp_x = v;
        if (std::strcmp(key, "y") == 0)      r.vp_y = v;
        if (std::strcmp(key, "width")  == 0) r.vp_w = v;
        if (std::strcmp(key, "height") == 0) r.vp_h = v;
        return;
    }
    if (std::strcmp(sec, "Chatbox") == 0) {
        if (std::strcmp(key, "x") == 0)      r.cb_x = v;
        if (std::strcmp(key, "y") == 0)      r.cb_y = v;
        if (std::strcmp(key, "width")  == 0 ||
            std::strcmp(key, "chatmed_width")  == 0 ||
            std::strcmp(key, "chatbig_width")  == 0) r.cb_w = v;
        if (std::strcmp(key, "height") == 0 ||
            std::strcmp(key, "chatmed_height") == 0 ||
            std::strcmp(key, "chatbig_height") == 0) r.cb_h = v;
        return;
    }
    if (std::strcmp(sec, "IC text") == 0) {
        if (std::strcmp(key, "x") == 0)      r.ic_x = v;
        if (std::strcmp(key, "y") == 0)      r.ic_y = v;
        if (std::strcmp(key, "width")  == 0) r.ic_w = v;
        if (std::strcmp(key, "height") == 0) r.ic_h = v;
        return;
    }
    if (std::strcmp(sec, "Showname") == 0 || std::strcmp(sec, "Nameplate") == 0) {
        if (std::strcmp(key, "x") == 0)      r.np_x = v;
        if (std::strcmp(key, "y") == 0)      r.np_y = v;
        if (std::strcmp(key, "width")  == 0) r.np_w = v;
        if (std::strcmp(key, "height") == 0) r.np_h = v;
        return;
    }
    if (std::strcmp(sec, "Defense HP bar") == 0) {
        if (std::strcmp(key, "x") == 0)                                  r.hd_x = v;
        if (std::strcmp(key, "y") == 0)                                  r.hd_y = v;
        if (std::strcmp(key, "bar_w") == 0 || std::strcmp(key, "width")  == 0) r.hd_w = v;
        if (std::strcmp(key, "bar_h") == 0 || std::strcmp(key, "height") == 0) r.hd_h = v;
        return;
    }
    if (std::strcmp(sec, "Prosecution HP bar") == 0) {
        if (std::strcmp(key, "x") == 0)                                  r.hp_x = v;
        if (std::strcmp(key, "y") == 0)                                  r.hp_y = v;
        if (std::strcmp(key, "bar_w") == 0 || std::strcmp(key, "width")  == 0) r.hp_w = v;
        if (std::strcmp(key, "bar_h") == 0 || std::strcmp(key, "height") == 0) r.hp_h = v;
        return;
    }
    if (std::strcmp(sec, "Log") == 0) {
        if (std::strcmp(key, "x") == 0)      r.log_x = v;
        if (std::strcmp(key, "y") == 0)      r.log_y = v;
        if (std::strcmp(key, "width")  == 0) r.log_w = v;
        if (std::strcmp(key, "height") == 0) r.log_h = v;
        return;
    }
    if (std::strcmp(sec, "Music name") == 0) {
        if (std::strcmp(key, "x") == 0)      r.mn_x = v;
        if (std::strcmp(key, "y") == 0)      r.mn_y = v;
        if (std::strcmp(key, "width")  == 0) r.mn_w = v;
        if (std::strcmp(key, "height") == 0) r.mn_h = v;
        return;
    }
}

// ── Sounds INI handler ────────────────────────────────────────────────────────

static void sounds_cb(const char* sec, const char* key, const char* val, void* ud) {
    if (std::strcmp(sec, "Sounds") != 0 && std::strcmp(sec, "sounds") != 0) return;
    ThemeLayout& l = ((ThemeManager*)ud)->layout_;
    auto set = [&](char* dst, int cap) {
        std::strncpy(dst, val, cap - 1); dst[cap - 1] = '\0';
    };
    if (std::strcmp(key, "realization")       == 0) set(l.sfx_realization, sizeof(l.sfx_realization));
    if (std::strcmp(key, "witness_testimony") == 0) set(l.sfx_testimony,   sizeof(l.sfx_testimony));
    if (std::strcmp(key, "cross_examination") == 0) set(l.sfx_cross,       sizeof(l.sfx_cross));
    if (std::strcmp(key, "blink")             == 0) set(l.sfx_blink,       sizeof(l.sfx_blink));
    if (std::strcmp(key, "objection")         == 0) set(l.sfx_objection,   sizeof(l.sfx_objection));
    if (std::strcmp(key, "holdit")            == 0) set(l.sfx_holdit,      sizeof(l.sfx_holdit));
    if (std::strcmp(key, "takethat")          == 0) set(l.sfx_takethat,    sizeof(l.sfx_takethat));
    if (std::strcmp(key, "sfx-guilty")        == 0) set(l.sfx_guilty,      sizeof(l.sfx_guilty));
    if (std::strcmp(key, "sfx-notguilty")     == 0) set(l.sfx_notguilty,   sizeof(l.sfx_notguilty));
}

// ── Scale raw → ThemeLayout ───────────────────────────────────────────────────

void ThemeManager::scale_layout(float sx, float sy) {
    auto sc = [&](int x, int y, int w, int h) -> SDL_Rect {
        return {(int)(x * sx), (int)(y * sy), (int)(w * sx), (int)(h * sy)};
    };

    layout_.viewport   = sc(raw_.vp_x,  raw_.vp_y,  raw_.vp_w,  raw_.vp_h);
    layout_.chatbox    = sc(raw_.cb_x,  raw_.cb_y,  raw_.cb_w,  raw_.cb_h);
    layout_.ic_text    = sc(raw_.ic_x,  raw_.ic_y,  raw_.ic_w,  raw_.ic_h);
    layout_.nameplate  = sc(raw_.np_x,  raw_.np_y,  raw_.np_w,  raw_.np_h);
    layout_.hp_def     = sc(raw_.hd_x,  raw_.hd_y,  raw_.hd_w,  raw_.hd_h);
    layout_.hp_pro     = sc(raw_.hp_x,  raw_.hp_y,  raw_.hp_w,  raw_.hp_h);
    layout_.log        = sc(raw_.log_x, raw_.log_y, raw_.log_w, raw_.log_h);
    layout_.music_name = sc(raw_.mn_x,  raw_.mn_y,  raw_.mn_w,  raw_.mn_h);

    // Panels: use the log/side area
    layout_.panel_ooc      = layout_.log;
    layout_.panel_music    = layout_.log;
    layout_.panel_evidence = layout_.log;

    // Buttons: stacked at the bottom-right of the log panel
    int bx = layout_.log.x + layout_.log.w - 80;
    int by = layout_.log.y + layout_.log.h - 130;
    layout_.btn_ooc      = {bx, by,      76, 36};
    layout_.btn_music    = {bx, by + 44, 76, 36};
    layout_.btn_evidence = {bx, by + 88, 76, 36};
}

// ── load ──────────────────────────────────────────────────────────────────────

bool ThemeManager::load(const char* theme_name) {
    reset_to_defaults();
    if (!theme_name || theme_name[0] == '\0') return false;

    std::strncpy(theme_name_, theme_name, sizeof(theme_name_) - 1);

    const char* prefixes[] = { "misc", "themes", nullptr };
    uint8_t* design_data = nullptr;
    int      design_size = 0;

    for (int pi = 0; prefixes[pi]; ++pi) {
        char rel[512];
        std::snprintf(rel, sizeof(rel), "%s/%s/courtroom_design.ini",
            prefixes[pi], theme_name);
        design_data = AssetManager::fetch_bytes(rel, &design_size);
        if (design_data) {
            std::snprintf(theme_dir_, sizeof(theme_dir_),
                "%s/%s", prefixes[pi], theme_name);
            break;
        }
    }

    if (!design_data) {
        std::fprintf(stderr, "[theme] '%s' not found — using defaults\n", theme_name);
        theme_name_[0] = '\0';
        return false;
    }

    // Parse design
    DesignCtx dctx = {this};
    parse_ini_bytes(design_data, design_size, design_cb, &dctx);
    SDL_free(design_data);

    // Parse sounds (optional, same directory)
    char rel[512];
    std::snprintf(rel, sizeof(rel), "%s/courtroom_sounds.ini", theme_dir_);
    int sounds_size = 0;
    uint8_t* sounds_data = AssetManager::fetch_bytes(rel, &sounds_size);
    if (sounds_data) {
        parse_ini_bytes(sounds_data, sounds_size, sounds_cb, this);
        SDL_free(sounds_data);
    }

    float sx = (float)Renderer::WIDTH  / (float)base_w_;
    float sy = (float)Renderer::HEIGHT / (float)base_h_;
    scale_layout(sx, sy);

    is_default_ = false;
    std::fprintf(stdout,
        "[theme] loaded '%s' from '%s' (base %dx%d, scale %.2fx%.2f)\n",
        theme_name, theme_dir_, base_w_, base_h_, (double)sx, (double)sy);
    return true;
}

// ── Path helpers ──────────────────────────────────────────────────────────────

bool ThemeManager::resolve_sfx(const char* sfx_name, char* out_path, int out_cap) const {
    if (!sfx_name || sfx_name[0] == '\0') return false;
    if (theme_dir_[0] != '\0') {
        // Check theme-local sounds subdir first
        std::snprintf(out_path, out_cap, "%s/sounds/%s.ogg", theme_dir_, sfx_name);
        return true;
    }
    std::snprintf(out_path, out_cap, "sounds/general/%s.ogg", sfx_name);
    return true;
}

bool ThemeManager::resolve_image(const char* image_name, char* out_path, int out_cap) const {
    if (!image_name || image_name[0] == '\0') return false;
    const char* dir = (theme_dir_[0] != '\0') ? theme_dir_ : "misc/default";
    std::snprintf(out_path, out_cap, "%s/%s.png", dir, image_name);
    return true;
}

} // namespace ao
