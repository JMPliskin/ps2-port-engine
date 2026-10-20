#pragma once
/*
 * High frame rates for a ported game: more pictures a second than the
 * game's own, the game still stepping at its own rate (its physics, timers
 * and menus as on the console), the pictures between its steps drawn with
 * what moves placed between them. The FRAME RATE setting ("frame_rate":
 * "60", "120" or "display", the screen's own refresh; PS2HOST_FRAME_RATE
 * instead).
 *
 * The engine keeps the rate (the vertical blanks: SetFastForward's whole
 * multiples, SetBlankRate's others), the steps a picture asks for, where the
 * picture is between the game's last two steps, the transforms and camera
 * values taken down after each step, placed for each picture and put back,
 * cuts, stepping down where the machine cannot keep up, and a trace. A game
 * says where its things are (HighFpsGame) and calls in at five places of its
 * own loop (the hooks below, from its overrides). The skill
 * `ps2-high-fps` (engine/.claude/skills) is how a game's are found.
 *
 * Test switches: PS2HOST_HIGH_FPS_FROM=<frame> (60 until then: a scripted
 * run's presses land as at 60), PS2HOST_HIGH_FPS_NO_BLEND=1 (nothing
 * placed), PS2HOST_HIGH_FPS_TRACE=<file> (a CSV line a frame:
 * engine/tools/high_fps_trace.py), PS2HOST_HIGH_FPS_POKE=<hex address> (+0.5
 * to the float there before each picture's draw: whether the draw reads it).
 */

#include <vector>

#include "ps2rt/types.h"

namespace ps2host {

/* Where a game's transforms are: memory scanned after each step for
   RenderWare's RwMatrix (right, up, at, pos - four floats each; the first
   three orthonormal), every 16 bytes. Only memory that holds the game's
   objects: a scan that blends a buffer of something else (a movie's)
   breaks it. */
struct HighFpsRegion {
    u32 begin, end;
};

/* A camera's own values (where it is, the way it looks, its field of view):
   the floats from `address`, blended one by one (only values of 1e-6 to
   1e6 either way: a pointer, a count or a flag reads as a far smaller
   float), its position at float `position` (x, y, z) and the way it looks
   at `direction` (-1: none) - those two for its cuts. */
struct HighFpsCamera {
    u32 address;
    int floats = 16;
    int position = 12;
    int direction = 8;
};

struct HighFpsGame {
    std::vector<HighFpsRegion> transforms;
    std::vector<HighFpsCamera> cameras;
    // Further a step than this (in the game's units): a cut, not motion.
    float cut = 50.0f;
    // The game's own frames going on (HighFpsFrameSteps) within this many
    // HighFpsPicture calls: it is running; else 60 again.
    int running = 8;
    // Down to 60 only after this many HighFpsPicture calls wanting it.
    int settle = 30;
    // Blanks (at 60: so many more above it) without a frame of the game's:
    // it has stopped (a loading screen, a movie) - 60 again (HighFpsBlank).
    int gone = 30;
};

/* The game's, once, at start-up. */
void HighFpsConfigure(const HighFpsGame& game);

// ------------------------------------------------------------ the hooks
//
// In the order a frame of the game's runs them:

/* 1. Its frame starting, before its steps: what was placed for the last
   picture put back (only what is still as placed: what its draw wrote
   anew stays). */
void HighFpsFrameStart();

/* 2. Its steps run; then, once a frame, after them: `steps_so_far` - the
   game's own count of the steps it has run (any counter it keeps, that
   goes up by one a step) - and `blanks`, the vertical blanks the last
   picture took. True: the frame ran a step (at 60, always) - its
   once-a-frame logic that must go once a step runs, then HighFpsStepped. */
bool HighFpsFrameSteps(u32 steps_so_far, unsigned blanks);
void HighFpsStepped();

/* 3. Before its draw, each picture: the rate chosen (as the setting, and
   whether the game is running), what moves placed for the picture. May be
   called more than once a picture. */
void HighFpsPicture();

/* 4. Where it works out how many steps its next frame runs: above 60, as
   many as 60 a second asks for the blanks the picture took (`blanks`), up
   to `most`; -1 at 60 (the game's own count). */
int HighFpsSteps(unsigned blanks, int most);

/* 5. Each vertical blank's start, in every mode of the game (its blank
   handler): back to 60 when its frames have stopped. */
void HighFpsBlank();

/* Above 60: the rate (pictures a second; 0 at the game's own). */
double HighFpsRate();

}  // namespace ps2host
