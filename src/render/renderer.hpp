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
namespace Layout {
    // Full screen
    inline constexpr int W = 1280;
    inline constexpr int H = 720;

    // Viewport: 4:3 sprite area occupying the left portion
    inline constexpr SDL_Rect VIEWPORT      = {0,   0,   853, 480};

    // Chat area at the bottom
    inline constexpr SDL_Rect CHAT_AREA     = {0,   480, 1280, 240};
    inline constexpr SDL_Rect CHATBOX       = {10,  492, 1260, 200};
    inline constexpr SDL_Rect NAMEPLATE     = {16,  478, 260,  32 };

    // Side panel (right of viewport)
    inline constexpr SDL_Rect SIDE_PANEL    = {853, 0,   427, 480};

    // HP bars (inside side panel)
    inline constexpr SDL_Rect HP_DEF        = {870, 10,  390, 24 };
    inline constexpr SDL_Rect HP_PROS       = {870, 44,  390, 24 };

    // Bottom-right action buttons
    inline constexpr SDL_Rect BTN_OOC       = {1040, 622, 70, 38};
    inline constexpr SDL_Rect BTN_MUSIC     = {1120, 622, 70, 38};
    inline constexpr SDL_Rect BTN_EVIDENCE  = {1200, 622, 72, 38};

    // Overlay panels (slide in from the right)
    inline constexpr SDL_Rect PANEL_OOC     = {640, 0, 640, 720};
    inline constexpr SDL_Rect PANEL_MUSIC   = {640, 0, 640, 720};
    inline constexpr SDL_Rect PANEL_EVIDENCE= {640, 0, 640, 720};

    // IC input overlay (full-screen modal)
    inline constexpr SDL_Rect IC_INPUT_BG   = {0,   0,   W,   H  };
    inline constexpr SDL_Rect IC_EMOTE_GRID = {20,  20,  840, 520};
    inline constexpr SDL_Rect IC_OPTIONS    = {20,  550, 840, 100};
    inline constexpr SDL_Rect IC_TEXT_BOX   = {880, 20,  380, 80 };
}

} // namespace ao
