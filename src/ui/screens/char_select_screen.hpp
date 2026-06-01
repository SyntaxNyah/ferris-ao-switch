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
    void pick_char(int idx);   // claim char idx (CC) and enter the courtroom

    int selected_ = 0;
    int scroll_   = 0;
    int pf_scroll_ = -1;   // scroll value the icon window was last queued for
    static constexpr int COLS = 8;
    static constexpr int ROWS = 4;
    static constexpr int PAGE = COLS * ROWS;
    // Grid geometry — shared by render() and the touch hit-test.
    static constexpr int CELL_W = 140, CELL_H = 140, START_X = 40, START_Y = 80, CELL_GAP = 8;
};

} // namespace ao
