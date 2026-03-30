#pragma once
#include <SDL2/SDL.h>

namespace ao {

enum class Action {
    Up, Down, Left, Right,
    Confirm,    // A
    Back,       // B
    Menu,       // X  — open IC input
    Secondary,  // Y  — evidence
    TabL,       // L  — prev panel
    TabR,       // R  — next panel
    TriggerL,   // ZL — OOC panel
    TriggerR,   // ZR — music panel / connect
    Plus,       // +  — disconnect / settings
    Minus,      // -  — toggle mode
    ScrollUp,   // right stick up
    ScrollDown, // right stick down
    _Count
};

struct InputState {
    bool pressed [(int)Action::_Count] = {};  // true for one frame on press
    bool held    [(int)Action::_Count] = {};  // true while held
    bool released[(int)Action::_Count] = {};  // true for one frame on release
};

class InputManager {
public:
    InputManager();
    ~InputManager();

    // Call at the start of each frame, before processing SDL events.
    void begin_frame();

    // Feed one SDL event (call for every SDL_PollEvent result).
    void handle_event(const SDL_Event& e);

    const InputState& state() const { return state_; }

    bool pressed (Action a) const { return state_.pressed [(int)a]; }
    bool held    (Action a) const { return state_.held    [(int)a]; }
    bool released(Action a) const { return state_.released[(int)a]; }

private:
    Action button_to_action(SDL_GameControllerButton b) const;
    Action axis_to_action  (SDL_GameControllerAxis a, int16_t val) const;
    Action key_to_action   (SDL_Keycode k) const;

    SDL_GameController* controller_ = nullptr;
    InputState state_;
    bool       held_prev_[(int)Action::_Count] = {};
};

} // namespace ao
