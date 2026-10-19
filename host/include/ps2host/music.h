/*
 * Music of the host's own: an MP3 played over the console's sound.
 *
 * Mixed into the SPU2's output as it comes (AudioFrames), so it keeps the
 * emulated clock: it pauses when the game does not run, and a recording
 * (--audio-wav) has it too. One song at a time; a game's port decides when
 * (Burnout 3's custom EA Trax songs: games/burnout3/port/custom_songs.cpp).
 */

#ifndef PS2HOST_MUSIC_H
#define PS2HOST_MUSIC_H

#include <string>

namespace ps2host {

/* Plays the MP3 at `path` from `from_seconds` in (its start by default), in
   place of any playing; false (and nothing playing) if it cannot be read or
   holds no MP3 frames. Paused from the start with `paused`. */
bool MusicPlay(const std::string& path, double from_seconds = 0, bool paused = false);
void MusicStop();
/* Held where it is (and silent) while paused. */
void MusicPause(bool paused);
/* 0 to 1, reached over a few milliseconds (no click). */
void MusicSetVolume(float volume);
/* A song started and not stopped: playing, paused, or played to its end. */
bool MusicLoaded();
/* Played to its end since MusicPlay. */
bool MusicEnded();
/* The seconds played, and the song's length (0 until it is known: the file's
   frames counted as it is opened). */
double MusicPosition();
double MusicLength();

/* The SPU2's frames (stereo, 48 kHz) with the music mixed in: audio.cpp's,
   before they go out. */
void MusicMix(short* frames, unsigned count);
bool MusicMixing();
/* A vertical blank (devices.cpp's FrameDone): with no sound going out (no
   device, no --audio-wav), nothing mixes, so the song goes on by the
   emulated clock instead - silent, and ending when it would have. */
void MusicBlank();

}  // namespace ps2host

#endif  // PS2HOST_MUSIC_H
