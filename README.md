# daisy-bass

A multi-effect bass pedal built on an [Electro-Smith Daisy Seed](https://daisy.audio/products/daisy-seed) and a [PedalPCB Terrarium](https://www.pedalpcb.com/product/pcb351/) in a 125B enclosure.

**So far:**
- An **envelope filter** voiced for bass. It's a resonant filter that opens as you dig in, with an optional crossover that keeps everything below 150/250 Hz dry so the low end never thins out.
- A **tuner** that mutes the output while you tune. It works down to a 5-string's low B.
- Groundwork for more effects: a common effect interface, a click-free effect chain, effect pages with knob pickup, and an optional OLED display.

Shopping list: [PARTS.md](PARTS.md).

## Layout

```
dsp/        Sound. Pure C++, no hardware dependencies, shared by desktop and pedal
  Effect.h            interface every effect implements (indexed 0..1 params)
  Chain.h             runs effects in series, click-free on/off per effect
  EnvelopeFilter.h    the envelope filter
  Tuner.h             YIN pitch detector (25–500 Hz, ±1 cent)
  Svf.h, Crossover.h, EnvelopeFollower.h, Smoother.h   building blocks
control/    UI logic, also hardware-independent and tested
  KnobPickup.h        pots don't jump when switching effect pages
  FootswitchGesture.h tap vs hold
desktop/    bassfx: run the DSP on WAV files on your Mac
tests/      21 tests across DSP, tuner and controls (plain C++)
firmware/   Daisy/Terrarium glue: controls, audio callback, LEDs, OLED
lib/        libDaisy + DaisySP (git submodules)
```

The rule: **anything that touches sound lives in `dsp/` and is tested on the desktop.** The firmware only reads controls and moves numbers around.

## Desktop workflow

```bash
cmake -S . -B build && cmake --build build
./build/dsp_tests

./build/bassfx --demo renders/riff.wav                       # synthetic bass riff
./build/bassfx renders/riff.wav renders/wah.wav --sens 0.6 --res 0.6
afplay renders/wah.wav
./build/bassfx --tune renders/riff.wav                       # what the tuner hears
```

Better still, record a dry DI track of your own bass (any WAV: 16/24/32-bit or float, mono or stereo) and run it through. `./build/bassfx` with no arguments lists every option. Each knob is 0–1, exactly like the pots.

## Pedal controls

| Control | Function |
|---|---|
| **FS 1 tap** | Current effect on/off (LED 1 lit = on). A click-free crossfade |
| **FS 2 hold** (½ s) | Tuner on/off. The output is muted while tuning |
| **FS 2 tap** | Next effect page (once there's more than one effect). Also exits the tuner |
| LED 2 | Flickers with your playing (envelope) |

**Env Filter page**

| Control | Function |
|---|---|
| Pot 1 | Sensitivity: how far your picking pushes the filter |
| Pot 2 | Range: where the sweep starts (60–600 Hz; it sweeps 4 octaves) |
| Pot 3 | Resonance: Q 0.7 (smooth) to 12 (vocal/quacky) |
| Pot 4 | Response: envelope release, 40–600 ms |
| Pot 5 | Mix: dry to wet |
| Pot 6 | Level: −18 to +18 dB, noon is unity |
| SW 1 | Low-pass / band-pass |
| SW 2 | Sweep up / sweep down |
| SW 3 | Lows filtered / clean |
| SW 4 | Crossover at 150 Hz / 250 Hz |

**Tuner (no screen)**
- LED 1 blinks: you're **flat**.
- LED 2 blinks: you're **sharp**.
- Faster blinking means further off.
- **Both solid:** in tune (±3 cents).
- **Slow heartbeat:** no note detected.

The LEDs can't show the note name. The OLED shows the note, a needle and the cents.

**Effect pages:** the pots edit whichever effect is on the current page. After switching pages, a pot does nothing until you turn it past the effect's stored value ("pickup"), so the sound never jumps. On the OLED, a tick marks where to turn to. Toggles only act when flipped.

## Firmware setup (one time)

```bash
brew install --cask gcc-arm-embedded      # asks for your password (pkg installer)
brew install dfu-util
git submodule update --init --recursive   # libDaisy + DaisySP in lib/
make -C lib/libDaisy && make -C lib/DaisySP
```

## Building and flashing

```bash
make -C firmware                 # build → firmware/build/daisy-bass.bin
make -C firmware program-dfu     # flash: hold BOOT, tap RESET on the Seed first
```

You can also flash `firmware/build/daisy-bass.bin` with Electro-Smith's [web programmer](https://electro-smith.github.io/Programmer/).

Build flags (run `make -C firmware clean` when changing them):

| Flag | What it does |
|---|---|
| `OLED=1` | Draws on a 128×64 SSD1306 on D11/D12. Needs the bootloader (implies `BOOT_SRAM=1`) |
| `LOG=1` | Prints CPU load over USB serial once a second |
| `KNOB_CHECK=1` | Control diagnostic: prints every pot/switch over USB serial instead of processing audio |
| `BOOT_SRAM=1` | Runs via Electro-Smith's bootloader, allowing programs of about 480 KB |

**Memory budget:** the plain build uses 113 KB of the Seed's 128 KB internal flash (87%), so the next effect will probably need the bootloader anyway. With the bootloader the OLED build uses 137 KB of about 480 KB. To switch, install the bootloader once, then build with `BOOT_SRAM=1` (or `OLED=1`):

```bash
make -C firmware program-boot    # one time: installs the bootloader
make -C firmware clean && make -C firmware OLED=1 && make -C firmware program-dfu
```

**First flash:** run the control check and confirm every pot and switch:

```bash
make -C firmware clean && make -C firmware KNOB_CHECK=1 && make -C firmware program-dfu
screen /dev/tty.usbmodem* 115200
```

The pot and switch pins in `firmware/terrarium.h` come straight from the Terrarium schematic. If a toggle reads backwards from how you'd like it, flip its meaning in `main.cpp`'s page mapping or just mount it the other way round.

## Hardware notes

From PedalPCB's build doc (rev. 10/23/25). Check their current PDF before ordering.

- **Power:** standard 9 V center-negative. The Seed takes 9 V on its own `V_IN` regulator. The board's L78L05 only powers the TL072 buffers, and a 1N5817 diode protects against reverse polarity.
- **Audio path:** TL072 buffers on input and output, so the Daisy's line-level codec sees a proper instrument input. This revision already includes the **3K3 + 2n2 low-pass** (R10/C10) before the output buffer. That's the fix builders applied for the hiss on newer Seeds.
- **Seed:** plugs into 2 × 20-pin female headers. There's little room underneath it, so check that the parts fit before soldering.
- **Enclosure:** jacks and DC on the top edge; two rows of three pots, four toggles, two LEDs, two footswitches. Drill a side hole for USB so you can reflash without opening the box.
- **OLED (optional):** SCL → D11, SDA → D12, VCC → Seed 3V3, GND → GND. These pins are unused on the Terrarium.
