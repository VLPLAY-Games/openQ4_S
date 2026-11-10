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

extern void switch_audio_diag_pad(int comboDown);
extern void switch_audio_diag_pad_test(int comboDown);
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

/* Motion: the gyroscope of the console (handheld) or of the connected controller, handed to SDL as
   a gyro sensor. The engine turns it into look input (in_gyro); it reads the yaw rate from sensor
   value 2 (clockwise positive) and the pitch rate from value 0 (looking down positive), in radians per
   second, so the rates are mapped to that here. The Switch reports the angular velocity in turns per
   second and the acceleration in G, in a frame with X toward the top edge, Y to the left and Z out of
   the face (controller lying flat, face up: X forward). Yaw is the turn about the direction the
   accelerometer reports as up, so it works held upright (handheld) or flat (controller). */
#define SW_NUM_SENSOR_HANDLES 4
static HidSixAxisSensorHandle s_sensorHandles[SW_NUM_SENSOR_HANDLES];
static u64 s_sensorLastSample[SW_NUM_SENSOR_HANDLES];
static bool s_sensorsStarted;
static bool s_sensorsWanted;
static Uint64 s_sensorTimestamp;
static float s_upX = 1.0f, s_upY = 0.0f, s_upZ = 0.0f;

static void SWITCH_StartSensors(void)
{
    if (s_sensorsStarted) {
        return;
    }
    Result rc[SW_NUM_SENSOR_HANDLES], rcStart[SW_NUM_SENSOR_HANDLES];
    rc[0] = hidGetSixAxisSensorHandles(&s_sensorHandles[0], 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
    rc[1] = hidGetSixAxisSensorHandles(&s_sensorHandles[1], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey);
    rc[2] = hidGetSixAxisSensorHandles(&s_sensorHandles[2], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual);
    rc[3] = hidGetSixAxisSensorHandles(&s_sensorHandles[3], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyLeft);
    for (int i = 0; i < SW_NUM_SENSOR_HANDLES; ++i) {
        rcStart[i] = hidStartSixAxisSensor(s_sensorHandles[i]);
        s_sensorLastSample[i] = 0;
    }
    printf("gyro: handles rc=%x %x %x %x start rc=%x %x %x %x\n", (unsigned)rc[0], (unsigned)rc[1], (unsigned)rc[2], (unsigned)rc[3],
           (unsigned)rcStart[0], (unsigned)rcStart[1], (unsigned)rcStart[2], (unsigned)rcStart[3]);
    s_sensorsStarted = true;
}

static void SWITCH_StopSensors(void)
{
    if (!s_sensorsStarted) {
        return;
    }
    for (int i = 0; i < SW_NUM_SENSOR_HANDLES; ++i) {
        hidStopSixAxisSensor(s_sensorHandles[i]);
    }
    s_sensorsStarted = false;
}

static void SWITCH_SendMotion(Uint64 timestamp, SDL_Joystick *joystick)
{
    HidSixAxisSensorState states[8];
    static Uint64 lastReport;
    static float peak[3];
    static int sentSamples;
    const bool report = timestamp - lastReport > 3000000000ULL;
    for (int h = 0; h < SW_NUM_SENSOR_HANDLES; ++h) {
        const size_t count = hidGetSixAxisSensorStates(s_sensorHandles[h], states, 8);
        if (report && h == 0) {
            lastReport = timestamp;
            printf("gyro: polling, handles started=%d\n", (int)s_sensorsStarted);
        }
        if (report) {
            printf("gyro: handle %d count=%d attrs=%x sample=%llu gyro=%.3f %.3f %.3f acc=%.2f %.2f %.2f (sent %d, peak %.2f %.2f %.2f)\n", h, (int)count,
                   count ? states[0].attributes : 0, count ? (unsigned long long)states[0].sampling_number : 0ULL,
                   count ? states[0].angular_velocity.x : 0.0f, count ? states[0].angular_velocity.y : 0.0f, count ? states[0].angular_velocity.z : 0.0f,
                   count ? states[0].acceleration.x : 0.0f, count ? states[0].acceleration.y : 0.0f, count ? states[0].acceleration.z : 0.0f,
                   sentSamples, peak[0], peak[1], peak[2]);
        }
        if (count == 0 || !(states[0].attributes & HidSixAxisSensorAttribute_IsConnected)) {
            continue;
        }
        // newest first: walk the new samples oldest to newest
        int fresh = 0;
        while (fresh < (int)count && states[fresh].sampling_number > s_sensorLastSample[h]) {
            ++fresh;
        }
        if (s_sensorLastSample[h] == 0 && fresh > 1) {
            fresh = 1;      // first read: do not replay history
        }
        for (int i = fresh - 1; i >= 0; --i) {
            const HidSixAxisSensorState *st = &states[i];
            const float turn = 6.2831853f;
            const float gx = st->angular_velocity.x * turn;
            const float gy = st->angular_velocity.y * turn;
            const float gz = st->angular_velocity.z * turn;

            // follow the accelerometer's idea of up (slowly: it also feels the hand's own motion)
            const float ax = st->acceleration.x, ay = st->acceleration.y, az = st->acceleration.z;
            const float len = SDL_sqrtf(ax * ax + ay * ay + az * az);
            if (len > 0.5f && len < 1.6f) {
                s_upX += ((ax / len) - s_upX) * 0.05f;
                s_upY += ((ay / len) - s_upY) * 0.05f;
                s_upZ += ((az / len) - s_upZ) * 0.05f;
                const float ul = SDL_sqrtf(s_upX * s_upX + s_upY * s_upY + s_upZ * s_upZ);
                if (ul > 0.01f) {
                    s_upX /= ul;
                    s_upY /= ul;
                    s_upZ /= ul;
                }
            }

            float data[3];
            data[0] = gy;                                              // tilting the top edge away: looking down
            data[1] = 0.0f;
            data[2] = -(gx * s_upX + gy * s_upY + gz * s_upZ);         // turning right (clockwise seen from above)
            s_sensorTimestamp += st->delta_time;
            sentSamples++;
            if (SDL_fabsf(data[0]) > peak[0]) peak[0] = SDL_fabsf(data[0]);
            if (SDL_fabsf(data[2]) > peak[2]) peak[2] = SDL_fabsf(data[2]);
            SDL_SendJoystickSensor(timestamp, joystick, SDL_SENSOR_GYRO, s_sensorTimestamp, data, 3);
        }
        if (count > 0) {
            s_sensorLastSample[h] = states[0].sampling_number;
        }
        break;      // one source at a time: the first connected one in the list
    }
    if (report) {
        peak[0] = peak[1] = peak[2] = 0.0f;
        lastReport = timestamp;
    }
}

static bool SWITCH_JoystickOpen(SDL_Joystick *joystick, int device_index)
{
    joystick->nbuttons = SW_NUM_BUTTONS;
    joystick->naxes = 4;
    joystick->nhats = 0;
    SDL_PrivateJoystickAddSensor(joystick, SDL_SENSOR_GYRO, 200.0f);
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
    switch_audio_diag_pad((held & HidNpadButton_ZL) && (held & HidNpadButton_ZR) && (held & HidNpadButton_Minus));
    switch_audio_diag_pad_test((held & HidNpadButton_ZL) && (held & HidNpadButton_ZR) && (held & HidNpadButton_Plus));
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

    if (s_sensorsWanted) {
        SWITCH_StartSensors();
        SWITCH_SendMotion(timestamp, joystick);
    }
}

static void SWITCH_JoystickClose(SDL_Joystick *joystick) {}
static void SWITCH_JoystickQuit(void) {}

static bool SWITCH_JoystickRumble(SDL_Joystick *joystick, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble) { return SDL_Unsupported(); }
static bool SWITCH_JoystickRumbleTriggers(SDL_Joystick *joystick, Uint16 left_rumble, Uint16 right_rumble) { return SDL_Unsupported(); }
static bool SWITCH_JoystickSetLED(SDL_Joystick *joystick, Uint8 red, Uint8 green, Uint8 blue) { return SDL_Unsupported(); }
static bool SWITCH_JoystickSendEffect(SDL_Joystick *joystick, const void *data, int size) { return SDL_Unsupported(); }
static bool SWITCH_JoystickSetSensorsEnabled(SDL_Joystick *joystick, bool enabled)
{
    s_sensorsWanted = enabled;
    printf("gyro: sensors %s\n", enabled ? "enabled" : "disabled");
    if (!enabled) {
        SWITCH_StopSensors();
    }
    return true;
}

static bool SWITCH_JoystickGetGamepadMapping(int device_index, SDL_GamepadMapping *out)
{
    // Map by label, as every Switch port does: A confirms and B cancels, like the
    // console's own menus (the physical positions are mirrored on a Nintendo pad).
    *out = (SDL_GamepadMapping){
        .a = { EMappingKind_Button, SW_BTN_A },
        .b = { EMappingKind_Button, SW_BTN_B },
        .x = { EMappingKind_Button, SW_BTN_X },
        .y = { EMappingKind_Button, SW_BTN_Y },
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
