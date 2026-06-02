#pragma once
#include <SDL2/SDL.h>

namespace ao {

class Renderer;
class TextRenderer;

// In-app on-screen keyboard overlay.
//
// Unlike the libnx system keyboard (`show_keyboard` / swkbdShow), this does NOT
// block the main loop. The owning screen keeps updating, rendering and draining
// the network queue while the user types — so incoming IC/OOC chat keeps flowing
// instead of freezing and "catching up" when you finally send (the swkbd
// behaviour, especially bad on Ryujinx whose keyboard applet freezes the app).
//
// Input works three ways so it's reliable everywhere:
//   - on-screen key taps  (Switch touchscreen, and Ryujinx which maps the mouse
//                           to touch — this is the path that always works),
//   - mouse clicks        (desktop),
//   - a physical keyboard (SDL_TEXTINPUT / SDL_KEYDOWN, desktop / dev).
//
// Usage by the owner:
//   kb_.open("IC message", "", 255);
//   // each event:        auto rs = kb_.handle_event(e, renderer);
//   //   rs == SUBMIT  -> use kb_.text()
//   //   rs == CANCEL  -> discard
//   // each frame, last: kb_.render(renderer, text_renderer);
class SoftKeyboard {
public:
    enum Result { NONE, SUBMIT, CANCEL };

    void open(const char* hint, const char* initial, int max_len);
    void close();
    bool active() const { return active_; }
    const char* text() const { return buf_; }

    Result handle_event(const SDL_Event& e, SDL_Renderer* r);
    void   render(Renderer& r, TextRenderer& txt);

private:
    // type: 0 char, 1 backspace, 2 enter/send, 3 cancel, 4 shift, 5 space
    struct Key { SDL_Rect rect; int type; char ch; };
    int    build_keys(Key* out) const;   // fills the fixed layout, returns count
    Result activate(const Key& k);       // apply a key press (tap or A-button)
    int    nav_vert(int dir) const;      // D-pad up/down: nearest key in the next row
    void   insert(char c);
    void   backspace();

    bool active_ = false;
    bool shift_  = false;
    int  sel_    = 10;     // highlighted key for D-pad/controller navigation (docked)
    char buf_[512] = {};
    int  len_  = 0;
    int  max_  = 255;
    char hint_[64] = {};
};

} // namespace ao
