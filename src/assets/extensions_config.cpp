#include "extensions_config.hpp"
#include "asset_manager.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#ifdef __SWITCH__
#include <sys/stat.h>   // mkdir for the formats.cfg directory
#endif

namespace ao {

// ── Static storage ─────────────────────────────────────────────────────────────

static ExtensionsConfig s_cfg;
static bool             s_loaded = false;

// ── Defaults ───────────────────────────────────────────────────────────────────

static void apply_defaults(ExtensionsConfig& c) {
    // Candidate extensions, WebP-first. Modern AO2 content is overwhelmingly
    // WebP, so probing it first (then .webp.static, .png, .gif) lands the real
    // asset on the first try for almost every server while the classic formats
    // remain as fallbacks — no asset 404s, just a smarter order. A server's
    // extensions.json still overrides these, and the learned-format hint
    // re-tunes the order per server at runtime if it turns out to be png/gif.

    // charicon: .webp, .png
    c.charicon_count = 2;
    std::strncpy(c.charicon[0], ".webp", ExtensionsConfig::EXT_LEN - 1);
    std::strncpy(c.charicon[1], ".png",  ExtensionsConfig::EXT_LEN - 1);

    // emote (pre-anims + (a)/(b) sprites): .webp, .webp.static, .png, .gif, .apng
    c.emote_count = 5;
    std::strncpy(c.emote[0], ".webp",        ExtensionsConfig::EXT_LEN - 1);
    std::strncpy(c.emote[1], ".webp.static", ExtensionsConfig::EXT_LEN - 1);
    std::strncpy(c.emote[2], ".png",         ExtensionsConfig::EXT_LEN - 1);
    std::strncpy(c.emote[3], ".gif",         ExtensionsConfig::EXT_LEN - 1);
    std::strncpy(c.emote[4], ".apng",        ExtensionsConfig::EXT_LEN - 1);

    // emotions (emote-button icons): .webp, .png
    c.emotions_count = 2;
    std::strncpy(c.emotions[0], ".webp", ExtensionsConfig::EXT_LEN - 1);
    std::strncpy(c.emotions[1], ".png",  ExtensionsConfig::EXT_LEN - 1);

    // background: .webp, .png, .gif, .apng
    c.background_count = 4;
    std::strncpy(c.background[0], ".webp", ExtensionsConfig::EXT_LEN - 1);
    std::strncpy(c.background[1], ".png",  ExtensionsConfig::EXT_LEN - 1);
    std::strncpy(c.background[2], ".gif",  ExtensionsConfig::EXT_LEN - 1);
    std::strncpy(c.background[3], ".apng", ExtensionsConfig::EXT_LEN - 1);
}

// ── Learned winning format (thread-safe) ────────────────────────────────────────
// Stores (ext_index + 1) per category so static zero-init naturally means
// "unknown" (0). SDL_atomic_t reads/writes are atomic; workers and the main
// thread both touch this, but it only biases probe order so relaxed is fine.
static SDL_atomic_t s_learned[ExtensionsConfig::CAT_COUNT];

int ExtensionsConfig::learned(Category c) {
    if (c < 0 || c >= CAT_COUNT) return -1;
    return SDL_AtomicGet(&s_learned[c]) - 1;   // 0 (unknown) → -1
}

void ExtensionsConfig::note(Category c, int ext_index) {
    if (c < 0 || c >= CAT_COUNT || ext_index < 0 || ext_index >= MAX_EXTS) return;
    SDL_AtomicSet(&s_learned[c], ext_index + 1);
}

// Resolve a category to its extension array + count in the current config.
static int cat_exts(ExtensionsConfig::Category c,
                    const char (**out)[ExtensionsConfig::EXT_LEN]) {
    const ExtensionsConfig& ec = ExtensionsConfig::get();
    switch (c) {
        case ExtensionsConfig::CAT_CHARICON:   *out = ec.charicon;   return ec.charicon_count;
        case ExtensionsConfig::CAT_EMOTE:      *out = ec.emote;      return ec.emote_count;
        case ExtensionsConfig::CAT_EMOTIONS:   *out = ec.emotions;   return ec.emotions_count;
        case ExtensionsConfig::CAT_BACKGROUND: *out = ec.background; return ec.background_count;
        default: *out = nullptr; return 0;
    }
}

const char* ExtensionsConfig::ext_at(Category c, int idx) {
    const char (*exts)[EXT_LEN] = nullptr;
    int n = cat_exts(c, &exts);
    return (exts && idx >= 0 && idx < n) ? exts[idx] : "";
}

int ExtensionsConfig::probe_order(Category c, int out_idx[], int max_out) {
    const char (*exts)[EXT_LEN] = nullptr;
    int n = cat_exts(c, &exts);
    if (n > max_out) n = max_out;
    int li = learned(c);
    int w = 0;
    if (li >= 0 && li < n) out_idx[w++] = li;       // learned format first
    for (int i = 0; i < n && w < max_out; ++i)
        if (i != li) out_idx[w++] = i;
    return w;
}

// ── Per-server learned-format persistence ───────────────────────────────────────
// File of lines "<asset_url>|<charicon>|<emote>|<emotions>|<background>", newest
// first. Each field is a learned extension (".webp") or empty if that category
// never decoded for that server. Switch-only.
#ifdef __SWITCH__
static constexpr const char* FORMATS_PATH = "sdmc:/switch/ferris-ao/formats.cfg";
static constexpr int FORMATS_MAX_LINES = 64;

static int ext_index_of(ExtensionsConfig::Category c, const char* ext) {
    if (!ext || !ext[0]) return -1;
    const char (*exts)[ExtensionsConfig::EXT_LEN] = nullptr;
    int n = cat_exts(c, &exts);
    for (int i = 0; i < n; ++i) if (std::strcmp(exts[i], ext) == 0) return i;
    return -1;
}

void ExtensionsConfig::persist() {
    const char* url = AssetManager::asset_url();
    if (!url || !url[0]) return;

    const char* le[CAT_COUNT];
    bool any = false;
    for (int c = 0; c < CAT_COUNT; ++c) {
        int li = learned((Category)c);
        le[c] = (li >= 0) ? ext_at((Category)c, li) : "";
        if (le[c][0]) any = true;
    }
    if (!any) return;   // learned nothing this session — leave the file as-is

    // Read existing lines, dropping any for this same URL (we replace it).
    static char lines[FORMATS_MAX_LINES][640];
    int nlines = 0;
    size_t ulen = std::strlen(url);
    if (FILE* f = std::fopen(FORMATS_PATH, "rb")) {
        char buf[640];
        while (nlines < FORMATS_MAX_LINES - 1 && std::fgets(buf, sizeof(buf), f)) {
            int L = (int)std::strlen(buf);
            while (L > 0 && (buf[L-1] == '\n' || buf[L-1] == '\r')) buf[--L] = '\0';
            if (L == 0) continue;
            if (std::strncmp(buf, url, ulen) == 0 && buf[ulen] == '|') continue;  // old entry
            std::strncpy(lines[nlines], buf, sizeof(lines[0]) - 1);
            lines[nlines][sizeof(lines[0]) - 1] = '\0';
            ++nlines;
        }
        std::fclose(f);
    }

    ::mkdir("sdmc:/switch",           0777);
    ::mkdir("sdmc:/switch/ferris-ao", 0777);
    FILE* f = std::fopen(FORMATS_PATH, "wb");
    if (!f) return;
    std::fprintf(f, "%s|%s|%s|%s|%s\n", url,
                 le[CAT_CHARICON], le[CAT_EMOTE], le[CAT_EMOTIONS], le[CAT_BACKGROUND]);
    for (int i = 0; i < nlines; ++i) std::fprintf(f, "%s\n", lines[i]);
    std::fclose(f);
}

void ExtensionsConfig::restore() {
    const char* url = AssetManager::asset_url();
    if (!url || !url[0]) return;
    FILE* f = std::fopen(FORMATS_PATH, "rb");
    if (!f) return;
    char buf[640];
    size_t ulen = std::strlen(url);
    while (std::fgets(buf, sizeof(buf), f)) {
        int L = (int)std::strlen(buf);
        while (L > 0 && (buf[L-1] == '\n' || buf[L-1] == '\r')) buf[--L] = '\0';
        if (std::strncmp(buf, url, ulen) != 0 || buf[ulen] != '|') continue;
        char* p = buf + ulen + 1;     // split the rest on '|' into per-category exts
        for (int c = 0; c < CAT_COUNT && p; ++c) {
            char* bar = std::strchr(p, '|');
            if (bar) *bar = '\0';
            int idx = ext_index_of((Category)c, p);
            if (idx >= 0) note((Category)c, idx);
            p = bar ? bar + 1 : nullptr;
        }
        break;   // first (newest) match wins
    }
    std::fclose(f);
}
#else
void ExtensionsConfig::persist() {}
void ExtensionsConfig::restore() {}
#endif

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
    for (int c = 0; c < CAT_COUNT; ++c)
        SDL_AtomicSet(&s_learned[c], 0);   // forget learned formats — next server may differ
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
        restore();   // seed learned formats from a previous visit to this server
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

    restore();   // seed learned formats from a previous visit to this server
}

} // namespace ao
