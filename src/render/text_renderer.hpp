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

    // Typewriter helper: draw the first `reveal` characters of `text`, wrapped to
    // max_w, using the layout of the FULL string. The full string is rendered to a
    // single cached texture exactly once and a growing prefix is blitted from it —
    // so a typing animation costs one texture, not one per character (the old
    // growing-substring path created/destroyed a texture every step and thrashed
    // the text cache, forcing all other UI text to re-render every frame).
    // Returns the height used so far, 0 on failure.
    int draw_wrapped_upto(const char* text, int x, int y, int max_w,
                          SDL_Color color, int reveal);

    // Measure single-line text width without rendering (no cache).
    int measure_w(const char* text);

    // Height (px) the text would occupy when word-wrapped to max_w, using the
    // same greedy wrap as draw_wrapped_upto. For laying out stacked wrapped
    // blocks (e.g. the IC log) without rendering them first.
    int wrapped_height(const char* text, int max_w);

    int  line_h() const { return line_h_; }
    bool ready()  const { return font_ != nullptr; }

private:
    SDL_Texture* get_cached(const char* text, SDL_Color color, int max_w,
                             int* out_w, int* out_h);
    SDL_Texture* render_to_tex(const char* text, SDL_Color color, int max_w,
                                int* out_w, int* out_h);
    int          lru_victim() const;

    // Recompute wrap_starts_ (char index where each wrapped line begins) for the
    // given text+width, unless it matches the last computation (cheap on repeat).
    void ensure_wrap(const char* text, int max_w);

    SDL_Renderer* renderer_ = nullptr;
    TTF_Font*     font_     = nullptr;
    int           line_h_   = 0;

    // Sized to hold every simultaneously-visible string without thrashing: the
    // always-on IC log (~14 lines × 2) plus the HUD, a typewriter line, and an
    // open panel's rows. Stable strings then stay cached frame-to-frame, so the
    // log and UI never re-rasterise — only genuinely new text allocates.
    static constexpr int CACHE_SIZE = 96;
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

    // Wrap cache for draw_wrapped_upto — recomputed only when the text or width
    // changes, so progressive reveal is O(1) per frame.
    static constexpr int MAX_WRAP_LINES = 32;
    char wrap_text_[513]      = {};
    int  wrap_max_w_          = -1;
    int  wrap_starts_[MAX_WRAP_LINES] = {};
    int  wrap_count_          = 0;
};

} // namespace ao
