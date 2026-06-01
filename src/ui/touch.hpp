#pragma once
#include <SDL2/SDL.h>
#include "../render/renderer.hpp"

namespace ao {

// Tap helpers shared by every screen.
//
// `tap_point` converts a tap event to logical (1280x720) coordinates, handling
// both the Switch touchscreen (SDL_FINGERDOWN, normalized 0..1) and a real mouse
// click (SDL_MOUSEBUTTONDOWN) for Ryujinx/desktop. SDL also synthesises mouse
// events from touch; those carry which == SDL_TOUCH_MOUSEID and are ignored so a
// single tap is never handled twice. Returns true and fills x,y on a tap.
inline bool tap_point(const SDL_Event& e, SDL_Renderer* r, int& x, int& y) {
    if (e.type == SDL_FINGERDOWN) {
        x = (int)(e.tfinger.x * (float)Renderer::WIDTH);
        y = (int)(e.tfinger.y * (float)Renderer::HEIGHT);
        return true;
    }
    if (e.type == SDL_MOUSEBUTTONDOWN &&
        e.button.button == SDL_BUTTON_LEFT &&
        e.button.which  != SDL_TOUCH_MOUSEID) {
        float lx = 0.0f, ly = 0.0f;
        SDL_RenderWindowToLogical(r, e.button.x, e.button.y, &lx, &ly);
        x = (int)lx;
        y = (int)ly;
        return true;
    }
    return false;
}

// Point-in-rect test.
inline bool pt_in(int x, int y, const SDL_Rect& rc) {
    return x >= rc.x && x < rc.x + rc.w && y >= rc.y && y < rc.y + rc.h;
}

} // namespace ao
