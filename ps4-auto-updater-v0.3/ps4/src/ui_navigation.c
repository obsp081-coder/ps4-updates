/* All logic is deterministic, bounded and pure except state mutations.
 * Text matching is conservative ASCII case-insensitive on the byte sequence.
 * Arabic exact UTF-8 substrings work; Unicode casefold is NOT implemented. */
#include "ui_navigation.h"
#include <stddef.h>
#include <string.h>

static unsigned char lower_ascii(unsigned char value) {
    return (value >= 'A' && value <= 'Z') ? (unsigned char)(value + ('a'-'A')) : value;
}
static int contains_ascii_fold(const char *haystack, const char *needle) {
    if (!needle || !*needle) return 1;
    if (!haystack) return 0;
    for (const unsigned char *p = (const unsigned char *)haystack; *p; ++p) {
        const unsigned char *a = p;
        const unsigned char *b = (const unsigned char *)needle;
        while (*a && *b && lower_ascii(*a) == lower_ascii(*b)) { ++a; ++b; }
        if (!*b) return 1;
    }
    return 0;
}
static unsigned limit(unsigned n) {
    return n < UI_MAX_TITLES ? n : UI_MAX_TITLES;
}
void ui_nav_filter(UiNavigation *state, const UiRecord *items,
                   unsigned count, const char *query) {
    if (!state) return;
    if (!query) query = "";
    size_t n = 0;
    while (n + 1 < UI_SEARCH_CAPACITY && query[n]) ++n;
    memcpy(state->query, query, n);
    state->query[n] = 0;
    state->available = 0;
    if (items) {
        for (unsigned i = 0; i < limit(count); ++i) {
            const UiRecord *r = &items[i];
            if (contains_ascii_fold(r->cusa, state->query) ||
                contains_ascii_fold(r->name, state->query)) {
                state->matched[state->available++] = i;
            }
        }
    }
    /* Changing the results must not preserve an out-of-bounds selection. */
    state->selected = 0;
    state->scroll = 0;
    if (state->page == UI_PAGE_DETAILS && state->available == 0)
        state->page = UI_PAGE_HOME;
}
void ui_nav_init(UiNavigation *state, const UiRecord *items, unsigned count) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->page = UI_PAGE_HOME;
    ui_nav_filter(state, items, count, "");
}
unsigned ui_nav_resolve(const UiNavigation *state, unsigned idx) {
    if (!state || idx >= state->available) return UI_MAX_TITLES;
    return state->matched[idx];
}
int ui_nav_action(UiNavigation *state, UiAction action) {
    if (!state || state->exit_requested) return 0;
    if (action == UI_ACT_BACK) {
        if (state->page == UI_PAGE_HOME) {
            state->exit_requested = 1;
        } else {
            state->page = UI_PAGE_HOME;
        }
        return 1;
    }
    if (action == UI_ACT_DOWNLOADS || action == UI_ACT_NEXT_TAB) {
        if (state->page == UI_PAGE_DOWNLOADS) return 0;
        state->page = UI_PAGE_DOWNLOADS;
        return 1;
    }
    if (action == UI_ACT_PREVIOUS_TAB) {
        if (state->page == UI_PAGE_HOME) return 0;
        state->page = UI_PAGE_HOME;
        return 1;
    }
    if (action == UI_ACT_OPEN) {
        if (state->page != UI_PAGE_HOME || !state->available) return 0;
        state->page = UI_PAGE_DETAILS;
        return 1;
    }
    if (state->page != UI_PAGE_HOME) return 0;
    if (action == UI_ACT_DOWN && state->selected + 1 < state->available) {
        ++state->selected;
        if (state->selected >= state->scroll + UI_VISIBLE_ROWS)
            state->scroll = state->selected - UI_VISIBLE_ROWS + 1;
        return 1;
    }
    if (action == UI_ACT_UP && state->selected > 0) {
        --state->selected;
        if (state->selected < state->scroll) state->scroll = state->selected;
        return 1;
    }
    return 0;
}
