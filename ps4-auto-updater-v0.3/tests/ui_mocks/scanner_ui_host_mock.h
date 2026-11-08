#ifndef SCANNER_UI_HOST_MOCK_H
#define SCANNER_UI_HOST_MOCK_H
/* Testing shims only; do not include this file in PS4 production build. */
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define ORBIS_VIDEO_USER_MAIN 0xFF
#define ORBIS_VIDEO_OUT_BUS_MAIN 0
#define ORBIS_VIDEO_OUT_FLIP_VSYNC 1
#define ORBIS_USER_SERVICE_USER_ID_INVALID (-1)
#define ORBIS_KERNEL_PRIO_FIFO_LOWEST 0x2FF
#define ORBIS_PAD_BUTTON_CIRCLE 0x2000
#define ORBIS_PAD_BUTTON_DOWN 0x0040
#define ORBIS_PAD_BUTTON_UP 0x0010

typedef struct OrbisVideoOutBufferAttribute {
    int32_t format, tiling, aspect;
    uint32_t width, height, pitch;
    uint64_t reserved[2];
} OrbisVideoOutBufferAttribute;

typedef struct OrbisVideoOutFlipStatus {
    uint64_t num, ptime, stime;
    int64_t flipArg;
    uint64_t reserved[2];
    int32_t numGpuFlipPending, numFlipPending, currentBuffer;
    uint32_t reserved1;
} OrbisVideoOutFlipStatus;

typedef struct OrbisPadData { uint32_t buttons; } OrbisPadData;
typedef struct OrbisUserServiceInitializeParams {
    uint32_t priority;
} OrbisUserServiceInitializeParams;

#ifdef __cplusplus
extern "C" {
#endif
int32_t sceUserServiceInitialize(void *);
int32_t sceUserServiceGetInitialUser(int32_t *);
int32_t sceVideoOutOpen(int32_t, int32_t, int32_t, const void *);
int32_t sceVideoOutClose(int32_t);
int32_t sceVideoOutUnregisterBuffers(int32_t, int32_t);
void sceVideoOutSetBufferAttribute(void *, uint32_t, uint32_t, uint32_t,
                                    uint32_t, uint32_t, uint32_t);
int32_t sceVideoOutRegisterBuffers(int32_t, int32_t, void * const *,
                                    int32_t, const OrbisVideoOutBufferAttribute *);
int32_t sceVideoOutSetFlipRate(int32_t, int32_t);
int32_t sceVideoOutSubmitFlip(int32_t, int32_t, uint32_t, int64_t);
int32_t sceVideoOutGetFlipStatus(int32_t, OrbisVideoOutFlipStatus *);
int32_t scePadInit(void);
int32_t scePadOpen(int32_t, int32_t, int32_t, void *);
int32_t scePadClose(int32_t);
int32_t scePadReadState(int32_t, OrbisPadData *);
size_t sceKernelGetDirectMemorySize(void);
int32_t sceKernelAllocateDirectMemory(off_t, off_t, size_t, size_t,
                                       int32_t, off_t *);
int32_t sceKernelMapDirectMemory(void **, size_t, int32_t, int32_t,
                                  off_t, size_t);
int32_t sceKernelMunmap(void *, size_t);
int32_t sceKernelReleaseDirectMemory(off_t, size_t);
int32_t sceKernelUsleep(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
