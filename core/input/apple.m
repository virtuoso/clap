// SPDX-License-Identifier: Apache-2.0
#import <GameController/GameController.h>
#include "common.h"
#include "input-joystick.h"

#if __MAC_OS_X_VERSION_MIN_REQUIRED < 110000
#error "MacOS older than 11.0 is not supported"
#endif

static void apple_motion_poll(int i, GCController *c)
{
    joystick_set_motion(i, c.motion != nil);

    if (!c.motion)
        return;

    if (c.motion.sensorsRequireManualActivation)
        c.motion.sensorsActive = joystick_get_sensors_enabled(i);

    if (!joystick_get_sensors_enabled(i))
        return;

    float accel[3], gyro[3];
    quat attitude;
    float *paccel = NULL, *pgyro = NULL, *pattitude = NULL;

    /*
     * GCController separates gravity and user acceleration, but we
     * want the raw accelerometer data which includes both.
     */
    if (c.motion.hasGravityAndUserAcceleration) {
        accel[0] = c.motion.gravity.x + c.motion.userAcceleration.x;
        accel[1] = c.motion.gravity.y + c.motion.userAcceleration.y;
        accel[2] = c.motion.gravity.z + c.motion.userAcceleration.z;
        paccel = accel;
    }

    if (c.motion.hasRotationRate) {
        gyro[0] = c.motion.rotationRate.x;
        gyro[1] = c.motion.rotationRate.y;
        gyro[2] = c.motion.rotationRate.z;
        pgyro = gyro;
    }

    if (c.motion.hasAttitude) {
        attitude[0] = c.motion.attitude.x;
        attitude[1] = c.motion.attitude.y;
        attitude[2] = c.motion.attitude.z;
        attitude[3] = c.motion.attitude.w;
        pattitude = attitude;
    }

    if (paccel || pgyro || pattitude)
        joystick_motion_update(i, paccel, pgyro, pattitude);
}

void apple_input_poll(void)
{
    NSArray<GCController *> *controllers = [GCController controllers];

    for (int i = 0; i < NR_JOYS; i++) {
        if (i >= [controllers count]) {
            joystick_name_update(i, NULL);
            continue;
        }

        auto c = controllers[i];
        auto gp = c.extendedGamepad;
        if (!gp)    continue;

        joystick_name_update(i, [c.vendorName UTF8String]);

        float axes[CLAP_JOY_AXIS_COUNT];
        axes[CLAP_JOY_AXIS_LX] = gp.leftThumbstick.xAxis.value;
        axes[CLAP_JOY_AXIS_LY] = -gp.leftThumbstick.yAxis.value;
        axes[CLAP_JOY_AXIS_RX] = gp.rightThumbstick.xAxis.value;
        axes[CLAP_JOY_AXIS_RY] = -gp.rightThumbstick.yAxis.value;
        axes[CLAP_JOY_AXIS_LT] = gp.leftTrigger.value;
        axes[CLAP_JOY_AXIS_RT] = gp.rightTrigger.value;

        joystick_faxes_update(i, axes, array_size(axes));

        unsigned char buttons[CLAP_JOY_BTN_COUNT];
        memset(buttons, 0, sizeof(buttons));

        buttons[CLAP_JOY_BTN_A] = !!gp.buttonA.pressed;
        buttons[CLAP_JOY_BTN_B] = !!gp.buttonB.pressed;
        buttons[CLAP_JOY_BTN_X] = !!gp.buttonX.pressed;
        buttons[CLAP_JOY_BTN_Y] = !!gp.buttonY.pressed;
        buttons[CLAP_JOY_BTN_LB] = !!gp.leftShoulder.pressed;
        buttons[CLAP_JOY_BTN_RB] = !!gp.rightShoulder.pressed;
        
        buttons[CLAP_JOY_BTN_BACK] = !!gp.buttonOptions.pressed;
        buttons[CLAP_JOY_BTN_START] = !!gp.buttonMenu.pressed;
        buttons[CLAP_JOY_BTN_GUIDE] = !!gp.buttonHome.pressed;

        buttons[CLAP_JOY_BTN_LTHUMB] = !!gp.leftThumbstickButton.pressed;
        buttons[CLAP_JOY_BTN_RTHUMB] = !!gp.rightThumbstickButton.pressed;

        buttons[CLAP_JOY_BTN_DPAD_UP] = !!gp.dpad.up.pressed;
        buttons[CLAP_JOY_BTN_DPAD_RIGHT] = !!gp.dpad.right.pressed;
        buttons[CLAP_JOY_BTN_DPAD_DOWN] = !!gp.dpad.down.pressed;
        buttons[CLAP_JOY_BTN_DPAD_LEFT] = !!gp.dpad.left.pressed;

        /* Virtual buttons for triggers */
        buttons[CLAP_JOY_BTN_LT] = !!gp.leftTrigger.pressed;
        buttons[CLAP_JOY_BTN_RT] = !!gp.rightTrigger.pressed;

        GCControllerElement *e;
        e = [c.physicalInputProfile.elements objectForKey:@"Button Paddle1"];
        if (!e) e = [c.physicalInputProfile.elements objectForKey:@"Left Paddle"];
        if (e && [e isKindOfClass:[GCControllerButtonInput class]])
            buttons[CLAP_JOY_BTN_LBACK] = !!((GCControllerButtonInput *)e).pressed;

        e = [c.physicalInputProfile.elements objectForKey:@"Button Paddle2"];
        if (!e) e = [c.physicalInputProfile.elements objectForKey:@"Right Paddle"];
        if (e && [e isKindOfClass:[GCControllerButtonInput class]])
            buttons[CLAP_JOY_BTN_RBACK] = !!((GCControllerButtonInput *)e).pressed;

        joystick_buttons_update(i, buttons, array_size(buttons));

        apple_motion_poll(i, c);
    }
}
