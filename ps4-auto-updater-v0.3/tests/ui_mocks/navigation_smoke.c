#include "../../ps4/src/ui_navigation.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static UiRecord entries[UI_MAX_TITLES];
static char ids[UI_MAX_TITLES][10];
static UiNavigation nav;

static void fill(unsigned n) {
    assert(n<=UI_MAX_TITLES);
    for(unsigned i=0;i<n;i++) {
        snprintf(ids[i],sizeof(ids[i]),"CUSA%05u",i);
        entries[i].cusa=ids[i];
        entries[i].name=(i==3)?"Uncharted Collection":(i==1)?"روبلكس":"Sample game";
    }
    ui_nav_init(&nav,entries,n);
}
int main(void) {
    fill(0);
    assert(nav.available==0 && nav.page==UI_PAGE_HOME);
    assert(ui_nav_resolve(&nav,0)==UI_MAX_TITLES);
    assert(!ui_nav_action(&nav,UI_ACT_OPEN));
    assert(ui_nav_action(&nav,UI_ACT_BACK) && nav.exit_requested);

    fill(9);
    assert(nav.available==9 && nav.selected==0 && nav.scroll==0);
    for(unsigned i=0;i<6;i++) assert(ui_nav_action(&nav,UI_ACT_DOWN));
    assert(nav.selected==6 && nav.scroll==1);
    for(unsigned i=0;i<6;i++) assert(ui_nav_action(&nav,UI_ACT_UP));
    assert(nav.selected==0 && nav.scroll==0);
    assert(!ui_nav_action(&nav,UI_ACT_UP));

    assert(ui_nav_action(&nav,UI_ACT_OPEN));
    assert(nav.page==UI_PAGE_DETAILS);
    assert(!ui_nav_action(&nav,UI_ACT_UP));
    assert(ui_nav_action(&nav,UI_ACT_BACK) && nav.page==UI_PAGE_HOME);
    assert(!nav.exit_requested);
    assert(ui_nav_action(&nav,UI_ACT_DOWNLOADS));
    assert(nav.page==UI_PAGE_DOWNLOADS);
    assert(!ui_nav_action(&nav,UI_ACT_OPEN));
    assert(ui_nav_action(&nav,UI_ACT_PREVIOUS_TAB));
    assert(nav.page==UI_PAGE_HOME);
    assert(ui_nav_action(&nav,UI_ACT_NEXT_TAB));
    assert(nav.page==UI_PAGE_DOWNLOADS);
    assert(ui_nav_action(&nav,UI_ACT_BACK) && nav.page==UI_PAGE_HOME);
    assert(ui_nav_action(&nav,UI_ACT_SETTINGS));
    assert(nav.page==UI_PAGE_SETTINGS && !nav.exit_requested);
    assert(!ui_nav_action(&nav,UI_ACT_SETTINGS));
    assert(!ui_nav_action(&nav,UI_ACT_OPEN));
    assert(ui_nav_action(&nav,UI_ACT_BACK));
    assert(nav.page==UI_PAGE_HOME && !nav.exit_requested);

    ui_nav_filter(&nav,entries,9,"unCHARTed");
    assert(nav.available==1 && ui_nav_resolve(&nav,0)==3);
    assert(!ui_nav_action(&nav,UI_ACT_DOWN));
    ui_nav_filter(&nav,entries,9,"cusa00007");
    assert(nav.available==1 && ui_nav_resolve(&nav,0)==7);
    ui_nav_filter(&nav,entries,9,"روبلكس");
    assert(nav.available==1 && ui_nav_resolve(&nav,0)==1);
    ui_nav_filter(&nav,entries,9,"no-match");
    assert(nav.available==0);
    assert(!ui_nav_action(&nav,UI_ACT_OPEN));
    assert(ui_nav_resolve(&nav,0)==UI_MAX_TITLES);

    fill(9);
    assert(ui_nav_action(&nav,UI_ACT_SEARCH));
    assert(nav.page==UI_PAGE_SEARCH);
    nav.keyboard_index=20; /* U */
    assert(ui_nav_action(&nav,UI_ACT_OPEN));
    nav.keyboard_index=13; /* N */
    assert(ui_nav_action(&nav,UI_ACT_OPEN));
    assert(strcmp(nav.query,"UN")==0);
    assert(nav.available==1 && ui_nav_resolve(&nav,0)==3);
    assert(ui_nav_action(&nav,UI_ACT_TOGGLE_KEYBOARD));
    assert(nav.keyboard_arabic==1);
    assert(strcmp(ui_nav_keyboard_key(&nav,9),"ر")==0);
    nav.keyboard_index=9;
    assert(ui_nav_action(&nav,UI_ACT_OPEN));
    assert(nav.available==0);
    assert(ui_nav_action(&nav,UI_ACT_REMOVE));
    assert(strcmp(nav.query,"UN")==0 && nav.available==1);
    assert(ui_nav_action(&nav,UI_ACT_CLEAR_SEARCH));
    unsigned ar_indices[]={9,26,1,22,21,11};
    for(unsigned i=0;i<sizeof(ar_indices)/sizeof(ar_indices[0]);++i) {
        nav.keyboard_index=ar_indices[i];
        assert(ui_nav_action(&nav,UI_ACT_OPEN));
    }
    assert(strcmp(nav.query,"روبلكس")==0 && nav.available==1);
    assert(ui_nav_resolve(&nav,0)==1);
    assert(ui_nav_action(&nav,UI_ACT_BACK));
    assert(nav.page==UI_PAGE_HOME && !nav.exit_requested);
    assert(ui_nav_action(&nav,UI_ACT_SEARCH));
    assert(nav.page==UI_PAGE_SEARCH);
    assert(ui_nav_action(&nav,UI_ACT_BACK));

    fill(256);
    assert(nav.available==256);
    for(unsigned i=0;i<300;i++) (void)ui_nav_action(&nav,UI_ACT_DOWN);
    assert(nav.selected==255 && nav.scroll==250);
    assert(ui_nav_resolve(&nav,255)==255);
    assert(ui_nav_resolve(&nav,256)==UI_MAX_TITLES);
    assert(!ui_nav_action(&nav,UI_ACT_DOWN));
    ui_nav_filter(&nav,NULL,100,NULL);
    assert(nav.available==0);

    fill(2);
    assert(ui_nav_action(&nav,UI_ACT_OPEN));
    ui_nav_filter(&nav,entries,2,"absent");
    assert(nav.page==UI_PAGE_HOME && nav.available==0);

    /* A fresh scan may reorder records after a user opened Details.
     * The selected CUSA must follow identity, never the old list index. */
    fill(9);
    for(unsigned i=0;i<7;i++)assert(ui_nav_action(&nav,UI_ACT_DOWN));
    assert(nav.selected==7 && nav.scroll==2);
    assert(ui_nav_action(&nav,UI_ACT_OPEN));
    UiRecord reordered[9];
    for(unsigned i=0;i<9;++i)reordered[i]=entries[8-i];
    ui_nav_rebind(&nav,reordered,9,"CUSA00007");
    assert(nav.page==UI_PAGE_DETAILS);
    assert(ui_nav_resolve(&nav,nav.selected)==1);
    assert(strcmp(reordered[ui_nav_resolve(&nav,nav.selected)].cusa,
                  "CUSA00007")==0);
    /* Removed selected title closes Details; it must not display another
     * game's metadata as though it were the previously selected title. */
    ui_nav_rebind(&nav,reordered,9,"CUSA99999");
    assert(nav.page==UI_PAGE_HOME && nav.selected==0);
    assert(nav.scroll==0);

    /* Search, Arabic keyboard and query survive a completed rescan. */
    fill(9);
    assert(ui_nav_action(&nav,UI_ACT_SEARCH));
    nav.keyboard_arabic=1;
    nav.keyboard_index=27;
    ui_nav_filter(&nav,entries,9,"روبلكس");
    assert(nav.available==1);
    ui_nav_rebind(&nav,reordered,9,"CUSA00001");
    assert(nav.page==UI_PAGE_SEARCH && nav.available==1);
    assert(strcmp(nav.query,"روبلكس")==0);
    assert(nav.keyboard_arabic==1 && nav.keyboard_index==27);
    assert(strcmp(reordered[ui_nav_resolve(&nav,nav.selected)].cusa,
                  "CUSA00001")==0);
    puts("UI_NAVIGATION_HOST_OK");
    return 0;
}
