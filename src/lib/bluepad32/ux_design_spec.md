## Main Analog Control Mode

Pick between one of these

Most of these are meant for making it easy to control a robot

 * Flat Mapping: LX LY RX RY DpadX DpadY LTrig RTrig
 * Both Sticks Fully Automatic (uses the max function on axis values to figure out)
 * Both Sticks Direct Y Control (LY RY only)
 * Only Left Stick for Tank Drive
 * Only Right Stick for Tank Drive
 * Only D-pad for Tank Drive
 * Left Stick Throttle, Right Stick Steering
 * Right Stick Throttle, Left Stick Steering
 * Racing Game (Left Trigger Brake/Reverse, Right Trigger Accelerator, Left Stick Steering)
 * Racing Game (Left Trigger Brake/Reverse, Right Trigger Accelerator, Right Stick Steering)

## Auxiliary Channels

There are two of these. Each one can be assigned to any actual CRSF channel.

This is meant for aircraft throttle, or robot combat weapon throttle, or servos, etc.

The user can set a failsafe value.

The `START` button always makes channels go back to failsafe value

### Analog For Auxiliary

2x of these available

 * Default - Do Nothing, Uses Buttons
 * Left Stick Y Mapped Directly (failsafe must be about 1500)
 * Right Stick Y Mapped Directly (failsafe must be about 1500)
 * Left Stick Y Relative Changes (failsafe must be about 988)
 * Right Stick Y Relative Changes (failsafe must be about 988)
 * Left Trigger Mapped Directly (failsafe must be about 988)
 * Right Trigger Mapped Directly (failsafe must be about 988)
 * Left Trigger Lowers, Right Trigger Raises
 * Right Trigger Lowers, Left Trigger Raises

### Buttons for Auxiliary

10x of these are available (4 face buttons, 4 D-pad buttons, L1 and R1, ignore L3 and R3 and misc buttons)

 * Default - Do Nothing
 * Tap to Set and Latch
 * Set While Held, Release to Failsafe
 * Tap to Increment
 * Tap to Decrement

## Web Configuration User Interface

The top of the page should have controls for activating and deactivating pairing.

A list of paired devices is shown as BD addresses, it is updated every 3 seconds (or upon expected change). Each one can be deleted individually.

Then the next major section is the configuration editor

First item is `Main Analog Control Mode` via a drop-down

Then a table. 2x main rows each for each auxiliary channel. Columns: actual channel, failsafe value, drop-down for items listed in `Analog For Auxiliary`

Then another table, each row for each of the 10 available buttons. Columns: drop-down, number-box (either to choose a value to set or to choose a increment/decrement step size)

Then a field to show warnings. Upon any form value change, do a validation and place a warning in this field. For example, you can't have the Y stick used for both driving and a weapon throttle.

Each of the drop-down items sets some occupied flags for what gamepad elements they occupy. The sticks are consider one flag, not two because of having two axis.

The save button goes below the warning field.
