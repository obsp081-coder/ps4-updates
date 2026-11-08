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
    UI_PAGE_DOWNLOADS = 2
} UiPage;
typedef enum UiAction {
    UI_ACT_UP = 0,
    UI_ACT_DOWN,
    UI_ACT_OPEN,
    UI_ACT_BACK,
    UI_ACT_NEXT_TAB,
    UI_ACT_PREVIOUS_TAB,
    UI_ACT_DOWNLOADS
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
} UiNavigation;
/* Rebuilding the filter never invents content and never changes files. */
void ui_nav_init(UiNavigation *state, const UiRecord *items, unsigned count);
void ui_nav_filter(UiNavigation *state, const UiRecord *items,
                   unsigned count, const char *query);
unsigned ui_nav_resolve(const UiNavigation *state, unsigned filtered_index);
/* Returns 1 if UI needs repaint; 0 for an ignored action. */
int ui_nav_action(UiNavigation *state, UiAction action);
#ifdef __cplusplus
}
#endif
#endif
