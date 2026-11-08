/* All logic is deterministic, bounded and pure except state mutations.
 * Text matching is conservative ASCII case-insensitive on the byte sequence.
 * Arabic exact UTF-8 substrings work; Unicode casefold is NOT implemented. */
#include "ui_navigation.h"
#include <stddef.h>
#include <string.h>
static const char *const latin_keys[] = {
    "A","B","C","D","E","F","G","H","I","J",
    "K","L","M","N","O","P","Q","R","S","T",
    "U","V","W","X","Y","Z","0","1","2","3",
    "4","5","6","7","8","9"," "
};
static const char *const arabic_keys[] = {
    "ا","ب","ت","ث","ج","ح","خ","د","ذ","ر",
    "ز","س","ش","ص","ض","ط","ظ","ع","غ","ف",
    "ق","ك","ل","م","ن","ه","و","ي","ة","ى",
    "ء","ئ","أ","إ","ؤ","آ"," "
};
#define UI_KEY_COUNT 37u
unsigned ui_nav_keyboard_count(const UiNavigation *state) {
    return state ? UI_KEY_COUNT : 0;
}
const char *ui_nav_keyboard_key(const UiNavigation *state,unsigned index) {
    if(!state||index>=UI_KEY_COUNT)return NULL;
    return state->keyboard_arabic?arabic_keys[index]:latin_keys[index];
}

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
    state->items=items;
    state->item_count=limit(count);
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
    if(!state->available)state->search_result_focus=0;
    if (state->page == UI_PAGE_DETAILS && state->available == 0)
        state->page = UI_PAGE_HOME;
}
/* This function is side-effect-free outside UiNavigation. The caller
 * captures selected_cusa before overwriting the previous UiRecord array.
 * A missing selection cannot silently switch a Details page to a new title. */
void ui_nav_rebind(UiNavigation *state, const UiRecord *items,
                   unsigned count, const char *selected_cusa) {
    if(!state)return;
    char saved_query[UI_SEARCH_CAPACITY];
    memcpy(saved_query,state->query,sizeof saved_query);
    saved_query[UI_SEARCH_CAPACITY-1]=0;
    const UiPage old_page=state->page;
    const unsigned keyboard_index=state->keyboard_index;
    const unsigned keyboard_arabic=state->keyboard_arabic;
    const unsigned search_focus=state->search_result_focus;
    const unsigned search_origin=state->details_from_search;
    ui_nav_filter(state,items,count,saved_query);
    int selected_found=0;
    if(selected_cusa && selected_cusa[0] && items) {
        for(unsigned i=0;i<state->available;++i) {
            const unsigned idx=state->matched[i];
            if(items[idx].cusa &&
               strcmp(items[idx].cusa,selected_cusa)==0) {
                state->selected=i;
                state->scroll=i>=UI_VISIBLE_ROWS?i-UI_VISIBLE_ROWS+1:0;
                selected_found=1;
                break;
            }
        }
    }
    state->page=(old_page==UI_PAGE_DETAILS && !selected_found)
        ? UI_PAGE_HOME : old_page;
    state->keyboard_index=keyboard_index;
    state->keyboard_arabic=keyboard_arabic;
    state->search_result_focus=state->available?search_focus:0;
    state->details_from_search=
        state->page==UI_PAGE_DETAILS?search_origin:0;
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
static int update_query(UiNavigation *state,const char *value) {
    char tmp[UI_SEARCH_CAPACITY];
    size_t len=strlen(value);
    if(len>=UI_SEARCH_CAPACITY)return 0;
    memcpy(tmp,value,len+1);
    ui_nav_filter(state,state->items,state->item_count,tmp);
    return 1;
}
int ui_nav_action(UiNavigation *state, UiAction action) {
    if (!state || state->exit_requested) return 0;
    /* OPTIONS always opens the informational settings page. It must not
     * silently change download/install flags or save any preferences. */
    if (action == UI_ACT_SETTINGS) {
        if (state->page == UI_PAGE_SETTINGS) return 0;
        state->page = UI_PAGE_SETTINGS;
        return 1;
    }
    if (state->page == UI_PAGE_SEARCH) {
        unsigned keys=ui_nav_keyboard_count(state);
        if (action == UI_ACT_BACK) {
            if(state->search_result_focus) {
                state->search_result_focus=0;
            } else {
                state->page=UI_PAGE_HOME;
            }
            return 1;
        }
        /* A focused result opens Details without typing a keyboard key.
         * LEFT/RIGHT cycle known matching CUSA IDs, UP returns to the
         * last keyboard row. No other page bypasses read-only gates. */
        if(state->search_result_focus) {
            if(action==UI_ACT_OPEN && state->available) {
                state->details_from_search=1;
                state->page=UI_PAGE_DETAILS;
                return 1;
            }
            if(action==UI_ACT_UP) {
                state->search_result_focus=0;
                return 1;
            }
            if(action==UI_ACT_LEFT && state->selected>0) {
                --state->selected;
                return 1;
            }
            if(action==UI_ACT_RIGHT &&
               state->selected+1<state->available) {
                ++state->selected;
                return 1;
            }
            if(action==UI_ACT_DOWN ||
               action==UI_ACT_LEFT || action==UI_ACT_RIGHT)return 0;
        }
        if (action == UI_ACT_TOGGLE_KEYBOARD ||
            action == UI_ACT_NEXT_TAB || action == UI_ACT_PREVIOUS_TAB) {
            state->keyboard_arabic=!state->keyboard_arabic;
            return 1;
        }
        if (action == UI_ACT_LEFT) {
            if (state->keyboard_index%10==0) return 0;
            --state->keyboard_index;
            return 1;
        }
        if (action == UI_ACT_RIGHT) {
            if (state->keyboard_index%10==9 ||
                state->keyboard_index+1>=keys)return 0;
            ++state->keyboard_index;
            return 1;
        }
        if (action == UI_ACT_UP) {
            if (state->keyboard_index<10)return 0;
            state->keyboard_index-=10;
            return 1;
        }
        if (action == UI_ACT_DOWN) {
            if (state->keyboard_index+10>=keys) {
                if(state->available && !state->search_result_focus) {
                    state->search_result_focus=1;
                    return 1;
                }
                return 0;
            }
            state->keyboard_index+=10;
            return 1;
        }
        if (action == UI_ACT_CLEAR_SEARCH)
            return update_query(state,"");
        if (action == UI_ACT_REMOVE) {
            char tmp[UI_SEARCH_CAPACITY];
            memcpy(tmp,state->query,sizeof tmp);
            size_t n=strlen(tmp);
            if(!n)return 0;
            --n;
            while(n>0 && ((unsigned char)tmp[n]&0xC0)==0x80)--n;
            tmp[n]=0;
            return update_query(state,tmp);
        }
        if (action == UI_ACT_OPEN) {
            const char *key=ui_nav_keyboard_key(state,state->keyboard_index);
            if(!key)return 0;
            size_t old_len=strlen(state->query),new_len=strlen(key);
            if(old_len+new_len>=UI_SEARCH_CAPACITY)return 0;
            char tmp[UI_SEARCH_CAPACITY];
            memcpy(tmp,state->query,old_len);
            memcpy(tmp+old_len,key,new_len+1);
            return update_query(state,tmp);
        }
        return 0;
    }
    if (action == UI_ACT_SEARCH) {
        state->page=UI_PAGE_SEARCH;
        state->keyboard_index=0;
        state->search_result_focus=0;
        state->details_from_search=0;
        return 1;
    }
    if (action == UI_ACT_BACK) {
        if(state->page==UI_PAGE_DETAILS && state->details_from_search) {
            state->page=UI_PAGE_SEARCH;
            state->details_from_search=0;
            state->search_result_focus=state->available>0;
        } else if (state->page == UI_PAGE_HOME) {
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
