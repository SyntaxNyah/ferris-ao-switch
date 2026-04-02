#include "input_manager.hpp"
#include <cstring>

namespace ao {

InputManager::InputManager() {}

void InputManager::open_controllers() {
    if (controller_) return;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            controller_ = SDL_GameControllerOpen(i);
            if (controller_) break;
        }
    }
}

InputManager::~InputManager() {
    if (controller_) SDL_GameControllerClose(controller_);
}

void InputManager::begin_frame() {
    // pressed/released are only valid for one frame
    std::memset(state_.pressed,  0, sizeof(state_.pressed));
    std::memset(state_.released, 0, sizeof(state_.released));
}

void InputManager::handle_event(const SDL_Event& e) {
    switch (e.type) {
        case SDL_CONTROLLERDEVICEADDED:
            if (!controller_)
                controller_ = SDL_GameControllerOpen(e.cdevice.which);
            break;

        case SDL_CONTROLLERDEVICEREMOVED:
            if (controller_ &&
                SDL_GameControllerGetJoystick(controller_) ==
                SDL_JoystickFromInstanceID(e.cdevice.which)) {
                SDL_GameControllerClose(controller_);
                controller_ = nullptr;
            }
            break;

        case SDL_CONTROLLERBUTTONDOWN: {
            Action a = button_to_action((SDL_GameControllerButton)e.cbutton.button);
            if (a != Action::_Count) {
                state_.pressed[(int)a] = true;
                state_.held   [(int)a] = true;
            }
            break;
        }
        case SDL_CONTROLLERBUTTONUP: {
            Action a = button_to_action((SDL_GameControllerButton)e.cbutton.button);
            if (a != Action::_Count) {
                state_.released[(int)a] = true;
                state_.held    [(int)a] = false;
            }
            break;
        }
        case SDL_CONTROLLERAXISMOTION: {
            // Right stick Y for scrolling
            if (e.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY) {
                const int16_t DEAD = 10000;
                if (e.caxis.value < -DEAD) {
                    state_.pressed[(int)Action::ScrollUp] = true;
                    state_.held   [(int)Action::ScrollUp] = true;
                    state_.held   [(int)Action::ScrollDown] = false;
                } else if (e.caxis.value > DEAD) {
                    state_.pressed[(int)Action::ScrollDown] = true;
                    state_.held   [(int)Action::ScrollDown] = true;
                    state_.held   [(int)Action::ScrollUp] = false;
                } else {
                    if (state_.held[(int)Action::ScrollUp])
                        state_.released[(int)Action::ScrollUp] = true;
                    if (state_.held[(int)Action::ScrollDown])
                        state_.released[(int)Action::ScrollDown] = true;
                    state_.held[(int)Action::ScrollUp]   = false;
                    state_.held[(int)Action::ScrollDown] = false;
                }
            }
            break;
        }
        // Keyboard fallback (useful on emulator / desktop dev)
        case SDL_KEYDOWN: {
            Action a = key_to_action(e.key.keysym.sym);
            if (a != Action::_Count && !e.key.repeat) {
                state_.pressed[(int)a] = true;
                state_.held   [(int)a] = true;
            }
            break;
        }
        case SDL_KEYUP: {
            Action a = key_to_action(e.key.keysym.sym);
            if (a != Action::_Count) {
                state_.released[(int)a] = true;
                state_.held    [(int)a] = false;
            }
            break;
        }
        default: break;
    }
}

Action InputManager::button_to_action(SDL_GameControllerButton b) const {
    switch (b) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:          return Action::Up;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:        return Action::Down;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:        return Action::Left;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:       return Action::Right;
        case SDL_CONTROLLER_BUTTON_A:                return Action::Confirm;
        case SDL_CONTROLLER_BUTTON_B:                return Action::Back;
        case SDL_CONTROLLER_BUTTON_X:                return Action::Menu;
        case SDL_CONTROLLER_BUTTON_Y:                return Action::Secondary;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:     return Action::TabL;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:    return Action::TabR;
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:        return Action::TriggerL;
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:       return Action::TriggerR;
        case SDL_CONTROLLER_BUTTON_START:            return Action::Plus;
        case SDL_CONTROLLER_BUTTON_BACK:             return Action::Minus;
        default: return Action::_Count;
    }
}

Action InputManager::key_to_action(SDL_Keycode k) const {
    switch (k) {
        case SDLK_UP:     return Action::Up;
        case SDLK_DOWN:   return Action::Down;
        case SDLK_LEFT:   return Action::Left;
        case SDLK_RIGHT:  return Action::Right;
        case SDLK_RETURN: return Action::Confirm;
        case SDLK_ESCAPE: return Action::Back;
        case SDLK_x:      return Action::Menu;
        case SDLK_y:      return Action::Secondary;
        case SDLK_q:      return Action::TabL;
        case SDLK_e:      return Action::TabR;
        case SDLK_z:      return Action::TriggerL;
        case SDLK_c:      return Action::TriggerR;
        case SDLK_PLUS:   return Action::Plus;
        case SDLK_MINUS:  return Action::Minus;
        case SDLK_PAGEUP: return Action::ScrollUp;
        case SDLK_PAGEDOWN:return Action::ScrollDown;
        default: return Action::_Count;
    }
}

} // namespace ao
