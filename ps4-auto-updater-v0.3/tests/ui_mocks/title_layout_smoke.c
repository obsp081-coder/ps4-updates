#include "../../ps4/src/ui_title_layout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    UiTitleLayout plan={0};
    /* "PS4 لعبة": Latin-first titles must stay LTR, with Arabic RTL run. */
    const uint32_t english_first[]={
        'P','S','4',' ',0x0644u,0x0639u,0x0628u,0x0629u
    };
    assert(ui_title_layout(english_first,8,&plan)==0);
    assert(plan.paragraph==UI_TITLE_LTR && plan.count==2);
    assert(plan.runs[0].start==0 && plan.runs[0].length==4);
    assert(plan.runs[0].direction==UI_TITLE_LTR);
    assert(plan.runs[1].start==4 && plan.runs[1].length==4);
    assert(plan.runs[1].direction==UI_TITLE_RTL);

    /* "لعبة PS4": Arabic-first titles stay RTL; digits read LTR. */
    const uint32_t arabic_first[]={
        0x0644u,0x0639u,0x0628u,0x0629u,' ','P','S','4'
    };
    assert(ui_title_layout(arabic_first,8,&plan)==0);
    assert(plan.paragraph==UI_TITLE_RTL && plan.count==2);
    assert(plan.runs[0].length==5 && plan.runs[0].direction==UI_TITLE_RTL);
    assert(plan.runs[1].start==5 && plan.runs[1].direction==UI_TITLE_LTR);

    /* Combining fatha/shadda must stay with Arabic word, not create a new
     * direction run or cause text order to flip. */
    const uint32_t marked[]={
        0x0644u,0x0651u,0x0639u,' ','2','0','2','6'
    };
    assert(ui_title_layout(marked,8,&plan)==0);
    assert(plan.paragraph==UI_TITLE_RTL && plan.count==2);
    assert(plan.runs[0].length==4 && plan.runs[1].length==4);

    /* A neutral prefix cannot override the first strong letter. */
    const uint32_t neutral_prefix[]={' ', '-', ' ', 0x0644u,0x0639u};
    assert(ui_title_layout(neutral_prefix,5,&plan)==0);
    assert(plan.paragraph==UI_TITLE_RTL && plan.count==1);
    assert(plan.runs[0].length==5);

    /* Arabic comma/punctuation does not count as a strong RTL letter.
     * It inherits the surrounding title's direction. */
    const uint32_t comma_first[]={0x060Cu,' ',0x0644u,0x0639u};
    assert(ui_title_layout(comma_first,4,&plan)==0);
    assert(plan.paragraph==UI_TITLE_RTL && plan.count==1);
    const uint32_t mixed_comma[]={'G','T','A',0x060Cu,' ',0x0644u,0x0639u};
    assert(ui_title_layout(mixed_comma,7,&plan)==0);
    assert(plan.paragraph==UI_TITLE_LTR && plan.count==2);
    assert(plan.runs[0].length==5 && plan.runs[1].length==2);

    /* ASCII-only, empty, invalid and oversized input remain bounded. */
    const uint32_t ascii_only[]={'C','U','S','A','0','0','0','0','1'};
    assert(ui_title_layout(ascii_only,9,&plan)==0);
    assert(plan.count==1 && plan.paragraph==UI_TITLE_LTR);
    assert(ui_title_layout(NULL,0,&plan)==0 && plan.count==0);
    assert(ui_title_layout(NULL,1,&plan)<0);
    assert(ui_title_layout(ascii_only,192,&plan)<0);
    assert(ui_title_layout(ascii_only,9,NULL)<0);
    puts("UI_TITLE_LAYOUT_HOST_OK");
    return 0;
}
