#pragma once
#include "../screen.hpp"

namespace ao {

class CharSelectScreen : public Screen {
public:
    explicit CharSelectScreen(App& app);
    void on_enter() override;
    void handle_event(const SDL_Event& e) override;
    void update(uint32_t dt_ms) override;
    void render() override;

private:
    int selected_ = 0;
    int scroll_   = 0;
    static constexpr int COLS = 8;
    static constexpr int ROWS = 4;
    static constexpr int PAGE = COLS * ROWS;
};

} // namespace ao
