/* Lightweight PS4 UI navigation: no renderer, network, install APIs or heap. */
#ifndef PS4_UI_NAVIGATION_H
#define PS4_UI_NAVIGATION_H
#ifdef __cplusplus
extern "C" {
#endif
#define UI_MAX_TITLES 256u
#define UI_VISIBLE_ROWS 6u
#define UI_SEARCH_CAPACITY 72u
typedef enum UiPage {
    UI_PAGE_HOME = 0,
    UI_PAGE_DETAILS = 1,
    UI_PAGE_DOWNLOADS = 2,
    UI_PAGE_SEARCH = 3,
    UI_PAGE_SETTINGS = 4,
    /* Eight real Sidebar destinations. DETAILS and SEARCH are auxiliary
     * screens reached through Cross / Triangle, not circular tabs. */
    UI_PAGE_GAMES = 5,
    UI_PAGE_AVAILABLE = 6,
    UI_PAGE_QUEUE = 7,
    UI_PAGE_INSTALLED = 8,
    UI_PAGE_ABOUT = 9
} UiPage;
typedef enum UiAction {
    UI_ACT_UP = 0,
    UI_ACT_DOWN,
    UI_ACT_OPEN,
    UI_ACT_BACK,
    UI_ACT_NEXT_TAB,
    UI_ACT_PREVIOUS_TAB,
    UI_ACT_DOWNLOADS,
    UI_ACT_LEFT,
    UI_ACT_RIGHT,
    UI_ACT_SEARCH,
    UI_ACT_REMOVE,
    UI_ACT_TOGGLE_KEYBOARD,
    UI_ACT_CLEAR_SEARCH,
    UI_ACT_SETTINGS
} UiAction;
typedef struct UiRecord {
    const char *cusa;
    const char *name;
} UiRecord;
typedef struct UiNavigation {
    UiPage page;
    unsigned selected;       /* index in filtered set */
    unsigned scroll;         /* first visible filtered index */
    unsigned available;
    unsigned matched[UI_MAX_TITLES];
    char query[UI_SEARCH_CAPACITY];
    int exit_requested;
    unsigned keyboard_index;
    unsigned keyboard_arabic;
    unsigned search_result_focus; /* down from last keyboard row */
    unsigned details_from_search; /* Circle returns to search */
    UiPage details_origin; /* Home or Games; never silently swap CUSA */
    const UiRecord *items; /* caller-owned; valid for visible UI lifetime */
    unsigned item_count;
} UiNavigation;
/* Rebuilding the filter never invents content and never changes files. */
void ui_nav_init(UiNavigation *state, const UiRecord *items, unsigned count);
void ui_nav_filter(UiNavigation *state, const UiRecord *items,
                   unsigned count, const char *query);
/* Replace scanned inventory without losing selected CUSA or search state.
 * selected_cusa is copied by the caller BEFORE its old record array changes.
 * Missing selected IDs close Details instead of showing another game. */
void ui_nav_rebind(UiNavigation *state, const UiRecord *items,
                   unsigned count, const char *selected_cusa);
unsigned ui_nav_resolve(const UiNavigation *state, unsigned filtered_index);
/* Returns 1 if UI needs repaint; 0 for an ignored action. */
int ui_nav_action(UiNavigation *state, UiAction action);
/* 10-column lightweight native onscreen keyboard (Latin/Arabic). */
unsigned ui_nav_keyboard_count(const UiNavigation *state);
const char *ui_nav_keyboard_key(const UiNavigation *state, unsigned index);
#ifdef __cplusplus
}
#endif
#endif
