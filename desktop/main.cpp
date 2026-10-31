// bassfx: run the pedal's DSP on WAV files on your computer.
//
//   bassfx --demo renders/riff.wav                 # write a synthetic bass riff
//   bassfx renders/riff.wav renders/wah.wav --sens 0.7 --res 0.6
//   afplay renders/wah.wav

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../dsp/EnvelopeFilter.h"
#include "TestSignal.h"
#include "Wav.h"

namespace {

void Usage() {
    std::puts(
        "usage: bassfx <in.wav> <out.wav> [options]\n"
        "       bassfx --demo <out.wav>          write a synthetic bass riff\n"
        "\n"
        "Knobs (0..1, like the pedal's pots):\n"
        "  --sens   <v>   sensitivity        (default 0.5)\n"
        "  --range  <v>   sweep start        (default 0.3)\n"
        "  --res    <v>   resonance          (default 0.5)\n"
        "  --resp   <v>   envelope response  (default 0.4)\n"
        "  --mix    <v>   dry/wet            (default 1.0)\n"
        "  --level  <v>   output, 0.5 = unity (default 0.5)\n"
        "Switches:\n"
        "  --bp           band-pass instead of low-pass\n"
        "  --down         sweep down instead of up\n"
        "  --full-range   filter the lows too (no clean-low crossover)\n"
        "  --xover <hz>   crossover frequency (default 150)\n"
        "  --block <n>    block size, like the pedal's audio callback (default 48)");
}

float Arg(int& i, int argc, char** argv) {
    if (i + 1 >= argc) {
        std::fprintf(stderr, "missing value for %s\n", argv[i]);
        std::exit(2);
    }
    return std::strtof(argv[++i], nullptr);
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && !std::strcmp(argv[1], "--demo")) {
        wav::Audio audio{48000.f, testsignal::BassRiff(48000.f)};
        wav::Write(argv[2], audio);
        std::printf("wrote %s (%.1f s)\n", argv[2], double(audio.samples.size() / audio.sampleRate));
        return 0;
    }

    if (argc < 3) {
        Usage();
        return 2;
    }

    bass::EnvelopeFilter::Params p;
    int block = 48;
    for (int i = 3; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--sens") p.sensitivity = Arg(i, argc, argv);
        else if (a == "--range") p.range = Arg(i, argc, argv);
        else if (a == "--res") p.resonance = Arg(i, argc, argv);
        else if (a == "--resp") p.response = Arg(i, argc, argv);
        else if (a == "--mix") p.mix = Arg(i, argc, argv);
        else if (a == "--level") p.level = Arg(i, argc, argv);
        else if (a == "--bp") p.mode = bass::EnvelopeFilter::Mode::BandPass;
        else if (a == "--down") p.sweepDown = true;
        else if (a == "--full-range") p.cleanLows = false;
        else if (a == "--xover") p.crossoverHz = Arg(i, argc, argv);
        else if (a == "--block") block = int(Arg(i, argc, argv));
        else {
            std::fprintf(stderr, "unknown option %s\n\n", a.c_str());
            Usage();
            return 2;
        }
    }

    try {
        wav::Audio audio = wav::Read(argv[1]);
        bass::EnvelopeFilter fx;
        fx.Init(audio.sampleRate);

        // Mirror the pedal: params are applied once per block, samples processed inside it.
        float peak = 0.f;
        for (size_t start = 0; start < audio.samples.size(); start += block) {
            fx.SetParams(p);
            const size_t end = std::min(audio.samples.size(), start + size_t(block));
            for (size_t i = start; i < end; i++) {
                audio.samples[i] = fx.Process(audio.samples[i]);
                peak = std::fmax(peak, std::fabs(audio.samples[i]));
            }
        }

        wav::Write(argv[2], audio);
        std::printf("wrote %s (peak %.2f%s)\n", argv[2], double(peak), peak > 1.f ? ", CLIPPED: lower --level" : "");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
