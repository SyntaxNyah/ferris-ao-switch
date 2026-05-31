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
    // Defaults taken verbatim from webAO's fetchExtensions() fallback
    // (webAO/src/client/fetchLists.ts). Exactly matching webAO avoids
    // assets 404'ing when a server has no extensions.json file.

    // charicon: .png, .webp
    c.charicon_count = 2;
    std::strncpy(c.charicon[0], ".png",  ExtensionsConfig::EXT_LEN - 1);
    std::strncpy(c.charicon[1], ".webp", ExtensionsConfig::EXT_LEN - 1);

    // emote (pre-anims + (a)/(b) sprites): .gif, .png, .apng, .webp, .webp.static
    c.emote_count = 5;
    std::strncpy(c.emote[0], ".gif",         ExtensionsConfig::EXT_LEN - 1);
    std::strncpy(c.emote[1], ".png",         ExtensionsConfig::EXT_LEN - 1);
    std::strncpy(c.emote[2], ".apng",        ExtensionsConfig::EXT_LEN - 1);
    std::strncpy(c.emote[3], ".webp",        ExtensionsConfig::EXT_LEN - 1);
    std::strncpy(c.emote[4], ".webp.static", ExtensionsConfig::EXT_LEN - 1);

    // emotions (emote-button icons): .png, .webp
    c.emotions_count = 2;
    std::strncpy(c.emotions[0], ".png",  ExtensionsConfig::EXT_LEN - 1);
    std::strncpy(c.emotions[1], ".webp", ExtensionsConfig::EXT_LEN - 1);

    // background: .png, .gif, .webp, .apng
    c.background_count = 4;
    std::strncpy(c.background[0], ".png",  ExtensionsConfig::EXT_LEN - 1);
    std::strncpy(c.background[1], ".gif",  ExtensionsConfig::EXT_LEN - 1);
    std::strncpy(c.background[2], ".webp", ExtensionsConfig::EXT_LEN - 1);
    std::strncpy(c.background[3], ".apng", ExtensionsConfig::EXT_LEN - 1);
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

    // Start from defaults so any field the JSON omits keeps a sane fallback,
    // then override each category that the file actually specifies.
    apply_defaults(s_cfg);
    int n;
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
