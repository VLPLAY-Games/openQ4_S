/*
  Nintendo Switch (libnx audout) audio driver for SDL3.

  audout is the simple PCM output service: fixed 48 kHz, stereo, signed 16-bit,
  buffers aligned to 0x1000 bytes. Modelled on SDL's N3DS driver. Untested on
  hardware.
*/
#include "SDL_internal.h"

#ifdef SDL_AUDIO_DRIVER_SWITCH

#include "../../../../subprojects/SDL3-3.4.18/src/audio/SDL_sysaudio.h"
#include <switch.h>
#include <malloc.h>

#define SWITCH_NUM_BUFFERS 3
#define SWITCH_ALIGN 0x1000

struct SDL_PrivateAudioData
{
    Uint8 *mixbuf;
    AudioOutBuffer out[SWITCH_NUM_BUFFERS];
    void *mem[SWITCH_NUM_BUFFERS];
    u32 aligned;
    int next;
};

static bool SWITCHAUDIO_OpenDevice(SDL_AudioDevice *device)
{
    device->hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*device->hidden));
    if (!device->hidden) {
        return false;
    }
    if (R_FAILED(audoutInitialize()) || R_FAILED(audoutStartAudioOut())) {
        return SDL_SetError("audout initialization failed");
    }
    // The service dictates the format.
    device->spec.freq = (int)audoutGetSampleRate();
    device->spec.channels = (Uint8)audoutGetChannelCount();
    device->spec.format = SDL_AUDIO_S16;
    SDL_UpdatedAudioDeviceFormat(device);

    device->hidden->aligned = ((u32)device->buffer_size + SWITCH_ALIGN - 1) & ~(SWITCH_ALIGN - 1);
    device->hidden->mixbuf = (Uint8 *)SDL_malloc(device->buffer_size);
    if (!device->hidden->mixbuf) {
        return false;
    }
    SDL_memset(device->hidden->mixbuf, device->silence_value, device->buffer_size);
    for (int i = 0; i < SWITCH_NUM_BUFFERS; ++i) {
        void *mem = memalign(SWITCH_ALIGN, device->hidden->aligned);
        if (!mem) {
            return SDL_OutOfMemory();
        }
        SDL_memset(mem, 0, device->hidden->aligned);
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

static bool SWITCHAUDIO_WaitDevice(SDL_AudioDevice *device)
{
    AudioOutBuffer *released = NULL;
    u32 count = 0;
    static bool pinned = false;
    if (!pinned) {
        // keep the mixer off the cores the main and render threads use
        svcSetThreadCoreMask(CUR_THREAD_HANDLE, 2, 1u << 2);
        pinned = true;
    }
    // Wake regularly so shutdown is noticed.
    while (!SDL_GetAtomicInt(&device->shutdown)) {
        if (R_SUCCEEDED(audoutWaitPlayFinish(&released, &count, 10000000ULL)) && count > 0) {
            // After a stall several buffers come back at once. PlayDevice refills one;
            // refill the rest with silence so the queue keeps its depth (otherwise it
            // shrinks to a single buffer and every later hiccup is audible).
            for (u32 k = 1; k < count; ++k) {
                const int i = device->hidden->next;
                device->hidden->next = (i + 1) % SWITCH_NUM_BUFFERS;
                SDL_memset(device->hidden->mem[i], 0, device->hidden->aligned);
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
    SDL_memcpy(device->hidden->mem[i], buffer, buflen);
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
    audoutStopAudioOut();
    audoutExit();
    for (int i = 0; i < SWITCH_NUM_BUFFERS; ++i) {
        free(device->hidden->mem[i]);
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
