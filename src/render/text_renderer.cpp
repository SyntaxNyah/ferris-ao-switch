#include "text_renderer.hpp"
#include "../assets/asset_manager.hpp"
#include <SDL2/SDL_ttf.h>
#include <cstring>
#include <cstdio>

namespace ao {

TextRenderer::~TextRenderer() {
    for (int i = 0; i < CACHE_SIZE; ++i) {
        if (cache_[i].valid && cache_[i].tex)
            SDL_DestroyTexture(cache_[i].tex);
    }
    if (font_) TTF_CloseFont(font_);
}

bool TextRenderer::init(SDL_Renderer* r, const char* font_rel, int pt_size) {
    renderer_ = r;

    SDL_RWops* rw = AssetManager::open_rwops(font_rel);
    if (!rw) {
        std::fprintf(stderr, "TextRenderer: font not found: %s\n", font_rel);
        return false;
    }

    font_ = TTF_OpenFontRW(rw, 1, pt_size); // freesrc=1 — rw closed after load
    if (!font_) {
        std::fprintf(stderr, "TTF_OpenFontRW(%s, %d): %s\n",
                     font_rel, pt_size, TTF_GetError());
        return false;
    }

    line_h_ = TTF_FontLineSkip(font_);
    return true;
}

// ── Cache internals ────────────────────────────────────────────────────────────

int TextRenderer::lru_victim() const {
    int v = 0;
    for (int i = 1; i < CACHE_SIZE; ++i)
        if (cache_[i].lru < cache_[v].lru) v = i;
    return v;
}

SDL_Texture* TextRenderer::render_to_tex(const char* text, SDL_Color color,
                                          int max_w, int* out_w, int* out_h) {
    SDL_Surface* surf = (max_w > 0)
        ? TTF_RenderUTF8_Blended_Wrapped(font_, text, color, (Uint32)max_w)
        : TTF_RenderUTF8_Blended(font_, text, color);
    if (!surf) return nullptr;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    if (tex) { *out_w = surf->w; *out_h = surf->h; }
    SDL_FreeSurface(surf);
    return tex;
}

SDL_Texture* TextRenderer::get_cached(const char* text, SDL_Color color,
                                       int max_w, int* out_w, int* out_h) {
    ++frame_;

    // Cache lookup
    for (int i = 0; i < CACHE_SIZE; ++i) {
        Entry& e = cache_[i];
        if (!e.valid) continue;
        if (e.max_w   != max_w)   continue;
        if (e.color.r != color.r || e.color.g != color.g ||
            e.color.b != color.b || e.color.a != color.a) continue;
        if (std::strcmp(e.text, text) != 0) continue;
        e.lru  = frame_;
        *out_w = e.w;
        *out_h = e.h;
        return e.tex;
    }

    // Miss — pick a free slot or evict LRU
    int slot = -1;
    for (int i = 0; i < CACHE_SIZE; ++i)
        if (!cache_[i].valid) { slot = i; break; }
    if (slot < 0) slot = lru_victim();

    Entry& e = cache_[slot];
    if (e.valid && e.tex) {
        SDL_DestroyTexture(e.tex);
        e.tex   = nullptr;
        e.valid = false;
    }

    SDL_Texture* tex = render_to_tex(text, color, max_w, out_w, out_h);
    if (!tex) return nullptr;

    std::strncpy(e.text, text, MAX_TEXT);
    e.text[MAX_TEXT] = '\0';
    e.color = color;
    e.max_w = max_w;
    e.tex   = tex;
    e.w     = *out_w;
    e.h     = *out_h;
    e.lru   = frame_;
    e.valid = true;
    return tex;
}

// ── Public API ─────────────────────────────────────────────────────────────────

int TextRenderer::draw(const char* text, int x, int y, SDL_Color color) {
    if (!font_ || !text || text[0] == '\0') return 0;
    int w = 0, h = 0;
    SDL_Texture* tex = get_cached(text, color, 0, &w, &h);
    if (!tex) return 0;
    SDL_Rect dst = {x, y, w, h};
    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
    return w;
}

int TextRenderer::draw_wrapped(const char* text, int x, int y,
                                int max_w, SDL_Color color) {
    if (!font_ || !text || text[0] == '\0') return 0;
    int w = 0, h = 0;
    SDL_Texture* tex = get_cached(text, color, max_w, &w, &h);
    if (!tex) return 0;
    SDL_Rect dst = {x, y, w, h};
    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
    return h;
}

int TextRenderer::measure_w(const char* text) {
    if (!font_ || !text || text[0] == '\0') return 0;
    int w = 0, h = 0;
    TTF_SizeUTF8(font_, text, &w, &h);
    return w;
}

int TextRenderer::wrapped_height(const char* text, int max_w) {
    if (!font_ || !text || text[0] == '\0') return 0;
    ensure_wrap(text, max_w);
    const int ls = line_h_ > 0 ? line_h_ : 1;
    return wrap_count_ * ls;
}

// ── Progressive reveal (typewriter) ──────────────────────────────────────────────

void TextRenderer::ensure_wrap(const char* text, int max_w) {
    if (max_w == wrap_max_w_ &&
        std::strncmp(text, wrap_text_, sizeof(wrap_text_) - 1) == 0)
        return;  // already computed for this exact text + width

    std::strncpy(wrap_text_, text, sizeof(wrap_text_) - 1);
    wrap_text_[sizeof(wrap_text_) - 1] = '\0';
    wrap_max_w_ = max_w;
    wrap_count_ = 0;
    wrap_starts_[wrap_count_++] = 0;

    const int n = (int)std::strlen(text);
    char buf[600];
    int line_start = 0;
    int i = 0;
    while (i < n && wrap_count_ < MAX_WRAP_LINES) {
        // Next word spans [i, we).
        int we = i;
        while (we < n && text[we] != ' ' && text[we] != '\n') ++we;

        // Does the line so far plus this word still fit?
        int seg = we - line_start;
        if (seg > (int)sizeof(buf) - 1) seg = (int)sizeof(buf) - 1;
        std::memcpy(buf, text + line_start, seg);
        buf[seg] = '\0';
        bool fits = (max_w <= 0) || measure_w(buf) <= max_w;

        if (!fits && i > line_start) {
            // Break before this word — it starts the next line.
            line_start = i;
            wrap_starts_[wrap_count_++] = line_start;
            continue;  // re-measure the word on its fresh line
        }

        i = we;
        while (i < n && text[i] == ' ') ++i;      // consume trailing spaces
        if (we < n && text[we] == '\n') {          // explicit hard break
            i = we + 1;
            line_start = i;
            if (wrap_count_ < MAX_WRAP_LINES) wrap_starts_[wrap_count_++] = line_start;
        }
    }
}

int TextRenderer::draw_wrapped_upto(const char* text, int x, int y, int max_w,
                                    SDL_Color color, int reveal) {
    if (!font_ || !text || text[0] == '\0' || reveal <= 0) return 0;

    int w = 0, h = 0;
    SDL_Texture* tex = get_cached(text, color, max_w, &w, &h);  // full string, cached
    if (!tex) return 0;

    const int n = (int)std::strlen(text);
    if (reveal >= n) {                                          // fully revealed
        SDL_Rect dst = {x, y, w, h};
        SDL_RenderCopy(renderer_, tex, nullptr, &dst);
        return h;
    }

    ensure_wrap(text, max_w);
    const int ls = line_h_ > 0 ? line_h_ : 1;

    // Current line = last wrap start that is at or before the last revealed char.
    int L = 0;
    for (int k = 1; k < wrap_count_; ++k) {
        if (wrap_starts_[k] <= reveal - 1) L = k; else break;
    }

    // Fully revealed lines [0, L) blit as one block.
    if (L > 0) {
        int bh = L * ls; if (bh > h) bh = h;
        SDL_Rect src = {0, 0, w, bh};
        SDL_Rect dst = {x, y, w, bh};
        SDL_RenderCopy(renderer_, tex, &src, &dst);
    }

    // Partial current line L: reveal up to (reveal - line_start) chars.
    int lstart = wrap_starts_[L];
    int count  = reveal - lstart; if (count < 0) count = 0;
    char buf[600];
    int seg = count; if (seg > (int)sizeof(buf) - 1) seg = (int)sizeof(buf) - 1;
    std::memcpy(buf, text + lstart, seg);
    buf[seg] = '\0';
    int pw = measure_w(buf); if (pw > w) pw = w;
    int sy = L * ls;
    int sh = ls; if (sy + sh > h) sh = h - sy;
    if (pw > 0 && sh > 0) {
        SDL_Rect src = {0, sy, pw, sh};
        SDL_Rect dst = {x, y + sy, pw, sh};
        SDL_RenderCopy(renderer_, tex, &src, &dst);
    }
    return (L + 1) * ls;
}

} // namespace ao
