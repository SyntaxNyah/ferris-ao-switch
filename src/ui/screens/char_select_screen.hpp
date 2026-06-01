#pragma once
#include "../screen.hpp"
#include "../../state/game_state.hpp"

namespace ao {

class CharSelectScreen : public Screen {
public:
    explicit CharSelectScreen(App& app);
    void on_enter() override;
    void handle_event(const SDL_Event& e) override;
    void update(uint32_t dt_ms) override;
    void render() override;

private:
    void pick_char(int real_idx);   // claim a character (CC) and enter the courtroom
    void open_search();             // keyboard → set the name filter
    void rebuild_filter();          // recompute filt_ from search_
    void prefetch_area_scene();     // warm the room background while you browse
    void prefetch_sel_sprite();     // warm the highlighted char's default sprite

    // The visible/navigable list is the filtered set when searching, else every
    // slot. selected_/scroll_ are positions in THAT list; real_index() maps a
    // position back to the gs.characters[] index.
    bool searching()   const { return search_[0] != '\0'; }
    int  vis_count()   const { return searching() ? filt_count_ : char_count_; }
    int  real_index(int pos) const { return searching() ? filt_[pos] : pos; }

    int  selected_  = 0;     // position in the visible list
    int  scroll_    = 0;     // row-aligned top position (multiple of COLS)
    int  pf_scroll_ = -1;    // scroll the icon window was last queued for
    int  ci_pf_sel_ = -1;    // selection the char.ini was last pre-warmed for
    int  sprite_pf_sel_ = -1; // selection the default sprite was last pre-warmed for
    int  char_count_ = 0;    // last-seen gs.char_count (rebuild trigger)

    char search_[48]  = {};
    int  filt_[GameState::MAX_CHARS];
    int  filt_count_  = 0;

    static constexpr int COLS = 8;
    static constexpr int ROWS = 4;
    static constexpr int PAGE = COLS * ROWS;
    // Grid + search-bar geometry — shared by render() and the touch hit-test.
    static constexpr int CELL_W = 140, CELL_H = 132, START_X = 40, START_Y = 104, CELL_GAP = 8;
    static constexpr SDL_Rect SEARCH_BAR = {40, 64, 760, 32};
};

} // namespace ao
