#pragma once
#include "../screen.hpp"

namespace ao {

class AreaSelectScreen : public Screen {
public:
    explicit AreaSelectScreen(App& app);
    void on_enter() override;
    void handle_event(const SDL_Event& e) override;
    void render() override;

private:
    int selected_ = 0;
    int scroll_   = 0;
    static constexpr int VISIBLE = 10;
};

} // namespace ao
