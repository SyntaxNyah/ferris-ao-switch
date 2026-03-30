#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

namespace ao {

// SDL_ttf wrapper with a 32-slot LRU texture cache.
//
// Each cache entry stores the rendered string (up to 255 chars), color, and
// wrap-width so identical draws hit the cache instantly. On eviction the
// oldest texture (by frame counter) is destroyed and replaced.
//
// Init: pass a *relative* font path; AssetManager::open_rwops resolves it
//       via HTTP CDN → sdmc: base → romfs, just like every other asset.
//
// If init() fails (font not found) every draw/measure call returns 0/false
// silently — the game runs without text rather than crashing.
class TextRenderer {
public:
    TextRenderer()  = default;
    ~TextRenderer();

    // Resolve font via AssetManager, open with TTF_OpenFontRW.
    // Returns true on success; text rendering is a no-op if false.
    bool init(SDL_Renderer* r, const char* font_rel, int pt_size);

    // Draw single-line text. Returns rendered width in pixels, 0 on failure.
    int draw(const char* text, int x, int y, SDL_Color color);

    // Draw word-wrapped text clipped to max_w pixels wide.
    // Returns total height used (may be multiple lines), 0 on failure.
    int draw_wrapped(const char* text, int x, int y, int max_w, SDL_Color color);

    // Measure single-line text width without rendering (no cache).
    int measure_w(const char* text);

    int  line_h() const { return line_h_; }
    bool ready()  const { return font_ != nullptr; }

private:
    SDL_Texture* get_cached(const char* text, SDL_Color color, int max_w,
                             int* out_w, int* out_h);
    SDL_Texture* render_to_tex(const char* text, SDL_Color color, int max_w,
                                int* out_w, int* out_h);
    int          lru_victim() const;

    SDL_Renderer* renderer_ = nullptr;
    TTF_Font*     font_     = nullptr;
    int           line_h_   = 0;

    static constexpr int CACHE_SIZE = 32;
    static constexpr int MAX_TEXT   = 255;

    struct Entry {
        char         text[MAX_TEXT + 1];
        SDL_Color    color;
        int          max_w;       // 0 = single-line, >0 = wrapped
        SDL_Texture* tex;
        int          w, h;
        int          lru;         // frame_ value when last accessed (LRU key)
        bool         valid;
    };

    Entry cache_[CACHE_SIZE] = {};
    int   frame_ = 0;
};

} // namespace ao
