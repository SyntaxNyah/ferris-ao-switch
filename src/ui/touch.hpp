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

// Tap + drag-scroll classifier (one per scrollable screen). The Switch handheld
// has no mouse wheel, so long lists need finger-drag scrolling. feed() turns a
// raw event into:
//   TAP    — a discrete press (mouse left-click, or a finger press+release that
//            didn't move): the screen does its tap hit-test at (tx,ty).
//   SCROLL — a finger drag past the threshold: `rows` is how many list rows to
//            move, in the SAME sign convention as a mouse wheel's `y` (positive
//            = toward earlier entries, the way content follows the finger), so a
//            screen routes a wheel and a drag through the SAME scroll_by() call.
//   NONE   — nothing actionable.
// row_px is the focused list's row height (drag pixels → rows). Mouse wheel is
// handled separately by each screen (SDL_MOUSEWHEEL).
struct TouchDrag {
    bool down = false;
    int  start_y = 0, last_y = 0, accum = 0;
    bool moved = false;
    static constexpr int THRESH = 10;   // px of finger travel before it's a drag

    enum Kind { NONE, TAP, SCROLL };

    Kind feed(const SDL_Event& e, SDL_Renderer* r, int row_px,
              int& tx, int& ty, int& rows) {
        rows = 0;
        switch (e.type) {
        case SDL_FINGERDOWN:
            down = true; moved = false; accum = 0;
            start_y = last_y = (int)(e.tfinger.y * (float)Renderer::HEIGHT);
            return NONE;
        case SDL_FINGERMOTION: {
            if (!down) return NONE;
            int y  = (int)(e.tfinger.y * (float)Renderer::HEIGHT);
            int dy = y - last_y;
            last_y = y;
            if (!moved && (y - start_y > THRESH || start_y - y > THRESH)) moved = true;
            if (moved && row_px > 0) {
                accum += dy;
                // Content follows the finger: dragging DOWN (dy>0) pulls earlier
                // entries into view — same as a mouse wheel scrolling up, so
                // rows>0 matches SDL wheel.y>0 and both route through scroll_by().
                while (accum >=  row_px) { rows += 1; accum -= row_px; }
                while (accum <= -row_px) { rows -= 1; accum += row_px; }
            }
            return moved ? SCROLL : NONE;
        }
        case SDL_FINGERUP:
            if (!down) return NONE;
            down = false;
            if (!moved) {
                tx = (int)(e.tfinger.x * (float)Renderer::WIDTH);
                ty = (int)(e.tfinger.y * (float)Renderer::HEIGHT);
                return TAP;
            }
            return NONE;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT &&
                e.button.which  != SDL_TOUCH_MOUSEID) {
                float lx = 0.0f, ly = 0.0f;
                SDL_RenderWindowToLogical(r, e.button.x, e.button.y, &lx, &ly);
                tx = (int)lx; ty = (int)ly;
                return TAP;
            }
            return NONE;
        default:
            return NONE;
        }
    }
};

} // namespace ao
