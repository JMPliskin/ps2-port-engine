/*
 * Music of the host's own (music.h): an MP3 decoded as it plays (minimp3,
 * public domain: the file read whole, a frame of it decoded at a time) and
 * mixed into the SPU2's output, resampled to its 48 kHz by straight lines
 * between samples.
 */

#include "ps2host/music.h"
#include "ps2host/run.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <vector>

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

namespace ps2host {

namespace {

constexpr int kRate = 48000;            // the SPU2's
constexpr float kRamp = 1.0f / 480.0f;  // the gain's step a sample: 10 ms from silence to full

struct Song {
    std::vector<std::uint8_t> file;
    std::size_t at = 0;                 // the next frame's byte
    mp3dec_t decoder{};
    int channels = 2, hz = 44100;
    // The samples decoded and not yet passed (stereo, as floats), and where
    // between the first two the output is.
    std::vector<float> pending;
    std::size_t first = 0;              // pending's frame the output is past
    double phase = 0;
    bool ended = false;
    double played_frames = 0;           // in the song's own rate
    double total_frames = 0;
};

struct Music {
    std::mutex lock;
    bool loaded = false, paused = false;
    Song song;
    float volume = 1.0f, gain = 0.0f;
};

Music& M() {
    static Music m;
    return m;
}

/* Where an ID3v2 tag ends (its frames, and album art, can be large: the
   decoder's own search for a frame would give up inside one). */
std::size_t AfterId3(const std::vector<std::uint8_t>& f) {
    if (f.size() < 10 || f[0] != 'I' || f[1] != 'D' || f[2] != '3') return 0;
    const std::size_t size = (std::size_t(f[6] & 0x7F) << 21) | (std::size_t(f[7] & 0x7F) << 14) |
                             (std::size_t(f[8] & 0x7F) << 7) | std::size_t(f[9] & 0x7F);
    const std::size_t footer = (f[5] & 0x10) ? 10 : 0;
    return std::min(f.size(), 10 + size + footer);
}

/* The next frame's samples onto `pending`; false at the end. */
bool DecodeMore(Song& s) {
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    while (s.at < s.file.size()) {
        mp3dec_frame_info_t info{};
        const int samples = mp3dec_decode_frame(&s.decoder, s.file.data() + s.at, int(s.file.size() - s.at), pcm, &info);
        if (info.frame_bytes <= 0) break;               // nothing more it can find
        s.at += std::size_t(info.frame_bytes);
        if (samples <= 0) continue;                     // a skipped frame, or junk
        s.channels = info.channels;
        s.hz = info.hz;
        for (int i = 0; i < samples; ++i) {
            const float l = pcm[i * info.channels] * (1.0f / 32768.0f);
            const float r = info.channels > 1 ? pcm[i * info.channels + 1] * (1.0f / 32768.0f) : l;
            s.pending.push_back(l);
            s.pending.push_back(r);
        }
        return true;
    }
    return false;
}

}  // namespace

bool MusicPlay(const std::string& path, double from_seconds, bool paused) {
    Song s;
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (size > 0) {
            s.file.resize(std::size_t(size));
            if (std::fread(s.file.data(), 1, s.file.size(), f) != s.file.size()) s.file.clear();
        }
        std::fclose(f);
    }
    s.at = AfterId3(s.file);
    // Its length: the frames counted, not decoded (pcm null).
    {
        mp3dec_t counter;
        mp3dec_init(&counter);
        std::size_t at = s.at;
        while (at < s.file.size()) {
            mp3dec_frame_info_t info{};
            const int samples = mp3dec_decode_frame(&counter, s.file.data() + at, int(s.file.size() - at), nullptr, &info);
            if (info.frame_bytes <= 0) break;
            at += std::size_t(info.frame_bytes);
            if (samples > 0 && info.hz > 0) {
                s.total_frames += samples;
                s.hz = info.hz;
            }
        }
    }
    // From a point in: the frames before it passed over without decoding
    // (the decoder starts afresh there - a frame's overlap with the one
    // before is lost, a few milliseconds no one hears).
    if (from_seconds > 0 && s.total_frames > 0) {
        mp3dec_t skipper;
        mp3dec_init(&skipper);
        const double target = from_seconds * s.hz;
        while (s.at < s.file.size() && s.played_frames < target) {
            mp3dec_frame_info_t info{};
            const int samples = mp3dec_decode_frame(&skipper, s.file.data() + s.at, int(s.file.size() - s.at), nullptr, &info);
            if (info.frame_bytes <= 0) break;
            if (s.played_frames + samples > target) break;
            s.at += std::size_t(info.frame_bytes);
            s.played_frames += samples;
        }
    }
    mp3dec_init(&s.decoder);
    const bool ok = s.total_frames > 0 && DecodeMore(s);
    Music& m = M();
    std::lock_guard<std::mutex> hold(m.lock);
    m.loaded = ok;
    m.paused = paused;
    m.gain = 0.0f;                      // faded in from silence
    m.song = ok ? std::move(s) : Song{};
    if (!ok) std::fprintf(stderr, "[music] %s: no MP3 frames to play\n", path.c_str());
    return ok;
}

void MusicStop() {
    Music& m = M();
    std::lock_guard<std::mutex> hold(m.lock);
    m.loaded = false;
    m.song = Song{};
}

void MusicPause(bool paused) {
    Music& m = M();
    std::lock_guard<std::mutex> hold(m.lock);
    m.paused = paused;
}

void MusicSetVolume(float volume) {
    Music& m = M();
    std::lock_guard<std::mutex> hold(m.lock);
    m.volume = std::clamp(volume, 0.0f, 1.0f);
}

bool MusicLoaded() {
    Music& m = M();
    std::lock_guard<std::mutex> hold(m.lock);
    return m.loaded;
}

bool MusicEnded() {
    Music& m = M();
    std::lock_guard<std::mutex> hold(m.lock);
    return m.loaded && m.song.ended;
}

double MusicPosition() {
    Music& m = M();
    std::lock_guard<std::mutex> hold(m.lock);
    return m.loaded && m.song.hz ? m.song.played_frames / m.song.hz : 0.0;
}

double MusicLength() {
    Music& m = M();
    std::lock_guard<std::mutex> hold(m.lock);
    return m.loaded && m.song.hz ? m.song.total_frames / m.song.hz : 0.0;
}

void MusicBlank() {
    if (AudioActive()) return;
    Music& m = M();
    std::lock_guard<std::mutex> hold(m.lock);
    if (!m.loaded || m.paused || m.song.ended || !m.song.hz) return;
    m.song.played_frames += m.song.hz / 59.94;      // NTSC's blanks
    if (m.song.played_frames >= m.song.total_frames) m.song.ended = true;
}

bool MusicMixing() {
    Music& m = M();
    std::lock_guard<std::mutex> hold(m.lock);
    return m.loaded && !m.song.ended;
}

void MusicMix(short* frames, unsigned count) {
    Music& m = M();
    std::lock_guard<std::mutex> hold(m.lock);
    if (!m.loaded || m.paused || m.song.ended) return;
    Song& s = m.song;
    const double step = double(s.hz) / kRate;
    for (unsigned k = 0; k < count; ++k) {
        // Two frames to draw between: decoded as needed.
        while (s.pending.size() / 2 < s.first + 2) {
            if (!DecodeMore(s)) {
                s.ended = true;
                break;
            }
        }
        if (s.ended) break;
        const float t = float(s.phase);
        const float* a = &s.pending[2 * s.first];
        const float l = a[0] + (a[2] - a[0]) * t, r = a[1] + (a[3] - a[1]) * t;
        if (m.gain < m.volume) m.gain = std::min(m.volume, m.gain + kRamp);
        else if (m.gain > m.volume) m.gain = std::max(m.volume, m.gain - kRamp);
        const int left = int(frames[2 * k]) + int(std::lround(l * m.gain * 32767.0f));
        const int right = int(frames[2 * k + 1]) + int(std::lround(r * m.gain * 32767.0f));
        frames[2 * k] = short(std::clamp(left, -32768, 32767));
        frames[2 * k + 1] = short(std::clamp(right, -32768, 32767));
        s.phase += step;
        const std::size_t whole = std::size_t(s.phase);
        s.phase -= double(whole);
        s.first += whole;
        s.played_frames += step;
        // What is behind dropped now and then (not every sample: a vector's
        // front erase moves the rest).
        if (s.first > 4096) {
            s.pending.erase(s.pending.begin(), s.pending.begin() + std::ptrdiff_t(2 * s.first));
            s.first = 0;
        }
    }
}

}  // namespace ps2host
