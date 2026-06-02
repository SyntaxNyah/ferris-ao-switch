#pragma once
#include "../screen.hpp"
#include "../touch.hpp"
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
    void scroll_by(int rows);       // move selection by whole rows (wheel / touch drag)
    void apply_zoom();              // recompute cols_/rows_/cell_w_/cell_h_ from zoom_
    void set_zoom(int delta);       // change zoom level (clamped), keeps selection on-screen

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

    TouchDrag drag_;         // tap vs finger drag-scroll classifier for the grid

    // Grid geometry is DYNAMIC so the user can zoom — packing more, smaller icons
    // on screen is what makes a 1000s-character roster navigable. cols_/rows_ and
    // the cell size are recomputed from zoom_ by apply_zoom(); the rest are fixed.
    int  zoom_   = 0;        // 0 = biggest cells / fewest per page … ZOOM_COUNT-1 = densest
    int  cols_   = 8, rows_ = 4;    // visible grid dimensions for the current zoom
    int  cell_w_ = 140, cell_h_ = 132;
    bool tl_held_ = false, tr_held_ = false;  // trigger-axis edge detect (page scroll)
    bool sb_drag_ = false;                     // dragging the right-edge scrollbar

    void scrollbar_jump(int py);               // jump the view from a scrollbar y
    SDL_Rect scrollbar_track() const;          // right-edge scrollbar rect

    static constexpr int ZOOM_COUNT = 5;
    // Fixed margins; cell_w_/cell_h_ are derived to fill (START_X..W-START_X) ×
    // (START_Y..GRID_BOTTOM). Shared by render() and the touch hit-test.
    static constexpr int START_X = 40, START_Y = 104, CELL_GAP = 8, GRID_BOTTOM = 684;
    static constexpr SDL_Rect SEARCH_BAR    = {40, 64, 760, 32};
    // On-screen zoom buttons (tap/click — work on Ryujinx where Ctrl+wheel doesn't).
    static constexpr SDL_Rect ZOOM_OUT_BTN  = {1280 - 104, 14, 38, 32};   // "−" (more, smaller)
    static constexpr SDL_Rect ZOOM_IN_BTN   = {1280 - 60,  14, 38, 32};   // "+" (fewer, bigger)
};

} // namespace ao
