#pragma once
#include <SDL2/SDL.h>

namespace ao {

// Convert any decoded surface to ARGB8888 with correct transparency.
//
// Many AO sprites are GIFs — and plenty are GIFs shipped with a `.webp` (or
// `.png`) name (e.g. Skrapegropen's Polly and Tyrell Badd). A GIF's transparency
// is a palette colorkey, not per-pixel alpha, so a plain
// SDL_CreateTextureFromSurface renders the colorkey as a solid chroma block —
// the infamous "giant pink screen". Blitting the source (in BLENDMODE_NONE, so
// the copy is raw) onto a zero-filled (fully transparent) ARGB8888 surface
// honors BOTH cases at once: colorkeyed pixels are skipped and stay alpha 0,
// while real pixels (incl. per-pixel alpha from PNG/WebP) copy verbatim.
//
// Used by the off-thread decoder (asset_stream) AND the direct main-thread
// loader (apng_player) so transparency is identical no matter which path a
// sprite takes.
inline SDL_Surface* to_argb8888(SDL_Surface* src) {
    if (!src) return nullptr;
    SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32,
                                                      SDL_PIXELFORMAT_ARGB8888);
    if (!out) return nullptr;
    SDL_FillRect(out, nullptr, 0);                      // fully transparent
    SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);   // raw copy; colorkey honored
    if (SDL_BlitSurface(src, nullptr, out, nullptr) != 0) { SDL_FreeSurface(out); return nullptr; }
    return out;
}

} // namespace ao
