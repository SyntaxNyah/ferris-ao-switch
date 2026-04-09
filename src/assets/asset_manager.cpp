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

static char s_asset_url[512] = {};

// clear_failed() forward declaration — defined after the failure cache block below.
static void clear_failed();

void AssetManager::set_asset_url(const char* url) {
    std::strncpy(s_asset_url, url ? url : "", sizeof(s_asset_url) - 1);
    // Strip trailing slash(es)
    int len = (int)std::strlen(s_asset_url);
    while (len > 0 && s_asset_url[len - 1] == '/') s_asset_url[--len] = '\0';
    clear_failed(); // URL changed — old failure entries are for a different server
    std::fprintf(stderr, "[assets] streaming URL set: '%s'\n", s_asset_url);
}

void AssetManager::clear_asset_url() {
    s_asset_url[0] = '\0';
    clear_failed(); // new server may have the same paths — reset failure cache
    std::fprintf(stderr, "[assets] streaming URL cleared\n");
}

bool        AssetManager::has_asset_url() { return s_asset_url[0] != '\0'; }
const char* AssetManager::asset_url()     { return s_asset_url; }

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
// Remembers paths that returned a 404/error so we never retry until the
// asset URL changes (which resets the cache in clear_asset_url).

static constexpr int FAIL_SLOTS = 512;
static char          s_failed[FAIL_SLOTS][256] = {};
static int           s_failed_count            = 0;
// Reuse the prefetch mutex to guard this cache too.

static bool check_failed(const char* relative) {
    // Called with prefetch mutex already held.
    for (int i = 0; i < s_failed_count; ++i)
        if (std::strcmp(s_failed[i], relative) == 0) return true;
    return false;
}

static void add_failed(const char* relative) {
    SDL_LockMutex(get_prefetch_mutex());
    if (!check_failed(relative)) {
        if (s_failed_count < FAIL_SLOTS) {
            std::strncpy(s_failed[s_failed_count], relative, 255);
            s_failed[s_failed_count][255] = '\0';
            ++s_failed_count;
        }
    }
    SDL_UnlockMutex(get_prefetch_mutex());
}

static bool is_failed(const char* relative) {
    SDL_LockMutex(get_prefetch_mutex());
    bool found = check_failed(relative);
    SDL_UnlockMutex(get_prefetch_mutex());
    return found;
}

static void clear_failed() {
    SDL_LockMutex(get_prefetch_mutex());
    s_failed_count = 0;
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

uint8_t* AssetManager::fetch_bytes(const char* relative, int* out_size) {
    // 0. Prefetch cache (pre-fetched by AssetStream)
    uint8_t* pre = consume_prefetch(relative, out_size);
    if (pre) return pre;

    // 1. HTTP streaming (if server provided an asset URL)
    if (s_asset_url[0] != '\0') {
        // Skip paths we already know 404 on this server
        if (!is_failed(relative)) {
            char full_url[768];
            std::snprintf(full_url, sizeof(full_url), "%s/%s", s_asset_url, relative);
            HttpResult hr = http_get(full_url);
            if (hr.ok) {
                *out_size = hr.size;
                return hr.data; // caller owns SDL_malloc'd buffer
            }
            add_failed(relative);
            std::fprintf(stderr, "[assets] HTTP miss '%s', trying local\n", relative);
        }
    }

    return fetch_local(relative, out_size);
}

uint8_t* AssetManager::fetch_bytes_with_client(const char* relative, int* out_size,
                                               HttpClient& client) {
    // 0. Prefetch cache (pre-fetched by AssetStream)
    uint8_t* pre = consume_prefetch(relative, out_size);
    if (pre) return pre;

    // 1. HTTP streaming via persistent client (reuses TCP/TLS connection)
    if (s_asset_url[0] != '\0') {
        if (!is_failed(relative)) {
            char full_url[768];
            std::snprintf(full_url, sizeof(full_url), "%s/%s", s_asset_url, relative);
            HttpResult hr = client.get(full_url);
            if (hr.ok) {
                *out_size = hr.size;
                return hr.data;
            }
            add_failed(relative);
        }
    }

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
