#include "soft_keyboard.hpp"
#include "../render/renderer.hpp"
#include "../render/text_renderer.hpp"
#include <cstring>
#include <cstdio>
#include <cctype>

namespace ao {

enum { K_CHAR = 0, K_BACK = 1, K_ENTER = 2, K_CANCEL = 3, K_SHIFT = 4, K_SPACE = 5 };

// Overlay occupies the bottom of the screen; the top stays visible so the live
// courtroom (sprites + the top of the IC log) shows while you type.
static constexpr int OV_X = 30, OV_W = 1220;
static constexpr int OV_Y = 400, OV_H = 308;
static constexpr int GAP = 6, KEY_H = 44;
static constexpr int FIELD_Y = OV_Y + 22, FIELD_H = 34;
static constexpr int ROW_Y0  = OV_Y + 64;

void SoftKeyboard::open(const char* hint, const char* initial, int max_len) {
    active_ = true;
    shift_  = false;
    sel_    = 10;     // start the cursor on a letter ('q'), for D-pad use
    max_ = (max_len > 0 && max_len < (int)sizeof(buf_)) ? max_len : (int)sizeof(buf_) - 1;
    std::strncpy(hint_, hint ? hint : "", sizeof(hint_) - 1); hint_[sizeof(hint_) - 1] = '\0';
    std::strncpy(buf_, initial ? initial : "", sizeof(buf_) - 1); buf_[sizeof(buf_) - 1] = '\0';
    len_ = (int)std::strlen(buf_);
    if (len_ > max_) { len_ = max_; buf_[len_] = '\0'; }
    // NOTE: deliberately do NOT call SDL_StartTextInput() — the devkitPro Switch
    // SDL2 port routes text input through the system swkbd, which would pop the
    // blocking keyboard right on top of this one (eating clicks / freezing). We
    // read the physical keyboard straight from SDL_KEYDOWN instead (see below).
}

void SoftKeyboard::close() {
    active_ = false;
}

// US-layout shift mapping for the physical keyboard.
static char shift_char(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - 32);
    switch (c) {
        case '1': return '!'; case '2': return '@'; case '3': return '#';
        case '4': return '$'; case '5': return '%'; case '6': return '^';
        case '7': return '&'; case '8': return '*'; case '9': return '(';
        case '0': return ')'; case '-': return '_'; case '=': return '+';
        case ',': return '<'; case '.': return '>'; case '/': return '?';
        case ';': return ':'; case '\'':return '"'; case '[': return '{';
        case ']': return '}'; case '\\':return '|'; case '`': return '~';
        default:  return c;
    }
}

void SoftKeyboard::insert(char c) {
    if (len_ >= max_) return;
    buf_[len_++] = c;
    buf_[len_] = '\0';
}

void SoftKeyboard::backspace() {
    if (len_ > 0) buf_[--len_] = '\0';
}

// Build the fixed QWERTY layout. Rows: numbers, qwerty x3 (last with shift +
// backspace), and a function row (cancel / punctuation / wide space / send).
int SoftKeyboard::build_keys(Key* out) const {
    int n = 0;
    const int kw = (OV_W - 9 * GAP) / 10;          // 10 columns
    auto put = [&](int x, int y, int w, int type, char ch) {
        out[n++] = { {x, y, w, KEY_H}, type, ch };
    };
    auto row = [&](const char* s, int y, int x0) {
        int x = x0;
        for (const char* p = s; *p; ++p) { put(x, y, kw, K_CHAR, *p); x += kw + GAP; }
    };

    int y = ROW_Y0;
    row("1234567890", y, OV_X);                 y += KEY_H + GAP;
    row("qwertyuiop", y, OV_X);                 y += KEY_H + GAP;
    row("asdfghjkl",  y, OV_X + kw / 2);        y += KEY_H + GAP;

    // Shift | z x c v b n m | Backspace
    {
        int x = OV_X;
        int wide = kw + kw / 2;
        put(x, y, wide, K_SHIFT, 0); x += wide + GAP;
        for (const char* p = "zxcvbnm"; *p; ++p) { put(x, y, kw, K_CHAR, *p); x += kw + GAP; }
        put(x, y, OV_X + OV_W - x, K_BACK, 0);
        y += KEY_H + GAP;
    }
    // Cancel | ' | , | Space | . | ! | ? | Send
    {
        int x = OV_X;
        put(x, y, kw + kw / 2, K_CANCEL, 0); x += kw + kw / 2 + GAP;
        put(x, y, kw, K_CHAR, '\''); x += kw + GAP;
        put(x, y, kw, K_CHAR, ',');  x += kw + GAP;
        int space_w = kw * 3;
        put(x, y, space_w, K_SPACE, ' '); x += space_w + GAP;
        put(x, y, kw, K_CHAR, '.'); x += kw + GAP;
        put(x, y, kw, K_CHAR, '!'); x += kw + GAP;
        put(x, y, kw, K_CHAR, '?'); x += kw + GAP;
        put(x, y, OV_X + OV_W - x, K_ENTER, 0);
    }
    return n;
}

SoftKeyboard::Result SoftKeyboard::activate(const Key& k) {
    switch (k.type) {
        case K_CHAR:
            insert(shift_ && std::isalpha((unsigned char)k.ch)
                       ? (char)std::toupper((unsigned char)k.ch) : k.ch);
            break;
        case K_SPACE:  insert(' '); break;
        case K_BACK:   backspace(); break;
        case K_SHIFT:  shift_ = !shift_; break;
        case K_ENTER:  close(); return SUBMIT;
        case K_CANCEL: close(); return CANCEL;
    }
    return NONE;
}

// D-pad up/down: pick the key in the nearest row above/below whose centre is
// closest in x (keys don't align in columns, so this is a best-fit walk).
int SoftKeyboard::nav_vert(int dir) const {
    Key keys[64];
    int kn = build_keys(keys);
    if (sel_ < 0 || sel_ >= kn) return 0;
    int cy = keys[sel_].rect.y;
    int cx = keys[sel_].rect.x + keys[sel_].rect.w / 2;
    int target_y = -1;
    for (int i = 0; i < kn; ++i) {
        int ky = keys[i].rect.y;
        if (dir < 0 && ky < cy) { if (target_y < 0 || ky > target_y) target_y = ky; }
        if (dir > 0 && ky > cy) { if (target_y < 0 || ky < target_y) target_y = ky; }
    }
    if (target_y < 0) return sel_;
    int best = sel_, bestdx = 1 << 30;
    for (int i = 0; i < kn; ++i) {
        if (keys[i].rect.y != target_y) continue;
        int kx = keys[i].rect.x + keys[i].rect.w / 2;
        int dx = kx > cx ? kx - cx : cx - kx;
        if (dx < bestdx) { bestdx = dx; best = i; }
    }
    return best;
}

SoftKeyboard::Result SoftKeyboard::handle_event(const SDL_Event& e, SDL_Renderer* r) {
    if (!active_) return NONE;

    Key keys[64];
    int kn = build_keys(keys);
    if (sel_ >= kn) sel_ = kn - 1;
    if (sel_ < 0)   sel_ = 0;

    // Physical keyboard (desktop / a real USB keyboard): type directly from key
    // events (we don't use SDL_TEXTINPUT — see open()), plus arrow-key cursor.
    if (e.type == SDL_KEYDOWN) {
        SDL_Keycode sym = e.key.keysym.sym;
        switch (sym) {
            case SDLK_RETURN: case SDLK_KP_ENTER: close(); return SUBMIT;
            case SDLK_ESCAPE:    close(); return CANCEL;
            case SDLK_BACKSPACE: backspace(); return NONE;
            case SDLK_LEFT:  if (sel_ > 0)      --sel_; return NONE;
            case SDLK_RIGHT: if (sel_ < kn - 1) ++sel_; return NONE;
            case SDLK_UP:    sel_ = nav_vert(-1); return NONE;
            case SDLK_DOWN:  sel_ = nav_vert(+1); return NONE;
            default:
                if (sym >= 32 && sym < 127) {     // printable ASCII keysym
                    bool shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
                    insert(shift ? shift_char((char)sym) : (char)sym);
                    return NONE;
                }
                break;
        }
    }

    // Controller — the only way to type DOCKED (no touchscreen). Cursor + A/B.
    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  if (sel_ > 0)      --sel_; return NONE;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: if (sel_ < kn - 1) ++sel_; return NONE;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:    sel_ = nav_vert(-1); return NONE;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  sel_ = nav_vert(+1); return NONE;
            case SDL_CONTROLLER_BUTTON_A:          return activate(keys[sel_]);
            case SDL_CONTROLLER_BUTTON_B:          close(); return CANCEL;
            default: break;
        }
    }

    // Key taps/clicks (Switch touch, Ryujinx mouse-as-touch, desktop mouse).
    int px = 0, py = 0; bool down = false;
    if (e.type == SDL_FINGERDOWN) {
        px = (int)(e.tfinger.x * Renderer::WIDTH); py = (int)(e.tfinger.y * Renderer::HEIGHT); down = true;
    } else if (e.type == SDL_MOUSEBUTTONDOWN &&
               e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID) {
        float lx = 0, ly = 0; SDL_RenderWindowToLogical(r, e.button.x, e.button.y, &lx, &ly);
        px = (int)lx; py = (int)ly; down = true;
    }
    if (down) {
        for (int i = 0; i < kn; ++i) {
            const SDL_Rect& k = keys[i].rect;
            if (px < k.x || px >= k.x + k.w || py < k.y || py >= k.y + k.h) continue;
            sel_ = i;
            return activate(keys[i]);
        }
    }
    return NONE;
}

void SoftKeyboard::render(Renderer& r, TextRenderer& txt) {
    if (!active_) return;

    // Opaque panel (no full-screen dim — keep the live courtroom visible above).
    r.fill_rect({OV_X - 6, OV_Y - 6, OV_W + 12, OV_H + 12}, {16, 18, 28, 245});
    r.draw_rect({OV_X - 6, OV_Y - 6, OV_W + 12, OV_H + 12}, {80, 100, 150, 255});

    // Hint + the text field with a blinking-ish cursor.
    txt.draw(hint_, OV_X, OV_Y - 2, {150, 160, 190, 255});
    SDL_Rect field = {OV_X, FIELD_Y, OV_W, FIELD_H};
    r.fill_rect(field, {8, 10, 18, 255});
    r.draw_rect(field, {70, 90, 140, 255});
    char shown[600];
    std::snprintf(shown, sizeof(shown), "%s|", buf_);
    txt.draw(shown, field.x + 8, field.y + (FIELD_H - txt.line_h()) / 2, {235, 240, 255, 255});

    Key keys[64];
    int kn = build_keys(keys);
    for (int i = 0; i < kn; ++i) {
        const SDL_Rect& k = keys[i].rect;
        SDL_Color bg = {40, 46, 68, 255};
        if      (keys[i].type == K_ENTER)  bg = {40, 95, 60, 255};
        else if (keys[i].type == K_CANCEL) bg = {95, 45, 52, 255};
        else if (keys[i].type == K_SHIFT)  bg = shift_ ? SDL_Color{70, 95, 150, 255}
                                                       : SDL_Color{46, 54, 80, 255};
        r.fill_rect(k, bg);
        if (i == sel_) {   // controller/keyboard cursor highlight
            r.draw_rect(k, {255, 235, 140, 255});
            r.draw_rect({k.x + 1, k.y + 1, k.w - 2, k.h - 2}, {255, 235, 140, 255});
        } else {
            r.draw_rect(k, {82, 92, 124, 255});
        }

        char lbl[8];
        switch (keys[i].type) {
            case K_CHAR: {
                char c = shift_ && std::isalpha((unsigned char)keys[i].ch)
                             ? (char)std::toupper((unsigned char)keys[i].ch) : keys[i].ch;
                std::snprintf(lbl, sizeof(lbl), "%c", c);
            } break;
            case K_SPACE:  std::strcpy(lbl, "space"); break;
            case K_BACK:   std::strcpy(lbl, "del");  break;
            case K_ENTER:  std::strcpy(lbl, "send"); break;
            case K_CANCEL: std::strcpy(lbl, "esc");  break;
            case K_SHIFT:  std::strcpy(lbl, "shft"); break;
            default:       lbl[0] = '\0'; break;
        }
        int tw = txt.measure_w(lbl);
        txt.draw(lbl, k.x + (k.w - tw) / 2, k.y + (k.h - txt.line_h()) / 2, {222, 230, 248, 255});
    }
}

} // namespace ao
