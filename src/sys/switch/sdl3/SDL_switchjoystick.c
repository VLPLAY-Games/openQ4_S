/*
  Nintendo Switch (libnx HID) joystick driver for SDL3.

  Exposes the combined set of connected controllers (handheld Joy-Cons, a docked
  pair, or a Pro Controller) as one standard gamepad, which is how libnx's
  PadState presents them. Modelled on SDL's N3DS driver. Untested on hardware.
*/
#include "SDL_internal.h"

#ifdef SDL_JOYSTICK_SWITCH

#include "../../../../subprojects/SDL3-3.4.18/src/joystick/SDL_sysjoystick.h"
#include <switch.h>

enum {
    SW_BTN_A, SW_BTN_B, SW_BTN_X, SW_BTN_Y, SW_BTN_STICK_L, SW_BTN_STICK_R,
    SW_BTN_L, SW_BTN_R, SW_BTN_ZL, SW_BTN_ZR, SW_BTN_PLUS, SW_BTN_MINUS,
    SW_BTN_LEFT, SW_BTN_UP, SW_BTN_RIGHT, SW_BTN_DOWN, SW_NUM_BUTTONS
};

static const u64 s_buttonMask[SW_NUM_BUTTONS] = {
    HidNpadButton_A, HidNpadButton_B, HidNpadButton_X, HidNpadButton_Y,
    HidNpadButton_StickL, HidNpadButton_StickR, HidNpadButton_L, HidNpadButton_R,
    HidNpadButton_ZL, HidNpadButton_ZR, HidNpadButton_Plus, HidNpadButton_Minus,
    HidNpadButton_Left, HidNpadButton_Up, HidNpadButton_Right, HidNpadButton_Down,
};

static PadState s_pad;
static u64 s_previousButtons;
static HidAnalogStickState s_previousSticks[2];

static bool SWITCH_JoystickInit(void)
{
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    padInitializeAny(&s_pad);
    SDL_PrivateJoystickAdded(1);
    return true;
}

static int SWITCH_JoystickGetCount(void) { return 1; }
static void SWITCH_JoystickDetect(void) {}
static bool SWITCH_JoystickIsDevicePresent(Uint16 vendor_id, Uint16 product_id, Uint16 version, const char *name) { return false; }
static const char *SWITCH_JoystickGetDeviceName(int device_index) { return "Nintendo Switch Controller"; }
static const char *SWITCH_JoystickGetDevicePath(int device_index) { return NULL; }
static int SWITCH_JoystickGetDeviceSteamVirtualGamepadSlot(int device_index) { return -1; }
static int SWITCH_JoystickGetDevicePlayerIndex(int device_index) { return -1; }
static void SWITCH_JoystickSetDevicePlayerIndex(int device_index, int player_index) {}
static SDL_GUID SWITCH_JoystickGetDeviceGUID(int device_index) { return SDL_CreateJoystickGUIDForName("Nintendo Switch Controller"); }
static SDL_JoystickID SWITCH_JoystickGetDeviceInstanceID(int device_index) { return device_index + 1; }

static bool SWITCH_JoystickOpen(SDL_Joystick *joystick, int device_index)
{
    joystick->nbuttons = SW_NUM_BUTTONS;
    joystick->naxes = 4;
    joystick->nhats = 0;
    return true;
}

static void SWITCH_SendStick(Uint64 timestamp, SDL_Joystick *joystick, int axisX, const HidAnalogStickState *now, const HidAnalogStickState *was)
{
    // libnx reports +/-32767 with +y up; SDL wants +y down.
    if (now->x != was->x) {
        SDL_SendJoystickAxis(timestamp, joystick, axisX, (Sint16)now->x);
    }
    if (now->y != was->y) {
        SDL_SendJoystickAxis(timestamp, joystick, axisX + 1, (Sint16)(now->y == -32768 ? 32767 : -now->y));
    }
}

static void SWITCH_JoystickUpdate(SDL_Joystick *joystick)
{
    const Uint64 timestamp = SDL_GetTicksNS();
    padUpdate(&s_pad);

    const u64 held = padGetButtons(&s_pad);
    const u64 changed = held ^ s_previousButtons;
    if (changed) {
        for (int i = 0; i < SW_NUM_BUTTONS; ++i) {
            if (changed & s_buttonMask[i]) {
                SDL_SendJoystickButton(timestamp, joystick, (Uint8)i, (held & s_buttonMask[i]) != 0);
            }
        }
    }
    s_previousButtons = held;

    const HidAnalogStickState left = padGetStickPos(&s_pad, 0);
    const HidAnalogStickState right = padGetStickPos(&s_pad, 1);
    SWITCH_SendStick(timestamp, joystick, 0, &left, &s_previousSticks[0]);
    SWITCH_SendStick(timestamp, joystick, 2, &right, &s_previousSticks[1]);
    s_previousSticks[0] = left;
    s_previousSticks[1] = right;
}

static void SWITCH_JoystickClose(SDL_Joystick *joystick) {}
static void SWITCH_JoystickQuit(void) {}

static bool SWITCH_JoystickRumble(SDL_Joystick *joystick, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble) { return SDL_Unsupported(); }
static bool SWITCH_JoystickRumbleTriggers(SDL_Joystick *joystick, Uint16 left_rumble, Uint16 right_rumble) { return SDL_Unsupported(); }
static bool SWITCH_JoystickSetLED(SDL_Joystick *joystick, Uint8 red, Uint8 green, Uint8 blue) { return SDL_Unsupported(); }
static bool SWITCH_JoystickSendEffect(SDL_Joystick *joystick, const void *data, int size) { return SDL_Unsupported(); }
static bool SWITCH_JoystickSetSensorsEnabled(SDL_Joystick *joystick, bool enabled) { return SDL_Unsupported(); }

static bool SWITCH_JoystickGetGamepadMapping(int device_index, SDL_GamepadMapping *out)
{
    // Nintendo layout: SDL's south/east buttons are the console's B/A.
    *out = (SDL_GamepadMapping){
        .a = { EMappingKind_Button, SW_BTN_B },
        .b = { EMappingKind_Button, SW_BTN_A },
        .x = { EMappingKind_Button, SW_BTN_Y },
        .y = { EMappingKind_Button, SW_BTN_X },
        .back = { EMappingKind_Button, SW_BTN_MINUS },
        .guide = { EMappingKind_None, 255 },
        .start = { EMappingKind_Button, SW_BTN_PLUS },
        .leftstick = { EMappingKind_Button, SW_BTN_STICK_L },
        .rightstick = { EMappingKind_Button, SW_BTN_STICK_R },
        .leftshoulder = { EMappingKind_Button, SW_BTN_L },
        .rightshoulder = { EMappingKind_Button, SW_BTN_R },
        .dpup = { EMappingKind_Button, SW_BTN_UP },
        .dpdown = { EMappingKind_Button, SW_BTN_DOWN },
        .dpleft = { EMappingKind_Button, SW_BTN_LEFT },
        .dpright = { EMappingKind_Button, SW_BTN_RIGHT },
        .misc1 = { EMappingKind_None, 255 },
        .right_paddle1 = { EMappingKind_None, 255 },
        .left_paddle1 = { EMappingKind_None, 255 },
        .right_paddle2 = { EMappingKind_None, 255 },
        .left_paddle2 = { EMappingKind_None, 255 },
        .leftx = { EMappingKind_Axis, 0 },
        .lefty = { EMappingKind_Axis, 1 },
        .rightx = { EMappingKind_Axis, 2 },
        .righty = { EMappingKind_Axis, 3 },
        .lefttrigger = { EMappingKind_Button, SW_BTN_ZL },
        .righttrigger = { EMappingKind_Button, SW_BTN_ZR },
    };
    return true;
}

SDL_JoystickDriver SDL_SWITCH_JoystickDriver = {
    SWITCH_JoystickInit,
    SWITCH_JoystickGetCount,
    SWITCH_JoystickDetect,
    SWITCH_JoystickIsDevicePresent,
    SWITCH_JoystickGetDeviceName,
    SWITCH_JoystickGetDevicePath,
    SWITCH_JoystickGetDeviceSteamVirtualGamepadSlot,
    SWITCH_JoystickGetDevicePlayerIndex,
    SWITCH_JoystickSetDevicePlayerIndex,
    SWITCH_JoystickGetDeviceGUID,
    SWITCH_JoystickGetDeviceInstanceID,
    SWITCH_JoystickOpen,
    SWITCH_JoystickRumble,
    SWITCH_JoystickRumbleTriggers,
    SWITCH_JoystickSetLED,
    SWITCH_JoystickSendEffect,
    SWITCH_JoystickSetSensorsEnabled,
    SWITCH_JoystickUpdate,
    SWITCH_JoystickClose,
    SWITCH_JoystickQuit,
    SWITCH_JoystickGetGamepadMapping
};

#endif // SDL_JOYSTICK_SWITCH
