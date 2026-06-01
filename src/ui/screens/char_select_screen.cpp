#include "char_select_screen.hpp"
#include "courtroom_screen.hpp"
#include "../touch.hpp"
#include "../../app.hpp"
#include "../../render/renderer.hpp"
#include "../../state/game_state.hpp"
#include "../../input/input_manager.hpp"
#include "../../net/packet_queue.hpp"
#include "../../protocol/commands.hpp"
#include "../../assets/texture_cache.hpp"
#include "../../assets/asset_manager.hpp"
#include "../../assets/asset_stream.hpp"
#include "../../assets/extensions_config.hpp"
#include <SDL2/SDL.h>
#include <cstdio>
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
    // App already bulk-queued every character icon at lobby-enter, so there's
    // nothing to do here — update() will pick them up as they land in the
    // prefetch cache.
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
    GameState& gs = app_.state();
    int total = gs.char_count;
    if (total == 0) return;

    // Touch / mouse: tap a cell to highlight it, tap the highlighted cell to pick.
    int tx, ty;
    if (tap_point(e, app_.renderer().raw(), tx, ty)) {
        for (int row = 0; row < ROWS; ++row)
            for (int col = 0; col < COLS; ++col) {
                int idx = scroll_ + row * COLS + col;
                if (idx >= total) continue;
                SDL_Rect cell = {START_X + col * (CELL_W + CELL_GAP),
                                 START_Y + row * (CELL_H + CELL_GAP), CELL_W, CELL_H};
                if (pt_in(tx, ty, cell)) {
                    if (idx == selected_) pick_char(idx);
                    else                  selected_ = idx;
                    return;
                }
            }
        return;
    }

    // Use keyboard for emulator convenience
    if (e.type == SDL_KEYDOWN) {
        switch (e.key.keysym.sym) {
            case SDLK_RIGHT: selected_ = (selected_ + 1) % total; break;
            case SDLK_LEFT:  selected_ = (selected_ - 1 + total) % total; break;
            case SDLK_DOWN:  selected_ = (selected_ + COLS) % total; break;
            case SDLK_UP:    selected_ = (selected_ - COLS + total) % total; break;
            case SDLK_RETURN: pick_char(selected_); break;
            default: break;
        }
    }
    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: selected_=(selected_+1)%total; break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  selected_=(selected_-1+total)%total; break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  selected_=(selected_+COLS)%total; break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:    selected_=(selected_-COLS+total)%total; break;
            case SDL_CONTROLLER_BUTTON_A:          pick_char(selected_); break;
            default: break;
        }
    }
}

void CharSelectScreen::update(uint32_t /*dt*/) {
    GameState& gs = app_.state();
    int total = gs.char_count;
    if (total == 0) return;
    if (selected_ >= total) selected_ = total - 1;
    // Keep scroll in sync with selected
    if (selected_ < scroll_) scroll_ = selected_;
    if (selected_ >= scroll_ + PAGE) scroll_ = selected_ - PAGE + 1;
    if (scroll_ < 0) scroll_ = 0;

    // On-demand icon streaming for the visible + lookahead window only. Icons
    // are NOT bulk-queued at lobby-enter (that flooded the AssetStream queue on
    // big servers and starved the courtroom).
    //
    // Two passes, deliberately split so neither slows fetching:
    //   - DECODE runs every frame: cheap peek() (no lock) + has_prefetch(), and
    //     self-limits as icons land (a decoded icon hits peek() and is skipped).
    //   - ENQUEUE runs ONLY when the scroll window moved. Queueing takes the
    //     worker queue lock, so doing it every frame for still-downloading icons
    //     would contend with the 8 workers and slow them down — gating on scroll
    //     means we queue each window exactly once.
    constexpr int DECODE_BUDGET   = 12;
    constexpr int LOOKAHEAD_PAGES = 2;  // scan 2 pages (current + next)
    const ExtensionsConfig& ec = ExtensionsConfig::get();
    const bool do_enqueue = (scroll_ != pf_scroll_);
    int decoded = 0;
    int scan_start = scroll_;
    int scan_end   = scroll_ + PAGE * LOOKAHEAD_PAGES;
    if (scan_end > total) scan_end = total;
    for (int i = scan_start; i < scan_end; ++i) {
        if (!gs.characters[i].name[0]) continue;
        char lname[64]; lower_copy(lname, gs.characters[i].name, sizeof(lname));

        bool cached = false, prefetched = false;
        for (int e = 0; e < ec.charicon_count; ++e) {
            char path[256];
            std::snprintf(path, sizeof(path), "characters/%s/char_icon%s",
                lname, ec.charicon[e]);
            if (app_.tex_cache().peek(path)) { cached = true; break; }
            if (!prefetched && AssetManager::has_prefetch(path)) {
                if (decoded < DECODE_BUDGET) {
                    app_.tex_cache().get(app_.renderer().raw(), path);
                    ++decoded;
                }
                prefetched = true;   // bytes are in cache; decoded now or soon
            }
        }
        if (cached || prefetched || !do_enqueue) continue;

        // Not cached and not yet downloaded — queue it once for this window.
        char base[256];
        std::snprintf(base, sizeof(base), "characters/%s/char_icon", lname);
        app_.asset_stream().prefetch_charicon(base);
    }
    pf_scroll_ = scroll_;
}

void CharSelectScreen::render() {
    Renderer& r = app_.renderer();
    GameState& gs = app_.state();

    r.fill_rect({0, 0, Renderer::WIDTH, Renderer::HEIGHT}, {10, 10, 25, 255});
    // Title bar
    r.fill_rect({0, 0, Renderer::WIDTH, 60}, {25, 40, 80, 255});
    app_.text().draw("Select Character", 20, 18, {220, 220, 255, 255});

    // Asset streaming status (bottom bar)
    r.fill_rect({0, 690, Renderer::WIDTH, 30}, {15, 15, 30, 255});
    if (AssetManager::has_asset_url()) {
        char hint[300];
        std::snprintf(hint, sizeof(hint), "Streaming assets from: %s", AssetManager::asset_url());
        app_.text().draw(hint, 10, 697, {80, 180, 80, 255});
    } else {
        app_.text().draw(
            "No asset URL — icons need servers.cfg or server ASS packet",
            10, 697, {140, 100, 60, 255});
    }

    if (gs.char_count == 0) {
        app_.text().draw("Waiting for character list...", 40, 100, {160, 160, 160, 255});
        return;
    }

    // Character grid (geometry constants live in the header so the touch
    // hit-test in handle_event stays in lockstep with this layout).
    for (int row = 0; row < ROWS; ++row) {
        for (int col = 0; col < COLS; ++col) {
            int idx = scroll_ + row * COLS + col;
            if (idx >= gs.char_count) break;

            int x = START_X + col * (CELL_W + CELL_GAP);
            int y = START_Y + row * (CELL_H + CELL_GAP);
            SDL_Rect cell = {x, y, CELL_W, CELL_H};

            SDL_Color bg = gs.char_taken[idx]
                ? SDL_Color{40, 20, 20, 255}
                : (idx == selected_
                    ? SDL_Color{60, 100, 180, 255}
                    : SDL_Color{30, 30, 55, 255});
            r.fill_rect(cell, bg);
            r.draw_rect(cell, {80, 80, 120, 255});

            // Try to draw character icon — check each extension in order
            if (gs.characters[idx].name[0]) {
                const ExtensionsConfig& ec2 = ExtensionsConfig::get();
                char lname[64]; lower_copy(lname, gs.characters[idx].name, sizeof(lname));
                SDL_Texture* icon = nullptr;
                for (int e = 0; e < ec2.charicon_count && !icon; ++e) {
                    char icon_path[256];
                    std::snprintf(icon_path, sizeof(icon_path),
                        "characters/%s/char_icon%s", lname, ec2.charicon[e]);
                    icon = app_.tex_cache().peek(icon_path);
                }
                if (icon) {
                    SDL_Rect icon_dst = {x + 4, y + 4, CELL_W - 8, CELL_H - 28};
                    r.draw(icon, nullptr, &icon_dst);
                }
            }

            // Show character name if known, otherwise slot number
            const char* name = gs.characters[idx].name;
            char label[32];
            if (name[0] != '\0') {
                std::snprintf(label, sizeof(label), "%.12s", name);
            } else {
                std::snprintf(label, sizeof(label), "%d", idx);
            }
            SDL_Color tc = gs.char_taken[idx]
                ? SDL_Color{120, 80, 80, 255}
                : SDL_Color{200, 200, 220, 255};
            app_.text().draw(label, x + 6, y + CELL_H - 22, tc);
        }
    }
}

} // namespace ao
