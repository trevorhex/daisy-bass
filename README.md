# daisy-bass

A multi-effect bass pedal built on an [Electro-Smith Daisy Seed](https://daisy.audio/products/daisy-seed) and a [PedalPCB Terrarium](https://www.pedalpcb.com/product/pcb351/) in a 125B enclosure.

**So far:**
- An **envelope filter** voiced for bass. It's a resonant filter that opens as you dig in, with an optional crossover that keeps everything below 150/250 Hz dry so the low end never thins out.
- A **tuner** that mutes the output while you tune. It works down to a 5-string's low B.
- Groundwork for more effects: a common effect interface, a click-free effect chain, effect pages with knob pickup, and an OLED display.

**This build** uses the *screen panel*: a 0.96" OLED where SW 2, SW 3 and the LEDs normally go, keeping only SW 1 and SW 4. Build it with `make -C firmware SCREEN_PANEL=1`. The firmware still supports the stock Terrarium panel (4 toggles, 2 LEDs).

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
  PotSwitch.h         a pot standing in for a toggle (flips at the midpoint)
  PageLayout.h        spreads an effect's params over pages of pots + toggles
  FootswitchGesture.h tap vs hold
desktop/    bassfx: run the DSP on WAV files on your Mac
tests/      tests across DSP, tuner and controls (plain C++, no framework)
firmware/   Daisy/Terrarium glue: controls, audio callback, LEDs, OLED
  PageView.h          snapshot of the current page that the main loop draws from
lib/        libDaisy + DaisySP (git submodules)
```

The rule: **anything that touches sound lives in `dsp/` and is tested on the desktop.** The firmware only reads controls and moves numbers around.

**Adding an effect:** implement `bass::Effect` in `dsp/` (continuous params first, then switches; see `EnvelopeFilter.h`), add tests, then in `firmware/main.cpp` declare a static instance and `chain.Add()` it. Pages, knob pickup, toggles, the OLED and the activity LED all work from the `Effect` interface. Override `Activity()` if the effect has something worth metering. Param lookups must tolerate out-of-range indices (see `Effect.h`).

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
| **FS 1 tap** | Current effect on/off. A click-free crossfade. Shown as an inverted **ON** on the screen (LED 1 on the stock panel). Also exits the tuner |
| **FS 2 hold** (½ s) | Tuner on/off. The output is muted while tuning |
| **FS 2 tap** | Next page. Also exits the tuner |
| Screen divider | Fills with your playing: the current effect's activity, e.g. the Env Filter's envelope (LED 2 on the stock panel) |

**Env Filter, screen panel (this build)**

| Control | Page 1 | Page 2 |
|---|---|---|
| Pot 1 | Sensitivity: how far your picking pushes the filter | Lows: left = filtered, right = clean |
| Pot 2 | Range: where the sweep starts (60–600 Hz; it sweeps 4 octaves) | Crossover: left = 150 Hz, right = 250 Hz |
| Pot 3 | Resonance: Q 0.7 (smooth) to 12 (vocal/quacky) | — |
| Pot 4 | Response: envelope release, 40–600 ms | — |
| Pot 5 | Mix: dry to wet (with Lows clean, it blends only the highs; the lows stay dry) | — |
| Pot 6 | Level: −18 to +18 dB, noon is unity | — |
| SW 1 | Low-pass / band-pass | same |
| SW 4 | Sweep up / sweep down | same |

Lows and Crossover are set-and-forget, so they live on page 2 (tap FS 2). They flip only when you turn the pot across its midpoint, so changing pages never changes them. The toggles work on either page.

**Env Filter, stock panel:** one page. Pots as page 1 above; SW 1 LP/BP, SW 2 sweep up/down, SW 3 lows filtered/clean, SW 4 crossover 150/250 Hz.

**Tuner (stock panel, LEDs only)**
- LED 1 blinks: you're **flat**.
- LED 2 blinks: you're **sharp**.
- Faster blinking means further off.
- **Both solid:** in tune (±3 cents).
- **Slow heartbeat:** no note detected.

The LEDs can't show the note name. The OLED shows the note, a needle and the cents.

**Pages:** each effect gets as many pages as it needs; FS 2 steps through all of them. The pots edit the current page. After switching pages, a pot does nothing until you turn it past the effect's stored value ("pickup"), so the sound never jumps. On the OLED, the bar shows the stored value and a tick shows where the pot actually is: turn the tick to the end of the bar. Toggles only act when flipped.

## Firmware setup (one time)

```bash
brew install --cask gcc-arm-embedded      # asks for your password (pkg installer)
brew install dfu-util
git submodule update --init --recursive   # libDaisy + DaisySP in lib/
make -C lib/libDaisy && make -C lib/DaisySP
```

## Building and flashing

```bash
make -C firmware program-boot          # one time: installs Electro-Smith's bootloader (see below)
make -C firmware SCREEN_PANEL=1        # this build → firmware/build/daisy-bass.bin
make -C firmware program-dfu           # flashes the last build, at the right address
```

The Makefile remembers the flags of the last build (`firmware/build/flags`). Building with different flags starts from scratch automatically, and `program-dfu` with no flags flashes whatever was built last, the right way for that build. Plain `make -C firmware` builds for the stock 4-toggle, 2-LED panel.

**Getting the Seed into DFU mode** depends on the build:
- **Bootloader builds** (`SCREEN_PANEL`, `OLED`, `BOOT_SRAM`): tap **RESET**. The LED pulses for 2.5 s while the bootloader listens; press **BOOT** during that time to keep it listening (a few quick blinks confirm), then run `program-dfu`. Holding BOOT while tapping RESET won't work for these builds: that enters the chip's built-in DFU, which can only write internal flash, not the external flash these builds run from.
- **Stock-panel builds and `program-boot`**: hold **BOOT**, tap **RESET**, release BOOT. This is the chip's built-in DFU. Note that flashing a stock build this way replaces the bootloader; run `program-boot` again before going back to the screen build.

You can also flash `firmware/build/daisy-bass.bin` with Electro-Smith's [web programmer](https://electro-smith.github.io/Programmer/) (choose the bootloader option for bootloader builds).

Build flags:

| Flag | What it does |
|---|---|
| `SCREEN_PANEL=1` | **This build.** SW 1 + SW 4 only, no LEDs, OLED in their place. Implies `OLED=1` |
| `OLED=1` | Draws on a 128×64 SSD1306 on D11/D12. Needs the bootloader (implies `BOOT_SRAM=1`) |
| `LOG=1` | Prints CPU load over USB serial once a second |
| `KNOB_CHECK=1` | Control diagnostic: prints every pot/switch over USB serial instead of processing audio |
| `BOOT_SRAM=1` | Runs via Electro-Smith's bootloader, allowing programs of about 480 KB |

**Memory budget:** the stock build uses 117 KB of the Seed's 128 KB internal flash (89%), so the next effect will need the bootloader anyway. With the bootloader the screen build uses 137 KB of about 480 KB.

**First flash:** run the control check and confirm every pot and switch:

```bash
make -C firmware SCREEN_PANEL=1 KNOB_CHECK=1 && make -C firmware program-dfu
screen /dev/tty.usbmodem* 115200
```

The pot and switch pins in `firmware/terrarium.h` come straight from the Terrarium schematic. If a toggle reads backwards from how you'd like it, flip its meaning in `main.cpp`'s page mapping or just mount it the other way round.

## Hardware notes

From PedalPCB's build doc (rev. 10/23/25). Check their current PDF before ordering.

- **Power:** standard 9 V center-negative. The Seed takes 9 V on its own `V_IN` regulator. The board's L78L05 only powers the TL072 buffers, and a 1N5817 diode protects against reverse polarity.
- **Audio path:** TL072 buffers on input and output, so the Daisy's line-level codec sees a proper instrument input. This revision already includes the **3K3 + 2n2 low-pass** (R10/C10) before the output buffer. That's the fix builders applied for the hiss on newer Seeds.
- **Seed:** plugs into 2 × 20-pin female headers. There's little room underneath it, so check that the parts fit before soldering.
- **Enclosure:** jacks and DC on the top edge; two rows of three pots, four toggles, two LEDs, two footswitches. Drill a side hole for USB so you can reflash without opening the box.
- **OLED:** SCL → D11, SDA → D12, VCC → Seed 3V3, GND → GND. These pins are unused on the Terrarium.
