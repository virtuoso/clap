// SPDX-License-Identifier: Apache-2.0
#import <GameController/GameController.h>
#include "common.h"
#include "input-joystick.h"

#if __MAC_OS_X_VERSION_MIN_REQUIRED < 101500
#error "MacOS older than 10.15 is not supported"
#endif

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

        float axes[6];
        axes[GLFW_GAMEPAD_AXIS_LEFT_X] = gp.leftThumbstick.xAxis.value;
        axes[GLFW_GAMEPAD_AXIS_LEFT_Y] = -gp.leftThumbstick.yAxis.value;
        axes[GLFW_GAMEPAD_AXIS_RIGHT_X] = gp.rightThumbstick.xAxis.value;
        axes[GLFW_GAMEPAD_AXIS_RIGHT_Y] = -gp.rightThumbstick.yAxis.value;
        axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] = gp.leftTrigger.value;
        axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] = gp.rightTrigger.value;

        joystick_faxes_update(i, axes, array_size(axes));

        unsigned char buttons[15];
        memset(buttons, 0, sizeof(buttons));

        buttons[GLFW_GAMEPAD_BUTTON_A] = !!gp.buttonA.pressed;
        buttons[GLFW_GAMEPAD_BUTTON_B] = !!gp.buttonB.pressed;
        buttons[GLFW_GAMEPAD_BUTTON_X] = !!gp.buttonX.pressed;
        buttons[GLFW_GAMEPAD_BUTTON_Y] = !!gp.buttonY.pressed;
        buttons[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER] = !!gp.leftShoulder.pressed;
        buttons[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER] = !!gp.rightShoulder.pressed;
        
        buttons[GLFW_GAMEPAD_BUTTON_BACK] = !!gp.buttonOptions.pressed;
        buttons[GLFW_GAMEPAD_BUTTON_START] = !!gp.buttonMenu.pressed;
        buttons[GLFW_GAMEPAD_BUTTON_GUIDE] = !!gp.buttonHome.pressed;

        buttons[GLFW_GAMEPAD_BUTTON_LEFT_THUMB] = !!gp.leftThumbstickButton.pressed;
        buttons[GLFW_GAMEPAD_BUTTON_RIGHT_THUMB] = !!gp.rightThumbstickButton.pressed;

        buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP] = !!gp.dpad.up.pressed;
        buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT] = !!gp.dpad.right.pressed;
        buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN] = !!gp.dpad.down.pressed;
        buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT] = !!gp.dpad.left.pressed;

        joystick_buttons_update(i, buttons, array_size(buttons));
    }
}
