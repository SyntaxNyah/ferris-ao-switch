#include "char_select_screen.hpp"
#include "courtroom_screen.hpp"
#include "../touch.hpp"
#include "../../app.hpp"
#include "../../render/renderer.hpp"
#include "../../state/game_state.hpp"
#include "../../input/virtual_keyboard.hpp"
#include "../../protocol/commands.hpp"
#include "../../assets/texture_cache.hpp"
#include "../../assets/asset_manager.hpp"
#include "../../assets/asset_stream.hpp"
#include "../../assets/extensions_config.hpp"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <cctype>

namespace ao {

// webAO lowercases character names in asset paths: characters/${name.toLowerCase()}/...
static void lower_copy(char* dst, const char* src, int cap) {
    int i = 0;
    for (; src[i] && i < cap - 1; ++i)
        dst[i] = (char)std::tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

CharSelectScreen::CharSelectScreen(App& app) : Screen(app) {}

void CharSelectScreen::on_enter() {
    selected_ = 0;
    scroll_   = 0;
    pf_scroll_ = -1;
    search_[0] = '\0';
    char_count_ = app_.state().char_count;
    filt_count_ = 0;
}

// Recompute the filtered index list from the current search query. Only used
// while searching; otherwise the visible list is every slot (identity mapping).
void CharSelectScreen::rebuild_filter() {
    GameState& gs = app_.state();
    filt_count_ = 0;
    if (!searching()) return;
    char q[48]; lower_copy(q, search_, sizeof(q));
    for (int i = 0; i < gs.char_count &&
                    filt_count_ < (int)(sizeof(filt_) / sizeof(filt_[0])); ++i) {
        if (!gs.characters[i].name[0]) continue;
        char nm[64]; lower_copy(nm, gs.characters[i].name, sizeof(nm));
        if (std::strstr(nm, q)) filt_[filt_count_++] = i;
    }
}

void CharSelectScreen::open_search() {
    char q[48] = {};
    if (!show_keyboard("Search characters", search_, q, sizeof(q))) return;
    std::strncpy(search_, q, sizeof(search_) - 1);
    search_[sizeof(search_) - 1] = '\0';
    rebuild_filter();
    selected_ = 0;
    scroll_   = 0;
    pf_scroll_ = -1;
}

// Claim a character (CC) and enter the courtroom. Optimistically records
// my_char_id so the IC composer works before the server's PV confirmation lands.
void CharSelectScreen::pick_char(int idx) {
    GameState& gs = app_.state();
    if (idx < 0 || idx >= gs.char_count || gs.char_taken[idx]) return;
    gs.my_char_id = idx;
    char buf[256];
    int n = cmd::cc(buf, sizeof(buf), gs.my_uid, idx, "ferris-ao-switch");
    app_.send_packet(buf, n);
    app_.push_screen(new CourtroomScreen(app_));
}

void CharSelectScreen::handle_event(const SDL_Event& e) {
    char_count_ = app_.state().char_count;
    int total = vis_count();

    // Touch / mouse.
    int tx, ty;
    if (tap_point(e, app_.renderer().raw(), tx, ty)) {
        if (pt_in(tx, ty, SEARCH_BAR)) { open_search(); return; }
        for (int row = 0; row < ROWS; ++row)
            for (int col = 0; col < COLS; ++col) {
                int pos = scroll_ + row * COLS + col;
                if (pos >= total) continue;
                SDL_Rect cell = {START_X + col * (CELL_W + CELL_GAP),
                                 START_Y + row * (CELL_H + CELL_GAP), CELL_W, CELL_H};
                if (pt_in(tx, ty, cell)) {
                    if (pos == selected_) pick_char(real_index(pos));
                    else                  selected_ = pos;
                    return;
                }
            }
        return;
    }

    if (total == 0) {
        // No matches (or no roster yet): still allow opening search, and always
        // allow clearing it so a bad query can't trap you on an empty grid.
        bool open = (e.type == SDL_KEYDOWN &&
                     (e.key.keysym.sym == SDLK_f || e.key.keysym.sym == SDLK_SLASH)) ||
                    (e.type == SDL_CONTROLLERBUTTONDOWN &&
                     e.cbutton.button == SDL_CONTROLLER_BUTTON_Y);
        bool clear = (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) ||
                     (e.type == SDL_CONTROLLERBUTTONDOWN &&
                      e.cbutton.button == SDL_CONTROLLER_BUTTON_B);
        if (open)  open_search();
        if (clear && searching()) { search_[0] = '\0'; selected_ = scroll_ = 0; pf_scroll_ = -1; }
        // A tap also reaches here (handled above); let tapping the search bar work.
        return;
    }

    // Mouse wheel scrolls a row at a time (update() row-aligns scroll_).
    if (e.type == SDL_MOUSEWHEEL && e.wheel.y != 0) {
        selected_ -= e.wheel.y * COLS;
        if (selected_ < 0) selected_ = 0;
        if (selected_ >= total) selected_ = total - 1;
        return;
    }

    if (e.type == SDL_KEYDOWN) {
        switch (e.key.keysym.sym) {
            case SDLK_RIGHT: selected_ = (selected_ + 1) % total; break;
            case SDLK_LEFT:  selected_ = (selected_ - 1 + total) % total; break;
            case SDLK_DOWN:  selected_ = (selected_ + COLS) % total; break;
            case SDLK_UP:    selected_ = (selected_ - COLS + total) % total; break;
            case SDLK_RETURN: pick_char(real_index(selected_)); break;
            case SDLK_f: case SDLK_SLASH: open_search(); break;
            case SDLK_ESCAPE:
                if (searching()) { search_[0] = '\0'; selected_ = scroll_ = 0; pf_scroll_ = -1; }
                break;
            default: break;
        }
    }
    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: selected_=(selected_+1)%total; break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  selected_=(selected_-1+total)%total; break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  selected_=(selected_+COLS)%total; break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:    selected_=(selected_-COLS+total)%total; break;
            case SDL_CONTROLLER_BUTTON_A:          pick_char(real_index(selected_)); break;
            case SDL_CONTROLLER_BUTTON_Y:          open_search(); break;       // search
            case SDL_CONTROLLER_BUTTON_B:
                if (searching()) { search_[0] = '\0'; selected_ = scroll_ = 0; pf_scroll_ = -1; }
                break;
            default: break;
        }
    }
}

void CharSelectScreen::update(uint32_t /*dt*/) {
    GameState& gs = app_.state();

    // Rebuild the filter if the roster grew/changed while searching.
    if (gs.char_count != char_count_) {
        char_count_ = gs.char_count;
        if (searching()) rebuild_filter();
    }

    int total = vis_count();
    if (total == 0) return;
    if (selected_ >= total) selected_ = total - 1;
    if (selected_ < 0) selected_ = 0;

    // Row-aligned scroll so the grid never shows a half-shifted row.
    int sel_row = selected_ / COLS;
    int top_row = scroll_ / COLS;
    if (sel_row < top_row)            top_row = sel_row;
    if (sel_row >= top_row + ROWS)    top_row = sel_row - ROWS + 1;
    if (top_row < 0)                  top_row = 0;
    scroll_ = top_row * COLS;

    // On-demand icon streaming for the visible + lookahead window only (never
    // bulk-queued — that starved the courtroom on big servers). DECODE runs each
    // frame (cheap peek/has_prefetch, self-limiting as icons land); ENQUEUE runs
    // only when the window moved, so the worker queue lock isn't hammered.
    constexpr int DECODE_BUDGET   = 12;
    constexpr int LOOKAHEAD_PAGES = 2;
    const ExtensionsConfig& ec = ExtensionsConfig::get();
    const bool do_enqueue = (scroll_ != pf_scroll_);
    int decoded = 0;
    int scan_end = scroll_ + PAGE * LOOKAHEAD_PAGES;
    if (scan_end > total) scan_end = total;
    for (int pos = scroll_; pos < scan_end; ++pos) {
        int idx = real_index(pos);
        if (idx < 0 || idx >= gs.char_count || !gs.characters[idx].name[0]) continue;
        char lname[64]; lower_copy(lname, gs.characters[idx].name, sizeof(lname));

        bool cached = false, prefetched = false;
        for (int x = 0; x < ec.charicon_count; ++x) {
            char path[256];
            std::snprintf(path, sizeof(path), "characters/%s/char_icon%s", lname, ec.charicon[x]);
            if (app_.tex_cache().peek(path)) { cached = true; break; }
            if (!prefetched && AssetManager::has_prefetch(path)) {
                if (decoded < DECODE_BUDGET) { app_.tex_cache().get(app_.renderer().raw(), path); ++decoded; }
                prefetched = true;
            }
        }
        if (cached || prefetched || !do_enqueue) continue;

        char base[256];
        std::snprintf(base, sizeof(base), "characters/%s/char_icon", lname);
        app_.asset_stream().prefetch_charicon(base);
    }
    pf_scroll_ = scroll_;
}

void CharSelectScreen::render() {
    Renderer& r = app_.renderer();
    GameState& gs = app_.state();
    TextRenderer& txt = app_.text();
    char_count_ = gs.char_count;

    r.fill_rect({0, 0, Renderer::WIDTH, Renderer::HEIGHT}, {10, 10, 25, 255});
    r.fill_rect({0, 0, Renderer::WIDTH, 60}, {25, 40, 80, 255});
    txt.draw("Select Character", 20, 18, {220, 220, 255, 255});

    // Search bar (tap, or Y / F to type).
    r.fill_rect(SEARCH_BAR, {18, 22, 42, 255});
    r.draw_rect(SEARCH_BAR, {70, 90, 150, 255});
    int sy = SEARCH_BAR.y + (SEARCH_BAR.h - txt.line_h()) / 2;
    if (searching()) {
        char sb[96];
        std::snprintf(sb, sizeof(sb), "Search: %s", search_);
        txt.draw(sb, SEARCH_BAR.x + 10, sy, {255, 235, 150, 255});
        char cnt[48];
        std::snprintf(cnt, sizeof(cnt), "%d match%s   (B clears)",
                      filt_count_, filt_count_ == 1 ? "" : "es");
        txt.draw(cnt, SEARCH_BAR.x + SEARCH_BAR.w + 16, sy, {150, 160, 180, 255});
    } else {
        txt.draw("Search characters  (Y / tap)", SEARCH_BAR.x + 10, sy, {140, 150, 175, 255});
    }

    // Asset streaming status (bottom bar)
    r.fill_rect({0, 690, Renderer::WIDTH, 30}, {15, 15, 30, 255});
    if (AssetManager::has_asset_url()) {
        char hint[300];
        std::snprintf(hint, sizeof(hint), "Streaming assets from: %s", AssetManager::asset_url());
        txt.draw(hint, 10, 697, {80, 180, 80, 255});
    } else {
        txt.draw("No asset URL — icons need servers.cfg or server ASS packet",
                 10, 697, {140, 100, 60, 255});
    }

    if (gs.char_count == 0) {
        txt.draw("Waiting for character list...", 40, 120, {160, 160, 160, 255});
        return;
    }
    int total = vis_count();
    if (total == 0) {
        txt.draw("No characters match your search.  (B / Esc to clear)",
                 40, 130, {200, 170, 120, 255});
        return;
    }

    const ExtensionsConfig& ec = ExtensionsConfig::get();
    for (int row = 0; row < ROWS; ++row) {
        for (int col = 0; col < COLS; ++col) {
            int pos = scroll_ + row * COLS + col;
            if (pos >= total) break;
            int idx = real_index(pos);

            int x = START_X + col * (CELL_W + CELL_GAP);
            int y = START_Y + row * (CELL_H + CELL_GAP);
            SDL_Rect cell = {x, y, CELL_W, CELL_H};

            SDL_Color bg = gs.char_taken[idx]
                ? SDL_Color{40, 20, 20, 255}
                : (pos == selected_ ? SDL_Color{60, 100, 180, 255}
                                    : SDL_Color{30, 30, 55, 255});
            r.fill_rect(cell, bg);
            r.draw_rect(cell, {80, 80, 120, 255});

            if (gs.characters[idx].name[0]) {
                char lname[64]; lower_copy(lname, gs.characters[idx].name, sizeof(lname));
                SDL_Texture* icon = nullptr;
                for (int x2 = 0; x2 < ec.charicon_count && !icon; ++x2) {
                    char icon_path[256];
                    std::snprintf(icon_path, sizeof(icon_path),
                        "characters/%s/char_icon%s", lname, ec.charicon[x2]);
                    icon = app_.tex_cache().peek(icon_path);
                }
                if (icon) {
                    SDL_Rect icon_dst = {x + 4, y + 4, CELL_W - 8, CELL_H - 28};
                    r.draw(icon, nullptr, &icon_dst);
                }
            }

            const char* name = gs.characters[idx].name;
            char label[32];
            if (name[0] != '\0') std::snprintf(label, sizeof(label), "%.12s", name);
            else                 std::snprintf(label, sizeof(label), "%d", idx);
            SDL_Color tc = gs.char_taken[idx]
                ? SDL_Color{120, 80, 80, 255} : SDL_Color{200, 200, 220, 255};
            txt.draw(label, x + 6, y + CELL_H - 22, tc);
        }
    }
}

} // namespace ao
