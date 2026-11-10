/*
  Nintendo Switch audio driver for SDL3.

  Primary path: the audio renderer (audren), one 2-channel signed 16-bit voice at 48 kHz
  fed with a small ring of wave buffers. Fallback: the simple audout service (fixed
  48 kHz stereo S16, buffers aligned to 0x1000 bytes), modelled on SDL's N3DS driver.
*/
#include "SDL_internal.h"

#ifdef SDL_AUDIO_DRIVER_SWITCH

#include "../../../../subprojects/SDL3-3.4.18/src/audio/SDL_sysaudio.h"
#include <switch.h>
#include <malloc.h>

extern void switch_audio_diag_s16(const short *x, int frames);
extern int switch_audio_diag_test_direct(short *out, int frames);
extern void switch_audio_diag_underrun(int count);

#define SWITCH_NUM_BUFFERS 3
#define SWITCH_ALIGN 0x1000

struct SDL_PrivateAudioData
{
    Uint8 *mixbuf;
    bool renderer;
    // audout
    AudioOutBuffer out[SWITCH_NUM_BUFFERS];
    void *mem[SWITCH_NUM_BUFFERS];
    // audren
    AudioDriver drv;
    AudioDriverWaveBuf wave[SWITCH_NUM_BUFFERS];
    void *pool;
    bool drvOpen;
    bool everReady;
    u32 aligned;
    int next;
};

static bool RendererOpen(SDL_AudioDevice *device)
{
    static const AudioRendererConfig config = {
        .output_rate = AudioRendererOutputRate_48kHz,
        .num_voices = 1,
        .num_effects = 0,
        .num_sinks = 1,
        .num_mix_objs = 1,
        .num_mix_buffers = 2,
    };
    struct SDL_PrivateAudioData *h = device->hidden;
    static const u8 sink_channels[] = { 0, 1 };

    if (R_FAILED(audrenInitialize(&config))) {
        return false;
    }
    if (R_FAILED(audrvCreate(&h->drv, &config, 2))) {
        audrenExit();
        return false;
    }
    h->drvOpen = true;
    h->pool = memalign(SWITCH_ALIGN, (size_t)h->aligned * SWITCH_NUM_BUFFERS);
    if (!h->pool) {
        return false;
    }
    SDL_memset(h->pool, 0, (size_t)h->aligned * SWITCH_NUM_BUFFERS);
    armDCacheFlush(h->pool, (size_t)h->aligned * SWITCH_NUM_BUFFERS);
    const int mpid = audrvMemPoolAdd(&h->drv, h->pool, (size_t)h->aligned * SWITCH_NUM_BUFFERS);
    audrvMemPoolAttach(&h->drv, mpid);
    audrvDeviceSinkAdd(&h->drv, AUDREN_DEFAULT_DEVICE_NAME, 2, sink_channels);
    if (R_FAILED(audrvUpdate(&h->drv)) || R_FAILED(audrenStartAudioRenderer())) {
        return false;
    }
    audrvVoiceInit(&h->drv, 0, 2, PcmFormat_Int16, 48000);
    audrvVoiceSetDestinationMix(&h->drv, 0, AUDREN_FINAL_MIX_ID);
    audrvVoiceSetMixFactor(&h->drv, 0, 1.0f, 0, 0);
    audrvVoiceSetMixFactor(&h->drv, 0, 1.0f, 1, 1);
    for (int i = 0; i < SWITCH_NUM_BUFFERS; ++i) {
        AudioDriverWaveBuf *w = &h->wave[i];
        SDL_zerop(w);
        w->data_raw = (Uint8 *)h->pool + (size_t)i * h->aligned;
        w->size = (u64)device->buffer_size;
        w->start_sample_offset = 0;
        w->end_sample_offset = device->buffer_size / 4;
        audrvVoiceAddWaveBuf(&h->drv, 0, w);
    }
    audrvVoiceStart(&h->drv, 0);
    audrvUpdate(&h->drv);
    return true;
}

static void RendererClose(struct SDL_PrivateAudioData *h)
{
    if (h->drvOpen) {
        audrvVoiceStop(&h->drv, 0);
        audrvUpdate(&h->drv);
        audrvClose(&h->drv);
        audrenExit();
        h->drvOpen = false;
    }
    free(h->pool);
    h->pool = NULL;
}

static bool AudoutOpen(SDL_AudioDevice *device);
static bool SWITCHAUDIO_WaitDevice(SDL_AudioDevice *device);

static bool SWITCHAUDIO_OpenDevice(SDL_AudioDevice *device)
{
    device->hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*device->hidden));
    if (!device->hidden) {
        return false;
    }
    // The service dictates the format.
    device->spec.freq = 48000;
    device->spec.channels = 2;
    device->spec.format = SDL_AUDIO_S16;
    SDL_UpdatedAudioDeviceFormat(device);

    device->hidden->aligned = ((u32)device->buffer_size + SWITCH_ALIGN - 1) & ~(SWITCH_ALIGN - 1);
    device->hidden->mixbuf = (Uint8 *)SDL_malloc(device->buffer_size);
    if (!device->hidden->mixbuf) {
        return false;
    }
    SDL_memset(device->hidden->mixbuf, device->silence_value, device->buffer_size);

    if (RendererOpen(device)) {
        device->hidden->renderer = true;
        printf("audio: using audren (audio renderer), buffer %d bytes x %d\n", device->buffer_size, SWITCH_NUM_BUFFERS);
        return true;
    }
    RendererClose(device->hidden);
    printf("audio: audren unavailable, falling back to audout\n");
    return AudoutOpen(device);
}

static bool AudoutOpen(SDL_AudioDevice *device)
{
    if (R_FAILED(audoutInitialize()) || R_FAILED(audoutStartAudioOut())) {
        return SDL_SetError("audout initialization failed");
    }
    for (int i = 0; i < SWITCH_NUM_BUFFERS; ++i) {
        void *mem = memalign(SWITCH_ALIGN, device->hidden->aligned);
        if (!mem) {
            return SDL_OutOfMemory();
        }
        SDL_memset(mem, 0, device->hidden->aligned);
        armDCacheFlush(mem, device->hidden->aligned);
        device->hidden->mem[i] = mem;
        device->hidden->out[i].next = NULL;
        device->hidden->out[i].buffer = mem;
        device->hidden->out[i].buffer_size = device->hidden->aligned;
        device->hidden->out[i].data_size = (u64)device->buffer_size;
        device->hidden->out[i].data_offset = 0;
        // Prime the queue with silence so the first WaitDevice has something to wait on.
        audoutAppendAudioOutBuffer(&device->hidden->out[i]);
    }
    return true;
}

static void PinOnce(void)
{
    static bool pinned = false;
    if (!pinned) {
        // keep the mixer off the cores the main and render threads use
        svcSetThreadCoreMask(CUR_THREAD_HANDLE, 2, 1u << 2);
        pinned = true;
    }
}

static inline bool WaveReady(const AudioDriverWaveBuf *w)
{
    return w->state == AudioDriverWaveBufState_Done || w->state == AudioDriverWaveBufState_Free;
}

static void WaveSubmit(struct SDL_PrivateAudioData *h, int i, const void *src, int len)
{
    AudioDriverWaveBuf *w = &h->wave[i];
    if (src) {
        SDL_memcpy((Uint8 *)h->pool + (size_t)i * h->aligned, src, len);
    } else {
        SDL_memset((Uint8 *)h->pool + (size_t)i * h->aligned, 0, h->aligned);
    }
    armDCacheFlush((Uint8 *)h->pool + (size_t)i * h->aligned, h->aligned);
    w->size = (u64)len;
    w->start_sample_offset = 0;
    w->end_sample_offset = len / 4;
    audrvVoiceAddWaveBuf(&h->drv, 0, w);
}

static bool WaitRenderer(SDL_AudioDevice *device)
{
    struct SDL_PrivateAudioData *h = device->hidden;
    u64 start = armGetSystemTick(), lastLog = start;
    int iter = 0;
    while (!SDL_GetAtomicInt(&device->shutdown)) {
        const Result ur = audrvUpdate(&h->drv);
        const u64 now = armGetSystemTick();
        if (!h->everReady && (iter < 6 || now - lastLog > 19200000ULL)) {
            lastLog = now;
            printf("audio: audren wait iter=%d update=0x%x states=%d/%d/%d played=%u drops=%u playing=%d\n", iter, (unsigned)ur,
                   (int)h->wave[0].state, (int)h->wave[1].state, (int)h->wave[2].state,
                   audrvVoiceGetPlayedSampleCount(&h->drv, 0), audrvVoiceGetVoiceDropsCount(&h->drv, 0),
                   (int)audrvVoiceIsPlaying(&h->drv, 0));
        }
        ++iter;
        if (!h->everReady && now - start > 2ULL * 19200000ULL) {
            // the renderer never played anything: go back to the simple service
            printf("audio: audren produced no output in 2 s, falling back to audout\n");
            RendererClose(h);
            h->renderer = false;
            h->next = 0;
            return AudoutOpen(device) ? SWITCHAUDIO_WaitDevice(device) : true;
        }
        int n = 0;
        while (n < SWITCH_NUM_BUFFERS && WaveReady(&h->wave[(h->next + n) % SWITCH_NUM_BUFFERS])) {
            ++n;
        }
        if (n > 0) {
            h->everReady = true;
            switch_audio_diag_underrun(n - 1);
            // After a stall several buffers come back at once; keep the queue depth with silence.
            for (int k = 1; k < n; ++k) {
                const int i = h->next;
                h->next = (i + 1) % SWITCH_NUM_BUFFERS;
                WaveSubmit(h, i, NULL, device->buffer_size);
            }
            audrvUpdate(&h->drv);
            return true;
        }
        audrenWaitFrame();
    }
    return true;
}

static bool SWITCHAUDIO_WaitDevice(SDL_AudioDevice *device)
{
    AudioOutBuffer *released = NULL;
    u32 count = 0;
    PinOnce();
    if (device->hidden->renderer) {
        return WaitRenderer(device);
    }
    // Wake regularly so shutdown is noticed.
    while (!SDL_GetAtomicInt(&device->shutdown)) {
        if (R_SUCCEEDED(audoutWaitPlayFinish(&released, &count, 10000000ULL)) && count > 0) {
            switch_audio_diag_underrun((int)count - 1);
            // After a stall several buffers come back at once. PlayDevice refills one;
            // refill the rest with silence so the queue keeps its depth (otherwise it
            // shrinks to a single buffer and every later hiccup is audible).
            for (u32 k = 1; k < count; ++k) {
                const int i = device->hidden->next;
                device->hidden->next = (i + 1) % SWITCH_NUM_BUFFERS;
                SDL_memset(device->hidden->mem[i], 0, device->hidden->aligned);
                armDCacheFlush(device->hidden->mem[i], device->hidden->aligned);
                device->hidden->out[i].data_size = (u64)device->buffer_size;
                audoutAppendAudioOutBuffer(&device->hidden->out[i]);
            }
            return true;
        }
    }
    return true;
}

static bool SWITCHAUDIO_PlayDevice(SDL_AudioDevice *device, const Uint8 *buffer, int buflen)
{
    // Reuse the buffer the service just handed back, in submission order.
    const int i = device->hidden->next;
    device->hidden->next = (i + 1) % SWITCH_NUM_BUFFERS;
    if (device->hidden->renderer) {
        struct SDL_PrivateAudioData *h = device->hidden;
        Uint8 *dst = (Uint8 *)h->pool + (size_t)i * h->aligned;
        SDL_memcpy(dst, buffer, buflen);
        switch_audio_diag_test_direct((short *)dst, buflen / 4);
        switch_audio_diag_s16((const short *)dst, buflen / 4);
        armDCacheFlush(dst, h->aligned);
        h->wave[i].size = (u64)buflen;
        h->wave[i].start_sample_offset = 0;
        h->wave[i].end_sample_offset = buflen / 4;
        audrvVoiceAddWaveBuf(&h->drv, 0, &h->wave[i]);
        audrvUpdate(&h->drv);
        return true;
    }
    SDL_memcpy(device->hidden->mem[i], buffer, buflen);
    switch_audio_diag_test_direct((short *)device->hidden->mem[i], buflen / 4);
    switch_audio_diag_s16((const short *)device->hidden->mem[i], buflen / 4);
    // The audio service reads the buffer by DMA, not through the CPU cache.
    armDCacheFlush(device->hidden->mem[i], device->hidden->aligned);
    device->hidden->out[i].data_size = (u64)buflen;
    audoutAppendAudioOutBuffer(&device->hidden->out[i]);
    return true;
}

static Uint8 *SWITCHAUDIO_GetDeviceBuf(SDL_AudioDevice *device, int *buffer_size)
{
    return device->hidden->mixbuf;
}

static void SWITCHAUDIO_CloseDevice(SDL_AudioDevice *device)
{
    if (!device->hidden) {
        return;
    }
    if (device->hidden->renderer) {
        RendererClose(device->hidden);
    } else {
        audoutStopAudioOut();
        audoutExit();
        for (int i = 0; i < SWITCH_NUM_BUFFERS; ++i) {
            free(device->hidden->mem[i]);
        }
    }
    SDL_free(device->hidden->mixbuf);
    SDL_free(device->hidden);
    device->hidden = NULL;
}

static bool SWITCHAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = SWITCHAUDIO_OpenDevice;
    impl->WaitDevice = SWITCHAUDIO_WaitDevice;
    impl->PlayDevice = SWITCHAUDIO_PlayDevice;
    impl->GetDeviceBuf = SWITCHAUDIO_GetDeviceBuf;
    impl->CloseDevice = SWITCHAUDIO_CloseDevice;
    impl->OnlyHasDefaultPlaybackDevice = true;
    impl->HasRecordingSupport = false;
    return true;
}

AudioBootStrap SWITCHAUDIO_bootstrap = {
    "switch", "Nintendo Switch audio driver", SWITCHAUDIO_Init, false, false
};

#endif // SDL_AUDIO_DRIVER_SWITCH
