# daisy-bass

A multi-effect bass pedal built on an [Electro-Smith Daisy Seed](https://daisy.audio/products/daisy-seed) and a [PedalPCB Terrarium](https://www.pedalpcb.com/product/pcb351/) in a 125B enclosure.

**So far:**
- An **envelope filter** voiced for bass. It's a resonant filter that opens as you dig in, with an optional crossover that keeps everything below 150/250 Hz dry so the low end never thins out.
- A **compressor** with Fast (peak) and Opto (smooth, two-stage release) modes, a Blend knob for parallel compression, and a detector high-pass so low notes don't pump.
- An **overdrive/fuzz** with Grit and Fuzz characters, Tone and Mid controls, and a crossover that keeps the lows clean so the low end stays tight.
- A **tuner** that mutes the output while you tune. It works down to a 5-string's low B.
- **16 presets**, saved only when you choose to.
- A **desktop simulator** (`pedalsim`) that runs the real pedal code with live audio.
- Groundwork for more effects: a common effect interface, a click-free effect chain, effect pages with knob pickup, and an OLED display.

**This build** uses the *screen panel*: a 0.96" OLED where SW 2, SW 3 and the LEDs normally go, keeping only SW 1 and SW 4. Build it with `make -C firmware SCREEN_PANEL=1`. The firmware still supports the stock Terrarium panel (4 toggles, 2 LEDs).

## Layout

```
dsp/        Sound. Pure C++, no hardware dependencies, shared by desktop and pedal
  Effect.h            interface every effect implements (indexed 0..1 params)
  Chain.h             runs effects in series, click-free on/off per effect
  Compressor.h        the compressor
  Drive.h             overdrive / fuzz
  EnvelopeFilter.h    the envelope filter
  Tuner.h             YIN pitch detector (25–500 Hz, ±1 cent)
  Svf.h, Crossover.h, EnvelopeFollower.h, Smoother.h   building blocks
control/    The pedal minus hardware: shared by the firmware and the simulator, tested
  Pedal.h             effects in chain order, tuner, output mute; the audio callback's body
  PedalUi.h           what every footswitch, pot and toggle does (preset/edit modes, pages, saving)
  Presets.h           the preset bank: plain data, written to flash or a file as-is
  PageView.h          snapshot of the current state that the screen/LEDs draw from
  KnobPickup.h, PotSwitch.h, PageLayout.h, FootswitchGesture.h, FootswitchPair.h
ui/         Screens.h: every OLED screen + the stock panel's LEDs, drawn on any canvas
            Framebuffer.h: an in-memory OLED for the simulator and tests
sim/        pedalsim: the pedal on your Mac (SDL2 + Dear ImGui, fetched by CMake)
desktop/    bassfx: run the DSP on WAV files on your Mac
tests/      tests across DSP, tuner, controls, presets and screens (plain C++, no framework)
firmware/   Daisy/Terrarium glue: reads controls, runs Pedal, draws the OLED, saves presets to flash
lib/        libDaisy + DaisySP (git submodules)
```

The rule: **anything that touches sound lives in `dsp/`, anything about how the controls behave lives in `control/`, and both are tested on the desktop.** The firmware and the simulator only move numbers between hardware (real or drawn) and `bass::Pedal`, so the simulator behaves exactly like the pedal.

**Adding an effect:** implement `bass::Effect` in `dsp/` (continuous params first, then switches; names of up to 6 characters so they fit the screen; see `EnvelopeFilter.h`), add tests, then add it to `control/Pedal.h` (a member, plus `chain.Add()` in chain order) and optionally a factory preset. Pages, presets, knob pickup, toggles, the screen and the activity meter all work from the `Effect` interface. Override `Activity()` if the effect has something worth metering. Param lookups must tolerate out-of-range indices (see `Effect.h`).

## Simulator

```bash
cmake -S . -B build && cmake --build build   # first time also downloads SDL2 + Dear ImGui (~1 min)
./build/pedalsim
```

The pedal is drawn as it will be built (screen panel). Everything on it runs the real pedal code; the screen uses the pedal's own fonts, pixel for pixel.

- **Pots:** drag up/down or scroll. Hold Shift for fine moves.
- **Toggles:** click (up = on).
- **Footswitches:** hold the mouse button on them, or use **Z** (FS 1) and **X** (FS 2). Use the keys to press both at once.
- **Side panel:** the screen at 3×, what the footswitches do right now, what every control edits (orange = a pot waiting to pick up), the audio source and levels, and presets.
- **Audio sources:** the demo riff, any WAV file (drag it onto the window), or **live input** from your audio interface. Use headphones. The first time, macOS asks to let your terminal use the microphone.
- **Presets** are saved in `~/.daisy-bass-presets.bin`, the same format the pedal stores in flash. "Reset presets to factory" in the side panel starts over.

To build only the tests and `bassfx` (no download): `cmake -S . -B build -DBASS_SIM=OFF`.

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

The pedal has two modes. It powers up in **Preset mode** on the last preset you saved.

| | **Preset mode** | **Edit mode** |
|---|---|---|
| **FS 1 tap** | Previous preset | Current effect on/off (click-free) |
| **FS 2 tap** | Next preset | Next page |
| **FS 1 hold** (½ s) | Go to Edit mode | Back to Preset mode |
| **FS 2 hold** (½ s) | Tuner (output muted) | Tuner (output muted) |
| **Both held** (1 s) | Save to the current preset | Save to the current preset |
| **Pots / toggles** | Edit the current page; the page pops up on the screen for 2 s | Edit the current page |

- **Saving:** nothing is saved until you hold both footswitches. A `*` (and "edited") shows unsaved changes. Changing preset or powering off brings back the saved version.
- **Tuner:** any tap leaves it.
- **Taps:** a tap registers when you lift your foot, because a press might still turn into a hold.
- **Loading a preset** puts the pots on the first page of the first effect that's on in it.
- **Factory presets:** P1 compressor, P2 envelope filter, P3 both, P4 smooth opto squeeze, P5 compressed quack (band-pass), P6 sweep down, P7 compressed grit drive, P8 fuzz into the envelope filter, P9–P16 empty (all effects off).
- **Chain order:** compressor → drive → envelope filter. In Edit mode FS 2 steps through every page of every effect: Compressor (1 page), Drive (1 page), then Env Filter (2 pages on the screen panel).
- **Screen divider:** fills with your playing (the effect's activity, e.g. the Env Filter's envelope).

**Compressor** (one page on either panel)

| Control | Function |
|---|---|
| Pot 1 | Thresh: −40 to 0 dB |
| Pot 2 | Ratio: 1.5:1 to 20:1 |
| Pot 3 | Atk (attack): 0.3–50 ms. Slower lets the pluck through before it clamps down |
| Pot 4 | Rel (release): 40 ms – 1 s |
| Pot 5 | Blend: dry to compressed. Below full, the dry attack stays on top (parallel compression) |
| Pot 6 | Level: −18 to +18 dB, noon is unity |
| SW 1 | Mode: Fast (peak detector, grabs every pluck) / Opto (smooth, quick-then-slow release) |
| SW 4 | Detect: Full / HPF (the detector ignores lows below ~100 Hz, so open low strings don't pump) |

The screen's divider shows gain reduction (full width = 20 dB). On the stock panel Detect is on SW 2.

**Drive** (one page on either panel)

| Control | Function |
|---|---|
| Pot 1 | Drive: 0 to +40 dB into the clipper. The output level is compensated, so this changes grit, not volume |
| Pot 2 | Tone: low-pass on the driven signal, 500 Hz – 10 kHz |
| Pot 3 | Mid: ±12 dB at 800 Hz on the driven signal, noon is flat |
| Pot 4 | Xover: 50–400 Hz, where the clean lows end (Lows = Clean) |
| Pot 5 | Blend: clean to driven. With Lows clean it blends only the upper band |
| Pot 6 | Level: −18 to +18 dB, noon is unity |
| SW 1 | Char: Grit (asymmetric soft clipping, warm) / Fuzz (+12 dB into a harder clipper, buzzy) |
| SW 4 | Lows: Dirty (distort everything) / Clean (keep everything below Xover dry) |

The clipping runs at twice the sample rate to keep aliasing down. The screen's divider shows how hard you're clipping. On the stock panel Lows is on SW 2 (and SW 3, SW 4 do nothing on this page).

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

**Stock panel LEDs (no screen)**
- Preset mode: LED 1 blinks the preset number after a change, then stays lit. LED 2 shows activity.
- Edit mode: LED 1 = current effect on, LED 2 = its activity.
- Tuner: LED 1 blinks when you're **flat**, LED 2 when you're **sharp**, faster when further off. **Both solid** = in tune (±3 cents). **Slow heartbeat** = no note.

**Pages:** each effect gets as many pages as it needs; in Edit mode FS 2 steps through all of them. The pots edit the current page. After switching pages, a pot does nothing until you turn it past the effect's stored value ("pickup"), so the sound never jumps. On the OLED, the bar shows the stored value and a tick shows where the pot actually is: turn the tick to the end of the bar. Toggles only act when flipped.

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

**Memory budget:** the stock build uses 117 KB of the Seed's 128 KB internal flash (92%), so the next effect will probably need the bootloader. With the bootloader the screen build uses 144 KB of about 480 KB.

**Presets on the pedal** live in the last 64 KB of the Seed's 8 MB QSPI flash (offset `0x7F0000`), clear of the bootloader's program area. First boot writes the factory presets there. Presets saved by an older firmware with 8 slots are kept, in P1–P8.

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
