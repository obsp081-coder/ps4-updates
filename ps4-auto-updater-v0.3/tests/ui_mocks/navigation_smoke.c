#include "../../ps4/src/ui_navigation.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
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

    /* Arabic search must find practical spelling/diacritic variants
     * without modifying the displayed game title or the original bytes.
     * Non-Arabic Unicode casefold intentionally remains unsupported. */
    UiRecord spellings[]={
        {"CUSA11001","أحمد: الإصدار الأول"},
        {"CUSA11002","إسلام والفتى"},
        {"CUSA11003","لُعبـة مغامرات"},
        {"CUSA11004","PS4 Edition"},
        {"CUSA11005","bad\xF0\x28\x8C\x28"}
    };
    ui_nav_init(&nav,spellings,5);
    ui_nav_filter(&nav,spellings,5,"احمد");
    assert(nav.available==1 && ui_nav_resolve(&nav,0)==0);
    ui_nav_filter(&nav,spellings,5,"اسلام");
    assert(nav.available==1 && ui_nav_resolve(&nav,0)==1);
    ui_nav_filter(&nav,spellings,5,"الفتي");
    assert(nav.available==1 && ui_nav_resolve(&nav,0)==1);
    ui_nav_filter(&nav,spellings,5,"لعبة");
    assert(nav.available==1 && ui_nav_resolve(&nav,0)==2);
    ui_nav_filter(&nav,spellings,5,"ps4 edition");
    assert(nav.available==1 && ui_nav_resolve(&nav,0)==3);
    ui_nav_filter(&nav,spellings,5,"not-in-library");
    assert(nav.available==0);
    assert(!strcmp(spellings[0].name,"أحمد: الإصدار الأول"));
    assert(!strcmp(spellings[2].name,"لُعبـة مغامرات"));

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

    /* Search is useful only if matches can be opened, not just counted.
     * From the last keyboard row Down focuses a real CUSA match.
     * Right selects the next result, Cross opens Details, Circle returns
     * to the same search, then Circle again leaves keyboard to Home. */
    fill(9);
    assert(ui_nav_action(&nav,UI_ACT_SEARCH));
    ui_nav_filter(&nav,entries,9,"Sample");
    assert(nav.available==7);
    nav.keyboard_index=36;
    assert(ui_nav_action(&nav,UI_ACT_DOWN));
    assert(nav.search_result_focus==1 && nav.selected==0);
    assert(ui_nav_resolve(&nav,nav.selected)==0);
    assert(ui_nav_action(&nav,UI_ACT_RIGHT));
    assert(nav.selected==1 && ui_nav_resolve(&nav,nav.selected)==2);
    assert(ui_nav_action(&nav,UI_ACT_OPEN));
    assert(nav.page==UI_PAGE_DETAILS && nav.details_from_search==1);
    assert(ui_nav_action(&nav,UI_ACT_BACK));
    assert(nav.page==UI_PAGE_SEARCH && nav.search_result_focus==1 &&
           nav.selected==1 && nav.details_from_search==0);
    assert(ui_nav_action(&nav,UI_ACT_LEFT));
    assert(nav.selected==0);
    assert(!ui_nav_action(&nav,UI_ACT_LEFT));
    assert(nav.keyboard_index==36);
    assert(ui_nav_action(&nav,UI_ACT_UP));
    assert(nav.search_result_focus==0);
    assert(ui_nav_action(&nav,UI_ACT_BACK));
    assert(nav.page==UI_PAGE_HOME && !nav.exit_requested);
    assert(nav.available==9 && nav.query[0]==0);

    /* Search matches do not become an accidental permanent Home filter.
     * The currently selected CUSA follows its original list index when
     * leaving Search, including after switching results in the preview. */
    assert(ui_nav_action(&nav,UI_ACT_SEARCH));
    ui_nav_filter(&nav,entries,9,"Sample");
    assert(nav.available==7);
    nav.keyboard_index=36;
    assert(ui_nav_action(&nav,UI_ACT_DOWN));
    assert(ui_nav_action(&nav,UI_ACT_RIGHT));
    assert(ui_nav_resolve(&nav,nav.selected)==2);
    assert(ui_nav_action(&nav,UI_ACT_BACK)); /* leave result focus */
    assert(ui_nav_action(&nav,UI_ACT_BACK)); /* exit Search */
    assert(nav.page==UI_PAGE_HOME && nav.available==9);
    assert(nav.query[0]==0 && nav.selected==2 && nav.scroll==0);
    assert(!nav.search_result_focus && !nav.details_from_search);
    assert(!strcmp(entries[ui_nav_resolve(&nav,nav.selected)].cusa,
                   "CUSA00002"));

    /* An empty search must not focus or open fabricated results. */
    assert(ui_nav_action(&nav,UI_ACT_SEARCH));
    ui_nav_filter(&nav,entries,9,"not-present");
    nav.keyboard_index=36;
    assert(!ui_nav_action(&nav,UI_ACT_DOWN));
    assert(!nav.search_result_focus);
    assert(!ui_nav_action(&nav,UI_ACT_OPEN) || nav.page==UI_PAGE_SEARCH);
    assert(ui_nav_action(&nav,UI_ACT_BACK));
    assert(nav.page==UI_PAGE_HOME && nav.available==9);
    assert(nav.query[0]==0 && !nav.search_result_focus);
    assert(ui_nav_action(&nav,UI_ACT_OPEN));
    assert(nav.page==UI_PAGE_DETAILS); /* game still accessible */
    assert(ui_nav_action(&nav,UI_ACT_BACK));

    /* OPTIONS while Search/Details is active must also clean up the
     * temporary filter before returning to Home. */
    assert(ui_nav_action(&nav,UI_ACT_SEARCH));
    ui_nav_filter(&nav,entries,9,"no-match");
    assert(nav.available==0);
    assert(ui_nav_action(&nav,UI_ACT_SETTINGS));
    assert(nav.page==UI_PAGE_SETTINGS && nav.query[0]==0);
    assert(ui_nav_action(&nav,UI_ACT_BACK));
    assert(nav.page==UI_PAGE_HOME && nav.available==9);
    assert(ui_nav_action(&nav,UI_ACT_SEARCH));
    ui_nav_filter(&nav,entries,9,"Uncharted");
    nav.keyboard_index=36;
    assert(ui_nav_action(&nav,UI_ACT_DOWN));
    assert(ui_nav_action(&nav,UI_ACT_OPEN));
    assert(nav.page==UI_PAGE_DETAILS && nav.details_from_search);
    assert(ui_nav_action(&nav,UI_ACT_SETTINGS));
    assert(nav.page==UI_PAGE_SETTINGS && nav.query[0]==0);
    assert(ui_nav_action(&nav,UI_ACT_BACK));
    assert(nav.page==UI_PAGE_HOME && nav.available==9);

    /* R1/Square or L1 from Details opened through Search must discard
     * the temporary query exactly like Circle does when leaving Search.
     * The selected CUSA should remain selected in the full Home library. */
    fill(9);
    assert(ui_nav_action(&nav,UI_ACT_SEARCH));
    ui_nav_filter(&nav,entries,9,"Uncharted");
    assert(nav.available==1 && ui_nav_resolve(&nav,0)==3);
    nav.keyboard_index=36;
    assert(ui_nav_action(&nav,UI_ACT_DOWN));
    assert(ui_nav_action(&nav,UI_ACT_OPEN));
    assert(nav.page==UI_PAGE_DETAILS && nav.details_from_search);
    assert(ui_nav_action(&nav,UI_ACT_NEXT_TAB));
    assert(nav.page==UI_PAGE_DOWNLOADS && nav.available==9);
    assert(!nav.query[0] && !nav.details_from_search);
    assert(ui_nav_action(&nav,UI_ACT_BACK));
    assert(nav.page==UI_PAGE_HOME && nav.selected==3);
    assert(ui_nav_resolve(&nav,nav.selected)==3);

    assert(ui_nav_action(&nav,UI_ACT_SEARCH));
    ui_nav_filter(&nav,entries,9,"Uncharted");
    nav.keyboard_index=36;
    assert(ui_nav_action(&nav,UI_ACT_DOWN));
    assert(ui_nav_action(&nav,UI_ACT_OPEN));
    assert(ui_nav_action(&nav,UI_ACT_PREVIOUS_TAB));
    assert(nav.page==UI_PAGE_HOME && nav.available==9);
    assert(nav.selected==3 && nav.query[0]==0);
    assert(!nav.details_from_search && !nav.search_result_focus);

    /* A completed rescan can remove the selected CUSA while Details is
     * open. The remaining eight games must not be hidden by the old
     * search query when the now-invalid Details page closes. */
    assert(ui_nav_action(&nav,UI_ACT_SEARCH));
    ui_nav_filter(&nav,entries,9,"Uncharted");
    nav.keyboard_index=36;
    assert(ui_nav_action(&nav,UI_ACT_DOWN));
    assert(ui_nav_action(&nav,UI_ACT_OPEN));
    UiRecord without_game[8];
    for(unsigned i=0,j=0;i<9;++i) {
        if(i!=3)without_game[j++]=entries[i];
    }
    ui_nav_rebind(&nav,without_game,8,"CUSA00003");
    assert(nav.page==UI_PAGE_HOME);
    assert(nav.available==8 && nav.query[0]==0);
    assert(nav.selected==0 && nav.scroll==0);
    assert(!nav.details_from_search && !nav.search_result_focus);
    assert(ui_nav_resolve(&nav,0)==0);
    assert(strcmp(nav.items[ui_nav_resolve(&nav,0)].cusa,
                  "CUSA00000")==0);

    fill(256);
    assert(nav.available==256);
    /* Baseline only, not a PS4 performance assertion: exercise a full
     * 256-title library with long Arabic metadata and long missing terms.
     * Do not use timing as a CI pass/fail gate (runner CPU is variable).
     * Search remains allocation-free and results stay bounded. */
    static const char *long_title=
        "لعبة المغامرات العربية الطويلة والإصدار الجديد مع مئات "
        "الخيارات والأبطال وعوالم أخرى والنسخة المحسنة";
    for(unsigned i=0;i<256;++i)entries[i].name=long_title;
    const clock_t search_start=clock();
    for(unsigned i=0;i<128;++i) {
        ui_nav_filter(&nav,entries,256,
            (i&1u)?"zzz-no-matching-title":"لعبة غير موجودة تمامًا");
        assert(nav.available==0);
        assert(nav.item_count==256);
    }
    const clock_t search_end=clock();
    if(search_start!=(clock_t)-1 && search_end!=(clock_t)-1 &&
       search_end>=search_start) {
        printf("UI_SEARCH_256_ARABIC_HOST_CPU_MS_PER_128: %.3f\\n",
               (double)(search_end-search_start)*1000.0/CLOCKS_PER_SEC);
    }
    ui_nav_filter(&nav,entries,256,"");
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
