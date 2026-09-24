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
| 1.2 | On the PC run `VitaPad --monitor` (delete `vita_ip.txt` first) | "Found Vita at ...", then "Connection established!", and the Vita shows "Connected!" |
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
| 2.4 | Run it with `--no-discovery`, delete `vita_ip.txt` | Client says no Vita was found and asks for the IP; typing it connects |
| 2.5 | Put a wrong IP in `vita_ip.txt` | Connecting to it fails, then discovery finds the right one and overwrites the file |

## 2b. 3D viewer (fake Vita or real Vita)

Run `VitaPad --viewer` (add `--monitor` to skip controller emulation). The browser opens http://localhost:5050/.

| # | Do | Expect |
|---|----|--------|
| 2b.1 | With the fake Vita | Buttons light up and press in one by one, sticks circle, the front touch glows in each corner, the rear touch slides (pink ring on the screen, glow on the back with B), the model rocks left/right |
| 2b.2 | With the real Vita: hold it in front of you and press R (Recenter) | Model faces you the same way the Vita does |
| 2b.3 | Tilt, turn and flip the Vita | Model follows without lag; when you stop moving it settles and doesn't keep drifting up/down |
| 2b.4 | Turn slowly for a minute | Some left/right drift is normal (no compass); R fixes it |
| 2b.5 | Stop the client | Page shows "VitaPad client closed" |

## 3. Controller output (Windows, fake Vita or real Vita)

Set `VIGEM_MODE` to 1 in `windows.xml`, run `VitaPad` (without `--monitor`) and open a controller tester: Steam > Settings > Controller > Test Device Inputs, DS4Windows, or https://hardwaretester.com/gamepad.

With the fake Vita the sequence repeats, so compare against this:

| # | Expect |
|---|--------|
| 3.1 | Each face button, d-pad direction, Share (SELECT), Options (START), L2 (L), R2 (R) lights up in turn |
| 3.2 | The front touch visits the corners: L1, R1, L3, R3 |
| 3.3 | Rear touch slides across the DS4 touchpad (Steam's tester shows it) |
| 3.4 | Motion: the controller rocks left/right (Steam's gyro view) |
| 3.5 | Stop the fake Vita while a button is held: everything releases, nothing stays stuck |
| 3.6 | Edit `windows.xml` while running (e.g. `VIGEM_SWAP_SHOULDERS` to 1): change applies without a restart |

With the real Vita, check the tilt direction: tilt the Vita to the right and the controller in Steam's gyro view should tilt right too. If an axis is mirrored, note which one.

For vJoy (`VJOY_MODE` 1), use the vJoy Monitor app: front upper corners are buttons 5/6, lower corners 9/10 (with a 10+ button vJoy device).

For keyboard mode (both modes 0), open a text editor: buttons type their mapped keys, the front touch moves the mouse and rear touches click.
