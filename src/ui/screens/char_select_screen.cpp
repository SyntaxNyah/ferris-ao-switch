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
#include "../../assets/char_ini_parser.hpp"
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

// Mirror CourtroomScreen's emote candidate rules so we can pre-warm a picked
// character's default sprite before the courtroom even opens (.png/.webp.static
// use the bare name; every other format uses the (a)/(b) prefix).
static void cs_emote_path(char* out, int cap, const char* c, const char* e,
                          const char* prefix, const char* ext) {
    if (!std::strcmp(ext, ".png"))
        std::snprintf(out, cap, "characters/%s/%s.png", c, e);
    else if (!std::strcmp(ext, ".webp.static"))
        std::snprintf(out, cap, "characters/%s/%s.webp", c, e);
    else
        std::snprintf(out, cap, "characters/%s/%s%s%s", c, prefix, e, ext);
}
static void cs_prefetch_emote(AssetStream& s, const char* c, const char* e,
                              const char* prefix) {
    if (!c[0] || !e[0]) return;
    const ExtensionsConfig& ec = ExtensionsConfig::get();
    char p[256];
    for (int i = 0; i < ec.emote_count; ++i) {
        cs_emote_path(p, sizeof(p), c, e, prefix, ec.emote[i]);
        if (!AssetManager::has_prefetch(p)) s.prefetch_decode(p);   // decode off-thread
    }
}

CharSelectScreen::CharSelectScreen(App& app) : Screen(app) { apply_zoom(); }

void CharSelectScreen::on_enter() {
    selected_ = 0;
    scroll_   = 0;
    pf_scroll_ = -1;
    ci_pf_sel_ = -1;
    sprite_pf_sel_ = -1;
    search_[0] = '\0';
    char_count_ = app_.state().char_count;
    filt_count_ = 0;
    // Warm the room background now so it's cached the instant a character is
    // picked — it's the area's background, independent of which character.
    prefetch_area_scene();
}

// Pre-warm the room background (common positions) while the user browses, so the
// courtroom stage is already there when they pick. Char-independent.
void CharSelectScreen::prefetch_area_scene() {
    GameState& gs = app_.state();
    if (!gs.background[0]) return;
    char bg[128]; lower_copy(bg, gs.background, sizeof(bg));
    const ExtensionsConfig& ec = ExtensionsConfig::get();
    AssetStream& s = app_.asset_stream();
    static const char* files[] = {
        "witnessempty", "defenseempty", "prosecutorempty",   // backgrounds
        "stand", "defensedesk", "prosecutiondesk"            // desks
    };
    char p[256];
    for (int f = 0; f < 6; ++f)
        for (int e = 0; e < ec.background_count; ++e) {
            std::snprintf(p, sizeof(p), "background/%s/%s%s", bg, files[f], ec.background[e]);
            if (!AssetManager::has_prefetch(p)) s.prefetch_decode(p);   // decode off-thread
        }
}

// Once the highlighted character's char.ini is cached, parse it and pre-warm its
// default emote's (a)/(b) sprite — so the courtroom shows your character with no
// extra round-trip after you press A. Done once per selection.
void CharSelectScreen::prefetch_sel_sprite() {
    GameState& gs = app_.state();
    if (selected_ == sprite_pf_sel_) return;
    int real = real_index(selected_);
    if (real < 0 || real >= gs.char_count || !gs.characters[real].name[0]) return;
    char lname[64]; lower_copy(lname, gs.characters[real].name, sizeof(lname));
    char ini[160]; std::snprintf(ini, sizeof(ini), "characters/%s/char.ini", lname);
    if (!AssetManager::has_prefetch(ini)) return;   // wait until char.ini is cached
    // Parsing consumes the cached char.ini, but the disk cache still backs the
    // courtroom's later read, so it doesn't re-hit the network.
    CharDef def;
    if (load_char_ini(gs.characters[real].name, def) && def.emotion_count > 0) {
        char emo[64]; lower_copy(emo, def.emotions[0].idle_anim, sizeof(emo));
        cs_prefetch_emote(app_.asset_stream(), lname, emo, "(a)");
        cs_prefetch_emote(app_.asset_stream(), lname, emo, "(b)");
        // Also the default emote's button thumbnails — the courtroom IC composer's
        // preview shows these, so warming + decoding them here (off-thread) removes
        // the "loading sprite..." you'd otherwise see right after pressing A. Probe
        // webp→png via prefetch_emoticon (buttons aren't always .png).
        AssetStream& s = app_.asset_stream();
        char bb[256];
        std::snprintf(bb, sizeof(bb), "characters/%s/emotions/button1_off", lname);
        s.prefetch_emoticon(bb);
        std::snprintf(bb, sizeof(bb), "characters/%s/emotions/button1_on", lname);
        s.prefetch_emoticon(bb);
    }
    sprite_pf_sel_ = selected_;   // done for this selection (even if it had no emotes)
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

// Move the selection by whole rows — the shared path for the mouse wheel and a
// touch drag. update() row-aligns scroll_ afterwards to keep selection visible,
// so moving the cursor IS scrolling the grid here. `rows` follows the wheel sign
// convention (positive = toward earlier entries / scroll up).
void CharSelectScreen::scroll_by(int rows) {
    int total = vis_count();
    if (total == 0) return;
    selected_ -= rows * cols_;
    if (selected_ < 0) selected_ = 0;
    if (selected_ >= total) selected_ = total - 1;
}

// Recompute the visible grid for the current zoom level. cols_/rows_ come from a
// preset table; cell_w_/cell_h_ are derived to fill the grid area, so zooming out
// packs many small icons on screen — the key to navigating thousands of chars.
void CharSelectScreen::apply_zoom() {
    static const int Z[ZOOM_COUNT][2] = { {8,4}, {10,5}, {12,6}, {16,8}, {20,11} };
    if (zoom_ < 0) zoom_ = 0;
    if (zoom_ >= ZOOM_COUNT) zoom_ = ZOOM_COUNT - 1;
    cols_ = Z[zoom_][0];
    rows_ = Z[zoom_][1];
    cell_w_ = (Renderer::WIDTH - 2 * START_X - (cols_ - 1) * CELL_GAP) / cols_;
    cell_h_ = (GRID_BOTTOM - START_Y     - (rows_ - 1) * CELL_GAP) / rows_;
}

// Change zoom by `delta` (negative = zoom in / bigger). Keeps the current
// selection on screen and re-queues the icon window for the new density.
void CharSelectScreen::set_zoom(int delta) {
    int z = zoom_ + delta;
    if (z < 0) z = 0;
    if (z >= ZOOM_COUNT) z = ZOOM_COUNT - 1;
    if (z == zoom_) return;
    zoom_ = z;
    apply_zoom();
    pf_scroll_ = -1;        // window changed — re-evaluate which icons to prefetch
    scroll_    = 0;         // update() re-centres on the selection next frame
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

    // Touch / mouse: tap (press-release) or finger drag-scroll.
    int tx, ty, rows;
    TouchDrag::Kind k = drag_.feed(e, app_.renderer().raw(), cell_h_ + CELL_GAP, tx, ty, rows);
    if (k == TouchDrag::TAP) {
        if (pt_in(tx, ty, SEARCH_BAR)) { open_search(); return; }
        for (int row = 0; row < rows_; ++row)
            for (int col = 0; col < cols_; ++col) {
                int pos = scroll_ + row * cols_ + col;
                if (pos >= total) continue;
                SDL_Rect cell = {START_X + col * (cell_w_ + CELL_GAP),
                                 START_Y + row * (cell_h_ + CELL_GAP), cell_w_, cell_h_};
                if (pt_in(tx, ty, cell)) {
                    if (pos == selected_) pick_char(real_index(pos));
                    else                  selected_ = pos;
                    return;
                }
            }
        return;
    }
    if (k == TouchDrag::SCROLL) { scroll_by(rows); return; }

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

    // Mouse wheel (Ryujinx/desktop): scroll a row; hold Ctrl to ZOOM; hold Shift
    // to page (fast-scroll). update() row-aligns scroll_ afterwards.
    if (e.type == SDL_MOUSEWHEEL && e.wheel.y != 0) {
        SDL_Keymod mod = SDL_GetModState();
        if (mod & KMOD_CTRL)        set_zoom(e.wheel.y > 0 ? -1 : +1);  // wheel-up = zoom in
        else if (mod & KMOD_SHIFT)  scroll_by(e.wheel.y * rows_);       // a whole page per notch
        else                        scroll_by(e.wheel.y);               // a row per notch
        return;
    }

    if (e.type == SDL_KEYDOWN) {
        switch (e.key.keysym.sym) {
            case SDLK_RIGHT: selected_ = (selected_ + 1) % total; break;
            case SDLK_LEFT:  selected_ = (selected_ - 1 + total) % total; break;
            case SDLK_DOWN:  selected_ = (selected_ + cols_) % total; break;
            case SDLK_UP:    selected_ = (selected_ - cols_ + total) % total; break;
            case SDLK_RETURN: pick_char(real_index(selected_)); break;
            case SDLK_f: case SDLK_SLASH: open_search(); break;
            case SDLK_MINUS:  set_zoom(+1); break;                  // zoom out (more, smaller)
            case SDLK_EQUALS: case SDLK_PLUS: set_zoom(-1); break;  // zoom in (fewer, bigger)
            case SDLK_PAGEUP:   scroll_by(rows_);  break;           // fast scroll (a page)
            case SDLK_PAGEDOWN: scroll_by(-rows_); break;
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
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  selected_=(selected_+cols_)%total; break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:    selected_=(selected_-cols_+total)%total; break;
            case SDL_CONTROLLER_BUTTON_A:          pick_char(real_index(selected_)); break;
            case SDL_CONTROLLER_BUTTON_Y:          open_search(); break;       // search
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  set_zoom(+1); break;     // L: zoom out
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: set_zoom(-1); break;     // R: zoom in
            case SDL_CONTROLLER_BUTTON_B:
                if (searching()) { search_[0] = '\0'; selected_ = scroll_ = 0; pf_scroll_ = -1; }
                break;
            default: break;
        }
    }
    // ZL / ZR triggers page the grid (fast scroll). Edge-detect the analog axis.
    if (e.type == SDL_CONTROLLERAXISMOTION) {
        const int TH = 16000;
        if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
            bool down = e.caxis.value > TH;
            if (down && !tl_held_) scroll_by(rows_);    // ZL → page up
            tl_held_ = down;
        } else if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
            bool down = e.caxis.value > TH;
            if (down && !tr_held_) scroll_by(-rows_);   // ZR → page down
            tr_held_ = down;
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
    int sel_row = selected_ / cols_;
    int top_row = scroll_ / cols_;
    if (sel_row < top_row)            top_row = sel_row;
    if (sel_row >= top_row + rows_)   top_row = sel_row - rows_ + 1;
    if (top_row < 0)                  top_row = 0;
    scroll_ = top_row * cols_;

    // Pre-warm the HIGHLIGHTED character's char.ini as you browse (it's tiny).
    // The courtroom needs it parsed before it can fetch emote sprites/buttons, so
    // having it cached by the time you press A removes a whole round-trip from the
    // "loading sprites" wait.
    if (selected_ != ci_pf_sel_) {
        ci_pf_sel_ = selected_;
        int real = real_index(selected_);
        if (real >= 0 && real < gs.char_count && gs.characters[real].name[0]) {
            char lname[64]; lower_copy(lname, gs.characters[real].name, sizeof(lname));
            char rel[160]; std::snprintf(rel, sizeof(rel), "characters/%s/char.ini", lname);
            if (!AssetManager::has_prefetch(rel)) app_.asset_stream().prefetch(rel);
        }
    }
    // Once that char.ini lands, pre-warm the character's default sprite too.
    prefetch_sel_sprite();

    // On-demand icon streaming for the visible + lookahead window only (never
    // bulk-queued — that starved the courtroom on big servers). DECODE runs each
    // frame (cheap peek/has_prefetch, self-limiting as icons land); ENQUEUE runs
    // only when the window moved, so the worker queue lock isn't hammered.
    // With off-thread decode the per-frame cost here is just a GPU upload
    // (SDL_CreateTextureFromSurface) of an already-decoded icon, so the grid can
    // fill much faster — a full 32-cell page in ~1.5 frames instead of ~3.
    constexpr int DECODE_BUDGET = 24;
    const ExtensionsConfig& ec = ExtensionsConfig::get();
    const bool do_enqueue = (scroll_ != pf_scroll_);
    int decoded = 0;
    // Visible page + one page of look-ahead, but cap the look-ahead so a dense
    // zoom level (200+ cells/page) doesn't flood the prefetch queue.
    int page = cols_ * rows_;
    int lookahead = page > 128 ? 128 : page;
    int scan_end = scroll_ + page + lookahead;
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

    // The selected character's name — always visible, since dense zoom hides the
    // per-cell labels. Plus a one-line hint for the zoom / fast-scroll controls.
    {
        int total0 = vis_count();
        if (total0 > 0 && selected_ >= 0 && selected_ < total0) {
            int ridx = real_index(selected_);
            if (ridx >= 0 && ridx < gs.char_count && gs.characters[ridx].name[0]) {
                char hdr[64];
                std::snprintf(hdr, sizeof(hdr), "\xE2\x80\xBA %.32s", gs.characters[ridx].name);
                txt.draw(hdr, 270, 18, {255, 235, 150, 255});
            }
        }
    }
    txt.draw("Ctrl+Wheel: zoom   Shift+Wheel: fast", Renderer::WIDTH - 360, 22,
             {120, 130, 160, 255});

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

    // At dense zoom the cells are too small for a name — show icons only and
    // surface the selected character's name in the header instead.
    const bool show_labels = cell_w_ >= 96;
    const int  pad     = cell_w_ >= 96 ? 4 : 2;
    const int  label_h = show_labels ? 22 : 0;

    const ExtensionsConfig& ec = ExtensionsConfig::get();
    for (int row = 0; row < rows_; ++row) {
        for (int col = 0; col < cols_; ++col) {
            int pos = scroll_ + row * cols_ + col;
            if (pos >= total) break;
            int idx = real_index(pos);

            int x = START_X + col * (cell_w_ + CELL_GAP);
            int y = START_Y + row * (cell_h_ + CELL_GAP);
            SDL_Rect cell = {x, y, cell_w_, cell_h_};

            SDL_Color bg = gs.char_taken[idx]
                ? SDL_Color{40, 20, 20, 255}
                : (pos == selected_ ? SDL_Color{60, 100, 180, 255}
                                    : SDL_Color{30, 30, 55, 255});
            r.fill_rect(cell, bg);
            r.draw_rect(cell, pos == selected_ ? SDL_Color{150, 190, 255, 255}
                                               : SDL_Color{80, 80, 120, 255});

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
                    SDL_Rect icon_dst = {x + pad, y + pad,
                                         cell_w_ - 2 * pad, cell_h_ - 2 * pad - label_h};
                    r.draw(icon, nullptr, &icon_dst);
                }
            }

            if (show_labels) {
                const char* name = gs.characters[idx].name;
                char label[32];
                if (name[0] != '\0') std::snprintf(label, sizeof(label), "%.12s", name);
                else                 std::snprintf(label, sizeof(label), "%d", idx);
                SDL_Color tc = gs.char_taken[idx]
                    ? SDL_Color{120, 80, 80, 255} : SDL_Color{200, 200, 220, 255};
                txt.draw(label, x + 6, y + cell_h_ - 20, tc);
            }
        }
    }
}

} // namespace ao
