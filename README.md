# VITAPAD


VitaPad allows you to use your PSVITA as a wireless PC controller. It supports Windows (both 32 and 64 bit) and Linux (port made by nyorem). In the future i'm planning to add also Mac Os X support and also add a proper fiddler for vJoy.

## Usage

* Install VPK file on PSVITA. VitaPad needs **Enable Unsafe Homebrew** turned on in HENkaku settings (like VitaShell), because it loads a small kernel module to capture the PS button.
* Open VitaPad on PSVITA
* Optional: [Install vJoy driver](https://github.com/njz3/vJoy/releases/download/v2.2.0.0/vJoySetup.2.2.0.signed.exe) on Windows PC for vJoy functionality. Need to set `VJOY_MODE` to 1 in windows.xml. Configure the vJoy device with at least 10 buttons to get L3/R3.
* Optional: [Install ViGEm driver](https://github.com/nefarius/ViGEmBus/releases) on Windows PC for controller emulation. Set `VIGEM_MODE` in windows.xml to 1 for a DualShock 4 or 2 for an Xbox 360 controller (see [Xbox 360 controller output](#xbox-360-controller-output)). Touchpad and motion (gyro) support need ViGEmBus 1.17 or newer; on older drivers set `VIGEM_EXTENDED` to 0.
* Open VitaPad on PC (before or after the Vita app, it keeps looking until the Vita shows up). On Windows, `VitaPad.exe` lives in the notification area (see [Windows tray app](#windows-tray-app)); `VitaPad-console.exe` is the command line version. It will find your Vita on the local network by itself and remember it for the next time (saved in `vita_ip.txt`). If it's never found (e.g. your network blocks broadcasts), pass the IP shown on PSVITA on the command line, `VitaPad 192.168.1.20`, or write it in `vita_ip.txt`.

If the connection drops (e.g. Wi-Fi hiccups or the Vita goes to sleep), the PC client releases every pressed input and reconnects automatically.

To avoid screen burn-in during long sessions, hold L + R + SELECT for 1 second on the Vita to turn the screen off (black). Do it again to turn it back on.

### Windows tray app

`VitaPad.exe` runs in the notification area (system tray). Click its icon for a small window with:

- the connection status: Vita IP, streaming or polling, packets per second, the Vita's battery, and what the input is sent as
- **Controller**: Xbox 360 controller or DualShock 4 (both need the [ViGEm driver](https://github.com/nefarius/ViGEmBus/releases)), keyboard and mouse, or vJoy. Switching applies straight away
- **Low-latency streaming**, **Gyro sensitivity** (DualShock 4) and **Start VitaPad with Windows**
- **Key mapping...** opens `windows.xml` for everything else, **3D viewer** opens the viewer below

Right-click the icon for the same settings and **Quit**. Closing the window keeps VitaPad running. Settings are saved in `windows.xml`, and changes made to that file by hand apply straight away too. If something's wrong (e.g. the ViGEm driver is missing), the window says so and the icon shows a notification. For `--monitor`, `--viewer` or `--poll`, use `VitaPad-console.exe`.

Run `VitaPad --viewer` to open a live 3D view of your Vita in the browser: it follows the Vita's orientation (gyro + accelerometer), lights up pressed buttons, moves the sticks and shows front and rear touches. It runs alongside the normal controller emulation, only listens on localhost (port 5050), and needs internet the first time to download the 3D engine.

Run `VitaPad --monitor` to see everything the Vita sends without emulating any input. See [TESTING.md](TESTING.md) for a full test checklist.

The PC client and the Vita app must be updated together: the new client will tell you if the Vita app is outdated.

## USB mode (no PC client)

VitaPad can also be a **USB controller**: in the remapping menu (hold L + R + START) set **Connection** to one of the USB options, then plug the Vita into the computer with its USB cable. There's nothing to install on the computer.

- **USB, as a DualShock 4** (default USB option): the computer sees a wired PS4 controller, with gyro, accelerometer and touchpad (the Vita's rear touch). Steam, SDL games, macOS (GameController) and Linux (`hid-playstation`) recognize it natively; on Windows, games with DS4 support and Steam Input use it directly, and DS4Windows can turn it into an Xbox controller for everything else.
- **USB, as a standard gamepad**: a generic USB game controller with 14 buttons, a hat and two sticks, no motion or touchpad. Use it if something doesn't like the DualShock 4.
- Buttons follow the DualShock 4 layout (Square, Cross, Circle, Triangle, L1, R1, L2, R2, Share, Options, L3, R3, PS), with the D-pad as a hat switch and both sticks. Button remapping and the PS button settings apply.
- Vita L/R are L2/R2 and the front touchscreen corners are L1/R1 (top) and L3/R3 (bottom), like the ViGEm default.
- Needs Enable Unsafe Homebrew (same kernel module as the PS button). Not available on PS TV (no USB device port).
- While USB mode is on, the Vita's USB file transfer is off. Leaving VitaPad for more than 2 seconds gives USB back to the system; coming back turns USB mode on again.

Thanks to xerpi's [vitastick](https://github.com/xerpi/vitastick) for showing it can be done.

## Controls Mapping

### Remapping buttons on the Vita

Hold L + R + START for 1 second on the Vita to open the remapping menu. Up/Down picks a button, Left/Right changes what it sends, Triangle resets everything and START saves and closes. Each Vita button can send any other Vita button, nothing, or the DualShock 4 buttons the Vita doesn't have: L1, R1, L3 and R3 (e.g. SELECT = L3). The PC receives no input while the menu is open.

The mapping is saved on the Vita (`ux0:data/VitaPad/remap.txt`), so it works with every PC mode and survives reinstalling the PC client. L1/R1/L3/R3 work in ViGEm and vJoy mode; in keyboard mode they press `KEY_L1`, `KEY_R1`, `KEY_L3`, `KEY_R3` from the XML file (Q, E, Z, X by default). They also work with a PS TV controller.

### PS button

The PS button is sent to the PC too (the DualShock 4 PS button in ViGEm mode). The "PS button" row of the remapping menu picks how you get back to the LiveArea:

- **Send to PC, double-tap for the LiveArea** (default)
- **Send to PC, hold for the LiveArea** (about 0.8 seconds)
- **Normal**: the PS button isn't sent to the PC and works as usual

While the PS button is sent to the PC the quick menu is locked too, so an accidental hold doesn't open it. The first time VitaPad starts it loads its kernel module and restarts itself once.

In vJoy mode the PS button is button 11 (needs a vJoy device with 11 or more buttons); in keyboard mode it presses `KEY_PS` (unmapped by default).

Thanks to TheOfficialFloW's [Adrenaline](https://github.com/TheOfficialFloW/Adrenaline) for the technique.

### Fast buttons

The Vita's normal controller API only refreshes the buttons once per frame (60 times a second). With **Buttons → Fast** in the remapping menu (the default), VitaPad reads the 12 buttons (face buttons, D-pad, L/R, Start, Select) straight from the Vita's system controller about 250 times a second, so a press reaches the PC or computer about 10 ms sooner on average, over Wi-Fi and USB alike. Sticks and touch still update 60 times a second. It needs Enable Unsafe Homebrew (same kernel module as the PS button); if a button ever disagrees with the normal API for too long, VitaPad falls back to the normal API for it. The main screen shows how much sooner the presses arrive.

### Streaming (low latency over Wi-Fi)

With the Vita app 1.9 or newer, the Vita **streams** its input to the PC client over UDP: every button change goes out straight away (together with fast buttons, about 10 ms sooner overall), and a packet lost on Wi-Fi costs one sample instead of a hiccup. It's on by default (`STREAM_MODE` in the XML file); set it to 0, or run `VitaPad --poll`, to poll the Vita over TCP like older versions. Older Vita apps are polled automatically. The client sends UDP to port 5001 on the Vita; the Vita's replies get through the PC firewall as answers, so nothing needs to be opened.

### PC key mapping

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
- `VIGEM_GYRO_SENSITIVITY`: gyroscope multiplier, 1.0 = real rotation speed (e.g. 0.5 for half as sensitive). Applies live when you save the file

### Xbox 360 controller output

Many PC games only support Xbox controllers (XInput) and ignore a DualShock 4 unless Steam Input or DS4Windows translates it. The client can create a virtual **Xbox 360 controller** instead, which works everywhere (no touchpad or motion: an Xbox 360 pad has neither).

- **Windows**: set `VIGEM_MODE` to 2 in windows.xml (needs the ViGEm driver). `VIGEM_FRONT_TOUCH`/`VIGEM_REAR_TOUCH` set to 1 and `VIGEM_SWAP_SHOULDERS` apply.
- **Linux**: set `UINPUT_MODE` to 1 in linux.xml. The client creates the controller through uinput, so it doesn't need X11 (works on Wayland too), but it needs write access to `/dev/uinput`. Installing Steam's `steam-devices` package usually grants it; otherwise add a udev rule and reload it (then log out and back in):

  ```
  echo 'KERNEL=="uinput", SUBSYSTEM=="misc", TAG+="uaccess", OPTIONS+="static_node=uinput"' | sudo tee /etc/udev/rules.d/60-vitapad-uinput.rules
  sudo udevadm control --reload-rules && sudo udevadm trigger
  ```

  If `/dev/uinput` doesn't exist, run `sudo modprobe uinput`. `UINPUT_FRONT_TOUCH`, `UINPUT_REAR_TOUCH` and `UINPUT_SWAP_SHOULDERS` work like the ViGEm options.

Mapping: Cross/Circle/Square/Triangle = A/B/X/Y (same positions), Vita L/R = LT/RT, front touch upper corners = LB/RB, lower corners = LS/RS (stick clicks), Start = Start, Select = Back, PS = Guide. Buttons remapped on the Vita to L1/R1/L3/R3 become LB/RB/LS/RS.

Known limitation (Windows): Steam doesn't see the Guide button of ViGEm's virtual Xbox 360 controller (whether "Steam Input for Xbox controllers" is on or off), so PS won't open Steam's menu in this mode, although other programs see it (e.g. button 16 on https://hardwaretester.com/gamepad). For Steam, use the DualShock 4 mode (`VIGEM_MODE` 1): Steam supports it fully, PS button, gyro and touchpad included, and translates it for every game. Xbox mode is for games outside Steam.

Default vJoy mapping for the front touchscreen: upper corners = LB/RB (buttons 5/6), lower corners = L3/R3 (buttons 9/10, only if the vJoy device has 10 or more buttons, otherwise the whole left/right halves are LB/RB).

## Footage

You can see the VitaPad in action thanks to this footage from koog k:

[![VitaPad v.1.0 - PPSSPP - PSVITA wireless PC controller -](http://img.youtube.com/vi/CQ5wUMOpXoM/0.jpg)](http://www.youtube.com/watch?v=CQ5wUMOpXoM "VitaPad v.1.0 - PPSSPP - PSVITA wireless PC controller -")
