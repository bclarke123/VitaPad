# VITAPAD


VitaPad allows you to use your PSVITA as a wireless PC controller. It supports Windows (both 32 and 64 bit) and Linux (port made by nyorem). In the future i'm planning to add also Mac Os X support and also add a proper fiddler for vJoy.

## Usage

* Install VPK file on PSVITA
* Open VitaPad on PSVITA
* Optional: [Install vJoy driver](https://github.com/njz3/vJoy/releases/download/v2.2.0.0/vJoySetup.2.2.0.signed.exe) on Windows PC for vJoy functionality. Need to set `VJOY_MODE` to 1 in windows.xml. Configure the vJoy device with at least 10 buttons to get L3/R3.
* Optional: [Install ViGEm driver](https://github.com/nefarius/ViGEmBus/releases) on Windows PC for DualShock 4 emulation. Need to set `VIGEM_MODE` to 1 in windows.xml. Touchpad and motion (gyro) support need ViGEmBus 1.17 or newer; on older drivers set `VIGEM_EXTENDED` to 0.
* Open VitaPad on PC. It will find your Vita on the local network by itself and remember it for the next time (saved in `vita_ip.txt`). If it can't find it, insert the IP shown on PSVITA. You can also pass the IP on the command line: `VitaPad 192.168.1.20`.

If the connection drops (e.g. Wi-Fi hiccups or the Vita goes to sleep), the PC client releases every pressed input and reconnects automatically.

To avoid screen burn-in during long sessions, hold L + R + SELECT for 1 second on the Vita to turn the screen off (black). Do it again to turn it back on.

The PC client and the Vita app must be updated together: the new client will tell you if the Vita app is outdated.

## Controls Mapping

You can edit your controls mapping by editing the XML file inside the client folder (windows.xml / linux.xml)

Default mapping:

- DPAD = WASD
- Cross,Square,Triangle,Circle = IJKL
- L Trigger = Ctrl
- R Trigger = Spacebar
- Start = Enter
- Select = Shift
- Left Analog = DKeys
- Right Analog = 8,6,4,2
- Touchscreen = Mouse movement
- Retrotouch = Left/Right click

Default ViGEm mapping:

- PS button unmapped
- Select = Share
- Start = Options
- Left Front Touchscreen upper corner = L1, lower corner = L3
- Right Front Touchscreen upper corner = R1, lower corner = R3
- Rear Touchscreen = DS4 touchpad
- Left shoulder = L2
- Right shoulder = R2
- Gyroscope and accelerometer = DS4 motion sensors
- Rest of buttons are mapped to the expected buttons on DualShock 4

ViGEm options in windows.xml:

- `VIGEM_FRONT_TOUCH` / `VIGEM_REAR_TOUCH`: what each touch panel does. 0 = unused, 1 = buttons (upper corners L1/R1, lower corners L3/R3), 2 = DS4 touchpad, 3 = touching the panel clicks the DS4 touchpad
- `VIGEM_SWAP_SHOULDERS`: 1 = Vita L/R act as L1/R1 and the upper touch corners as L2/R2 (like PS4 Remote Play when used with `VIGEM_REAR_TOUCH` set to 1)
- `VIGEM_EXTENDED`: 1 = send touchpad and motion data (needs ViGEmBus 1.17+)
- `VIGEM_MOTION`: 1 = send gyroscope and accelerometer data

Default vJoy mapping for the front touchscreen: upper corners = LB/RB (buttons 5/6), lower corners = L3/R3 (buttons 9/10, only if the vJoy device has 10 or more buttons, otherwise the whole left/right halves are LB/RB).

## Footage

You can see the VitaPad in action thanks to this footage from koog k:

[![VitaPad v.1.0 - PPSSPP - PSVITA wireless PC controller -](http://img.youtube.com/vi/CQ5wUMOpXoM/0.jpg)](http://www.youtube.com/watch?v=CQ5wUMOpXoM "VitaPad v.1.0 - PPSSPP - PSVITA wireless PC controller -")
