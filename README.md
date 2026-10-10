# ESP32 Walkie Talkie

Encrypted push to talk voice between Adafruit ESP32 Feather V2 boards over ESP-NOW, built on FreeRTOS with ESP-IDF v5.4.

- 16 kHz voice, IMA ADPCM, 20 ms frames
- AES-128-CCM encryption with a separate key per channel, plus replay protection
- 8 channels, normal and Espressif Long Range radio modes
- Jitter buffer with packet loss concealment
- Busy-channel detection, presence beacons, roster of nearby units
- OLED menu driven by a rotary encoder

## Contents

```
walkie-talkie/
  main/                 app modes and the walkie talkie tasks
    app_hwtest.c        mode 1: hardware test
    app_loopback.c      mode 2: mic -> ADPCM -> speaker on one unit
    app_radiotest.c     mode 3: ESP-NOW range test (bare boards are enough)
    app_walkie.c        mode 4: the walkie talkie (audio + radio tasks)
    walkie_ui.c         mode 4: menus and OLED screens
    Kconfig.projbuild   menuconfig options (Walkie Talkie menu)
  components/
    board/              pin map for the Feather V2 (board.h)
    drivers/            I2S audio, SSD1306 OLED, encoder + buttons, battery ADC
    link/               AES-CCM crypto, ESP-NOW link
    proto/              portable C: ADPCM codec, jitter buffer, packet format, replay window
  host_tests/           unit tests and a WAV tool that run on a PC
  docs/                 schematic and wiring diagram (+ the scripts that draw them)
  modes/                one-line config per app mode, used by the build scripts
  build_mode.sh         build or flash one mode (macOS, Linux)
  build_mode.ps1        build or flash one mode (Windows PowerShell)
```

## Parts (per unit)

- Adafruit ESP32 Feather V2
- 3.7 V LiPo with a JST-PH connector (1200 mAh)
- INMP441 I2S MEMS microphone
- MAX98357A I2S amplifier and a 4 ohm, 3 W speaker
- SSD1306 0.96 inch 128x64 I2C OLED
- EC11 rotary encoder with push switch
- Tactile push button (push to talk)
- Slide switch (power)
- 470 uF electrolytic capacitor
- Perfboard or breadboard, headers, hookup wire

Build at least two units.

## 1. Install the tools

1. Install ESP-IDF **v5.4.x**. Either use the VS Code "ESP-IDF" extension (Express install, pick v5.4) or follow the command line guide: https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/get-started/
2. The Feather V2 uses a CH9102F USB to serial chip. macOS and Linux usually need nothing extra. On Windows, install the WCH CH9102 driver if no COM port appears when the board is plugged in.
3. Do not open the project with PlatformIO or any other extensions. This is an ESP-IDF CMake project.

## 2. Build

Open an ESP-IDF terminal in the project folder. On Windows, that is the "ESP-IDF PowerShell" shortcut the installer adds; on macOS or Linux, run ESP-IDF's `export.sh` first.

Set the chip once:

```
idf.py set-target esp32
```

Build the default mode (hardware test):

```
idf.py build
```

The first build takes several minutes. It is done when the output ends with `Project build complete`.

### Switching modes

**Option A, menuconfig (any OS):** run `idf.py menuconfig`, open **Walkie Talkie > App mode**, pick a mode, press `S` to save and `Q` to quit, then build again.

**Option B, build scripts:** each mode gets its own build folder, so switching does not rebuild everything.

| OS | Build only | Build, flash and monitor |
|---|---|---|
| Windows | `.\build_mode.ps1 hwtest` | `.\build_mode.ps1 hwtest COM5` |
| macOS / Linux | `./build_mode.sh hwtest` | `./build_mode.sh hwtest /dev/ttyUSB0` |

Modes: `hwtest`, `loopback`, `radiotest`, `walkie`.

If PowerShell refuses to run the script, allow local scripts once with `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`.

### Flashing

```
idf.py -p COM5 flash monitor
```

Replace `COM5` with your port. Find it in Device Manager under "Ports (COM & LPT)" on Windows, or use `/dev/ttyUSB0` on Linux or `/dev/cu.wchusbserial*` on macOS. Press `Ctrl+]` to leave the monitor.

### Editor setup (optional)

If VS Code underlines the `#include` lines in red, the build is still fine; the editor just does not know where the headers are. After one successful build, run **ESP-IDF: Add VS Code Configuration Folder** from the command palette (`Ctrl+Shift+P`), then **Developer: Reload Window**. If that command is not available, create `.vscode/c_cpp_properties.json` with:

```json
{
    "configurations": [
        {
            "name": "ESP-IDF",
            "compileCommands": "${workspaceFolder}/build/compile_commands.json"
        }
    ],
    "version": 4
}
```

### PC unit tests (optional)

The codec, jitter buffer and packet code are plain C and can be tested on a PC with gcc or clang and make (on Windows, use WSL or MSYS2):

```
make -C host_tests
```

Expected output ends with `277 checks, 0 failures`.

To hear the codec, record a 16 kHz, mono, 16-bit WAV (Audacity works) and run it through with simulated packet loss:

```
make -C host_tests wav
./host_tests/adpcm_wav input.wav output.wav 10
```

## 3. Wire a unit

- Wiring diagram: `docs/wiring.png`
- Schematic: `docs/schematic.png`

Wire by the **silkscreen labels** on the Feather, not by position. The diagram follows Adafruit's standard Feather header order, but the labels are what matter.

| Part | Part pin | Feather pin | ESP32 GPIO |
|---|---|---|---|
| INMP441 mic | VDD | 3V | |
| | GND, L/R | GND | |
| | SCK | A0 | 26 |
| | WS | A1 | 25 |
| | SD | A2 | 34 |
| MAX98357A amp | VIN | BAT | |
| | GND | GND | |
| | BCLK | 27 | 27 |
| | LRC | 33 | 33 |
| | DIN | 32 | 32 |
| | SD | TX | 8 |
| | GAIN | not connected (9 dB) | |
| | 470 uF cap | VIN to GND, close to the amp | |
| Speaker | + / - | amp + / - | |
| SSD1306 OLED | VCC / GND | 3V / GND | |
| | SCL | SCL | 20 |
| | SDA | SDA | 22 |
| EC11 encoder | A | SCK | 5 |
| | B | MO | 19 |
| | switch | MI | 21 |
| | C and other switch leg | GND | |
| PTT button | one leg / other leg | A5 / GND | 4 |
| Power switch | | EN / GND | |
| LiPo | | plugs into the Feather's JST socket | |
| Timing test points (optional) | capture / TX LED / playback / encode | RX / 13 / 14 / 15 | 7 / 13 / 14 / 15 |

The buttons and encoder switch to GND and use the ESP32's internal pull-ups, so no resistors are needed.

**Using a different ESP32 board:** change the pins in `components/board/include/board.h`. On a regular ESP32 DevKit (WROOM module), GPIO6 to GPIO11 are connected to the flash chip, so move `PIN_AMP_SD` off GPIO8 and turn off **Drive timing marker pins** in menuconfig (it uses GPIO7).

## 4. Bring-up

Go through the modes in order. Each one isolates a different piece, so when something breaks you know where.

### Mode 1: hardware test (`hwtest`)

The OLED shows a test screen and the serial monitor prints a line every second.

- [ ] OLED shows "HARDWARE TEST". If it stays dark, the serial log says whether anything answered on I2C.
- [ ] Turning the encoder changes `ENC`, and clockwise counts up. If it counts down, swap the A and B wires, or swap `PIN_ENC_A` and `PIN_ENC_B` in `board.h`.
- [ ] Pressing the encoder shows short or long press and beeps.
- [ ] Holding PTT plays a 1 kHz tone from the speaker.
- [ ] Talking into or tapping the mic moves the MIC bar. Silence sits near zero.
- [ ] BAT reads about 3.7 to 4.2 V with the LiPo plugged in.

### Mode 2: audio loopback (`loopback`)

Hold PTT and talk. You hear yourself through the full codec path about 50 to 60 ms later. Point the speaker away from the mic.

- [ ] Voice is clear. If it is quiet, lower **Mic sample right shift** in menuconfig (14 to 13 or 12). If it distorts, raise it.
- [ ] Turning the encoder changes volume.
- [ ] Pressing the encoder cycles simulated packet loss (0, 5, 10, 20, 30%) to hear how concealment handles it.
- [ ] The log prints `enc=` and `dec=` times per frame. Both should be well under 1000 us.

### Mode 3: radio range test (`radiotest`, on two boards)

Bare boards are enough; no peripherals are needed.

- [ ] Each board shows about 50 packets per second from the other, with close to 0% loss at short range.
- [ ] Pressing PTT on one board switches it to Long Range transmit. Check whether the other board still receives it (the firmware listens for both rates).
- [ ] Walk apart outdoors and note where loss passes 10% in each mode. That is the usable range.

### Mode 4: walkie talkie (`walkie`, on two or more units)

- [ ] Both units show READY and `PEERS 1` within a few seconds (beacons go out every 2 s).
- [ ] Holding PTT on one unit and talking makes the other show RX and play the audio.
- [ ] While one unit talks, pressing PTT on another gives a busy tone.
- [ ] The Stats screen (menu > Stats) shows `UNDER 0` during normal talking. If the final packet of a talk burst is lost, the receiver counts up to 10 underruns while it decides the talker stopped, so a few per burst at long range is expected.
- [ ] Changing the channel on one unit stops it from hearing the other. Changing it back restores audio.
- [ ] Every 10 s the serial log prints CPU time and free stack for each task.

All units must use the same **passphrase** and **Wi-Fi channel** (menuconfig > Walkie Talkie). Change the default passphrase before using it anywhere real.

### Controls

| Input | Action |
|---|---|
| Hold PTT | Talk (busy tone if someone else has the channel) |
| Turn encoder on home screen | Volume |
| Press encoder | Open menu; turn to move, press to select |
| Long press encoder | Back or cancel |

### Timing measurements (optional)

With **Drive timing marker pins** on (the default), probe RX/7 (capture), 15 (encode), 14 (playback) and 13 (lit while transmitting) with a logic analyzer. Each pulse is one task working on one 20 ms frame. The trace shows the frame period, each task's execution time, and the jitter between frames.

## Firmware design

| Task | Core | Priority | Paced by | RTOS objects it uses |
|---|---|---|---|---|
| audio_playback | 1 | 20 | I2S DMA write | jitter buffer (critical section), event group, task watchdog |
| audio_capture | 1 | 19 | I2S DMA read | frame pool queues, event group, task watchdog |
| audio_encode | 1 | 18 | frame queue | packet queue, crypto mutex |
| radio_rx | 0 | 17 | message buffer from the ESP-NOW callback | jitter buffer, roster mutex, event group, task watchdog |
| radio_tx | 0 | 16 | packet queue | |
| link_mgr | 0 | 10 | task notification from the beacon timer, or 100 ms | roster mutex, event group |
| ui | 0 | 8 | input event queue (button ISR -> debounce timer -> queue) | settings mutex, stuck PTT timer |
| display | 0 | 5 | 10 Hz | |
| monitor | 0 | 2 | 1 s | run-time stats, stack high water marks |

- **No software timer drives audio.** Capture and playback block on the I2S DMA, so the hardware sets the 20 ms clock.
- **No malloc in the audio path.** PCM frames move by pointer between a static pool and two queues.
- **The ESP-NOW receive callback does almost nothing.** It runs in the Wi-Fi task, so it copies the packet into a message buffer and returns.
- **Audio is isolated from Wi-Fi.** Audio tasks run on core 1; the Wi-Fi stack, radio, UI and display run on core 0.
- **The jitter buffer is shared across cores.** It is written on core 0 and read on core 1, behind a spinlock critical section held for a few microseconds.
- **No nonce reuse across reboots.** Sequence numbers are reserved in NVS in blocks, so a restart can never repeat an AES-CCM nonce.

Packet format (187 bytes for audio, under the 250 byte ESP-NOW limit): a 15 byte header (version, type, flags, channel, TTL, sender ID, sequence number, timestamp), then the 164 byte encrypted ADPCM frame, then an 8 byte authentication tag.

## Troubleshooting

| Symptom | Try |
|---|---|
| No serial port | Install the CH9102 driver (Windows); use a USB-C cable that carries data |
| Flash fails with "wrong boot mode" | Hold the Feather's BOOT button, tap RESET, release BOOT, flash again |
| OLED dark, log says no device at 0x3C | Check SDA and SCL are not swapped; try address 0x3D in menuconfig |
| Mic level always 0 | Check L/R is tied to GND, then turn on **Mic is on the right I2S slot** in menuconfig |
| Mic is loud hiss or garbage | SCK and WS swapped, or SD not on A2 |
| No sound from the speaker | Amp SD must go to TX (GPIO8), VIN to BAT. The hardware test tone is the simplest check |
| Clicks or pops at the start of received audio | The amp waking from shutdown |
| Units do not see each other | Same Wi-Fi channel and passphrase on all units; the stats line shows `auth` climbing if passphrases differ |
| Board resets with "task watchdog" | An audio task hung; the backtrace in the monitor names the task |
| Red underlines on includes in VS Code | See Editor setup above; the build is not affected |

## Known limitations and next steps

- Unit names default to `U` plus the last 4 hex digits of the ID; there is no name editor yet
- The jitter buffer depth is fixed (3 frames by default, set in menuconfig)
- No screen sleep yet
- Not yet implemented: relay mode, automatic switch to Long Range on packet loss, hands-free voice activation (VOX)
- To verify on hardware: which I2S slot the INMP441 lands in (menuconfig switch), encoder direction, whether a Long Range transmitter is heard by a unit not in Long Range mode, real current draw and battery life

## Regenerating the diagrams

```
pip install matplotlib schemdraw
python3 docs/draw_wiring.py
python3 docs/draw_schematic.py
```
