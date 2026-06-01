#include "connect_screen.hpp"
#include "../touch.hpp"
#include "../../app.hpp"
#include "../../render/renderer.hpp"
#include "../../input/virtual_keyboard.hpp"
#include "../../net/http_fetch.hpp"
#include "../../assets/asset_manager.hpp"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace ao {

// ── JSON parser helpers ────────────────────────────────────────────────────────

// Skip whitespace. Returns pointer to first non-space char.
static const char* json_skip_ws(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
        ++p;
    return p;
}

// Find the value for a given key within a flat JSON object { ... }.
// p should point just past the opening '{'.
// Writes the value string (unquoted for strings, raw for numbers) into out[cap].
// Returns pointer to the char after the value, or nullptr if not found.
static const char* json_find_key(const char* obj_start, const char* obj_end,
                                  const char* key, char* out, int cap) {
    const char* p = obj_start;
    while (p < obj_end) {
        p = json_skip_ws(p, obj_end);
        if (p >= obj_end || *p != '"') break;
        ++p; // skip opening quote of key
        // read key name
        char kbuf[64] = {};
        int  ki = 0;
        while (p < obj_end && *p != '"' && ki < 63)
            kbuf[ki++] = *p++;
        if (p >= obj_end) break;
        ++p; // skip closing quote
        p = json_skip_ws(p, obj_end);
        if (p >= obj_end || *p != ':') break;
        ++p; // skip colon
        p = json_skip_ws(p, obj_end);
        if (p >= obj_end) break;

        bool key_matches = (std::strcmp(kbuf, key) == 0);

        if (*p == '"') {
            // String value
            ++p;
            int vi = 0;
            while (p < obj_end && *p != '"') {
                if (*p == '\\' && (p + 1) < obj_end) {
                    ++p; // skip escape char
                    if (key_matches && vi < cap - 1) out[vi++] = *p;
                } else {
                    if (key_matches && vi < cap - 1) out[vi++] = *p;
                }
                ++p;
            }
            if (key_matches) {
                out[vi] = '\0';
                return (p < obj_end) ? p + 1 : p;
            }
            if (p < obj_end) ++p; // skip closing quote
        } else {
            // Numeric / bool / null — read until comma, }, or whitespace
            const char* val_start = p;
            while (p < obj_end && *p != ',' && *p != '}' && *p != ' '
                   && *p != '\t' && *p != '\r' && *p != '\n')
                ++p;
            if (key_matches) {
                int vlen = (int)(p - val_start);
                if (vlen >= cap) vlen = cap - 1;
                std::memcpy(out, val_start, vlen);
                out[vlen] = '\0';
                return p;
            }
        }

        // Advance past comma
        p = json_skip_ws(p, obj_end);
        if (p < obj_end && *p == ',') ++p;
    }
    out[0] = '\0';
    return nullptr;
}

// Parse the AAO master-server JSON array into out[max_out].
// Format: [{"ip":"..","port":N,"ws_port":N,"players":N,"name":"..","description":".."},...]
static int parse_servers(const char* json, int len,
                          ServerEntry* out, int max_out) {
    int count = 0;
    const char* p   = json;
    const char* end = json + len;

    while (p < end && count < max_out) {
        // Find next '{'
        while (p < end && *p != '{') ++p;
        if (p >= end) break;
        const char* obj_start = p + 1;

        // Find matching '}'
        int depth = 1;
        const char* q = obj_start;
        while (q < end && depth > 0) {
            if (*q == '{') ++depth;
            else if (*q == '}') --depth;
            ++q;
        }
        const char* obj_end = q - 1; // points at the '}'

        ServerEntry& e = out[count];
        std::memset(&e, 0, sizeof(e));
        e.port    = 27017;
        e.ws_port = 0;
        e.players = 0;

        char tmp[512];
        json_find_key(obj_start, obj_end, "name",        e.name,        sizeof(e.name));
        json_find_key(obj_start, obj_end, "ip",          e.ip,          sizeof(e.ip));
        json_find_key(obj_start, obj_end, "description", e.description, sizeof(e.description));
        json_find_key(obj_start, obj_end, "asset_url",   e.asset_url,   sizeof(e.asset_url));

        if (json_find_key(obj_start, obj_end, "port", tmp, sizeof(tmp)))
            e.port = std::atoi(tmp);
        if (json_find_key(obj_start, obj_end, "ws_port", tmp, sizeof(tmp)))
            e.ws_port = std::atoi(tmp);
        if (json_find_key(obj_start, obj_end, "wss_port", tmp, sizeof(tmp)))
            e.wss_port = std::atoi(tmp);
        if (json_find_key(obj_start, obj_end, "players", tmp, sizeof(tmp)))
            e.players = std::atoi(tmp);

        // Only add if we got a valid IP/host
        if (e.ip[0] != '\0')
            ++count;

        p = q;
    }
    return count;
}

// ── Fetch thread ──────────────────────────────────────────────────────────────

int SDLCALL ConnectScreen::fetch_thread_fn(void* ud) {
    auto* arg  = static_cast<FetchArg*>(ud);
    ConnectScreen* self = arg->self;
    delete arg;

    HttpResult r = http_get(self->ms_url_);

    if (!r.ok) {
        std::snprintf(self->fetch_error_, sizeof(self->fetch_error_),
            "%s", r.error[0] ? r.error : "fetch failed");
        SDL_AtomicSet(&self->fetch_state_atom_, (int)FetchState::Error);
        r.free();
        return 1;
    }

    // Null-terminate for safety (data is SDL_malloc'd; we have the exact size)
    auto* json = (char*)SDL_realloc(r.data, r.size + 1);
    if (!json) {
        SDL_AtomicSet(&self->fetch_state_atom_, (int)FetchState::Error);
        r.free();
        return 1;
    }
    r.data = nullptr; // ownership transferred to json
    json[r.size] = '\0';

    auto* buf = (ServerEntry*)SDL_malloc(sizeof(ServerEntry) * MAX_SERVERS);
    int   cnt = 0;
    if (buf) {
        cnt = parse_servers(json, r.size, buf, MAX_SERVERS);
    }
    SDL_free(json);

    SDL_LockMutex(self->fetch_mutex_);
    if (self->fetch_buf_) SDL_free(self->fetch_buf_);
    self->fetch_buf_   = buf;
    self->fetch_count_ = cnt;
    SDL_AtomicSet(&self->fetch_state_atom_, (int)FetchState::Done);
    SDL_UnlockMutex(self->fetch_mutex_);

    return 0;
}

// ── ConnectScreen ─────────────────────────────────────────────────────────────

// Parse sdmc:/switch/ferris-ao/servers.cfg for per-server asset URLs.
// Format: one entry per line, "hostname=https://asset-base-url"
// Lines starting with '#' or ';' are comments. Blank lines ignored.
void ConnectScreen::load_server_cfg() {
    cfg_count_ = 0;

    // Derive cfg path from user_base: strip trailing "/base" if present
    char cfg_path[512];
    const char* base = AssetManager::user_base(); // e.g. "sdmc:/switch/ferris-ao/base"
    std::strncpy(cfg_path, base, sizeof(cfg_path) - 20);
    cfg_path[sizeof(cfg_path) - 20] = '\0';

    // Strip "/base" suffix
    int len = (int)std::strlen(cfg_path);
    if (len >= 5 && std::strcmp(cfg_path + len - 5, "/base") == 0)
        cfg_path[len - 5] = '\0';

    std::strncat(cfg_path, "/servers.cfg", sizeof(cfg_path) - std::strlen(cfg_path) - 1);

    SDL_RWops* f = SDL_RWFromFile(cfg_path, "r");
    if (!f) {
        std::fprintf(stderr, "[servers.cfg] not found at %s\n", cfg_path);
        return;
    }

    Sint64 sz = SDL_RWsize(f);
    if (sz <= 0 || sz > 65536) { SDL_RWclose(f); return; }

    char* buf = (char*)SDL_malloc((int)sz + 1);
    if (!buf) { SDL_RWclose(f); return; }

    SDL_RWread(f, buf, 1, (int)sz);
    buf[sz] = '\0';
    SDL_RWclose(f);

    // Parse line by line
    char* line = buf;
    while (*line && cfg_count_ < MAX_CFG_ENTRIES) {
        // Find end of line
        char* eol = line;
        while (*eol && *eol != '\n' && *eol != '\r') ++eol;
        char saved = *eol;
        *eol = '\0';

        // Trim leading whitespace
        while (*line == ' ' || *line == '\t') ++line;

        if (*line && *line != '#' && *line != ';') {
            char* eq = std::strchr(line, '=');
            if (eq) {
                *eq = '\0';
                const char* host_part = line;
                const char* url_part  = eq + 1;

                // Trim trailing whitespace from host
                char* host_end = eq - 1;
                while (host_end >= line && (*host_end == ' ' || *host_end == '\t'))
                    *host_end-- = '\0';

                // Trim leading whitespace from url
                while (*url_part == ' ' || *url_part == '\t') ++url_part;

                if (host_part[0] && url_part[0]) {
                    std::strncpy(cfg_[cfg_count_].host,      host_part, sizeof(CfgEntry::host) - 1);
                    std::strncpy(cfg_[cfg_count_].asset_url, url_part,  sizeof(CfgEntry::asset_url) - 1);
                    cfg_[cfg_count_].host[sizeof(CfgEntry::host) - 1]           = '\0';
                    cfg_[cfg_count_].asset_url[sizeof(CfgEntry::asset_url) - 1] = '\0';
                    ++cfg_count_;
                    std::fprintf(stderr, "[servers.cfg] %s -> %s\n", host_part, url_part);
                }
            }
        }

        // Advance past end-of-line chars
        *eol = saved;
        line = eol;
        while (*line == '\n' || *line == '\r') ++line;
    }

    SDL_free(buf);
    std::fprintf(stderr, "[servers.cfg] loaded %d entries\n", cfg_count_);
}

const char* ConnectScreen::lookup_asset_url(const char* host) const {
    for (int i = 0; i < cfg_count_; ++i) {
        if (std::strcmp(cfg_[i].host, host) == 0)
            return cfg_[i].asset_url;
    }
    return nullptr;
}

ConnectScreen::ConnectScreen(App& app) : Screen(app) {
    fetch_mutex_ = SDL_CreateMutex();
    SDL_AtomicSet(&fetch_state_atom_, (int)FetchState::Idle);
    load_server_cfg();
}

ConnectScreen::~ConnectScreen() {
    // Join fetch thread if still running
    if (fetch_thread_) {
        SDL_WaitThread(fetch_thread_, nullptr);
        fetch_thread_ = nullptr;
    }
    if (fetch_buf_) {
        SDL_free(fetch_buf_);
        fetch_buf_ = nullptr;
    }
    if (fetch_mutex_) {
        SDL_DestroyMutex(fetch_mutex_);
        fetch_mutex_ = nullptr;
    }
}

void ConnectScreen::on_enter() {
    std::snprintf(status_, sizeof(status_),
        "Press R to refresh server list");

    // Pre-fill username from App
    // (App's username_ is the source of truth)

    if (server_count_ == 0 &&
        SDL_AtomicGet(&fetch_state_atom_) == (int)FetchState::Idle) {
        start_fetch();
    }
}

void ConnectScreen::on_exit() {
    // Nothing to clean up at screen-exit time
}

void ConnectScreen::start_fetch() {
    if (fetch_thread_) {
        // Join the old thread if it's done
        if (SDL_AtomicGet(&fetch_state_atom_) != (int)FetchState::Loading) {
            SDL_WaitThread(fetch_thread_, nullptr);
            fetch_thread_ = nullptr;
        } else {
            return; // still loading
        }
    }

    SDL_AtomicSet(&fetch_state_atom_, (int)FetchState::Loading);
    std::snprintf(status_, sizeof(status_), "Loading server list...");

    auto* arg  = new FetchArg{this};
    fetch_thread_ = SDL_CreateThread(fetch_thread_fn, "ms_fetch", arg);
    if (!fetch_thread_) {
        SDL_AtomicSet(&fetch_state_atom_, (int)FetchState::Error);
        std::snprintf(status_, sizeof(status_), "Failed to start fetch thread");
        delete arg;
    }
}

void ConnectScreen::parse_url(const char* src, char* out_host, int host_cap,
                               uint16_t* out_port, ConnMode* out_mode) const {
    *out_mode = ConnMode::TCP;
    uint16_t default_port = (uint16_t)std::atoi(port_str_);

    if (std::strncmp(src, "wss://", 6) == 0) {
        *out_mode    = ConnMode::WSS;
        default_port = 443;
        src += 6;
    } else if (std::strncmp(src, "ws://", 5) == 0) {
        *out_mode    = ConnMode::WS;
        src += 5;
    }

    // Copy bare hostname — stop at '/' or ':'
    int i = 0;
    while (*src && *src != '/' && *src != ':' && i < host_cap - 1)
        out_host[i++] = *src++;
    out_host[i] = '\0';

    // Optional embedded port
    if (*src == ':') {
        ++src;
        int p = std::atoi(src);
        if (p > 0 && p <= 65535) default_port = (uint16_t)p;
    }

    *out_port = default_port;
}

void ConnectScreen::open_keyboard(const char* hint, const char* initial,
                                   char* buf, int buf_sz, int max_len) {
    show_keyboard(hint, initial, buf, buf_sz);
    (void)max_len;
}

void ConnectScreen::connect_to_server(const ServerEntry& s) {
    char host[256];
    std::strncpy(host, s.ip, sizeof(host) - 1);
    host[sizeof(host) - 1] = '\0';

    uint16_t port;
    ConnMode mode;

    if (s.wss_port > 0) {
        mode = ConnMode::WSS;
        port = (uint16_t)s.wss_port;
    } else if (s.ws_port > 0) {
        mode = ConnMode::WS;
        port = (uint16_t)s.ws_port;
    } else {
        mode = ConnMode::TCP;
        port = (uint16_t)s.port;
    }

    // Priority: servers.cfg override > master-server JSON asset_url > nothing (ASS packet / auto)
    const char* cfg_url = lookup_asset_url(host);
    if (cfg_url) {
        AssetManager::set_asset_url(cfg_url);
        std::fprintf(stderr, "[connect] asset URL from servers.cfg: %s\n", cfg_url);
    } else if (s.asset_url[0] != '\0') {
        AssetManager::set_asset_url(s.asset_url);
        std::fprintf(stderr, "[connect] asset URL from server list: %s\n", s.asset_url);
    } else {
        AssetManager::clear_asset_url();
    }

    std::snprintf(status_, sizeof(status_), "Connecting to %s...", s.name);
    app_.connect(host, port, mode);
}

void ConnectScreen::connect_direct() {
    if (host_[0] == '\0') {
        std::snprintf(status_, sizeof(status_), "Enter a server address first");
        return;
    }
    char host[256];
    uint16_t port;
    ConnMode mode;
    parse_url(host_, host, sizeof(host), &port, &mode);

    // Apply per-server asset URL from servers.cfg if present
    const char* asset_url = lookup_asset_url(host);
    if (asset_url) {
        AssetManager::set_asset_url(asset_url);
        std::fprintf(stderr, "[connect] asset URL set to %s\n", asset_url);
    } else {
        AssetManager::clear_asset_url();
    }

    std::snprintf(status_, sizeof(status_), "Connecting to %s:%u...", host, (unsigned)port);
    app_.connect(host, port, mode);
}

// ── handle_event ──────────────────────────────────────────────────────────────

void ConnectScreen::handle_event(const SDL_Event& e) {
    auto btn_down = [&](SDL_GameControllerButton b) -> bool {
        return e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == b;
    };

    // ── Touch / mouse ──────────────────────────────────────────────────────────
    int tx, ty;
    if (tap_point(e, app_.renderer().raw(), tx, ty)) {
        // Tab bar [y 50..80]: Servers | Direct | Credits
        if (ty >= 50 && ty < 80) {
            if      (tx < 180) tab_ = 0;
            else if (tx < 362) tab_ = 1;
            else if (tx < 544) tab_ = 2;
            return;
        }
        if (tab_ == 0 && server_count_ > 0 && ty >= 80) {
            int i = (ty - 80) / ROW_H;
            int idx = scroll_offset_ + i;
            if (i >= 0 && i < VISIBLE_ROWS && idx < server_count_) {
                if (idx == server_sel_) connect_to_server(servers_[idx]);
                else                    server_sel_ = idx;
            }
            return;
        }
        if (tab_ == 1) {
            for (int i = 0; i < 4; ++i) {
                SDL_Rect row = {100, 80 + 20 + i * 100, Renderer::WIDTH - 200, 70};
                if (pt_in(tx, ty, row)) {
                    direct_sel_ = i;
                    if      (i == 0) open_keyboard("Server address", host_, host_, sizeof(host_), 255);
                    else if (i == 1) open_keyboard("Port", port_str_, port_str_, sizeof(port_str_), 7);
                    else if (i == 2) {
                        char uname[64];
                        std::strncpy(uname, app_.username(), sizeof(uname) - 1);
                        uname[sizeof(uname) - 1] = '\0';
                        open_keyboard("Username", uname, uname, sizeof(uname), 63);
                        app_.set_username(uname);
                    } else connect_direct();
                    return;
                }
            }
            return;
        }
        return;  // credits tab: tabs handled above, nothing else tappable
    }

    // ── Tab switch (L / R shoulder on either tab) ──────────────────────────────
    if (btn_down(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) ||
        (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_q)) {
        tab_ = (tab_ + 1) % 3;   // Servers → Direct → Credits → …
        return;
    }
    // Credits tab: only quit is actionable.
    if (tab_ == 2) {
        if (btn_down(SDL_CONTROLLER_BUTTON_START) ||
            (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
            app_.quit();
        return;
    }
    if (btn_down(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) && tab_ == 1) {
        // On direct tab, R connects
        connect_direct();
        return;
    }
    if (btn_down(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) && tab_ == 0) {
        // On server tab, R refreshes
        start_fetch();
        return;
    }

    // ── Tab 0: Server browser ──────────────────────────────────────────────────
    if (tab_ == 0) {
        if (btn_down(SDL_CONTROLLER_BUTTON_DPAD_UP) ||
            (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_UP)) {
            if (server_sel_ > 0) {
                --server_sel_;
                if (server_sel_ < scroll_offset_)
                    scroll_offset_ = server_sel_;
            }
            return;
        }
        if (btn_down(SDL_CONTROLLER_BUTTON_DPAD_DOWN) ||
            (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_DOWN)) {
            if (server_sel_ < server_count_ - 1) {
                ++server_sel_;
                if (server_sel_ >= scroll_offset_ + VISIBLE_ROWS)
                    scroll_offset_ = server_sel_ - VISIBLE_ROWS + 1;
            }
            return;
        }
        if (btn_down(SDL_CONTROLLER_BUTTON_A) ||
            (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_RETURN)) {
            if (server_count_ > 0 && server_sel_ < server_count_)
                connect_to_server(servers_[server_sel_]);
            return;
        }
        // ZL axis: edit master server URL
        if ((e.type == SDL_CONTROLLERAXISMOTION &&
             e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT &&
             e.caxis.value > 16000) ||
            (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_z)) {
            open_keyboard("Master server URL", ms_url_, ms_url_, sizeof(ms_url_), 255);
            return;
        }
        // + / Start: quit
        if (btn_down(SDL_CONTROLLER_BUTTON_START) ||
            (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
            app_.quit();
            return;
        }
        return;
    }

    // ── Tab 1: Direct connect ──────────────────────────────────────────────────
    if (tab_ == 1) {
        if (btn_down(SDL_CONTROLLER_BUTTON_DPAD_UP) ||
            (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_UP)) {
            if (direct_sel_ > 0) --direct_sel_;
            return;
        }
        if (btn_down(SDL_CONTROLLER_BUTTON_DPAD_DOWN) ||
            (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_DOWN)) {
            if (direct_sel_ < 3) ++direct_sel_;
            return;
        }
        if (btn_down(SDL_CONTROLLER_BUTTON_A) ||
            (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_RETURN)) {
            if (direct_sel_ == 0) {
                open_keyboard("Server address", host_, host_, sizeof(host_), 255);
            } else if (direct_sel_ == 1) {
                open_keyboard("Port", port_str_, port_str_, sizeof(port_str_), 7);
            } else if (direct_sel_ == 2) {
                char uname[64];
                std::strncpy(uname, app_.username(), sizeof(uname) - 1);
                uname[sizeof(uname) - 1] = '\0';
                open_keyboard("Username", uname, uname, sizeof(uname), 63);
                app_.set_username(uname);
            } else {
                connect_direct();
            }
            return;
        }
        // ZR axis (value > threshold) also triggers connect on direct tab
        if ((e.type == SDL_CONTROLLERAXISMOTION &&
             e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT &&
             e.caxis.value > 16000) ||
            (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_z)) {
            connect_direct();
            return;
        }
        // + / Escape: quit
        if (btn_down(SDL_CONTROLLER_BUTTON_START) ||
            (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
            app_.quit();
            return;
        }
    }
}

// ── update ────────────────────────────────────────────────────────────────────

void ConnectScreen::update(uint32_t /*dt_ms*/) {
    // Show any connection error reported by App (e.g. TCP connect failed,
    // WS upgrade failed, server closed connection during handshake).
    if (const char* err = app_.pending_error()) {
        std::snprintf(status_, sizeof(status_), "%s", err);
        app_.clear_pending_error();
    }

    FetchState fs = (FetchState)SDL_AtomicGet(&fetch_state_atom_);

    if (fs == FetchState::Done) {
        SDL_LockMutex(fetch_mutex_);
        if (fetch_buf_ && fetch_count_ > 0) {
            int cnt = (fetch_count_ < MAX_SERVERS) ? fetch_count_ : MAX_SERVERS;
            for (int i = 0; i < cnt; ++i) servers_[i] = fetch_buf_[i];
            server_count_ = cnt;
            SDL_free(fetch_buf_);
            fetch_buf_    = nullptr;
            fetch_count_  = 0;
        }
        SDL_AtomicSet(&fetch_state_atom_, (int)FetchState::Idle);
        SDL_UnlockMutex(fetch_mutex_);

        std::snprintf(status_, sizeof(status_),
            "Loaded %d servers  |  A: Connect  R: Refresh", server_count_);

        // Join the completed thread
        if (fetch_thread_) {
            SDL_WaitThread(fetch_thread_, nullptr);
            fetch_thread_ = nullptr;
        }
    } else if (fs == FetchState::Error) {
        SDL_AtomicSet(&fetch_state_atom_, (int)FetchState::Idle);
        std::snprintf(status_, sizeof(status_),
            "Error: %s  |  R to retry", fetch_error_);
        if (fetch_thread_) {
            SDL_WaitThread(fetch_thread_, nullptr);
            fetch_thread_ = nullptr;
        }
    }
}

// ── render ────────────────────────────────────────────────────────────────────

void ConnectScreen::render() {
    Renderer&     r    = app_.renderer();
    TextRenderer& text = app_.text();

    const SDL_Color WHITE  = {255, 255, 255, 255};
    const SDL_Color YELLOW = {255, 220,  50, 255};
    const SDL_Color GRAY   = {180, 180, 180, 255};
    const SDL_Color DIM    = {100, 100, 100, 255};
    const SDL_Color BLACK  = { 15,  15,  30, 255};
    const SDL_Color BLUE_SEL = { 40,  80, 160, 255};
    const SDL_Color DARK_ROW = { 25,  25,  45, 255};
    const SDL_Color TAB_ACT  = { 50,  80, 160, 255};
    const SDL_Color TAB_INACT= { 30,  30,  55, 255};

    // ── Background ────────────────────────────────────────────────────────────
    r.fill_rect({0, 0, Renderer::WIDTH, Renderer::HEIGHT}, BLACK);

    // ── Title bar [0,0,1280,50] ───────────────────────────────────────────────
    r.fill_rect({0, 0, Renderer::WIDTH, 50}, {30, 30, 60, 255});
    {
        char title[256];
        std::snprintf(title, sizeof(title),
            "Ferris-AO  |  Username: %s", app_.username());
        text.draw(title, 10, 15, WHITE);
    }

    // ── Tab bar [0,50,1280,30] ────────────────────────────────────────────────
    r.fill_rect({0,   50, 180, 30}, tab_ == 0 ? TAB_ACT : TAB_INACT);
    r.fill_rect({182, 50, 180, 30}, tab_ == 1 ? TAB_ACT : TAB_INACT);
    r.fill_rect({364, 50, 180, 30}, tab_ == 2 ? TAB_ACT : TAB_INACT);
    text.draw("Servers",       10,  57, tab_ == 0 ? YELLOW : GRAY);
    text.draw("Direct Connect",192, 57, tab_ == 1 ? YELLOW : GRAY);
    text.draw("Credits",       374, 57, tab_ == 2 ? YELLOW : GRAY);

    // ── Content area [0,80,1280,560] ──────────────────────────────────────────
    const int CONTENT_Y = 80;
    const int CONTENT_W = Renderer::WIDTH;

    if (tab_ == 0) {
        // ── Server browser ────────────────────────────────────────────────────
        FetchState fs = (FetchState)SDL_AtomicGet(&fetch_state_atom_);
        if (fs == FetchState::Loading) {
            text.draw("Loading server list...", CONTENT_W / 2 - 80, CONTENT_Y + 20, GRAY);
        } else if (server_count_ == 0) {
            text.draw("No servers. Press R to refresh.", 20, CONTENT_Y + 20, DIM);
        } else {
            int visible = (server_count_ < VISIBLE_ROWS) ? server_count_ : VISIBLE_ROWS;
            for (int i = 0; i < visible; ++i) {
                int idx = scroll_offset_ + i;
                if (idx >= server_count_) break;
                const ServerEntry& s = servers_[idx];

                bool selected = (idx == server_sel_);
                SDL_Rect row = {0, CONTENT_Y + i * ROW_H, CONTENT_W, ROW_H - 2};
                r.fill_rect(row, selected ? BLUE_SEL : DARK_ROW);
                if (selected)
                    r.draw_rect(row, YELLOW);

                // Name (left)
                text.draw(s.name, 12, CONTENT_Y + i * ROW_H + 14,
                          selected ? YELLOW : WHITE);

                // Player count (right)
                char pstr[32];
                std::snprintf(pstr, sizeof(pstr), "%d", s.players);
                int pw = text.measure_w(pstr);
                text.draw(pstr, CONTENT_W - pw - 12,
                          CONTENT_Y + i * ROW_H + 14, GRAY);

                // IP:port (small, dim)
                char addrstr[300];
                if (s.ws_port > 0)
                    std::snprintf(addrstr, sizeof(addrstr), "ws://%s:%d", s.ip, s.ws_port);
                else
                    std::snprintf(addrstr, sizeof(addrstr), "%s:%d", s.ip, s.port);
                text.draw(addrstr, 12, CONTENT_Y + i * ROW_H + 30, DIM);
            }

            // Scrollbar hint
            if (server_count_ > VISIBLE_ROWS) {
                char sc[32];
                std::snprintf(sc, sizeof(sc), "%d/%d", server_sel_ + 1, server_count_);
                text.draw(sc, CONTENT_W - 80, CONTENT_Y + VISIBLE_ROWS * ROW_H + 4, DIM);
            }

            // Selected server description
            if (server_sel_ < server_count_ && servers_[server_sel_].description[0]) {
                int desc_y = CONTENT_Y + VISIBLE_ROWS * ROW_H + 10;
                r.fill_rect({0, desc_y, CONTENT_W, 60}, {20, 20, 40, 200});
                text.draw_wrapped(servers_[server_sel_].description,
                                  10, desc_y + 4, CONTENT_W - 20, GRAY);
            }
        }
    } else if (tab_ == 1) {
        // ── Direct connect ────────────────────────────────────────────────────
        const char* labels[] = { "Server Address:", "Port:", "Username:", "[ Connect ]" };
        const char* values[] = { host_, port_str_, app_.username(), "" };

        for (int i = 0; i < 4; ++i) {
            bool sel = (i == direct_sel_);
            SDL_Rect row = {100, CONTENT_Y + 20 + i * 100, CONTENT_W - 200, 70};
            r.fill_rect(row, sel ? BLUE_SEL : DARK_ROW);
            if (sel) r.draw_rect(row, YELLOW);

            text.draw(labels[i], 110, CONTENT_Y + 20 + i * 100 + 10,
                      sel ? YELLOW : GRAY);
            if (i < 3 && values[i][0] != '\0') {
                text.draw(values[i], 110 + 280, CONTENT_Y + 20 + i * 100 + 10,
                          WHITE);
            } else if (i == 3) {
                int bw = text.measure_w("[ Connect ]");
                text.draw("[ Connect ]",
                          row.x + (row.w - bw) / 2,
                          CONTENT_Y + 20 + i * 100 + 25,
                          sel ? YELLOW : WHITE);
            }
        }
    } else {
        // ── Credits ───────────────────────────────────────────────────────────
        SDL_Rect box = {Renderer::WIDTH / 2 - 360, CONTENT_Y + 60, 720, 280};
        r.fill_rect(box, {20, 22, 44, 255});
        r.draw_rect(box, {70, 90, 150, 255});
        int cx = box.x + 30;
        int cy = box.y + 28;
        text.draw("ferris-ao-switch", cx, cy, YELLOW);                       cy += 46;
        text.draw("A Nintendo Switch homebrew client for Attorney Online 2",
                  cx, cy, WHITE);                                            cy += 50;
        text.draw("Created by SyntaxNyah", cx, cy, WHITE);                   cy += 36;
        text.draw("https://github.com/SyntaxNyah/ferris-ao-switch",
                  cx, cy, {130, 190, 255, 255});                            cy += 52;
        text.draw("Not affiliated with the official Attorney Online project.",
                  cx, cy, GRAY);
    }

    // ── Status bar [0,640,1280,50] ────────────────────────────────────────────
    r.fill_rect({0, 640, Renderer::WIDTH, 50}, {20, 20, 40, 255});
    if (status_[0])
        text.draw(status_, 10, 652, GRAY);

    // ── Hint bar [0,690,1280,30] ──────────────────────────────────────────────
    r.fill_rect({0, 690, Renderer::WIDTH, 30}, {15, 15, 30, 255});
    const char* hint = (tab_ == 0)
        ? "A:Connect  R:Refresh  ZL:Edit URL  L/R:Switch tab  +:Quit"
        : "A:Edit/Connect  ZR:Connect  L/R:Switch tab  +:Quit";
    text.draw(hint, 10, 697, DIM);
}

} // namespace ao
