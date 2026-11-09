/*
  SDL2 audio entry points for devkitPro's OpenAL Soft on SDL3.

  The switch-openal-soft package outputs through its SDL2 backend. SDL3 exports
  functions with the same names but different signatures (SDL_OpenAudioDevice,
  SDL_CloseAudioDevice, SDL_PauseAudioDevice, SDL_GetAudioDeviceName), so the
  build renames OpenAL's references to SWITCH_SDL2_* (see tools/switch/gen_ninja.py)
  and this file answers them with SDL3 audio streams.
*/
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

typedef Uint16 SDL2_AudioFormat;
typedef void (*SDL2_AudioCallback)(void *userdata, Uint8 *stream, int len);
typedef struct SDL2_AudioSpec {
    int freq;
    SDL2_AudioFormat format;
    Uint8 channels, silence;
    Uint16 samples, padding;
    Uint32 size;
    SDL2_AudioCallback callback;
    void *userdata;
} SDL2_AudioSpec;

typedef struct {
    SDL_AudioStream *stream;
    SDL2_AudioCallback callback;
    void *userdata;
    Uint8 *scratch;
    int scratchSize;
    int frameBytes;
} Compat;

static Compat s_dev;

static SDL_AudioFormat FromSDL2(SDL2_AudioFormat f)
{
    switch (f) {
    case 0x0008: return SDL_AUDIO_U8;
    case 0x8008: return SDL_AUDIO_S8;
    case 0x8010: return SDL_AUDIO_S16LE;
    case 0x9010: return SDL_AUDIO_S16BE;
    case 0x8120: return SDL_AUDIO_F32LE;
    default: return SDL_AUDIO_S16;
    }
}

static void SDLCALL Pull(void *userdata, SDL_AudioStream *stream, int additional, int total)
{
    Compat *c = (Compat *)userdata;
    (void)total;
    while (additional > 0) {
        const int chunk = additional < c->scratchSize ? additional : c->scratchSize;
        memset(c->scratch, 0, chunk);
        c->callback(c->userdata, c->scratch, chunk);
        SDL_PutAudioStreamData(stream, c->scratch, chunk);
        additional -= chunk;
    }
}

int SWITCH_SDL2_GetNumAudioDevices(int iscapture) { return iscapture ? 0 : 1; }

const char *SWITCH_SDL2_GetAudioDeviceName(int index, int iscapture)
{
    return ( index == 0 && !iscapture ) ? "Nintendo Switch Audio" : NULL;
}

Uint32 SWITCH_SDL2_OpenAudioDevice(const char *device, int iscapture, const SDL2_AudioSpec *desired,
                                   SDL2_AudioSpec *obtained, int allowed_changes)
{
    SDL_AudioSpec spec;
    (void)device; (void)allowed_changes;
    if (iscapture || !desired || s_dev.stream) {
        return 0;
    }
    spec.format = FromSDL2(desired->format);
    spec.channels = desired->channels ? desired->channels : 2;
    spec.freq = desired->freq ? desired->freq : 48000;
    s_dev.callback = desired->callback;
    s_dev.userdata = desired->userdata;
    s_dev.frameBytes = SDL_AUDIO_BYTESIZE(spec.format) * spec.channels;
    s_dev.scratchSize = (desired->samples ? desired->samples : 1024) * s_dev.frameBytes;
    s_dev.scratch = (Uint8 *)malloc(s_dev.scratchSize);
    if (!s_dev.scratch) {
        return 0;
    }
    s_dev.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Pull, &s_dev);
    if (!s_dev.stream) {
        free(s_dev.scratch);
        s_dev.scratch = NULL;
        return 0;
    }
    if (obtained) {
        *obtained = *desired;
        obtained->freq = spec.freq;
        obtained->channels = (Uint8)spec.channels;
        obtained->size = (Uint32)s_dev.scratchSize;
    }
    return 2; // SDL2 device ids start at 2
}

void SWITCH_SDL2_PauseAudioDevice(Uint32 dev, int pause_on)
{
    (void)dev;
    if (!s_dev.stream) {
        return;
    }
    if (pause_on) {
        SDL_PauseAudioStreamDevice(s_dev.stream);
    } else {
        SDL_ResumeAudioStreamDevice(s_dev.stream);
    }
}

void SWITCH_SDL2_CloseAudioDevice(Uint32 dev)
{
    (void)dev;
    if (s_dev.stream) {
        SDL_DestroyAudioStream(s_dev.stream);
        s_dev.stream = NULL;
    }
    free(s_dev.scratch);
    s_dev.scratch = NULL;
}
