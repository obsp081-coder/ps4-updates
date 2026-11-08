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
    puts("UI_NAVIGATION_HOST_OK");
    return 0;
}
