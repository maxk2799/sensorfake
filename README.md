# sensorfake

Simulates a Bluetooth LE heart rate strap. It shows up in fitness apps as a
Polar sensor and sends a realistic heart rate curve (warm-up, steady effort,
cool-down) together with a matching running/walking pace, cadence and
distance.

There are two versions of the same simulation, one folder per device:

| Device | Folder | What is in it |
| --- | --- | --- |
| Flipper Zero | [`flipper/`](flipper) | Flipper app (source and ready-made `.fap`) |
| ESP32 board | [`esp32/`](esp32) | Arduino sketch [`sensorfake.ino`](esp32/sensorfake/sensorfake.ino) |

## Flipper Zero

### 1. Install the app

Pick one of the two ways.

#### Option A: copy the ready-made app (no tools needed)

[`flipper/prebuilt/sensorfake.fap`](flipper/prebuilt/sensorfake.fap) is built
for the official firmware, release 1.5.1 (app API 88.2).

1. Update the Flipper to the latest official firmware (qFlipper or the Flipper
   mobile app will offer the update).
2. Download `sensorfake.fap` from the link above (on GitHub, open the file and
   press **Download raw file**).
3. Connect the Flipper to your computer by USB and open
   [qFlipper](https://flipperzero.one/update).
4. Open the **File manager** tab and go to `SD Card` → `apps` → `Bluetooth`.
5. Drag `sensorfake.fap` into that folder.

No computer nearby? Put the SD card in a card reader and copy the file to
`apps/Bluetooth/` on the card instead.

#### Option B: build it yourself

Use this if the Flipper refuses to open the app ("Outdated App", "Outdated
Firmware" or "Missing Imports"), if you run a custom firmware, or if you
changed the code.

1. Install [Python 3](https://www.python.org/downloads/), then the Flipper
   build tool:

   ```bash
   python3 -m pip install --upgrade ufbt
   ```

2. Download this repository and open a terminal in its `flipper` folder:

   ```bash
   cd flipper
   ```

3. Connect the Flipper by USB and close qFlipper (only one program can talk to
   the Flipper at a time).
4. Build the app, copy it to the Flipper and start it:

   ```bash
   ufbt launch
   ```

The first run downloads the Flipper SDK, which takes a few minutes. To only
build the file, run `ufbt`; the result is `flipper/dist/sensorfake.fap`, which
you can copy as in option A.

On a custom firmware (Momentum, Unleashed, ...), point `ufbt` at that
firmware's SDK first, as described in the firmware's own documentation, then
run `ufbt launch`.

### 2. Start it

1. On the Flipper, open **Apps** → **Bluetooth** → **SensorFake**.
2. The top right corner shows **Advertising**: the Flipper is now visible as a
   heart rate sensor named `Polar Sense 1234`.
3. In your fitness app or on your watch, add a heart rate sensor and pick
   `Polar Sense 1234`. Add it from inside the fitness app, not from the phone's
   Bluetooth settings. If you changed the name (section 3 below), look for
   that name instead.
4. The status changes to **Connected** and a new session starts. The screen
   shows the heart rate being sent, the pace, the elapsed time and the
   distance.

| Button | Action |
| --- | --- |
| OK | Switch between walk and run (starts a new session) |
| Back | Quit and give the Flipper its normal Bluetooth back |

The app starts in walk mode. A session only runs while an app is connected,
and every new connection starts a fresh one.

- **Walk**: heart rate settles at 120-130 bpm for about 40 minutes, then cools
  down.
- **Run**: warm-up, build-up to a tempo effort around 168-175 bpm with one
  short peak, then cool-down. Roughly 23 to 44 minutes before the cool-down.

### 3. Change the Bluetooth name

The name fitness apps see is stored in a small text file on the Flipper's SD
card:

```
SD Card/apps_data/sensorfake/name.txt
```

SensorFake creates this file the first time you open it, with
`Polar Sense 1234` inside. To use another name:

1. Open SensorFake once so the file exists, then quit with **Back**.
2. On your computer, make a text file called `name.txt` whose first line is
   the new name, for example:

   ```
   Polar H10 5678
   ```

3. Connect the Flipper by USB and open qFlipper → **File manager** →
   `SD Card` → `apps_data` → `sensorfake`.
4. Delete the old `name.txt` there (right-click → **Delete**), then drag your
   new `name.txt` into the folder.
5. Open SensorFake again. The new name is shown on the screen next to
   **Name:** until an app connects.

You can also edit the file with the file manager of the Flipper mobile app, or
with the SD card in a card reader.

Rules for the name:

- At most 16 characters; anything longer is cut off. This limit comes from the
  Flipper firmware.
- Plain English letters, digits, spaces and punctuation only. Other characters
  (accents, emoji) are dropped.
- If the file is empty or missing, the name is `Polar Sense 1234` again.

Phones and watches remember the old name for a while. If your fitness app
still shows it, remove the sensor in the app and add it again, or switch the
phone's Bluetooth off and on.

To change the built-in default instead, edit `SENSORFAKE_NAME_DEFAULT` in
[`flipper/sensorfake_name.h`](flipper/sensorfake_name.h) and rebuild
(option B).

### Good to know

- While SensorFake is open, the Flipper mobile app cannot connect to the
  Flipper. Everything is back to normal after you quit with **Back**.
- For the first minute the Flipper advertises quickly, after that more slowly
  to save battery. If your app does not find it, scan a little longer or
  reopen SensorFake.
- The screen may switch off; the session keeps running.
- The name is limited to 16 characters by the Flipper firmware, so the default
  is `Polar Sense 1234` instead of the sketch's `Polar Sense 1234567`.
- The Flipper can advertise one service only, so it advertises Heart Rate.
  The Running Speed and Cadence service (pace, cadence, distance) is there
  once an app connects.

### Troubleshooting

| Problem | Fix |
| --- | --- |
| "Outdated App", "Outdated Firmware" or "Missing Imports" when opening | Update the Flipper firmware, or build the app yourself (option B) |
| Status shows **No Bluetooth** | The radio firmware is missing or damaged: reinstall the firmware with qFlipper |
| The sensor is not in the app's list | Make sure the status shows **Advertising**, close the Flipper mobile app, and scan again |
| The fitness app connects but shows the old device | Remove the sensor in the fitness app and add it again |

## ESP32

1. Install the [Arduino IDE](https://www.arduino.cc/en/software) and add
   ESP32 board support (**Boards Manager** → search for `esp32`).
2. Open `esp32/sensorfake/sensorfake.ino`, select your ESP32 board and port,
   and press **Upload**.
3. Add the sensor `Polar Sense 1234567` in your fitness app.

The sketch starts in walk mode. Type `walk` or `run` in the serial monitor
(115200 baud) to switch, or press the BOOT button to switch from walk to run.

To change the Bluetooth name, edit the text in the line
`BLEDevice::init("Polar Sense 1234567");` and upload again.

## How the Flipper app is organised

| File | Purpose |
| --- | --- |
| `flipper/sensorfake_sim.c` | The heart rate and pace simulation, ported from the sketch. No hardware dependencies |
| `flipper/sensorfake_profile.c` | Bluetooth LE profile: Heart Rate (0x180D) and Running Speed and Cadence (0x1814) services |
| `flipper/sensorfake_name.c` | Reads the Bluetooth name from the contents of `name.txt` |
| `flipper/sensorfake.c` | The app: screen, buttons, one-second timer, name file |
| `flipper/application.fam` | App manifest for the Flipper build system |
