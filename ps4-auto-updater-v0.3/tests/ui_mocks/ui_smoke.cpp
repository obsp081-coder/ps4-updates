#include "scanner_ui_host_mock.h"
#include "../../ps4/src/scanner_ui.h"
#include "../../ps4/src/ui_models.h"
#include "../../ps4/src/ui_icon_cache.h"
#include "../../ps4/src/ui_navigation.h"
#include "../../ps4/src/ui_visual_fx.h"
#include "ui_arabic_generated.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <ctime>

static int video_opened = 0;
static int video_closed = 0;
static int pad_closed = 0;
static int flip_count = 0;
static int pad_reads = 0;
static int allocated = 0;
static size_t mapped_bytes = 0;
static int sleep_count = 0;
static int64_t last_flip = 0;
static const uint32_t *display_buffers[2] = {nullptr, nullptr};
static bool saw_blurred_background = false;
static bool test_cover_replacement = false;
static bool saw_backdrop_before_replace = false;
static bool test_wide_art = false;
static bool saw_wide_art = false;
static bool saw_backdrop_after_replace = false;
static bool capture_live_ui = false;
static bool verify_jobs = false;
static bool saw_job_progress = false;
static bool saw_job_scroll = false;
static bool saw_update_row = false;
static bool capture_cached_preview = false;
static bool saw_cached_sixth_row = false;
static bool check_offline_cache = false;
static bool saw_stale_details_warning = false;
static bool saw_scanning_details_warning = false;
static bool saw_offline_cache = false;
static bool test_async_scan = false;
static bool test_async_exit = false;
static bool saw_scan_details = false;
static bool saw_scan_settings = false;
static bool test_lazy_cover = false;
static unsigned lazy_cover_polls = 0;
static bool lazy_cover_offered = false;
static bool test_cover_scroll = false;
static bool test_idle_without_redraw = false;
static bool test_search_result_open = false;
static bool test_search_restore_home = false;
static bool test_circular_tabs = false;
static unsigned seen_circular_tabs = 0;
static bool saw_search_no_match = false;
static bool saw_home_restored = false;
static bool saw_restored_details = false;
static bool saw_search_focus = false;
static bool saw_search_details = false;
static bool saw_search_return = false;
static bool test_capacity_screen = false;
static bool saw_capacity_sixth_row = false;
static bool saw_empty_library_panel = false;
static int mixed_title_probe = 0;
static int long_title_probe = 0;
static bool saw_long_title_left = false;
static bool saw_long_title_right = false;
static bool long_title_no_bleed = true;
static bool saw_ltr_title_left = false;
static bool saw_rtl_title_right = false;
static bool saw_rtl_title_clear_left = false;
static bool saw_cover_beyond_eight = false;
static volatile uint32_t mock_scan_status = 0;
static unsigned captured_pages = 0;
/* Visual-regression snapshots use the SAME native framebuffer output that
 * is submitted to the host VideoOut mock; this is not a browser mock. */
static void capture_ppm(const uint32_t *frame, const char *page,
                        unsigned page_bit) {
    const char *root = std::getenv("PS4_UI_PREVIEW_DIR");
    if (!root || !*root || (captured_pages & page_bit)) return;
    char path[768];
    const int n = std::snprintf(path, sizeof path, "%s/%s.ppm", root, page);
    if(n<0 || (size_t)n>=sizeof path) return;
    FILE *out = std::fopen(path, "wb");
    if(!out) return;
    const unsigned width=1920u, height=1080u;
    if(std::fprintf(out, "P6\n%u %u\n255\n", width, height) < 0) {
        std::fclose(out); return;
    }
    unsigned char rgb[1920u*3u];
    for(unsigned y=0;y<height;++y) {
        for(unsigned x=0;x<width;++x) {
            const uint32_t pixel=frame[(size_t)y*width+x];
            rgb[3*x]=(unsigned char)(pixel>>16);
            rgb[3*x+1]=(unsigned char)(pixel>>8);
            rgb[3*x+2]=(unsigned char)pixel;
        }
        if(std::fwrite(rgb,1,sizeof rgb,out)!=sizeof rgb) {
            std::fclose(out); return;
        }
    }
    if(std::fclose(out)==0) captured_pages|=page_bit;
}

static constexpr unsigned SCREEN_WIDTH = 1920u;
static const unsigned char icon_png_sample[] = {
0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,0x08,0x06,0x00,0x00,0x00,0x72,0xb6,0x0d,0x24,0x00,0x00,0x00,0x14,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0x94,0xf4,0xde,0xf2,0x9f,0x81,0x81,0x81,0x81,0x89,0x01,0x0a,0x00,0x1f,0x0d,0x02,0x1b,0x4c,0x07,0x75,0x81,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82
};
static bool fail_video = false;
static bool fail_memory = false;
static bool fail_pad = false;
static bool stall_flip = false;
static bool registered = false;
static int unregister_count = 0;
static int trace_count = 0;
static int shutdown_done = 0;
static int retained_warning = 0;
static void trace(const char *name, int) {
    ++trace_count;
    if (std::strcmp(name, "UI_SHUTDOWN_DONE") == 0) ++shutdown_done;
    if (std::strcmp(name, "UI_MEMORY_RETAINED_FOR_SAFETY") == 0) ++retained_warning;
}

/* Host-only mailbox fixture: the image is intentionally absent at first.
 * A subsequent UI tick imports an already-decoded thumbnail and requests
 * a repaint. This is not a real FTP fetch or PS4 device test. */
static int cover_scroll_mock_tick(const char *cusa) {
    if(test_cover_scroll && cusa &&
       std::strcmp(cusa,"CUSA00008")==0)saw_cover_beyond_eight=true;
    return 0; /* Read-only mailbox fixture; never start FTP in a host test. */
}

static int lazy_cover_mock_tick(const char *cusa) {
    if(!test_lazy_cover || !cusa ||
       std::strcmp(cusa,"CUSA02343")!=0)return 0;
    ++lazy_cover_polls;
    if(lazy_cover_polls<3 || lazy_cover_offered)return 0;
    static uint32_t pixels[UI_ICON_PIXELS];
    for(size_t i=0;i<UI_ICON_PIXELS;++i)pixels[i]=0xFF194BB4u;
    if(scanner_ui_offer_thumbnail("CUSA02343",pixels,UI_ICON_PIXELS)!=0)
        return 0;
    lazy_cover_offered=true;
    return 1;
}

static void reset() {
    video_opened = video_closed = pad_closed = 0;
    flip_count = pad_reads = allocated = sleep_count = 0;
    mapped_bytes=0;
    last_flip = 0;
    display_buffers[0] = display_buffers[1] = nullptr;
    saw_blurred_background = false;
    test_cover_replacement = false;
    saw_backdrop_before_replace = false;
    saw_backdrop_after_replace = false;
    captured_pages = 0;
    seen_circular_tabs = 0;
    test_circular_tabs = false;
    capture_live_ui = false;
    verify_jobs = saw_job_progress = saw_job_scroll = saw_update_row = false;
    capture_cached_preview = saw_cached_sixth_row = false;
    check_offline_cache = saw_offline_cache =
        saw_stale_details_warning = saw_scanning_details_warning = false;
    test_async_scan = test_async_exit = saw_scan_details =
        saw_scan_settings = test_lazy_cover = lazy_cover_offered =
        test_cover_scroll = saw_cover_beyond_eight =
        test_idle_without_redraw =
        test_capacity_screen = saw_capacity_sixth_row =
        saw_empty_library_panel =
        test_search_result_open = test_search_restore_home =
        saw_search_no_match = saw_home_restored =
        saw_restored_details = saw_search_focus =
        saw_search_details = saw_search_return = false;
    mixed_title_probe = long_title_probe = 0;
    saw_long_title_left = saw_long_title_right = false;
    long_title_no_bleed = true;
    saw_ltr_title_left = saw_rtl_title_right =
        saw_rtl_title_clear_left = false;
    lazy_cover_polls = 0;
    mock_scan_status = 0;
    fail_video = fail_memory = fail_pad = stall_flip = false;
    registered = false;
    unregister_count = trace_count = shutdown_done = retained_warning = 0;
}
extern "C" {
int32_t sceUserServiceInitialize(void *) {return 0;}
int32_t sceUserServiceGetInitialUser(int32_t *id) {
    *id = 1;
    return 0;
}
int32_t sceVideoOutOpen(int32_t, int32_t, int32_t, const void *) {
    if (fail_video) return -1;
    ++video_opened;
    return 7;
}
int32_t sceVideoOutClose(int32_t) {
    assert(!registered);  // registered buffers must be unregistered first
    ++video_closed;
    return 0;
}
int32_t sceVideoOutUnregisterBuffers(int32_t, int32_t idx) {
    assert(idx == 0 && registered);
    ++unregister_count;
    registered = false;
    return 0;
}
void sceVideoOutSetBufferAttribute(void *, uint32_t, uint32_t, uint32_t,
                                    uint32_t, uint32_t, uint32_t) {}
int32_t sceVideoOutRegisterBuffers(int32_t, int32_t, void * const *p,
                                    int32_t count, const OrbisVideoOutBufferAttribute *) {
    if (count != 2 || !p || !p[0] || !p[1]) return -1;
    display_buffers[0] = static_cast<uint32_t *>(p[0]);
    display_buffers[1] = static_cast<uint32_t *>(p[1]);
    registered = true;
    return 0;
}
int32_t sceVideoOutSetFlipRate(int32_t, int32_t) { return 0; }
int32_t sceVideoOutSubmitFlip(int32_t, int32_t index, uint32_t, int64_t tag) {
    ++flip_count;
    last_flip = tag;
    if(index >= 0 && index < 2 && display_buffers[index]) {
        /* Source is the synthetic 2x2 PNG in icon_smoke.c, decoded to
         * 0xFF194BB4. The selected-game blurred backdrop is 25% icon and
         * 75% royal-blue with blending performed once, not per pixel. */
        /* x=72 is outside the 69px sidebar rail and outside
         * content panels, so it genuinely measures the blurred art. */
        const uint32_t sample =
            display_buffers[index][500u*SCREEN_WIDTH+1383u];
        if(sample == 0xFF0A1F49u) saw_blurred_background = true;
        if(test_wide_art && pad_reads==0) {
            const uint32_t *frame=display_buffers[index];
            /* The mock wide art is a top-half terracotta and lower-half
             * deep blue original test pattern. The right hero is truly
             * 16:9 rather than stretching the selected icon0 square. */
            if(frame[245u*SCREEN_WIDTH+1490u]==0xFFC07834u &&
               frame[427u*SCREEN_WIDTH+1490u]==0xFF285BA0u) {
                saw_wide_art=true;
                capture_ppm(frame,"wide_art",33554432u);
            }
        }
        if(test_cover_replacement) {
            /* Before: 0xFF18A04D (local green icon). After: decoded PNG
             * 0xFF194BB4. This background pixel is outside all panels. */
            if(sample==0xFF0A342Fu) saw_backdrop_before_replace=true;
            if(saw_backdrop_before_replace && sample==0xFF0A1F49u)
                saw_backdrop_after_replace=true;
        }
        if(mixed_title_probe) {
            /* Native framebuffer: RTL table title occupies x922..1273
             * immediately left of native local cover x1292. LTR-leading
             * starts at x922, RTL-leading starts at x1273. The sampled
             * band excludes CUSA/version and cover pixels. */
            const uint32_t *frame=display_buffers[index];
            unsigned left_ink=0,right_ink=0;
            for(unsigned yy=372;yy<414;++yy) {
                for(unsigned xx=929;xx<1006;++xx) {
                    const uint32_t c=frame[yy*SCREEN_WIDTH+xx];
                    if((c&0x00FFFFFFu)==0x00EAF4FFu)++left_ink;
                }
                for(unsigned xx=1180;xx<1267;++xx) {
                    const uint32_t c=frame[yy*SCREEN_WIDTH+xx];
                    if((c&0x00FFFFFFu)==0x00EAF4FFu)++right_ink;
                }
            }
            /* Capture the first FRAME WITH ACTUAL TITLE INK, not the
             * blank initialization frame from scanner_ui_begin(). */
            if(mixed_title_probe==1 && left_ink>8) {
                saw_ltr_title_left=true;
                capture_ppm(frame,"mixed_ltr",128u);
            }
            if(mixed_title_probe==2 && right_ink>8) {
                saw_rtl_title_right=true;
                if(left_ink==0) {
                    saw_rtl_title_clear_left=true;
                    capture_ppm(frame,"mixed_rtl",256u);
                }
            }
        }
        if(long_title_probe) {
            /* Home's title field is x922..1273, just left of cover
             * x1292..1361. Ensure long LTR and Arabic RTL names paint at
             * both ends and never bleed into the icon gutter. */
            const uint32_t *frame=display_buffers[index];
            unsigned left_ink=0,right_ink=0,bleed=0;
            for(unsigned yy=385; yy<421; ++yy) {
                for(unsigned xx=928; xx<1005; ++xx)
                    if(frame[yy*SCREEN_WIDTH+xx]==0xFFEAF4FFu)
                        ++left_ink;
                for(unsigned xx=1175; xx<1267; ++xx)
                    if(frame[yy*SCREEN_WIDTH+xx]==0xFFEAF4FFu)
                        ++right_ink;
                for(unsigned xx=1274; xx<1292; ++xx)
                    if(frame[yy*SCREEN_WIDTH+xx]==0xFFEAF4FFu)
                        ++bleed;
            }
            if(left_ink>8)saw_long_title_left=true;
            if(right_ink>8)saw_long_title_right=true;
            if(bleed)long_title_no_bleed=false;
            if(long_title_probe==1 && left_ink>8 && right_ink>8)
                capture_ppm(frame,"long_ltr",512u);
            if(long_title_probe==2 && left_ink>8 && right_ink>8)
                capture_ppm(frame,"long_rtl",1024u);
        }
        if(test_circular_tabs && pad_reads>=2 && pad_reads<=32 &&
           pad_reads%2==0) {
            /* Native Pad + VideoOut: eight R1 steps then eight L1 steps,
             * including Settings -> Home and Home -> Settings wrap. */
            const int idx=(pad_reads/2)-1;
            const int sequence[]={
                1,2,3,4,5,6,7,0,7,6,5,4,3,2,1,0
            };
            const unsigned y=(unsigned)(210+sequence[idx]*77);
            if(display_buffers[index][y*SCREEN_WIDTH+232u]==
               0xFF15589Du)
                seen_circular_tabs|=1u<<idx;
            const uint32_t *frame=display_buffers[index];
            if(idx==0)capture_ppm(frame,"games",65536u);
            if(idx==1)capture_ppm(frame,"available",131072u);
            if(idx==3)capture_ppm(frame,"queue",262144u);
            if(idx==4)capture_ppm(frame,"installed",524288u);
            if(idx==5)capture_ppm(frame,"about",1048576u);
        }
        if(test_search_restore_home) {
            const uint32_t *frame=display_buffers[index];
            /* Pad: search -> Arabic keyboard -> no-match Arabic letter
             * -> Back -> full library -> Details. Observe actual renderer
             * output rather than relying only on the pure nav model. */
            if(pad_reads==6 &&
               frame[480u*SCREEN_WIDTH+300u]==0xFF116BCEu)
                saw_search_no_match=true;
            if(pad_reads==8 &&
               frame[405u*SCREEN_WIDTH+600u]==0xFF164881u &&
               frame[884u*SCREEN_WIDTH+1500u]==0xFF15589Du) {
                saw_home_restored=true;
                capture_ppm(frame,"search_restored_home",32768u);
            }
            if(pad_reads==10 &&
               frame[210u*SCREEN_WIDTH+232u]==0xFF15589Du)
                saw_restored_details=true;
        }
        if(test_search_result_open) {
            const uint32_t *frame=display_buffers[index];
            /* This region is the bottom match preview only. The selected
             * row uses focused royal blue and survives returning from
             * Details; no download/install command is simulated. */
            const uint32_t result=frame[967u*SCREEN_WIDTH+650u];
            if(pad_reads==10 && result==0xFF164881u)
                saw_search_focus=true;
            if(pad_reads==12 &&
               frame[210u*SCREEN_WIDTH+232u]==0xFF15589Du)
                saw_search_details=true;
            if(pad_reads==14 && result==0xFF164881u)
                saw_search_return=true;
        }
        if(test_capacity_screen) {
            /* Empty library and the full 256-game list must have distinct
             * states in the genuine native framebuffer. No mocked game
             * or update is drawn for the zero-record case. */
            const uint32_t *frame=display_buffers[index];
            if(frame[620u*SCREEN_WIDTH+650u]==0xFF102D50u) {
                saw_empty_library_panel=true;
                capture_ppm(frame,"empty",4096u);
            }
            if(frame[903u*SCREEN_WIDTH+600u]==0xFF0C244Au)
                saw_capacity_sixth_row=true;
        }
        if(check_offline_cache) {
            const uint32_t *frame=display_buffers[index];
            if(pad_reads>=2 &&
               frame[210u*SCREEN_WIDTH+232u]==0xFF15589Du &&
               frame[954u*SCREEN_WIDTH+900u]==0xFF102D50u) {
                saw_stale_details_warning=true;
                capture_ppm(frame,"stale_details",8192u);
            }
            /* A failed FTP scan preserves all six rows, shows a cache
             * banner BELOW the list, and never overwrites the selected
             * game's read-only Details button at x=1500,y=884. */
            if(frame[903u*SCREEN_WIDTH+600u]==0xFF0C244Au &&
               frame[954u*SCREEN_WIDTH+800u]==0xFF102D50u &&
               frame[884u*SCREEN_WIDTH+1500u]==0xFF15589Du) {
                saw_offline_cache=true;
                /* Capture the failed-scan state separately from active
                 * cache revalidation. The labels must differ. */
                capture_ppm(frame,"stale",2048u);
            }
        }
        if(capture_cached_preview) {
            /* This is a real host CPU-framebuffer capture of the cached
             * native UI, not a screenshot from PS4 hardware. */
            capture_ppm(display_buffers[index],"cached",64u);
            /* Sixth row is y=827..915. The stale warning must not cover it. */
            if(display_buffers[index][903u*SCREEN_WIDTH+600u]==
                   0xFF0C244Au &&
               display_buffers[index][954u*SCREEN_WIDTH+800u]==
                   0xFF102D50u &&
               display_buffers[index][884u*SCREEN_WIDTH+1500u]==
                   0xFF15589Du)
                saw_cached_sixth_row=true;
        }
        if(verify_jobs) {
            const uint32_t *frame=display_buffers[index];
            /* Reference split pane: the first REAL model-backed progress
             * bar is at y=478..487; no mock network transfer is begun. */
            if(frame[483u*SCREEN_WIDTH+380u]==0xFF37ADFFu)
                saw_job_progress=true;
            if(pad_reads>=14 && pad_reads<=19 &&
               frame[850u*SCREEN_WIDTH+310u]==0xFF164881u)
                saw_job_scroll=true;
            if(pad_reads==22 &&
               frame[710u*SCREEN_WIDTH+450u]==0xFF102D50u)
                saw_update_row=true;
        }
        if(test_async_scan) {
            const uint32_t *frame=display_buffers[index];
            if(pad_reads>=2 &&
               frame[210u*SCREEN_WIDTH+232u]==0xFF15589Du) {
                saw_scan_details=true;
                if(frame[954u*SCREEN_WIDTH+900u]==0xFF102D50u) {
                    saw_scanning_details_warning=true;
                    capture_ppm(frame,"scanning_details",16384u);
                }
            }
            if(pad_reads>=6 && frame[749u*SCREEN_WIDTH+232u]==0xFF15589Du)
                saw_scan_settings=true;
        }
        if(capture_live_ui) {
            const uint32_t *frame=display_buffers[index];
            /* The left Arabic rail is now the ONLY navigation source.
             * Capture real screen states from its active highlight, not
             * removed duplicate top tabs. */
            /* Details and Search are not rail tabs in the approved eight-
             * section design. Capture those auxiliary screens by the
             * deterministic Pad sequence, not by stale tab y-positions. */
            if(pad_reads==2)capture_ppm(frame,"details",2u);
            else if(pad_reads==6)capture_ppm(frame,"downloads",4u);
            else if(pad_reads==10)capture_ppm(frame,"search",8u);
            else if(pad_reads==16)capture_ppm(frame,"search_ar",32u);
            else if(pad_reads==20)capture_ppm(frame,"settings",16u);
            else if(pad_reads==0 || pad_reads==4 || pad_reads==8 ||
                    pad_reads==18 || pad_reads==22)
                capture_ppm(frame,"home",1u);
        }
    }
    return 0;
}
int32_t sceVideoOutGetFlipStatus(int32_t, OrbisVideoOutFlipStatus *out) {
    std::memset(out, 0, sizeof(*out));
    out->flipArg = stall_flip ? 0 : last_flip;
    return 0;
}
int32_t scePadInit(void) { return 0; }
int32_t scePadOpen(int32_t, int32_t, int32_t, void *) {
    return fail_pad ? -1 : 3;
}
int32_t scePadClose(int32_t) {
    ++pad_closed;
    return 0;
}
int32_t scePadReadState(int32_t, OrbisPadData *data) {
    data->buttons = 0;
    ++pad_reads;
    if(test_cover_replacement) {
        if(pad_reads==2) {
            assert(saw_backdrop_before_replace);
            /* Regression: updating the same selected CUSA through the PNG
             * path must refresh the next drawn background, not just icon. */
            assert(scanner_ui_offer_icon(
                "CUSA02343",icon_png_sample,sizeof icon_png_sample)==0);
            data->buttons=ORBIS_PAD_BUTTON_CROSS;
        }
        if(pad_reads==4)data->buttons=ORBIS_PAD_BUTTON_CIRCLE;
        if(pad_reads==6)data->buttons=ORBIS_PAD_BUTTON_CIRCLE;
        return 0;
    }
    if(test_idle_without_redraw) {
        /* 120 mock input polls with no button or cover result. The native
         * renderer must not flip once per 33ms polling cycle. */
        if(pad_reads==120)data->buttons=ORBIS_PAD_BUTTON_CIRCLE;
        return 0;
    }
    if(test_circular_tabs) {
        if(pad_reads>=2 && pad_reads<=32 && pad_reads%2==0)
            data->buttons=pad_reads<=16 ? ORBIS_PAD_BUTTON_R1 :
                                         ORBIS_PAD_BUTTON_L1;
        if(pad_reads==34)data->buttons=ORBIS_PAD_BUTTON_CIRCLE;
        return 0;
    }
    if(test_search_restore_home) {
        if(pad_reads==2 || pad_reads==4)
            data->buttons=ORBIS_PAD_BUTTON_TRIANGLE;
        if(pad_reads==6 || pad_reads==10)
            data->buttons=ORBIS_PAD_BUTTON_CROSS;
        if(pad_reads==8 || pad_reads==12 || pad_reads==14)
            data->buttons=ORBIS_PAD_BUTTON_CIRCLE;
        return 0;
    }
    if(test_search_result_open) {
        if(pad_reads==2)data->buttons=ORBIS_PAD_BUTTON_TRIANGLE;
        if(pad_reads==4 || pad_reads==6 || pad_reads==8 ||
           pad_reads==10)data->buttons=ORBIS_PAD_BUTTON_DOWN;
        if(pad_reads==12)data->buttons=ORBIS_PAD_BUTTON_CROSS;
        if(pad_reads==14 || pad_reads==16 || pad_reads==18 ||
           pad_reads==20)data->buttons=ORBIS_PAD_BUTTON_CIRCLE;
        return 0;
    }
    if(test_capacity_screen) {
        /* No need to issue 256 expensive framebuffer redraws. Navigation
         * bounds are separately checked in navigation_smoke.c. */
        if(pad_reads==2)data->buttons=ORBIS_PAD_BUTTON_CIRCLE;
        return 0;
    }
    if(test_cover_scroll) {
        if(pad_reads>=2 && pad_reads<=16 && pad_reads%2==0)
            data->buttons=ORBIS_PAD_BUTTON_DOWN;
        if(pad_reads==18)data->buttons=ORBIS_PAD_BUTTON_CIRCLE;
        return 0;
    }
    if(test_async_exit) {
        if(pad_reads==2)data->buttons=ORBIS_PAD_BUTTON_CIRCLE;
        return 0;
    }
    if(test_async_scan) {
        /* Worker remains incomplete while we navigate cached details and
         * settings. It signals completion only after Pad input is polled. */
        if(pad_reads==2)data->buttons=ORBIS_PAD_BUTTON_CROSS;
        if(pad_reads==4)data->buttons=ORBIS_PAD_BUTTON_CIRCLE;
        if(pad_reads==6)data->buttons=ORBIS_PAD_BUTTON_OPTIONS;
        if(pad_reads==8)
            mock_scan_status=SCANNER_UI_SCAN_DONE;
        return 0;
    }
    if(verify_jobs) {
        /* Synthetic host-only data: six Down presses scroll beyond
         * the five visible rows without triggering network/install. */
        if(pad_reads==2)data->buttons=ORBIS_PAD_BUTTON_SQUARE;
        if(pad_reads==4 || pad_reads==6 || pad_reads==8 ||
           pad_reads==10 || pad_reads==12 || pad_reads==14 ||
           pad_reads==16 || pad_reads==18)
            data->buttons=ORBIS_PAD_BUTTON_DOWN;
        if(pad_reads==20 || pad_reads==24 || pad_reads==26)
            data->buttons=ORBIS_PAD_BUTTON_CIRCLE;
        if(pad_reads==22)data->buttons=ORBIS_PAD_BUTTON_CROSS;
        return 0;
    }
    /* Exercise actual controller flow: home -> details -> home ->
     * downloads -> home -> Arabic/English search -> home ->
     * settings -> home -> exit.
     * These are host input mocks, not simulated success of networking. */
    if (pad_reads == 2) data->buttons = ORBIS_PAD_BUTTON_CROSS;
    if (pad_reads == 4) data->buttons = ORBIS_PAD_BUTTON_CIRCLE;
    if (pad_reads == 6) data->buttons = ORBIS_PAD_BUTTON_SQUARE;
    if (pad_reads == 8) data->buttons = ORBIS_PAD_BUTTON_CIRCLE;
    if (pad_reads == 10) data->buttons = ORBIS_PAD_BUTTON_TRIANGLE;
    if (pad_reads == 12) data->buttons = ORBIS_PAD_BUTTON_CROSS;
    if (pad_reads == 14) data->buttons = ORBIS_PAD_BUTTON_SQUARE;
    if (pad_reads == 16) data->buttons = ORBIS_PAD_BUTTON_TRIANGLE;
    if (pad_reads == 18) data->buttons = ORBIS_PAD_BUTTON_CIRCLE;
    if (pad_reads == 20) data->buttons = ORBIS_PAD_BUTTON_OPTIONS;
    if (pad_reads == 22) data->buttons = ORBIS_PAD_BUTTON_CIRCLE;
    if (pad_reads == 24) data->buttons = ORBIS_PAD_BUTTON_CIRCLE;
    return 0;
}
size_t sceKernelGetDirectMemorySize(void) { return 512u * 1024u * 1024u; }
int32_t sceKernelAllocateDirectMemory(off_t, off_t, size_t, size_t,
                                       int32_t, off_t *offset) {
    if (fail_memory) return -1;
    *offset = 0x10000;
    return 0;
}
int32_t sceKernelMapDirectMemory(void **ptr, size_t size, int32_t, int32_t,
                                  off_t, size_t) {
    mapped_bytes=size;
    *ptr = std::malloc(size);
    if (!*ptr) return -1;
    ++allocated;
    return 0;
}
int32_t sceKernelMunmap(void *ptr, size_t) {
    assert(!registered);
    assert(allocated == 1);
    --allocated;
    std::free(ptr);
    return 0;
}
int32_t sceKernelReleaseDirectMemory(off_t, size_t) { return 0; }
int32_t sceKernelUsleep(uint32_t) {
    ++sleep_count;
    return 0;
}
} // extern "C"

int main() {
    /* Real atlas widths are computed at build time with Noto/RAQM.
     * Keep 18+ px separation between the observed-patch and local SFO
     * version headers at the native 1920x1080 table positions. The
     * previous long wording left only 4px and looked merged on shadPS4. */
    assert(634+(int)UI_AR_GLYPHS[UI_AR_TABLE_UPDATE_VERSION].width+18<=789);
    assert(789+(int)UI_AR_GLYPHS[UI_AR_TABLE_META_VERSION].width+18<=1180);
    assert(513+(int)UI_AR_GLYPHS[UI_AR_TABLE_UPDATE_SIZE].width+18<=634);
    /* Bilinear sampler: exact flat colors and predictable smooth steps.
     * Prevent channel overflow or tint changes in the real C++ renderer. */
    assert(ui_fx_lerp_rgb8(0xFF123456u,0xFF123456u,7u)==
           0xFF123456u);
    assert(ui_fx_lerp_rgb8(0xFF000000u,0xFFFFFFFFu,4u)==
           0xFF808080u);
    assert(ui_fx_lerp_rgb8(0xFFFF0000u,0xFF0000FFu,0u)==
           0xFFFF0000u);
    assert(ui_fx_lerp_rgb8(0xFFFF0000u,0xFF0000FFu,4u)==
           0xFF800080u);
    assert(ui_fx_lerp_rgb8(0xFFFF0000u,0xFF0000FFu,7u)==
           0xFF2000DFu);
    ScannerUiEntry entries[] = {
        {"CUSA02343", "Verified Game", "01.07", 1, -1, "gd"},
        {"CUSA00411", "", "", 0, -1, nullptr},
        {"CUSA08032", "Other Patch", "02.04", 1, -1, "gp"},
    };

    reset();
    assert(scanner_ui_begin() == 1);
    assert(scanner_ui_offer_icon("CUSA02343",
                                 icon_png_sample,sizeof icon_png_sample) == 0);
    /* Cached metadata is painted before the real scanner completes.
     * Its six rows must remain fully visible beneath the stale warning. */
    ScannerUiEntry cache_entries[6]={};
    char cache_ids[6][10]={};
    for(unsigned i=0;i<6;++i) {
        std::snprintf(cache_ids[i],10,"CUSA%05u",i+1u);
        cache_entries[i].cusa=cache_ids[i];
        cache_entries[i].title="Cached title";
        cache_entries[i].version="01.00";
        cache_entries[i].category="gd";
        cache_entries[i].metadata_verified=1; /* must be downrated */
        cache_entries[i].install_state=1; /* must be downrated */
    }
    capture_cached_preview=true;
    const int first_flip=flip_count;
    assert(scanner_ui_preview_snapshot(nullptr,1)==0);
    assert(scanner_ui_preview_snapshot(cache_entries,257)==0);
    assert(scanner_ui_preview_snapshot(cache_entries,6)==1);
    capture_cached_preview=false;
    assert(flip_count>first_flip && saw_cached_sixth_row);
    scanner_ui_progress("CUSA02343", 1, 3);
    capture_live_ui = true;
    /* Only a complete scan replaces the cached entries; this verified
     * fixture has the game icon needed for the blurred-background test. */
    scanner_ui_results(entries, 3, 2, 0, 1, 1);
    capture_live_ui = false;
    assert(saw_blurred_background);
    scanner_ui_shutdown(trace);
    assert(flip_count >= 8);
    assert(pad_reads >= 24);
    assert(allocated == 0 && video_closed == 1 && pad_closed == 1);
    assert(unregister_count == 1 && trace_count >= 7 && shutdown_done == 1);
    assert(retained_warning == 0);

    /* Host-only 16:9 display contract. Do not claim that pic1.png can
     * be fetched from GoldHEN FW14 until read-only hardware testing. */
    reset();
    assert(scanner_ui_begin()==1);
    static uint32_t wide_art[SCANNER_UI_WIDE_PIXELS];
    for(unsigned y=0;y<SCANNER_UI_WIDE_HEIGHT;++y)
        for(unsigned x=0;x<SCANNER_UI_WIDE_WIDTH;++x)
            wide_art[(size_t)y*SCANNER_UI_WIDE_WIDTH+x]=
                y<SCANNER_UI_WIDE_HEIGHT/2u?
                0xFFC07834u:0xFF285BA0u;
    assert(scanner_ui_offer_wide_art(nullptr,wide_art,
                                     SCANNER_UI_WIDE_PIXELS)<0);
    assert(scanner_ui_offer_wide_art("CUSA0123",wide_art,
                                     SCANNER_UI_WIDE_PIXELS)<0);
    assert(scanner_ui_offer_wide_art("CUSA02343",wide_art,
                                     SCANNER_UI_WIDE_PIXELS-1u)<0);
    assert(scanner_ui_offer_wide_art("CUSA02343",wide_art,
                                     SCANNER_UI_WIDE_PIXELS)==0);
    test_wide_art=true;
    scanner_ui_results(entries,3,2,0,1,1);
    assert(saw_wide_art && pad_reads>=2);
    scanner_ui_shutdown(trace);
    assert(allocated==0 && video_closed==1 && pad_closed==1);

    /* No idle redraws: the UI uses Pad polling but does not repaint
     * the 1080p framebuffer 120 times with unchanged input/artwork. */
    reset();
    assert(scanner_ui_begin()==1);
    test_idle_without_redraw=true;
    const int prior_idle_flip=flip_count;
    scanner_ui_results(entries,3,2,0,1,1);
    assert(pad_reads>=120 && pad_reads<=122);
    /* One initial result frame; no idle-frame submissions. */
    assert(flip_count-prior_idle_flip==1);
    scanner_ui_shutdown(trace);
    assert(allocated==0 && video_closed==1 && pad_closed==1);

    /* End-to-end native 1080p framebuffer CPU baseline (Host ONLY).
     * Unlike screenshot fixtures, this session does not perform disk I/O,
     * allowing a reproducible approximate cost per repaint. Timing also
     * includes mock Pad handling, therefore it is NOT GPU frame time, PS4
     * FPS, or a strict regression gate. Two 1920x1080 RGBA buffers are
     * backed by the platform's 2-MiB-aligned direct memory allocation. */
    reset();
    assert(scanner_ui_begin()==1);
    assert(mapped_bytes==16u*1024u*1024u);
    assert(scanner_ui_offer_icon("CUSA02343",
                                 icon_png_sample,sizeof icon_png_sample)==0);
    const int frame_before=flip_count;
    const std::clock_t render_start=std::clock();
    scanner_ui_results(entries,3,2,0,1,1);
    const std::clock_t render_end=std::clock();
    const int measured_frames=flip_count-frame_before;
    assert(measured_frames>=8);
    assert(render_start!=(std::clock_t)-1 &&
           render_end!=(std::clock_t)-1 &&
           render_end>=render_start);
    const double elapsed_ms=
        1000.0*(double)(render_end-render_start)/CLOCKS_PER_SEC;
    std::printf(
        "HOST_ONLY_NATIVE_1080P_CPU_MS=%.3f FRAMES=%d "
        "CPU_MS_PER_SUBMISSION=%.3f FRAMEBUFFER_BYTES=%zu\n",
        elapsed_ms,measured_frames,elapsed_ms/measured_frames,mapped_bytes);
    scanner_ui_shutdown(trace);
    assert(allocated==0 && video_closed==1 && pad_closed==1);
    assert(retained_warning==0);

    /* The selected cover can be absent at first render. A later,
     * nonblocking UI tick imports it and repaints the blurred backdrop.
     * The test uses synthetic pixels; no socket or file is opened. */
    reset();
    assert(scanner_ui_begin()==1);
    test_lazy_cover=true;
    scanner_ui_set_cover_tick(lazy_cover_mock_tick);
    scanner_ui_results(entries,3,2,0,1,1);
    scanner_ui_set_cover_tick(nullptr);
    assert(lazy_cover_offered && lazy_cover_polls>=3);
    assert(saw_blurred_background);
    scanner_ui_shutdown(trace);
    assert(allocated==0 && video_closed==1 && pad_closed==1);

    /* A PNG update for the SAME selected game ID must invalidate the
     * blurred-background cache after successful decode. All source pixels
     * are synthetic and the test runs only in Host VideoOut/Pad mocks. */
    reset();
    assert(scanner_ui_begin()==1);
    static uint32_t green_icon[UI_ICON_PIXELS];
    for(size_t i=0;i<UI_ICON_PIXELS;++i)
        green_icon[i]=0xFF18A04Du;
    assert(scanner_ui_offer_thumbnail(
        "CUSA02343",green_icon,UI_ICON_PIXELS)==0);
    test_cover_replacement=true;
    scanner_ui_results(entries,3,2,0,1,1);
    assert(saw_backdrop_before_replace && saw_backdrop_after_replace);
    assert(pad_reads>=6);
    scanner_ui_shutdown(trace);
    assert(allocated==0 && video_closed==1 && pad_closed==1);

    /* The on-demand callback must be reached after scrolling beyond the
     * eight icons previously fetched only at startup. No network is mocked
     * as successful: this verifies UI request coverage, not FTP behavior. */
    reset();
    assert(scanner_ui_begin()==1);
    ScannerUiEntry long_library[12]={};
    char long_ids[12][10]={};
    for(unsigned i=0;i<12;++i) {
        std::snprintf(long_ids[i],10,"CUSA%05u",i);
        long_library[i].cusa=long_ids[i];
        long_library[i].title="Read-only host entry";
        long_library[i].version="01.00";
        long_library[i].category="gd";
        long_library[i].install_state=-1;
    }
    test_cover_scroll=true;
    scanner_ui_set_cover_tick(cover_scroll_mock_tick);
    scanner_ui_results(long_library,12,12,0,1,1);
    scanner_ui_set_cover_tick(nullptr);
    assert(saw_cover_beyond_eight && pad_reads>=18);
    scanner_ui_shutdown(trace);
    assert(allocated==0 && video_closed==1 && pad_closed==1);

    /* Edge inventory sizes must be rendered by the real native UI
     * without heap corruption, false installed-state claims or leaks:
     * zero titles, exactly 256 titles, and a hostile oversized count.
     * These are synthetic HOST records, not detected games. */
    reset();
    test_capacity_screen=true;
    assert(scanner_ui_begin()==1);
    scanner_ui_results(nullptr,0,0,0,0,0);
    assert(saw_empty_library_panel &&
           !saw_capacity_sixth_row && pad_reads>=2);
    scanner_ui_shutdown(trace);
    assert(allocated==0 && video_closed==1 && pad_closed==1);

    static ScannerUiEntry capacity_entries[UI_MAX_TITLES];
    static char capacity_ids[UI_MAX_TITLES][10];
    for(unsigned i=0;i<UI_MAX_TITLES;++i) {
        std::snprintf(capacity_ids[i],10,"CUSA%05u",i);
        capacity_entries[i].cusa=capacity_ids[i];
        capacity_entries[i].title=i%2?"لعبة PS4":"PS4 لعبة";
        capacity_entries[i].version="01.00";
        capacity_entries[i].category="gd";
        capacity_entries[i].metadata_verified=1;
        capacity_entries[i].install_state=-1;
    }
    reset();
    test_capacity_screen=true;
    assert(scanner_ui_begin()==1);
    /* The UI must cap an inconsistent count BEFORE traversing its array. */
    scanner_ui_results(capacity_entries,UI_MAX_TITLES+1u,
                       UI_MAX_TITLES+1u,0,1,1);
    assert(saw_capacity_sixth_row && pad_reads>=2);
    scanner_ui_shutdown(trace);
    assert(allocated==0 && video_closed==1 && pad_closed==1);

    /* Actual native Pad-to-VideoOut mapping: R1/L1 must traverse every
     * sidebar page with zero scanned games and never invent metadata. */
    reset();
    test_circular_tabs=true;
    assert(scanner_ui_begin()==1);
    scanner_ui_results(nullptr,0,0,1,0,0);
    assert(seen_circular_tabs==0xFFFFu && pad_reads>=34);
    scanner_ui_shutdown(trace);
    assert(allocated==0 && video_closed==1 && pad_closed==1);

    /* Real C++ framebuffer and Pad mock: search -> keyboard last row ->
     * focus first match -> real Details -> return to same focused match.
     * This does not simulate installation, network, or device firmware. */
    reset();
    test_search_result_open=true;
    assert(scanner_ui_begin()==1);
    scanner_ui_results(entries,3,3,0,1,1);
    assert(saw_search_focus && saw_search_details && saw_search_return);
    assert(pad_reads>=20);
    scanner_ui_shutdown(trace);
    assert(allocated==0 && video_closed==1 && pad_closed==1);

    /* Real Pad + native VideoOut regression: a no-match search must
     * not permanently hide the main library on return to Home. */
    reset();
    test_search_restore_home=true;
    assert(scanner_ui_begin()==1);
    scanner_ui_results(entries,3,3,0,1,1);
    assert(saw_search_no_match && saw_home_restored &&
           saw_restored_details && pad_reads>=14);
    scanner_ui_shutdown(trace);
    assert(allocated==0 && video_closed==1 && pad_closed==1);

    /* Populate a fixture only for host tests, after scanner_ui_begin()
     * resets the model. Production has no provider yet and shows zero. */
    reset();
    verify_jobs = true;
    assert(scanner_ui_begin() == 1);
    UiDownloadInfo jobs[7] = {};
    for(unsigned i=0;i<7;++i) {
        std::snprintf(jobs[i].cusa,sizeof jobs[i].cusa,"CUSA%05u",i+1);
        std::snprintf(jobs[i].title,sizeof jobs[i].title,"HOST FIXTURE %u",i);
        std::strcpy(jobs[i].version,"01.02");
        jobs[i].bytes_total=1024;
        jobs[i].bytes_done=512;
        jobs[i].status=UI_JOB_DOWNLOADING;
    }
    assert(ui_model_set_downloads(jobs,7) == 0);
    UiUpdateInfo update = {};
    std::strcpy(update.cusa,"CUSA02343");
    std::strcpy(update.version,"01.09");
    update.verdict=UI_UPDATE_UNKNOWN;
    assert(ui_model_set_updates(&update,1) == 0);
    scanner_ui_results(entries,3,2,0,1,1);
    assert(saw_job_progress);
    assert(saw_job_scroll);
    assert(saw_update_row);
    scanner_ui_shutdown(trace);
    assert(allocated==0 && video_closed==1 && pad_closed==1);

    /* Failed FTP result must preserve the previous cache on screen,
     * with disabled installs and a visible stale-data warning. */
    reset();
    assert(scanner_ui_begin()==1);
    assert(scanner_ui_preview_snapshot(cache_entries,6)==1);
    check_offline_cache=true;
    scanner_ui_results(nullptr,0,0,1,0,0);
    check_offline_cache=false;
    assert(saw_offline_cache && saw_stale_details_warning);
    scanner_ui_shutdown(trace);
    assert(allocated==0 && video_closed==1 && pad_closed==1);

    /* Regression: a partial scan may return valid SFO entries and still
     * miss other games. Preserve all six cached base-game rows instead
     * of replacing the library with a shorter, incomplete result. */
    reset();
    assert(scanner_ui_begin()==1);
    assert(scanner_ui_preview_snapshot(cache_entries,6)==1);
    check_offline_cache=true;
    scanner_ui_results(entries,3,2,1,0,0);
    check_offline_cache=false;
    assert(saw_offline_cache && saw_stale_details_warning);
    scanner_ui_shutdown(trace);
    assert(allocated==0 && video_closed==1 && pad_closed==1);

    /* The scan worker is incomplete for eight input cycles: cached details
     * and settings must remain reachable on the VideoOut thread. */
    reset();
    assert(scanner_ui_begin()==1);
    assert(scanner_ui_preview_snapshot(cache_entries,6)==1);
    test_async_scan=true;
    assert(scanner_ui_wait_for_scan(&mock_scan_status)==0);
    assert(saw_scan_details && saw_scanning_details_warning &&
           saw_scan_settings && pad_reads>=8);
    test_async_scan=false;
    scanner_ui_shutdown(trace);
    assert(allocated==0 && video_closed==1 && pad_closed==1);

    /* Pressing Circle on Home during a pending scan must return a cancel
     * request to the owner so it can signal abort and join the worker. */
    reset();
    assert(scanner_ui_begin()==1);
    assert(scanner_ui_preview_snapshot(cache_entries,6)==1);
    test_async_exit=true;
    assert(scanner_ui_wait_for_scan(&mock_scan_status)==1);
    assert(pad_reads>=2 && !(mock_scan_status&SCANNER_UI_SCAN_DONE));
    test_async_exit=false;
    scanner_ui_shutdown(trace);
    assert(allocated==0 && video_closed==1 && pad_closed==1);

    /* Direction-sensitive title rendering is tested on the genuine
     * C++ framebuffer, not a browser approximation. Missing glyphs for
     * other writing systems are NOT claimed to be supported. */
    ScannerUiEntry mixed_title={
        "CUSA22222","PS4 لعبة","01.00",1,-1,"gd"
    };
    reset();
    mixed_title_probe=1;
    assert(scanner_ui_begin()==1);
    scanner_ui_results(&mixed_title,1,1,0,1,1);
    assert(saw_ltr_title_left);
    scanner_ui_shutdown(trace);
    assert(allocated==0 && video_closed==1);

    mixed_title.title="لعبة PS4";
    reset();
    mixed_title_probe=2;
    assert(scanner_ui_begin()==1);
    scanner_ui_results(&mixed_title,1,1,0,1,1);
    assert(saw_rtl_title_right && saw_rtl_title_clear_left);
    scanner_ui_shutdown(trace);
    assert(allocated==0 && video_closed==1);

    /* Long game titles MUST remain visible and indicate truncation.
     * Both directions exercise the actual 351px native RTL table field.
     * The data below is synthetic host-only text, not retail game data. */
    ScannerUiEntry long_title={
        "CUSA22223",
        "WWW WIDE ENGLISH NAME WITH MANY CHAPTERS AND AN EXTRA "
        "VERY LONG EDITION INCLUDING MULTIPLE SEASONS",
        "01.00",1,-1,"gd"
    };
    reset();
    test_capacity_screen=true; /* Circle on second Pad sample */
    long_title_probe=1;
    assert(scanner_ui_begin()==1);
    scanner_ui_results(&long_title,1,1,0,1,1);
    assert(saw_long_title_left && saw_long_title_right);
    assert(long_title_no_bleed);
    scanner_ui_shutdown(trace);
    assert(allocated==0 && video_closed==1 && pad_closed==1);

    long_title.title=
        "لعبة المغامرات الطويلة جدًا الجزء الأول والإصدار "
        "الجديد مع مستويات إضافية وأسماء عربية طويلة للغاية";
    reset();
    test_capacity_screen=true;
    long_title_probe=2;
    assert(scanner_ui_begin()==1);
    scanner_ui_results(&long_title,1,1,0,1,1);
    assert(saw_long_title_left && saw_long_title_right);
    assert(long_title_no_bleed);
    scanner_ui_shutdown(trace);
    assert(allocated==0 && video_closed==1 && pad_closed==1);

    reset();
    fail_video = true;
    assert(scanner_ui_begin() == 0);
    scanner_ui_results(entries, 3, 2, 1, 0, 0);
    scanner_ui_shutdown(trace);
    assert(allocated == 0 && video_closed == 0);

    reset();
    fail_memory = true;
    assert(scanner_ui_begin() == 0);
    scanner_ui_shutdown(trace);
    assert(allocated == 0 && video_closed == 1);

    reset();
    fail_pad = true;
    assert(scanner_ui_begin() == 1);
    scanner_ui_results(entries, 3, 2, 0, 1, 1);
    scanner_ui_shutdown(trace);
    assert(allocated == 0 && pad_closed == 0);
    assert(sleep_count >= 300);

    reset();
    stall_flip = true;
    assert(scanner_ui_begin() == 1);
    scanner_ui_progress("CUSA02343", 2, 3);
    scanner_ui_results(entries, 3, 2, 0, 1, 1);
    /* Flip latency in draw() must not force unsafe memory cleanup. */
    stall_flip = false;
    scanner_ui_shutdown(trace);
    assert(allocated == 0 && pad_closed == 1);
    assert(sleep_count >= 160);
    assert(unregister_count == 1 && retained_warning == 0);

    std::puts("UI_HOST_SMOKE_OK");
    return 0;
}
