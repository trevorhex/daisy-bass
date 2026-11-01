#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * Minimal WAV I/O for the desktop harness: reads 16/24/32-bit PCM and
 * 32-bit float, mixes to mono; writes mono 24-bit PCM.
 */
namespace wav {

struct Audio {
    float sampleRate = 48000.f;
    std::vector<float> samples; // mono, -1..1
};

namespace detail {
inline uint32_t U32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
inline uint16_t U16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

inline void Put(std::ofstream& f, uint32_t v, int bytes) {
    for (int i = 0; i < bytes; i++) f.put(char((v >> (8 * i)) & 0xff));
}
} // namespace detail

inline Audio Read(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("can't open " + path);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), {});
    if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) || std::memcmp(data.data() + 8, "WAVE", 4))
        throw std::runtime_error(path + " is not a WAV file");

    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t* pcm = nullptr;
    size_t pcmBytes = 0;

    for (size_t pos = 12; pos + 8 <= data.size();) {
        const uint8_t* chunk = data.data() + pos;
        const uint8_t* body = chunk + 8;
        const size_t available = data.size() - pos - 8;
        size_t size = detail::U32(chunk + 4);
        // Recorders that were stopped early (or stream to disk) can leave a
        // data size that runs past the end of the file: use what's there.
        const bool truncated = size > available;
        if (truncated) size = available;

        if (!std::memcmp(chunk, "fmt ", 4)) {
            if (size < 16) throw std::runtime_error(path + ": malformed fmt chunk");
            format = detail::U16(body);
            channels = detail::U16(body + 2);
            rate = detail::U32(body + 4);
            bits = detail::U16(body + 14);
            if (format == 0xFFFE && size >= 26) format = detail::U16(body + 24); // WAVE_FORMAT_EXTENSIBLE
        } else if (!std::memcmp(chunk, "data", 4)) {
            pcm = body;
            pcmBytes = size;
        }
        if (truncated) break;
        pos += 8 + size + (size & 1);
    }

    if (!pcm || !channels) throw std::runtime_error(path + ": missing fmt or data chunk");
    const bool isFloat = format == 3 && bits == 32;
    if (!(format == 1 && (bits == 16 || bits == 24 || bits == 32)) && !isFloat)
        throw std::runtime_error(path + ": only 16/24/32-bit PCM or 32-bit float WAVs are supported");

    const size_t bytesPerSample = bits / 8;
    const size_t frames = pcmBytes / (bytesPerSample * channels);
    Audio out;
    out.sampleRate = float(rate);
    out.samples.resize(frames);

    for (size_t i = 0; i < frames; i++) {
        float sum = 0.f;
        for (size_t c = 0; c < channels; c++) {
            const uint8_t* s = pcm + (i * channels + c) * bytesPerSample;
            float v;
            if (isFloat) {
                std::memcpy(&v, s, 4);
            } else if (bits == 16) {
                v = int16_t(detail::U16(s)) / 32768.f;
            } else if (bits == 24) {
                const uint32_t u = (uint32_t(s[0]) << 8) | (uint32_t(s[1]) << 16) | (uint32_t(s[2]) << 24);
                v = float(int32_t(u) >> 8) / 8388608.f;
            } else {
                v = int32_t(detail::U32(s)) / 2147483648.f;
            }
            sum += v;
        }
        out.samples[i] = sum / channels;
    }
    return out;
}

inline void Write(const std::string& path, const Audio& audio) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("can't write " + path);

    const uint32_t dataBytes = uint32_t(audio.samples.size() * 3);
    const uint32_t pad = dataBytes & 1; // RIFF chunks are word-aligned
    const uint32_t rate = uint32_t(audio.sampleRate);
    f.write("RIFF", 4);
    detail::Put(f, 36 + dataBytes + pad, 4);
    f.write("WAVEfmt ", 8);
    detail::Put(f, 16, 4);
    detail::Put(f, 1, 2);        // PCM
    detail::Put(f, 1, 2);        // mono
    detail::Put(f, rate, 4);
    detail::Put(f, rate * 3, 4); // byte rate
    detail::Put(f, 3, 2);        // block align
    detail::Put(f, 24, 2);
    f.write("data", 4);
    detail::Put(f, dataBytes, 4);

    for (float s : audio.samples) {
        s = s > 1.f ? 1.f : (s < -1.f ? -1.f : s);
        detail::Put(f, uint32_t(int32_t(s * 8388607.f)), 3);
    }
    if (pad) f.put(0);
}

} // namespace wav
