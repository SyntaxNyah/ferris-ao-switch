#include "extensions_config.hpp"
#include "asset_manager.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace ao {

// ── Static storage ─────────────────────────────────────────────────────────────

static ExtensionsConfig s_cfg;
static bool             s_loaded = false;

// ── Defaults ───────────────────────────────────────────────────────────────────

static void apply_defaults(ExtensionsConfig& c) {
    // charicon: try .png only (webAO default, AO2 classic default)
    c.charicon_count = 1;
    std::strncpy(c.charicon[0], ".png", ExtensionsConfig::EXT_LEN - 1);

    // emote (pre-anims and (a)/(b) standing sprites): .gif then .png fallback
    c.emote_count = 2;
    std::strncpy(c.emote[0], ".gif", ExtensionsConfig::EXT_LEN - 1);
    std::strncpy(c.emote[1], ".png", ExtensionsConfig::EXT_LEN - 1);

    // emotions (full idle/talk sheet variants): .png then .gif
    c.emotions_count = 2;
    std::strncpy(c.emotions[0], ".png", ExtensionsConfig::EXT_LEN - 1);
    std::strncpy(c.emotions[1], ".gif", ExtensionsConfig::EXT_LEN - 1);

    // background: .png
    c.background_count = 1;
    std::strncpy(c.background[0], ".png", ExtensionsConfig::EXT_LEN - 1);
}

// ── JSON parser ────────────────────────────────────────────────────────────────
// Minimal scanner: find a named array key and extract its string values.
// Returns count of extensions found, 0 if key missing or array empty.

static int parse_ext_array(const char* json, int json_len,
                            const char* key,
                            char out[][ExtensionsConfig::EXT_LEN], int max_out)
{
    // Find "key": [
    // We scan for the key string literally inside the JSON.
    const char* p   = json;
    const char* end = json + json_len;
    int count = 0;

    // locate the key
    int klen = (int)std::strlen(key);
    while (p + klen < end) {
        if (*p == '"' && std::strncmp(p + 1, key, klen) == 0 && p[klen + 1] == '"') {
            p += klen + 2; // skip past closing quote
            break;
        }
        ++p;
    }
    if (p + klen >= end) return 0; // key not found

    // skip to '['
    while (p < end && *p != '[') ++p;
    if (p >= end) return 0;
    ++p; // skip '['

    // extract each "value" until ']'
    while (p < end && count < max_out) {
        while (p < end && *p != '"' && *p != ']') ++p;
        if (p >= end || *p == ']') break;
        ++p; // skip opening '"'
        // copy until closing '"'
        int i = 0;
        while (p < end && *p != '"' && i < ExtensionsConfig::EXT_LEN - 1)
            out[count][i++] = *p++;
        out[count][i] = '\0';
        if (i > 0) ++count;
        if (p < end) ++p; // skip closing '"'
    }
    return count;
}

// ── Public API ─────────────────────────────────────────────────────────────────

void ExtensionsConfig::reset() {
    apply_defaults(s_cfg);
    s_loaded = false;
}

const ExtensionsConfig& ExtensionsConfig::get() {
    if (!s_loaded) apply_defaults(s_cfg);
    return s_cfg;
}

void ExtensionsConfig::fetch_and_apply() {
    if (!AssetManager::has_asset_url()) return;

    int  size = 0;
    uint8_t* data = AssetManager::fetch_bytes("extensions.json", &size);
    if (!data) {
        std::fprintf(stderr, "[ext] extensions.json not found — using defaults\n");
        apply_defaults(s_cfg);
        s_loaded = true;
        return;
    }

    const char* json = reinterpret_cast<const char*>(data);

    int n;

    n = parse_ext_array(json, size, "charicon_extensions",
                        s_cfg.charicon, MAX_EXTS);
    if (n > 0) s_cfg.charicon_count = n;
    else       apply_defaults(s_cfg); // partial — keep defaults for this field

    // re-parse the others after applying defaults so partial files are safe
    apply_defaults(s_cfg); // reset all first
    n = parse_ext_array(json, size, "charicon_extensions",   s_cfg.charicon,   MAX_EXTS); if (n > 0) s_cfg.charicon_count   = n;
    n = parse_ext_array(json, size, "emote_extensions",      s_cfg.emote,      MAX_EXTS); if (n > 0) s_cfg.emote_count      = n;
    n = parse_ext_array(json, size, "emotions_extensions",   s_cfg.emotions,   MAX_EXTS); if (n > 0) s_cfg.emotions_count   = n;
    n = parse_ext_array(json, size, "background_extensions", s_cfg.background, MAX_EXTS); if (n > 0) s_cfg.background_count = n;

    SDL_free(data);
    s_loaded = true;

    std::fprintf(stderr, "[ext] extensions.json loaded — charicon:%d emote:%d bg:%d\n",
        s_cfg.charicon_count, s_cfg.emote_count, s_cfg.background_count);
    for (int i = 0; i < s_cfg.emote_count; ++i)
        std::fprintf(stderr, "[ext]   emote[%d] = %s\n", i, s_cfg.emote[i]);
}

} // namespace ao
