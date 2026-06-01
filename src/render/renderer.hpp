#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

namespace ao {

class Renderer {
public:
    // 1280×720 — Switch native resolution (docked and handheld use same FB)
    static constexpr int WIDTH  = 1280;
    static constexpr int HEIGHT = 720;

    Renderer() = default;
    ~Renderer();

    bool init(SDL_Window* window);

    void clear(SDL_Color bg = {0, 0, 0, 255});
    void present();

    // Draw a texture (or portion of one) at dst. src == nullptr → full texture.
    void draw(SDL_Texture* tex,
              const SDL_Rect* src,
              const SDL_Rect* dst,
              bool flip_h = false);

    // Draw a filled rectangle
    void fill_rect(const SDL_Rect& r, SDL_Color c);

    // Draw a rectangle outline
    void draw_rect(const SDL_Rect& r, SDL_Color c);

    SDL_Renderer* raw() const { return renderer_; }

private:
    SDL_Renderer* renderer_ = nullptr;
};

// ── Layout constants ───────────────────────────────────────────────────────────
// Authentic AO2 composition for a 1280×720 screen: the courtroom stage
// (background + character sprites) fills the *entire* framebuffer, exactly like
// the desktop client, and a chat bar is overlaid across the bottom. HP bars and
// the now-playing strip sit in the top corners; the action-button row lives at
// the right edge of the chat bar. The old layout boxed the stage into a small
// 853×480 corner beside a huge side panel — that is what looked squashed.
namespace Layout {
    inline constexpr int W = 1280;
    inline constexpr int H = 720;

    // Full-screen stage. Sprites/background fill the screen; the chat bar and
    // HUD draw on top (the chat bar covers the lower body of the sprite, as in
    // every AO client).
    inline constexpr SDL_Rect VIEWPORT      = {0, 0, W, H};

    // Bottom chat bar (overlay).
    inline constexpr int CHAT_H = 176;
    inline constexpr int CHAT_Y = H - CHAT_H;                      // 544
    inline constexpr SDL_Rect CHAT_AREA     = {0, CHAT_Y, W, CHAT_H};
    inline constexpr SDL_Rect NAMEPLATE     = {28, CHAT_Y + 10, 380, 40};
    inline constexpr SDL_Rect CHATBOX       = {36, CHAT_Y + 60, 808, 104}; // IC text box

    // HP bars over the top corners (each gets an inline label chip when drawn).
    inline constexpr SDL_Rect HP_DEF        = {28, 46, 330, 26};
    inline constexpr SDL_Rect HP_PROS       = {W - 28 - 330, 46, 330, 26};

    // Now-playing music strip, centred along the top.
    inline constexpr SDL_Rect MUSIC_NAME    = {400, 44, W - 800, 30};

    // Action buttons: a row of five anchored to the right edge of the chat bar.
    // Tall enough for a two-line label (function + control-key hint) at 20pt.
    inline constexpr int BTN_W = 76, BTN_H = 64, BTN_GAP = 6;
    inline constexpr int BTN_Y  = CHAT_Y + 50;
    inline constexpr int BTN_DX = BTN_W + BTN_GAP;
    inline constexpr int BTN_X0 = W - 20 - (BTN_W * 5 + BTN_GAP * 4);   // 856
    inline constexpr SDL_Rect BTN_IC        = {BTN_X0,             BTN_Y, BTN_W, BTN_H};
    inline constexpr SDL_Rect BTN_OOC       = {BTN_X0 + BTN_DX * 1, BTN_Y, BTN_W, BTN_H};
    inline constexpr SDL_Rect BTN_MUSIC     = {BTN_X0 + BTN_DX * 2, BTN_Y, BTN_W, BTN_H};
    inline constexpr SDL_Rect BTN_EVIDENCE  = {BTN_X0 + BTN_DX * 3, BTN_Y, BTN_W, BTN_H};
    inline constexpr SDL_Rect BTN_AREA      = {BTN_X0 + BTN_DX * 4, BTN_Y, BTN_W, BTN_H};

    // Side/log area — retained so theme-file panel derivation has a sane base;
    // it is no longer painted as an opaque block.
    inline constexpr SDL_Rect SIDE_PANEL    = {W - 520, 0, 520, H};

    // Overlay panels (OOC / Music / Evidence) — right 40%, full height.
    inline constexpr SDL_Rect PANEL_OOC      = {W - 520, 0, 520, H};
    inline constexpr SDL_Rect PANEL_MUSIC    = {W - 520, 0, 520, H};
    inline constexpr SDL_Rect PANEL_EVIDENCE = {W - 520, 0, 520, H};

    // IC composer modal (centred) — holds the emote grid + sprite preview.
    inline constexpr SDL_Rect IC_COMPOSER    = {180, 96, 920, 528};
}

} // namespace ao
