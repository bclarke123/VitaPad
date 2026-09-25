# Testing VitaPad

Two tools let you test each half on its own:

- `VitaPad --monitor` connects to the Vita and prints everything it sends (buttons, sticks, touches, gyro, battery, packets per second) without emulating any input. It tests the **Vita app** by itself.
- `tools/fake_vita.py` (Python 3) pretends to be a Vita on your PC. It plays the same input sequence every time, so it tests the **PC client** by itself. Run it on the same PC as the client, or on another machine on the network.

Only run one of the real Vita and the fake Vita at a time, or discovery will find both.

## 1. Vita app (real Vita + `--monitor`)

Install `VitaPad.vpk` on the Vita and open it.

| # | Do | Expect |
|---|----|--------|
| 1.1 | Open VitaPad with Wi-Fi off, then turn Wi-Fi on | "Waiting for Wi-Fi connection..." changes to the IP within a second or two |
| 1.2 | On the PC run `VitaPad --monitor` (delete `vita_ip.txt` first). Also try starting it before the Vita app | "Found Vita at ...", then "Connection established!", and the Vita shows "Connected!" |
| 1.3 | Press every button, one at a time | Its name appears at the end of the line |
| 1.4 | Move both sticks to each edge | Values reach about 0 and 255, rest near 128 |
| 1.5 | Touch the front with one finger, then two | `front:` shows one, then two positions (0-1919, 0-1087) |
| 1.6 | Same on the rear panel | `rear:` shows the positions |
| 1.7 | Lay the Vita flat, face up | `accel` about (0, 0, -1) |
| 1.8 | Stand it up facing you | `accel` about (0, -1, 0) |
| 1.9 | Rotate it left/right, tilt it forward/back | `gyro` values move away from 0 while rotating, back to ~0 when still |
| 1.10 | Watch `pkt/s` | Steady (a few hundred on good Wi-Fi) |
| 1.11 | Hold L + R + SELECT for 1 s, then again | Screen goes black, then comes back; monitor keeps printing throughout |
| 1.12 | Close the PC client, then start it again | Reconnects straight away using the saved IP ("Using last Vita IP") |
| 1.13 | With the client running, press PS and suspend the Vita, wait 10 s, wake it and reopen VitaPad | Client prints "Connection lost, reconnecting..." then reconnects on its own |
| 1.14 | Leave it connected without touching anything for longer than the Vita's auto-sleep time | Vita does not go to sleep, connection stays up |

## 2. PC client (fake Vita)

Run `python3 tools/fake_vita.py` and the client in another window.

| # | Do | Expect |
|---|----|--------|
| 2.1 | Delete `vita_ip.txt`, run `VitaPad --monitor` | Finds the fake Vita by discovery; buttons cycle every 0.5 s, sticks circle, `accel`/`gyro` rock back and forth |
| 2.2 | Run the fake Vita with `--drop-every 5` | Every 5 s: "Connection lost, reconnecting..." and back within about a second |
| 2.3 | Run the fake Vita with `--legacy` | Client says the Vita app is outdated and exits |
| 2.4 | Run it with `--no-discovery`, delete `vita_ip.txt` | Client says it's waiting for the Vita and keeps trying; running `VitaPad <IP>` instead connects |
| 2.4b | Start the client first, the fake Vita 10 s later | Client waits quietly, then finds and connects to the fake Vita on its own |
| 2.5 | Put a wrong IP in `vita_ip.txt` | Connecting to it fails, then discovery finds the right one and overwrites the file |
| 2.6 | Plain fake Vita | "Streaming input from the Vita (UDP)."; `stream` at about 190 pkt/s |
| 2.7 | Fake Vita with `--no-stream` | After about a second: "The Vita app doesn't stream input ... polling instead."; `poll` lines |
| 2.8 | Fake Vita with `--stream-loss 0.2` | Still streaming, fewer pkt/s, max gap around 10-20 ms, no reconnects |
| 2.9 | Fake Vita with `--drop-every 5` while streaming | "Connection lost, reconnecting..." then streaming again |

## 2b. 3D viewer (fake Vita or real Vita)

Run `VitaPad --viewer` (add `--monitor` to skip controller emulation). The browser opens http://localhost:5050/.

| # | Do | Expect |
|---|----|--------|
| 2b.1 | With the fake Vita | Buttons light up and press in one by one, sticks circle, the front touch glows in each corner, the rear touch slides (pink ring on the screen, glow on the back with B), the model rocks left/right |
| 2b.2 | With the real Vita: hold it in front of you and press R (Recenter) | Model faces you the same way the Vita does |
| 2b.3 | Tilt, turn and flip the Vita | Model follows without lag; when you stop moving it settles and doesn't keep drifting up/down |
| 2b.4 | Turn slowly for a minute | Some left/right drift is normal (no compass); R fixes it |
| 2b.5 | Stop the client | Page shows "VitaPad client closed" |

## 1b. Button remapping (real Vita + `--monitor`)

| # | Do | Expect |
|---|----|--------|
| 1b.1 | Hold L + R + START for 1 s | Remap menu opens; the monitor shows no buttons, centered sticks and no touches while it's open |
| 1b.2 | Map Cross to Circle, Circle to None, Select to L3; press START | Menu closes; main screen says "3 remapped" |
| 1b.3 | Press Cross, Circle, Select | Monitor shows CIRCLE, nothing, L3 |
| 1b.4 | Close and reopen VitaPad on the Vita | Mapping is kept (saved in `ux0:data/VitaPad/remap.txt`) |
| 1b.5 | Open the menu, press Triangle, then START | Everything back to normal, "0 remapped" |
| 1b.6 | In ViGEm mode, map a button to L1/R1/L3/R3 | The controller tester shows that DS4 button |

## 1c. PS button (real Vita + `--monitor`)

| # | Do | Expect |
|---|----|--------|
| 1c.1 | With Unsafe Homebrew on, open VitaPad for the first time after installing | It blinks (restarts once), then the main screen says "PS button: sent to the PC" |
| 1c.2 | Tap PS once | Monitor shows PS; the Vita stays in VitaPad (no LiveArea, no "blocked" icon) |
| 1c.3 | Double-tap PS | Monitor shows two PS taps and the Vita goes to the LiveArea; reopening VitaPad reconnects and single taps are captured again |
| 1c.4 | Hold PS for 2 s (double-tap mode) | Monitor shows PS held; no quick menu, no LiveArea |
| 1c.4b | Remap menu: "Send to PC, hold for the LiveArea"; tap PS, double-tap PS, then hold it | Taps and double-taps only reach the PC; holding about 0.8 s goes to the LiveArea (monitor stops showing PS when it triggers) |
| 1c.5 | Put the Vita to sleep and wake it, open VitaPad | Single taps are still captured (lock taken again after sleep) |
| 1c.6 | Remap menu: set "PS button" to Normal, save | PS goes to the LiveArea on a single tap and holding opens the quick menu again; setting kept after restarting VitaPad |
| 1c.7 | With Unsafe Homebrew off | VitaPad still starts; main screen says to enable Unsafe Homebrew, PS works normally |
| 1c.8 | In ViGEm mode, tap PS | The controller tester shows the PS button; Steam may open its overlay |

## 1d. USB mode (real Vita + computer, no PC client)

| # | Do | Expect |
|---|----|--------|
| 1d.1 | Remap menu: Connection → "USB, as a standard gamepad", START; plug the Vita into a computer | Main screen: "USB mode: connected"; the computer shows a new game controller named VitaPad (Windows: "Set up USB game controllers"; macOS: System Information → USB; Linux: `jstest` / `evtest`) |
| 1d.2 | Open https://hardwaretester.com/gamepad and press everything | 14 buttons in DualShock 4 order, D-pad as a hat/POV, both sticks; front touch corners = L1/R1/L3/R3 |
| 1d.3 | Remap a button, tap PS | Remap and PS button apply over USB too |
| 1d.4 | Open the remap menu while connected | The gamepad goes neutral |
| 1d.5 | Double-tap PS to go to the LiveArea, wait 3 s | Controller disappears from the computer; USB file transfer (e.g. VitaShell USB) works again |
| 1d.6 | Reopen VitaPad | Controller comes back within a second or two |
| 1d.7 | Connection → Wi-Fi | Controller disappears; the PC client works as before |
| 1d.8 | Try it in Steam and a game | Steam lists the controller (may ask to map it once); inputs reach the game |

## 1e. USB mode as a DualShock 4 (real Vita + computer, no PC client)

| # | Do | Expect |
|---|----|--------|
| 1e.1 | Remap menu: Connection → "USB, as a DualShock 4", START; plug the Vita in | Main screen: "USB mode: connected, the computer sees a DualShock 4" |
| 1e.2 | macOS: System Information → USB; Linux: `dmesg` | A USB device with vendor 054c, product 09cc (Sony DualShock 4); Linux loads `hid-playstation` (a touchpad and a motion sensors device appear in `evtest`) |
| 1e.3 | https://hardwaretester.com/gamepad (Chrome) | Recognized as a DualShock 4 / standard mapping: 17 buttons incl. PS and touchpad click, both sticks; L/R show as analog L2/R2 at 0 or 1 |
| 1e.4 | macOS: a game or app using GameController (e.g. a controller-supporting Arcade game), or Safari on the tester page | The controller shows as a DualShock 4 |
| 1e.5 | Steam > Settings > Controller: it lists a PS4 controller; open Test Device Inputs | Buttons, sticks, PS; rear touch moves on the touchpad; gyro view follows the Vita |
| 1e.6 | Gyro direction in Steam's gyro view: turn the Vita left/right, tilt it forward/back, roll it | Each moves the same way on screen. If one is mirrored, note which one |
| 1e.7 | Lay the Vita flat, face up, still | Gyro view stays level and doesn't drift |
| 1e.8 | Touch the rear with two fingers | Both show on the touchpad, in the right corners (rear top-left = touchpad top-left, seen from the front) |
| 1e.9 | A game with rumble or light bar (or SDL's `testcontroller`) | No hang or disconnect when the game sends rumble/light bar (they're ignored) |
| 1e.10 | Switch Connection between the two USB options while plugged in | Controller disappears and comes back as the other kind within a few seconds |
| 1e.11 | Double-tap PS to the LiveArea, wait 3 s, then reopen VitaPad | USB file transfer works while away; the DualShock 4 comes back |

## 1f. Input update rates and fast buttons (real Vita)

The bottom of the Vita's main screen shows how often the Vita really updates its inputs (counted from the timestamps of new samples) and, while a PC client is connected, how many of its polls per second carried new button/stick data.

| # | Do | Expect / note down |
|---|----|--------|
| 1f.1 | Open VitaPad, don't touch anything | "Input updates/s": note the buttons & sticks, touch and motion numbers (they count updates even when nothing changes) |
| 1f.2 | Connect the PC client (`--monitor` is fine) | "PC polls/s" is about the client's pkt/s; the "with new data" number is at most the buttons & sticks rate |
| 1f.3 | Switch Connection to USB and back | The input rates don't change with the connection |
| 1f.4 | Remap menu: Buttons → Fast (default). Tap buttons (face buttons, D-pad, L/R, Start/Select) quickly, about 50 times | Main screen: "Fast buttons: X ms sooner": about 8-10 ms; about 250 reads/s; 0 fallbacks |
| 1f.5 | Hold each of the 12 buttons for a second, one at a time, with `--monitor` running | Each shows while held and releases cleanly; nothing sticks or flickers |
| 1f.6 | Volume up/down, power button (sleep and wake), PS button, with fast buttons on | All work as usual |
| 1f.7 | USB mode with fast buttons, gamepad tester | Buttons as before |
| 1f.8 | Remap menu: Buttons → Normal, START | Main screen: "Fast buttons: off"; everything works as before this feature; the setting is kept after restarting VitaPad |
| 1f.9 | Play for 30+ minutes with fast buttons on | Battery use similar to before; no stuck buttons |

## 1g. Streaming (real Vita + `--monitor`)

| # | Do | Expect |
|---|----|--------|
| 1g.1 | Run `VitaPad --monitor` | "Streaming input from the Vita (UDP)."; lines show `stream`, about 120-200 pkt/s, max gap under 20 ms; the Vita shows "Streaming to the PC: N packets/s" |
| 1g.2 | Tap buttons, move sticks, touch, tilt | Everything shows up as before |
| 1g.3 | Run `VitaPad --monitor --poll` | Lines show `poll`, like before 1.9 |
| 1g.4 | Walk away from the router / behind walls until Wi-Fi gets weak | Stream mode: `max gap` rises but input keeps flowing; compare with `--poll`, where a weak signal shows as long freezes |
| 1g.5 | Close the client | The Vita stops streaming within 3 s (the line disappears) |
| 1g.6 | Suspend the Vita (PS, sleep) and wake it | Client reconnects and streams again |
| 1g.7 | Windows: first run with streaming | No firewall prompt needed for input to arrive |

## 3. Controller output (Windows, fake Vita or real Vita)

Set `VIGEM_MODE` to 1 in `windows.xml`, run `VitaPad` (without `--monitor`) and open a controller tester: Steam > Settings > Controller > Test Device Inputs, DS4Windows, or https://hardwaretester.com/gamepad.

With the fake Vita the sequence repeats, so compare against this:

| # | Expect |
|---|--------|
| 3.1 | Each face button, d-pad direction, Share (SELECT), Options (START), L2 (L), R2 (R), then L1, R1, L3, R3 and PS lights up in turn |
| 3.2 | The front touch visits the corners: L1, R1, L3, R3 |
| 3.3 | Rear touch slides across the DS4 touchpad (Steam's tester shows it) |
| 3.4 | Motion: the controller rocks left/right (Steam's gyro view) |
| 3.5 | Stop the fake Vita while a button is held: everything releases, nothing stays stuck |
| 3.6 | Edit `windows.xml` while running (e.g. `VIGEM_SWAP_SHOULDERS` to 1): change applies without a restart |

With the real Vita, check the tilt direction: tilt the Vita to the right and the controller in Steam's gyro view should tilt right too. If an axis is mirrored, note which one.

For vJoy (`VJOY_MODE` 1), use the vJoy Monitor app: front upper corners are buttons 5/6, lower corners 9/10 (with a 10+ button vJoy device).

For keyboard mode (both modes 0), open a text editor: buttons type their mapped keys, the front touch moves the mouse and rear touches click.
