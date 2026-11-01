// pedalsim: the daisy-bass pedal on your Mac.
//
// Runs the real pedal code (control/Pedal.h: effects, presets, footswitch
// and page logic) on live audio, and draws the screen with the real screen
// code (ui/Screens.h) and the pedal's own fonts. Only the hardware is fake:
// pots, toggles and footswitches are drawn on a 125B enclosure.
//
// Threads mirror the pedal: the SDL audio callback is the pedal's audio
// callback (controls + DSP every 48 samples = 1 ms); the UI loop is its main
// loop (snapshot → screen, tuner analysis, preset saving).

#define SDL_MAIN_HANDLED

#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "../control/Pedal.h"
#include "../desktop/TestSignal.h"
#include "../desktop/Wav.h"
#include "../ui/Framebuffer.h"
#include "../ui/Screens.h"

namespace {

constexpr float kSampleRate = 48000.f;
constexpr int kNumPots = 6;
constexpr int kNumToggles = 2; // screen panel: SW 1 and SW 4

// ---------------------------------------------------------------------------
// Audio input sources

/** Single-producer/single-consumer ring for live input (capture thread → output thread). */
class LiveRing {
public:
    void Push(const float* x, size_t n) {
        size_t w = write_.load(std::memory_order_relaxed);
        for (size_t i = 0; i < n; i++) buf_[(w + i) & kMask] = x[i];
        write_.store(w + n, std::memory_order_release);
    }
    /** Pops up to n samples, pads with silence. Drops backlog so latency stays low. */
    void Pop(float* out, size_t n) {
        const size_t w = write_.load(std::memory_order_acquire);
        size_t r = read_.load(std::memory_order_relaxed);
        if (w - r > kMaxBacklog) r = w - kTargetBacklog;
        size_t i = 0;
        for (; i < n && r != w; i++, r++) out[i] = buf_[r & kMask];
        for (; i < n; i++) out[i] = 0.f;
        read_.store(r, std::memory_order_relaxed);
    }

private:
    static constexpr size_t kSize = 1 << 15, kMask = kSize - 1;
    static constexpr size_t kMaxBacklog = 4096, kTargetBacklog = 512;
    float buf_[kSize] = {};
    std::atomic<size_t> write_{0}, read_{0};
};

enum Source { kDemo, kFile, kLive, kSilence };
const char* const kSourceNames[] = {"Demo riff (loop)", "WAV file (loop)", "Live input", "Silence"};

/** Linear resample to 48 kHz (good enough for auditioning). */
std::vector<float> To48k(const wav::Audio& a) {
    if (a.sampleRate == kSampleRate || a.samples.empty()) return a.samples;
    const double ratio = double(a.sampleRate) / double(kSampleRate);
    const size_t n = size_t(double(a.samples.size()) / ratio);
    std::vector<float> out(n);
    for (size_t i = 0; i < n; i++) {
        const double pos = double(i) * ratio;
        const size_t j = size_t(pos);
        const float frac = float(pos - double(j));
        const float a0 = a.samples[j], a1 = j + 1 < a.samples.size() ? a.samples[j + 1] : a0;
        out[i] = a0 + frac * (a1 - a0);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Everything the audio thread and the UI thread share. Guarded by `mutex`,
// which plays the role of the pedal's "interrupts blocked" section.

struct Shared {
    std::mutex mutex;
    bass::Pedal pedal;
    bass::PedalUi::Inputs inputs; // the physical controls, as set in the UI
    uint32_t nowMs = 0;           // advances 1 per 48-sample block, like System::GetNow()

    int source = kDemo;
    std::vector<float> demo, file;
    size_t playPos = 0;
    float inputGainDb = 0.f, outputGainDb = 0.f;
    LiveRing live;

    std::atomic<float> cpu{0.f}; // fraction of real time spent in the audio callback
};

Shared g;

void NextInput(float* in, size_t n) {
    const std::vector<float>* clip = g.source == kDemo ? &g.demo : g.source == kFile ? &g.file : nullptr;
    if (g.source == kLive) {
        g.live.Pop(in, n);
    } else if (clip && !clip->empty()) {
        for (size_t i = 0; i < n; i++) {
            if (g.playPos >= clip->size()) g.playPos = 0;
            in[i] = (*clip)[g.playPos++];
        }
    } else {
        for (size_t i = 0; i < n; i++) in[i] = 0.f;
    }
    const float gain = std::pow(10.f, g.inputGainDb / 20.f);
    for (size_t i = 0; i < n; i++) in[i] *= gain;
}

void AudioOut(void*, Uint8* stream, int len) {
    const auto start = std::chrono::steady_clock::now();
    float* out = reinterpret_cast<float*>(stream);
    const size_t frames = size_t(len) / (2 * sizeof(float));
    {
        std::lock_guard<std::mutex> lock(g.mutex);
        const float gain = std::pow(10.f, g.outputGainDb / 20.f);
        float in[bass::Pedal::kBlockSize], mono[bass::Pedal::kBlockSize];
        for (size_t f = 0; f < frames; f += bass::Pedal::kBlockSize) {
            const size_t n = std::min(bass::Pedal::kBlockSize, frames - f);
            NextInput(in, n);
            g.pedal.Process(g.inputs, ++g.nowMs, in, mono, n);
            for (size_t i = 0; i < n; i++) {
                const float y = std::clamp(mono[i] * gain, -1.f, 1.f);
                out[2 * (f + i)] = out[2 * (f + i) + 1] = y;
            }
        }
    }
    const double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const float load = float(used / (double(frames) / double(kSampleRate)));
    g.cpu.store(0.95f * g.cpu.load() + 0.05f * load);
}

void AudioIn(void*, Uint8* stream, int len) {
    g.live.Push(reinterpret_cast<const float*>(stream), size_t(len) / sizeof(float));
}

// ---------------------------------------------------------------------------
// Presets file

std::string PresetPath() {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.daisy-bass-presets.bin";
}

bool LoadPresets(bass::PresetBank& bank) {
    std::ifstream f(PresetPath(), std::ios::binary);
    if (!f) return false;
    f.read(reinterpret_cast<char*>(&bank), sizeof bank);
    return f.gcount() == std::streamsize(sizeof bank) && bank.Valid();
}

void SavePresets(const bass::PresetBank& bank) {
    std::ofstream f(PresetPath(), std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(&bank), sizeof bank);
}

// ---------------------------------------------------------------------------
// Drawing the pedal

// Layout in millimetres on a 125B enclosure (66.5 × 122 mm), scaled to points.
constexpr float kMm = 5.4f;
constexpr float kBoxW = 66.5f, kBoxH = 122.f;
const ImVec2 kPotMm[kNumPots] = {{14.f, 24.f}, {33.25f, 24.f}, {52.5f, 24.f},
                                 {14.f, 44.f}, {33.25f, 44.f}, {52.5f, 44.f}};
const ImVec2 kToggleMm[kNumToggles] = {{9.5f, 62.f}, {57.f, 62.f}};
const char* const kToggleNames[kNumToggles] = {"SW 1", "SW 4"};
const ImVec2 kOledMm = {33.25f, 63.f};
const ImVec2 kFootMm[2] = {{16.6f, 100.f}, {49.9f, 100.f}};

ImU32 Rgb(int r, int gr, int b, int a = 255) { return IM_COL32(r, gr, b, a); }

struct PedalView {
    ImVec2 origin;
    ImVec2 P(ImVec2 mm) const { return {origin.x + mm.x * kMm, origin.y + mm.y * kMm}; }
};

/** A pot: drag up/down (or scroll) to turn; Shift for fine control. Returns true while held. */
void Knob(ImDrawList* dl, const PedalView& pv, int index, float& value, const bass::PageView& view) {
    const ImVec2 c = pv.P(kPotMm[index]);
    const float r = 6.6f * kMm;
    ImGui::SetCursorScreenPos({c.x - r, c.y - r});
    ImGui::PushID(index);
    ImGui::InvisibleButton("pot", {2 * r, 2 * r});
    ImGuiIO& io = ImGui::GetIO();
    const float fine = io.KeyShift ? 0.2f : 1.f;
    if (ImGui::IsItemActive()) value += (io.MouseDelta.x - io.MouseDelta.y) * 0.004f * fine;
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.f) value += io.MouseWheel * 0.02f * fine;
    value = std::clamp(value, 0.f, 1.f);
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        const bass::PageView::Control& ctl = view.pots[index];
        if (ctl.used)
            ImGui::SetTooltip("Pot %d: %s\npot at %d, value %d%s", index + 1, ctl.info.name, int(value * 100.f),
                              int(std::lround(ctl.value * 100.f)), ctl.caught ? "" : "\n(turn to the value to pick up)");
        else
            ImGui::SetTooltip("Pot %d: unused on this page", index + 1);
    }
    ImGui::PopID();

    // Body, skirt, pointer. 300° travel, from 7 o'clock to 5 o'clock.
    dl->AddCircleFilled({c.x + 2, c.y + 3}, r, Rgb(0, 0, 0, 70), 48);
    dl->AddCircleFilled(c, r, Rgb(28, 28, 32), 48);
    dl->AddCircle(c, r, Rgb(70, 70, 78), 48, 2.f);
    dl->AddCircleFilled(c, r * 0.72f, Rgb(44, 44, 50), 48);
    const float angle = (-150.f + 300.f * value - 90.f) * 3.14159265f / 180.f;
    const ImVec2 dir = {std::cos(angle), std::sin(angle)};
    dl->AddLine({c.x + dir.x * r * 0.25f, c.y + dir.y * r * 0.25f}, {c.x + dir.x * r * 0.92f, c.y + dir.y * r * 0.92f},
                Rgb(235, 235, 235), 3.f);
    char label[8];
    std::snprintf(label, sizeof label, "%d", index + 1);
    dl->AddText({c.x - 3, c.y + r + 3}, Rgb(200, 205, 215, 160), label);
}

void Toggle(ImDrawList* dl, const PedalView& pv, int index, bool& on) {
    const ImVec2 c = pv.P(kToggleMm[index]);
    ImGui::SetCursorScreenPos({c.x - 18, c.y - 26});
    ImGui::PushID(100 + index);
    if (ImGui::InvisibleButton("toggle", {36, 52})) on = !on;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s: click to flip (up = on)", kToggleNames[index]);
    ImGui::PopID();

    dl->AddCircleFilled(c, 12.f, Rgb(150, 152, 158), 6); // hex nut
    dl->AddCircleFilled(c, 7.f, Rgb(205, 207, 212), 24);
    const ImVec2 tip = {c.x, c.y + (on ? -22.f : 22.f)};
    dl->AddLine(c, tip, Rgb(225, 227, 232), 6.f);
    dl->AddCircleFilled(tip, 4.5f, Rgb(240, 241, 244), 16);
    dl->AddText({c.x - 14, c.y + 28}, Rgb(200, 205, 215, 160), kToggleNames[index]);
}

/** A momentary footswitch: hold the mouse button on it, or the key. */
bool Footswitch(ImDrawList* dl, const PedalView& pv, int index, ImGuiKey key) {
    const ImVec2 c = pv.P(kFootMm[index]);
    const float r = 6.f * kMm;
    ImGui::SetCursorScreenPos({c.x - r, c.y - r});
    ImGui::PushID(200 + index);
    ImGui::InvisibleButton("fs", {2 * r, 2 * r});
    const bool down = ImGui::IsItemActive() || ImGui::IsKeyDown(key);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("FS %d: hold the mouse button, or the %s key", index + 1, index ? "X" : "Z");
    ImGui::PopID();

    dl->AddCircleFilled(c, r, Rgb(120, 122, 128), 6);
    dl->AddCircleFilled(c, r * 0.78f, down ? Rgb(150, 152, 158) : Rgb(200, 202, 208), 40);
    dl->AddCircle(c, r * 0.78f, Rgb(90, 92, 98), 40, 2.f);
    dl->AddCircleFilled({c.x, c.y + (down ? 2.f : 0.f)}, r * 0.55f, down ? Rgb(170, 172, 178) : Rgb(225, 227, 232), 40);
    char label[16];
    std::snprintf(label, sizeof label, "FS %d (%s)", index + 1, index ? "X" : "Z");
    dl->AddText({c.x - 26, c.y + r + 6}, Rgb(200, 205, 215, 170), label);
    return down;
}

/** The OLED: one square per pixel, `scale` points each. */
void DrawOled(ImDrawList* dl, ImVec2 topLeft, float scale, const ui::Framebuffer& fb) {
    dl->AddRectFilled(topLeft, {topLeft.x + 128 * scale, topLeft.y + 64 * scale}, Rgb(4, 6, 10));
    const ImU32 lit = Rgb(205, 232, 255);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 128; x++)
            if (fb.Pixel(x, y))
                dl->AddRectFilled({topLeft.x + x * scale, topLeft.y + y * scale},
                                  {topLeft.x + (x + 1) * scale, topLeft.y + (y + 1) * scale}, lit);
}

void DrawEnclosure(ImDrawList* dl, const PedalView& pv) {
    const ImVec2 a = pv.origin, b = pv.P({kBoxW, kBoxH});
    // Jacks on the top edge: output, power, input (Terrarium order, viewed from above).
    const struct { float x; const char* name; } jacks[] = {{12.f, "OUT"}, {33.25f, "9V"}, {54.5f, "IN"}};
    for (const auto& j : jacks) {
        const ImVec2 c = pv.P({j.x, 0.f});
        dl->AddRectFilled({c.x - 14, c.y - 12}, {c.x + 14, c.y + 2}, Rgb(60, 60, 64), 3.f);
        dl->AddText({c.x - 8, c.y - 26}, Rgb(160, 165, 175), j.name);
    }
    dl->AddRectFilled({a.x + 4, a.y + 6}, {b.x + 4, b.y + 6}, Rgb(0, 0, 0, 90), 14.f);
    dl->AddRectFilled(a, b, Rgb(46, 58, 78), 14.f);
    dl->AddRect(a, b, Rgb(80, 96, 122), 14.f, 0, 2.f);
    const ImVec2 t = pv.P({kBoxW / 2, 114.f});
    dl->AddText({t.x - 34, t.y}, Rgb(220, 226, 236, 200), "daisy-bass");
}

// ---------------------------------------------------------------------------
// Side panel helpers

const char* ModeHelp(const bass::PageView& v) {
    if (v.screen == bass::PageView::Screen::Tuner) return "Tuner: tap either footswitch to leave.";
    if (v.editMode)
        return "EDIT mode\n  FS 1 tap   current effect on/off\n  FS 2 tap   next page\n  FS 1 hold  back to preset mode\n"
               "  FS 2 hold  tuner\n  both 1 s   save preset";
    return "PRESET mode\n  FS 1 tap   previous preset\n  FS 2 tap   next preset\n  FS 1 hold  edit mode\n"
           "  FS 2 hold  tuner\n  both 1 s   save preset\n  pots/toggles edit the current page";
}

void ControlsTable(const bass::PageView& v) {
    if (!ImGui::BeginTable("controls", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) return;
    ImGui::TableSetupColumn("Control");
    ImGui::TableSetupColumn("Edits");
    ImGui::TableSetupColumn("Value");
    ImGui::TableHeadersRow();
    for (int p = 0; p < kNumPots; p++) {
        const bass::PageView::Control& c = v.pots[p];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Pot %d", p + 1);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(c.used ? c.info.name : "-");
        ImGui::TableNextColumn();
        if (!c.used) continue;
        if (c.info.isSwitch) ImGui::TextUnformatted(c.value >= 0.5f ? c.info.onLabel : c.info.offLabel);
        else if (c.caught) ImGui::Text("%d", int(std::lround(c.value * 100.f)));
        else ImGui::TextColored({1.f, 0.75f, 0.3f, 1.f}, "%d (pot at %d)", int(std::lround(c.value * 100.f)),
                                int(c.position * 100.f));
    }
    for (int t = 0; t < kNumToggles; t++) {
        const bass::PageView::Control& c = v.toggles[t];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(kToggleNames[t]);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(c.used ? c.info.name : "-");
        ImGui::TableNextColumn();
        if (c.used) ImGui::Text("%s / %s: %s", c.info.offLabel, c.info.onLabel, c.value >= 0.5f ? c.info.onLabel : c.info.offLabel);
    }
    ImGui::EndTable();
}

} // namespace

int main(int, char**) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    // --- Pedal -------------------------------------------------------------
    g.demo = testsignal::BassRiff(kSampleRate, 4);
    for (float& p : g.inputs.pots) p = 0.5f;
    bass::PresetBank saved;
    const bool haveSaved = LoadPresets(saved);
    g.pedal.Init(kSampleRate, kNumPots, kNumToggles, haveSaved ? &saved : nullptr, g.inputs, 0);
    if (!haveSaved) SavePresets(g.pedal.ui.Bank());

    // --- Audio -------------------------------------------------------------
    SDL_AudioSpec want{}, have{};
    want.freq = int(kSampleRate);
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 256;
    want.callback = AudioOut;
    const SDL_AudioDeviceID outDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!outDev) std::fprintf(stderr, "No audio output: %s (the pedal won't run)\n", SDL_GetError());
    else SDL_PauseAudioDevice(outDev, 0);

    SDL_AudioDeviceID inDev = 0;
    int inDevIndex = -1; // -1 = system default
    std::string audioStatus;
    auto openInput = [&]() {
        if (inDev) SDL_CloseAudioDevice(inDev);
        SDL_AudioSpec w{}, h{};
        w.freq = int(kSampleRate);
        w.format = AUDIO_F32SYS;
        w.channels = 1;
        w.samples = 256;
        w.callback = AudioIn;
        const char* name = inDevIndex >= 0 ? SDL_GetAudioDeviceName(inDevIndex, 1) : nullptr;
        inDev = SDL_OpenAudioDevice(name, 1, &w, &h, 0);
        audioStatus = inDev ? "" : std::string("Can't open input: ") + SDL_GetError();
        if (inDev) SDL_PauseAudioDevice(inDev, 0);
    };

    // --- Window ------------------------------------------------------------
    SDL_Window* window = SDL_CreateWindow("daisy-bass simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1140,
                                          780, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (!window || !renderer) {
        std::fprintf(stderr, "SDL window: %s\n", SDL_GetError());
        return 1;
    }
    int winW, winH, pixW, pixH;
    SDL_GetWindowSize(window, &winW, &winH);
    SDL_GetRendererOutputSize(renderer, &pixW, &pixH);
    const float dpi = float(pixW) / float(winW);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    // Crisp text on Retina: rasterize at the real pixel size, draw at logical size.
    ImFontConfig fc;
    fc.SizePixels = 13.f * dpi;
    io.Fonts->AddFontDefault(&fc);
    io.FontGlobalScale = 1.f / dpi;
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    ui::Framebuffer fb;
    ui::TunerNeedle needle;
    std::string fileName = "(drop a WAV file on the window)";
    std::string presetStatus = haveSaved ? "Loaded " + PresetPath() : "New factory presets in " + PresetPath();

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_DROPFILE) {
                try {
                    std::vector<float> clip = To48k(wav::Read(e.drop.file));
                    std::lock_guard<std::mutex> lock(g.mutex);
                    g.file.swap(clip);
                    g.source = kFile;
                    g.playPos = 0;
                    fileName = e.drop.file;
                } catch (const std::exception& ex) {
                    fileName = std::string("error: ") + ex.what();
                }
                SDL_free(e.drop.file);
            }
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // --- The pedal's main loop: snapshot, tuner, save, draw ---------------
        bass::PageView view;
        bass::PresetBank toSave;
        bool save;
        uint32_t now;
        float reference;
        {
            std::lock_guard<std::mutex> lock(g.mutex);
            view = g.pedal.ui.Snapshot();
            save = g.pedal.ui.TakeSaveRequest(toSave);
            now = g.nowMs;
            if (view.screen == bass::PageView::Screen::Tuner) needle.Update(g.pedal.tuner, now);
            reference = g.pedal.tuner.Reference();
        }
        if (save) {
            SavePresets(toSave);
            presetStatus = "Saved P" + std::to_string(toSave.current + 1) + " to " + PresetPath();
        }
        ui::DrawScreen(fb, view, needle, reference, int(g.cpu.load() * 100.f));

        // --- Pedal (left) ----------------------------------------------------
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({430, io.DisplaySize.y});
        ImGui::Begin("pedal", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        PedalView pv{{ImGui::GetWindowPos().x + 34, ImGui::GetWindowPos().y + 44}};
        DrawEnclosure(dl, pv);

        bass::PedalUi::Inputs controls;
        {
            std::lock_guard<std::mutex> lock(g.mutex);
            controls = g.inputs;
        }
        for (int p = 0; p < kNumPots; p++) Knob(dl, pv, p, controls.pots[p], view);
        for (int t = 0; t < kNumToggles; t++) Toggle(dl, pv, t, controls.toggles[t]);
        controls.footswitches[0] = Footswitch(dl, pv, 0, ImGuiKey_Z);
        controls.footswitches[1] = Footswitch(dl, pv, 1, ImGuiKey_X);
        {
            std::lock_guard<std::mutex> lock(g.mutex);
            g.inputs = controls;
        }

        const ImVec2 oc = pv.P(kOledMm);
        dl->AddRectFilled({oc.x - 72, oc.y - 42}, {oc.x + 72, oc.y + 40}, Rgb(18, 40, 70), 4.f); // module PCB
        DrawOled(dl, {oc.x - 64, oc.y - 34}, 1.f, fb);
        ImGui::End();

        // --- Side panel (right) ---------------------------------------------
        ImGui::SetNextWindowPos({430, 0});
        ImGui::SetNextWindowSize({io.DisplaySize.x - 430, io.DisplaySize.y});
        ImGui::Begin("side", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

        ImGui::SeparatorText("Screen (3x)");
        const ImVec2 big = ImGui::GetCursorScreenPos();
        ImGui::Dummy({128 * 3.f, 64 * 3.f});
        DrawOled(ImGui::GetWindowDrawList(), big, 3.f, fb);

        ImGui::SeparatorText("Footswitches");
        ImGui::TextUnformatted(ModeHelp(view));

        ImGui::SeparatorText("Controls on this page");
        ControlsTable(view);

        ImGui::SeparatorText("Audio");
        {
            std::lock_guard<std::mutex> lock(g.mutex);
            int source = g.source;
            if (ImGui::Combo("Source", &source, kSourceNames, IM_ARRAYSIZE(kSourceNames))) {
                g.source = source;
                g.playPos = 0;
                if (source == kLive && !inDev) openInput();
            }
            if (g.source == kFile) ImGui::TextWrapped("File: %s", fileName.c_str());
            if (g.source == kLive) {
                const int n = SDL_GetNumAudioDevices(1);
                const char* current = inDevIndex >= 0 ? SDL_GetAudioDeviceName(inDevIndex, 1) : "System default";
                if (ImGui::BeginCombo("Input", current ? current : "?")) {
                    if (ImGui::Selectable("System default", inDevIndex < 0)) {
                        inDevIndex = -1;
                        openInput();
                    }
                    for (int i = 0; i < n; i++) {
                        const char* name = SDL_GetAudioDeviceName(i, 1);
                        if (ImGui::Selectable(name ? name : "?", inDevIndex == i)) {
                            inDevIndex = i;
                            openInput();
                        }
                    }
                    ImGui::EndCombo();
                }
                if (!audioStatus.empty()) ImGui::TextColored({1.f, 0.5f, 0.4f, 1.f}, "%s", audioStatus.c_str());
                ImGui::TextDisabled("Plug your bass into an audio interface. Use headphones to avoid feedback.");
            }
            ImGui::SliderFloat("Input trim", &g.inputGainDb, -24.f, 24.f, "%+.0f dB");
            ImGui::SliderFloat("Output", &g.outputGainDb, -40.f, 12.f, "%+.0f dB");
        }
        ImGui::Text("CPU (this Mac): %.1f%%", double(g.cpu.load() * 100.f));

        ImGui::SeparatorText("Presets");
        ImGui::TextWrapped("%s", presetStatus.c_str());
        if (ImGui::Button("Reset presets to factory...")) ImGui::OpenPopup("reset");
        if (ImGui::BeginPopupModal("reset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Replace all 8 presets with the factory presets?");
            if (ImGui::Button("Replace")) {
                bass::PresetBank bank;
                {
                    std::lock_guard<std::mutex> lock(g.mutex);
                    bank = g.pedal.FactoryBank();
                    g.pedal.ui.SetBank(bank, g.nowMs);
                }
                SavePresets(bank);
                presetStatus = "Factory presets restored";
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::End();

        ImGui::Render();
        SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 20, 22, 26, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    if (inDev) SDL_CloseAudioDevice(inDev);
    if (outDev) SDL_CloseAudioDevice(outDev);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
