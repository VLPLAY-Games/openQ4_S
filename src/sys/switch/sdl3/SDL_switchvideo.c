/*
  Nintendo Switch (Horizon OS, libnx) video driver for SDL3.

  One fullscreen window backed by the system native window and presented through
  Mesa's nouveau EGL. Modelled on SDL's Vita and offscreen EGL drivers; the generic
  EGL work stays in SDL_egl.c. The resolution follows the console's operation mode
  (handheld 1280x720, docked 1920x1080).

  This file is compiled by tools/switch/gen_ninja.py into the Switch build only. It
  is not part of the stock SDL3 tree and is untested on hardware.
*/
#include "SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_SWITCH

#include "../../../../subprojects/SDL3-3.4.18/src/video/SDL_sysvideo.h"
#include "../../../../subprojects/SDL3-3.4.18/src/video/SDL_egl_c.h"
#include "../../../../subprojects/SDL3-3.4.18/src/events/SDL_events_c.h"
#include "../../../../subprojects/SDL3-3.4.18/src/events/SDL_keyboard_c.h"

#include <switch.h>

struct SDL_WindowData
{
    EGLSurface egl_surface;
};

static bool SWITCH_VideoInit(SDL_VideoDevice *_this);
static void SWITCH_VideoQuit(SDL_VideoDevice *_this);
static bool SWITCH_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID create_props);
static void SWITCH_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window);
static void SWITCH_PumpEvents(SDL_VideoDevice *_this);
static bool SWITCH_StartTextInput(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props);
static bool SWITCH_StopTextInput(SDL_VideoDevice *_this, SDL_Window *window);

// The engine asks for text input while a text field (save name, player name) has focus. The Switch
// has no keyboard, so the system software keyboard applet is opened from the event pump, and what
// was typed is delivered as text input.
static volatile bool s_keyboardRequested = false;

static bool SWITCH_GLES_LoadLibrary(SDL_VideoDevice *_this, const char *path)
{
    // libEGL is linked statically; SDL_VIDEO_STATIC_ANGLE makes SDL_egl.c call it directly.
    return SDL_EGL_LoadLibrary(_this, path, EGL_DEFAULT_DISPLAY, 0);
}

SDL_EGL_SwapWindow_impl(SWITCH)
SDL_EGL_MakeCurrent_impl(SWITCH)
SDL_EGL_CreateContext_impl(SWITCH)

static void SWITCH_Destroy(SDL_VideoDevice *device)
{
    SDL_free(device);
}

static SDL_VideoDevice *SWITCH_Create(void)
{
    SDL_VideoDevice *device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) {
        return NULL;
    }
    device->free = SWITCH_Destroy;
    device->VideoInit = SWITCH_VideoInit;
    device->VideoQuit = SWITCH_VideoQuit;
    device->CreateSDLWindow = SWITCH_CreateWindow;
    device->DestroyWindow = SWITCH_DestroyWindow;
    device->PumpEvents = SWITCH_PumpEvents;
    device->StartTextInput = SWITCH_StartTextInput;
    device->StopTextInput = SWITCH_StopTextInput;

    device->GL_LoadLibrary = SWITCH_GLES_LoadLibrary;
    device->GL_GetProcAddress = SDL_EGL_GetProcAddressInternal;
    device->GL_UnloadLibrary = SDL_EGL_UnloadLibrary;
    device->GL_CreateContext = SWITCH_GLES_CreateContext;
    device->GL_MakeCurrent = SWITCH_GLES_MakeCurrent;
    device->GL_SetSwapInterval = SDL_EGL_SetSwapInterval;
    device->GL_GetSwapInterval = SDL_EGL_GetSwapInterval;
    device->GL_SwapWindow = SWITCH_GLES_SwapWindow;
    device->GL_DestroyContext = SDL_EGL_DestroyContext;

    device->device_caps = VIDEO_DEVICE_CAPS_FULLSCREEN_ONLY;
    return device;
}

VideoBootStrap SWITCH_bootstrap = {
    "switch",
    "Nintendo Switch video driver",
    SWITCH_Create,
    NULL, // no message box
    false
};

static void SWITCH_QueryResolution(int *w, int *h)
{
    // 720p handheld, 1080p docked. appletGetOperationMode needs the applet service
    // that libnx's default crt0 initializes.
    if (appletGetOperationMode() == AppletOperationMode_Console) {
        *w = 1920;
        *h = 1080;
    } else {
        *w = 1280;
        *h = 720;
    }
}

static bool SWITCH_VideoInit(SDL_VideoDevice *_this)
{
    SDL_DisplayMode mode;
    SDL_zero(mode);
    SWITCH_QueryResolution(&mode.w, &mode.h);
    mode.refresh_rate = 60.0f;
    mode.format = SDL_PIXELFORMAT_ABGR8888;
    if (SDL_AddBasicVideoDisplay(&mode) == 0) {
        return false;
    }
    return true;
}

static void SWITCH_VideoQuit(SDL_VideoDevice *_this)
{
}

static bool SWITCH_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID create_props)
{
    SDL_WindowData *data = (SDL_WindowData *)SDL_calloc(1, sizeof(*data));
    if (!data) {
        return false;
    }
    window->internal = data;

    // The Switch presents one fullscreen surface at the console's current size.
    SWITCH_QueryResolution(&window->floating.w, &window->floating.h);
    window->w = window->floating.w;
    window->h = window->floating.h;
    window->flags |= SDL_WINDOW_FULLSCREEN;

    if (window->flags & SDL_WINDOW_OPENGL) {
        NWindow *native = nwindowGetDefault();
        nwindowSetDimensions(native, (u32)window->w, (u32)window->h);
        if (!_this->egl_data) {
            if (!SDL_GL_LoadLibrary(NULL)) {
                return false;
            }
        }
        data->egl_surface = SDL_EGL_CreateSurface(_this, window, (NativeWindowType)native);
        if (data->egl_surface == EGL_NO_SURFACE) {
            return SDL_SetError("Could not create EGL window surface");
        }
    }

    SDL_SetKeyboardFocus(window);
    return true;
}

static void SWITCH_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_WindowData *data = window->internal;
    if (data) {
        if (data->egl_surface != EGL_NO_SURFACE) {
            SDL_EGL_DestroySurface(_this, data->egl_surface);
        }
        SDL_free(data);
        window->internal = NULL;
    }
}

static bool SWITCH_StartTextInput(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props)
{
    s_keyboardRequested = true;
    return true;
}

static bool SWITCH_StopTextInput(SDL_VideoDevice *_this, SDL_Window *window)
{
    s_keyboardRequested = false;
    return true;
}

static void SWITCH_ShowKeyboard(void)
{
    SwkbdConfig kbd;
    char text[256];

    SDL_memset(text, 0, sizeof(text));
    if (R_FAILED(swkbdCreate(&kbd, 0))) {
        return;
    }
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetHeaderText(&kbd, "openQ4");
    swkbdConfigSetGuideText(&kbd, "Type the text and confirm");
    swkbdConfigSetStringLenMax(&kbd, 64);
    swkbdConfigSetStringLenMin(&kbd, 0);
    if (R_SUCCEEDED(swkbdShow(&kbd, text, sizeof(text))) && text[0]) {
        SDL_SendKeyboardText(text);
        // confirming the keyboard confirms the field (save name, player name)
        SDL_SendKeyboardKey(0, SDL_GLOBAL_KEYBOARD_ID, 0, SDL_SCANCODE_RETURN, true);
        SDL_SendKeyboardKey(0, SDL_GLOBAL_KEYBOARD_ID, 0, SDL_SCANCODE_RETURN, false);
    }
    swkbdClose(&kbd);
}

static void SWITCH_PumpEvents(SDL_VideoDevice *_this)
{
    if (s_keyboardRequested) {
        // once per request: asking again needs the field to lose and regain focus. Returning from the
        // keyboard applet can restart text input at once, so ignore requests right after one closed.
        static u64 lastClosed = 0;
        s_keyboardRequested = false;
        if (lastClosed == 0 || armTicksToNs(armGetSystemTick() - lastClosed) > 1500000000ULL) {
            SWITCH_ShowKeyboard();
            lastClosed = armGetSystemTick();
        }
    }
    // appletMainLoop services HOME/sleep messages and returns false once the
    // system asks the application to exit.
    if (!appletMainLoop()) {
        SDL_SendQuit();
    }
}

#endif // SDL_VIDEO_DRIVER_SWITCH
