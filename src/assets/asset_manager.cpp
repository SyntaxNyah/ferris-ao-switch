#include "asset_manager.hpp"
#include "../net/http_fetch.hpp"
#include <cstdio>
#include <cstring>

namespace ao {

// ── Platform paths ────────────────────────────────────────────────────────────

#ifdef __SWITCH__
static constexpr const char* USER_BASE  = "sdmc:/switch/ferris-ao/base";
static constexpr const char* ROMFS_BASE = "romfs:";
#else
static constexpr const char* USER_BASE  = "base";
static constexpr const char* ROMFS_BASE = "romfs";
#endif

const char* AssetManager::user_base()  { return USER_BASE;  }
const char* AssetManager::romfs_base() { return ROMFS_BASE; }

// ── Asset URL state ───────────────────────────────────────────────────────────

static char s_asset_url[512]     = {};  // primary — from ASS packet
static char s_secondary_url[512] = {};  // secondary — community fallback CDN

// Forward declarations — defined after the failure cache block below.
static void clear_failed_primary();
static void clear_failed_secondary();

// Normalise a URL in-place: strip trailing slashes, then append exactly one.
// This guarantees path joins via "%s%s" produce a well-formed URL.
static void normalise_base_url(char* url, int cap) {
    int len = (int)std::strlen(url);
    while (len > 0 && url[len - 1] == '/') url[--len] = '\0';
    if (len > 0 && len + 1 < cap) {
        url[len]     = '/';
        url[len + 1] = '\0';
    }
}

void AssetManager::set_asset_url(const char* url) {
    std::strncpy(s_asset_url, url ? url : "", sizeof(s_asset_url) - 1);
    normalise_base_url(s_asset_url, (int)sizeof(s_asset_url));
    clear_failed_primary(); // URL changed — old failures are for a different server
    std::fprintf(stderr, "[assets] primary URL set: '%s'\n", s_asset_url);
}

void AssetManager::clear_asset_url() {
    s_asset_url[0] = '\0';
    clear_failed_primary();
    std::fprintf(stderr, "[assets] primary URL cleared\n");
}

bool        AssetManager::has_asset_url() { return s_asset_url[0] != '\0'; }
const char* AssetManager::asset_url()     { return s_asset_url; }

void AssetManager::set_secondary_url(const char* url) {
    std::strncpy(s_secondary_url, url ? url : "", sizeof(s_secondary_url) - 1);
    normalise_base_url(s_secondary_url, (int)sizeof(s_secondary_url));
    clear_failed_secondary();
    std::fprintf(stderr, "[assets] secondary URL set: '%s'\n", s_secondary_url);
}

void AssetManager::clear_secondary_url() {
    s_secondary_url[0] = '\0';
    clear_failed_secondary();
    std::fprintf(stderr, "[assets] secondary URL cleared\n");
}

bool        AssetManager::has_secondary_url() { return s_secondary_url[0] != '\0'; }
const char* AssetManager::secondary_url()     { return s_secondary_url; }

// ── URL composition (AO-SDL MountHttp semantics) ─────────────────────────────
// Lowercases the relative path, percent-encodes unsafe characters, joins with
// the base URL. Preserves `()` because AO2 emote filenames are "normal(a).png".

static void lowercase_path(const char* in, char* out, int out_cap) {
    int i = 0;
    for (; in[i] && i < out_cap - 1; ++i) {
        unsigned char c = (unsigned char)in[i];
        out[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
    }
    out[i] = '\0';
}

static void url_encode_path(const char* in, char* out, int out_cap) {
    static const char hex[] = "0123456789ABCDEF";
    int o = 0;
    for (int i = 0; in[i]; ++i) {
        if (o + 4 >= out_cap) break;
        unsigned char c = (unsigned char)in[i];
        bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') ||
                    c == '/' || c == '.' || c == '-' || c == '_' || c == '~' ||
                    c == '(' || c == ')';
        if (safe) {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0xF];
        }
    }
    out[o] = '\0';
}

// Build an absolute URL from a base (already slash-terminated) and a relative
// asset path. Returns false if the composed URL doesn't fit. Relative paths
// are bounded at 256 bytes by the request queue; worst-case URL encoding
// triples each byte, so the intermediate buffer needs 256 * 3 + 1 = 769 bytes.
static bool build_http_url(const char* base, const char* relative,
                           char* out, int out_cap) {
    if (!base || !base[0]) return false;
    char lc [512];
    char enc[1024];
    lowercase_path(relative, lc,  sizeof(lc));
    url_encode_path(lc,       enc, sizeof(enc));
    int n = std::snprintf(out, out_cap, "%s%s", base, enc);
    return n > 0 && n < out_cap;
}

// ── Prefetch cache ────────────────────────────────────────────────────────────

static constexpr int PREFETCH_SLOTS = 768;

struct PrefetchEntry {
    char     rel[256];
    uint8_t* data;
    int      size;
    bool     occupied;
};

static PrefetchEntry s_prefetch[PREFETCH_SLOTS] = {};
static SDL_mutex*    s_prefetch_mutex           = nullptr;

static SDL_mutex* get_prefetch_mutex() {
    if (!s_prefetch_mutex) s_prefetch_mutex = SDL_CreateMutex();
    return s_prefetch_mutex;
}

void AssetManager::store_prefetch(const char* relative, uint8_t* data, int size) {
    SDL_LockMutex(get_prefetch_mutex());
    // Find empty slot or evict oldest (slot 0 = round-robin fallback)
    int slot = -1;
    for (int i = 0; i < PREFETCH_SLOTS; ++i) {
        if (!s_prefetch[i].occupied) { slot = i; break; }
    }
    if (slot < 0) {
        // Evict slot 0 and shift (simple FIFO)
        SDL_free(s_prefetch[0].data);
        std::memmove(&s_prefetch[0], &s_prefetch[1],
            sizeof(PrefetchEntry) * (PREFETCH_SLOTS - 1));
        slot = PREFETCH_SLOTS - 1;
        s_prefetch[slot].occupied = false;
    }
    std::strncpy(s_prefetch[slot].rel, relative, sizeof(s_prefetch[slot].rel) - 1);
    s_prefetch[slot].data     = data;
    s_prefetch[slot].size     = size;
    s_prefetch[slot].occupied = true;
    SDL_UnlockMutex(get_prefetch_mutex());
}

bool AssetManager::has_prefetch(const char* relative) {
    SDL_LockMutex(get_prefetch_mutex());
    bool found = false;
    for (int i = 0; i < PREFETCH_SLOTS; ++i) {
        if (s_prefetch[i].occupied &&
            std::strcmp(s_prefetch[i].rel, relative) == 0) {
            found = true; break;
        }
    }
    SDL_UnlockMutex(get_prefetch_mutex());
    return found;
}

// Consume a prefetch entry (removes it from cache, transfers ownership to caller).
static uint8_t* consume_prefetch(const char* relative, int* out_size) {
    SDL_LockMutex(get_prefetch_mutex());
    for (int i = 0; i < PREFETCH_SLOTS; ++i) {
        if (s_prefetch[i].occupied &&
            std::strcmp(s_prefetch[i].rel, relative) == 0) {
            uint8_t* data = s_prefetch[i].data;
            *out_size = s_prefetch[i].size;
            s_prefetch[i].occupied = false;
            s_prefetch[i].data     = nullptr;
            SDL_UnlockMutex(get_prefetch_mutex());
            return data;
        }
    }
    SDL_UnlockMutex(get_prefetch_mutex());
    return nullptr;
}

// ── HTTP failure cache ────────────────────────────────────────────────────────
// One set per mount (primary and secondary) so that a 404 on the primary CDN
// never blocks the secondary from being tried. Entries are wiped when the
// corresponding URL changes. Guarded by the prefetch mutex.

static constexpr int FAIL_SLOTS = 1024;

struct FailSet {
    char paths[FAIL_SLOTS][256];
    int  count;
};

static FailSet s_failed_p = {}; // primary
static FailSet s_failed_s = {}; // secondary

static bool check_failed(const FailSet& fs, const char* relative) {
    // Called with prefetch mutex already held.
    for (int i = 0; i < fs.count; ++i)
        if (std::strcmp(fs.paths[i], relative) == 0) return true;
    return false;
}

static void add_failed(FailSet& fs, const char* relative) {
    SDL_LockMutex(get_prefetch_mutex());
    if (!check_failed(fs, relative) && fs.count < FAIL_SLOTS) {
        std::strncpy(fs.paths[fs.count], relative, 255);
        fs.paths[fs.count][255] = '\0';
        ++fs.count;
    }
    SDL_UnlockMutex(get_prefetch_mutex());
}

static bool is_failed(const FailSet& fs, const char* relative) {
    SDL_LockMutex(get_prefetch_mutex());
    bool found = check_failed(fs, relative);
    SDL_UnlockMutex(get_prefetch_mutex());
    return found;
}

static void clear_failed_primary() {
    SDL_LockMutex(get_prefetch_mutex());
    s_failed_p.count = 0;
    SDL_UnlockMutex(get_prefetch_mutex());
}

static void clear_failed_secondary() {
    SDL_LockMutex(get_prefetch_mutex());
    s_failed_s.count = 0;
    SDL_UnlockMutex(get_prefetch_mutex());
}

// ── Local file helpers ────────────────────────────────────────────────────────

static bool file_exists(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (f) { std::fclose(f); return true; }
    return false;
}

static uint8_t* read_local_file(const char* path, int* out_size) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return nullptr;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > HTTP_MAX_BODY) { std::fclose(f); return nullptr; }
    auto* buf = (uint8_t*)SDL_malloc((int)sz);
    if (!buf) { std::fclose(f); return nullptr; }
    if ((long)std::fread(buf, 1, (size_t)sz, f) != sz) {
        std::fclose(f); SDL_free(buf); return nullptr;
    }
    std::fclose(f);
    *out_size = (int)sz;
    return buf;
}

// ── resolve (local paths only) ────────────────────────────────────────────────

bool AssetManager::resolve(const char* relative, char* out_path, int out_cap) {
    int n = std::snprintf(out_path, out_cap, "%s/%s", USER_BASE, relative);
    if (n > 0 && n < out_cap && file_exists(out_path)) return true;

    n = std::snprintf(out_path, out_cap, "%s/%s", ROMFS_BASE, relative);
    if (n > 0 && n < out_cap && file_exists(out_path)) return true;

    out_path[0] = '\0';
    return false;
}

// ── fetch_bytes ───────────────────────────────────────────────────────────────

// Local fallback tiers 2 + 3 (sdmc: → romfs:). Used by both fetch_bytes variants.
static uint8_t* fetch_local(const char* relative, int* out_size) {
    char path[512];
    int n = std::snprintf(path, sizeof(path), "%s/%s", USER_BASE, relative);
    if (n > 0 && n < (int)sizeof(path) && file_exists(path)) {
        uint8_t* data = read_local_file(path, out_size);
        if (data) return data;
    }
    n = std::snprintf(path, sizeof(path), "%s/%s", ROMFS_BASE, relative);
    if (n > 0 && n < (int)sizeof(path) && file_exists(path)) {
        uint8_t* data = read_local_file(path, out_size);
        if (data) return data;
    }
    *out_size = 0;
    return nullptr;
}

// Try an HTTP mount (either primary or secondary). Returns SDL_malloc'd bytes
// on success, nullptr on 404/error (and records the failure in `fs`). The
// `client` pointer is optional — when non-null, the request uses a persistent
// keep-alive connection, matching AO-SDL's HttpPool behaviour per worker.
static uint8_t* try_http_mount(const char* base, FailSet& fs, const char* tag,
                               const char* relative, int* out_size,
                               HttpClient* client) {
    if (!base || !base[0])          return nullptr;
    if (is_failed(fs, relative))    return nullptr;

    char url[2048]; // base (512) + percent-encoded path (1024) + slack
    if (!build_http_url(base, relative, url, sizeof(url))) {
        add_failed(fs, relative);
        return nullptr;
    }

    HttpResult hr = client ? client->get(url) : http_get(url);
    if (hr.ok) {
        *out_size = hr.size;
        return hr.data; // caller owns
    }
    add_failed(fs, relative);
    std::fprintf(stderr, "[assets] %s miss '%s'\n", tag, relative);
    return nullptr;
}

uint8_t* AssetManager::fetch_bytes(const char* relative, int* out_size) {
    // 0. Prefetch cache (pre-fetched by AssetStream)
    uint8_t* pre = consume_prefetch(relative, out_size);
    if (pre) return pre;

    // 1. Primary HTTP (ASS-advertised CDN)
    if (uint8_t* d = try_http_mount(s_asset_url,     s_failed_p, "primary",
                                    relative, out_size, nullptr))
        return d;

    // 2. Secondary HTTP (community fallback CDN)
    if (uint8_t* d = try_http_mount(s_secondary_url, s_failed_s, "secondary",
                                    relative, out_size, nullptr))
        return d;

    // 3/4. sdmc: → romfs:
    return fetch_local(relative, out_size);
}

uint8_t* AssetManager::fetch_bytes_with_client(const char* relative, int* out_size,
                                               HttpClient& client) {
    uint8_t* pre = consume_prefetch(relative, out_size);
    if (pre) return pre;

    if (uint8_t* d = try_http_mount(s_asset_url,     s_failed_p, "primary",
                                    relative, out_size, &client))
        return d;

    if (uint8_t* d = try_http_mount(s_secondary_url, s_failed_s, "secondary",
                                    relative, out_size, &client))
        return d;

    return fetch_local(relative, out_size);
}

// ── open_rwops — owning SDL_RWops ────────────────────────────────────────────
//
// Returns an SDL_RWops whose close callback frees the underlying buffer.
// Safe to pass to any SDL/SDL_image/SDL_mixer function with freesrc=1.

struct OwningBuf {
    uint8_t* data;
    int      size;
    int      pos;
};

static Sint64 own_size(SDL_RWops* ctx) {
    return (Sint64)((OwningBuf*)ctx->hidden.unknown.data1)->size;
}

static Sint64 own_seek(SDL_RWops* ctx, Sint64 offset, int whence) {
    auto* b = (OwningBuf*)ctx->hidden.unknown.data1;
    Sint64 np;
    switch (whence) {
        case RW_SEEK_SET: np = offset;           break;
        case RW_SEEK_CUR: np = b->pos + offset;  break;
        case RW_SEEK_END: np = b->size + offset; break;
        default: return -1;
    }
    if (np < 0 || np > b->size) return -1;
    b->pos = (int)np;
    return np;
}

static size_t own_read(SDL_RWops* ctx, void* ptr, size_t size, size_t maxnum) {
    auto* b      = (OwningBuf*)ctx->hidden.unknown.data1;
    int   remain = b->size - b->pos;
    int   want   = (int)(size * maxnum);
    int   got    = (want < remain) ? want : remain;
    if (got <= 0 || size == 0) return 0;
    std::memcpy(ptr, b->data + b->pos, (size_t)got);
    b->pos += got;
    return (size_t)(got / (int)size);
}

static size_t own_write(SDL_RWops* /*ctx*/, const void* /*ptr*/,
                        size_t /*size*/, size_t /*num*/) {
    return 0; // read-only
}

static int own_close(SDL_RWops* ctx) {
    auto* b = (OwningBuf*)ctx->hidden.unknown.data1;
    SDL_free(b->data);
    SDL_free(b);
    SDL_FreeRW(ctx);
    return 0;
}

SDL_RWops* AssetManager::open_rwops(const char* relative) {
    int      size = 0;
    uint8_t* data = fetch_bytes(relative, &size);
    if (!data) return nullptr;

    auto* b = (OwningBuf*)SDL_malloc(sizeof(OwningBuf));
    if (!b) { SDL_free(data); return nullptr; }
    b->data = data;
    b->size = size;
    b->pos  = 0;

    SDL_RWops* rw = SDL_AllocRW();
    if (!rw) { SDL_free(data); SDL_free(b); return nullptr; }

    rw->size  = own_size;
    rw->seek  = own_seek;
    rw->read  = own_read;
    rw->write = own_write;
    rw->close = own_close;
    rw->type  = SDL_RWOPS_UNKNOWN;
    rw->hidden.unknown.data1 = b;
    return rw;
}

} // namespace ao
