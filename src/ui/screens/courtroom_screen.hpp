#pragma once
#include "../screen.hpp"

namespace ao {

enum class CourtroomPanel { None, OOC, Music, Evidence, ICInput };

class CourtroomScreen : public Screen {
public:
    explicit CourtroomScreen(App& app);
    ~CourtroomScreen() override = default;

    void on_enter() override;
    void handle_event(const SDL_Event& e) override;
    void update(uint32_t dt_ms) override;
    void render() override;

private:
    void render_viewport();
    void render_chat_area();
    void render_side_panel();
    void render_active_panel();

    CourtroomPanel active_panel_ = CourtroomPanel::None;

    // Typewriter state
    int    typewriter_pos_  = 0;
    int    typewriter_max_  = 0;
    uint32_t typewriter_acc_ = 0;
    static constexpr uint32_t TYPEWRITER_MS = 35; // ms per character

    // Screenshake
    int    shake_frames_ = 0;
    int    shake_x_      = 0;
    int    shake_y_      = 0;

    // OOC scroll
    int ooc_scroll_ = 0;
    // Music scroll
    int music_scroll_ = 0;
    // Evidence scroll
    int evi_scroll_ = 0;
};

} // namespace ao
