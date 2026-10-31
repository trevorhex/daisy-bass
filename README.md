# daisy-bass

A bass effects pedal built on an [Electro-Smith Daisy Seed](https://electro-smith.com/products/daisy-seed) and a [PedalPCB Terrarium](https://www.pedalpcb.com/product/pcb351/) in a 125B enclosure.

The first effect is an **envelope filter** voiced for bass: a resonant filter that opens as you dig in, with an optional crossover that keeps everything below 150/250 Hz dry so the low end never thins out.

## Layout

```
dsp/        Pure C++ effects with no hardware dependencies (shared by desktop and pedal)
  Svf.h               state-variable filter (stable under fast modulation)
  Crossover.h         4th-order Linkwitz-Riley split for clean lows
  EnvelopeFollower.h  pluck detector (attack/release)
  Smoother.h          de-zippers knob moves
  EnvelopeFilter.h    the effect
desktop/    bassfx: run the DSP on WAV files on your Mac
tests/      DSP tests (plain C++, no framework)
firmware/   Daisy/Terrarium glue: controls, audio callback, LEDs
renders/    (git-ignored) audio you render locally
```

The rule: **anything that touches sound lives in `dsp/` and is tested on the desktop.** The firmware only reads controls and calls `SetParams()` / `Process()`.

## Desktop workflow

```bash
cmake -S . -B build && cmake --build build
./build/dsp_tests

./build/bassfx --demo renders/riff.wav                       # synthetic bass riff
./build/bassfx renders/riff.wav renders/wah.wav --sens 0.6 --res 0.6
afplay renders/wah.wav
```

Better still, record a dry DI track of your own bass (any WAV: 16/24/32-bit or float, mono or stereo) and run it through. `./build/bassfx` with no arguments lists every option. Each knob is 0–1, exactly like the pots.

## Pedal controls

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
| SW 3 | Clean lows on / off |
| SW 4 | Crossover at 150 Hz / 250 Hz |
| FS 1 | Bypass (LED 1 lit = on). A click-free software crossfade |
| FS 2 | Reserved for the next effect. For now, LED 2 flickers with your playing |

## Firmware setup (one time)

You need the ARM toolchain and Electro-Smith's two libraries:

```bash
brew install --cask gcc-arm-embedded      # asks for your password (pkg installer)
brew install dfu-util
git submodule update --init --recursive   # libDaisy + DaisySP in lib/
make -C lib/libDaisy && make -C lib/DaisySP
```

Then:

```bash
make -C firmware                 # build → firmware/build/daisy-bass.bin
make -C firmware program-dfu     # flash: hold BOOT, tap RESET on the Seed first
```

You can also flash `firmware/build/daisy-bass.bin` with Electro-Smith's [web programmer](https://electro-smith.github.io/Programmer/).

**First flash:** build the control-mapping diagnostic, flash it, and open a serial monitor:

```bash
make -C firmware clean && make -C firmware KNOB_CHECK=1 && make -C firmware program-dfu
screen /dev/tty.usbmodem* 115200
```

Turn each pot and flip each switch to confirm the mapping in `firmware/terrarium.h` matches your board. Then rebuild normally with `make -C firmware clean && make -C firmware`.

**Flash budget:** the Seed's internal flash is 128 KB and this build uses about 100 KB (77%). Before adding several more effects, switch to Electro-Smith's bootloader (`APP_TYPE = BOOT_SRAM` in `firmware/Makefile`), which allows programs of roughly 480 KB.

## Building the Terrarium (125B)

From PedalPCB's build doc. Check their current PDF before ordering.

- **Board:** TL072 input/output buffers (so the Daisy's line-level codec sees a proper instrument input), an L78L05 regulator feeding the Seed from the standard 9 V center-negative supply, and a 1N5817 reverse-polarity diode.
- **Controls:** 6 × B10K 16 mm right-angle PCB-mount pots, 4 × SPDT ON/ON toggles, 2 × momentary SPST footswitches, 2 × LEDs.
- **Seed:** plugs into 2 × 20-pin female 2.54 mm headers, so it stays removable. There's little room under the Seed, so check that the parts underneath fit before soldering.
- **Enclosure layout (top to bottom):** jacks and DC on the top edge, two rows of three pots, a row of four toggles, two LEDs, then two footswitches. PedalPCB's doc includes a 125B drill template.
