# Peripherals: SDL2 screen, audio, keyboard, joystick (Phase 7)

The emulation core (Phases 1-6) should have **zero** dependency on SDL2
or any other library — it's plain C operating on in-memory buffers and
callbacks. SDL2 is a Phase 7-only, peripherals-layer dependency, kept
behind a clean boundary (mirrors the same "core has no runtime deps"
discipline worth keeping from the very first CMake/Makefile decision in
Phase 1 — link SDL2 only into the executable target that needs it, not
into a shared core library).

**Why SDL2 specifically**: it's the standard, well-maintained,
genuinely cross-platform (Linux/macOS/Windows) library for exactly this
combination of needs — a window/framebuffer, a pull-based audio
callback, and keyboard/joystick input — with no other dependencies of
its own. It's what real reference emulators in this space are commonly
built on. SDL3 is a reasonable alternative if you want the newer API;
pick one and document which.

**Status check from the Phase 4 session**: the runtime library
(`libsdl2-2.0-0`) is present on the machine this project has been built
on so far, but the development headers (`libsdl2-dev`) are not, and
installing them needs a `sudo apt-get install` — a system-level change
this project deliberately hasn't made without the user's explicit go-
ahead. Confirm/install this before actually starting Phase 7. In the
meantime, `tools/demos/framebuffer_dump.c` dumps the VIC-II's
framebuffer straight to a PPM image with zero new dependencies, as an
early, deliberately partial visual smoke test (no real-time display, no
audio, no input) — see `tools/demos/README.md`. It is not a starting
point for this phase's real SDL2 integration.

**Done.** `libsdl2-dev` (2.30.0) was installed with the user's explicit
go-ahead. `src/frontend/sdl_frontend.c` is the real SDL2 integration:
the only file in the project that includes SDL2 (`make run`'s own
Makefile target links it separately from every other tier, so `unit-
test`/`dormann`/`integration`/`demo` still need zero SDL2 at all — see
`docs/roadmap.md`/Makefile). It owns `Machine` directly and drives
screen, audio, keyboard, and joystick all from one real-time-paced main
loop — see each section below for what was actually built.

## Screen

- The VIC-II (Phase 4) produces pixel data — either a full frame buffer
  (if you took the per-scanline-but-still-buffer-the-frame approach) or
  incrementally scanline-by-scanline (if you want to watch it draw
  live, which is also a good visual debugging tool for verifying
  raster-split effects actually work).
- Blit to an SDL2 texture, present at the **real computed PAL rate**
  (≈50.125Hz — see `docs/vic-ii.md`, not a hardcoded 50) via
  `SDL_RenderPresent`, ideally with vsync where available but don't rely
  on vsync alone for correct emulated timing — the emulation's own real
  frame cadence (from Phase 6) is the source of truth, the display is
  just where you show the result.
- If Phase 6 measured that full-detail rendering every single frame
  competes too much with audio pacing, throttle actual draws to
  1-in-N frames (document the chosen N and why) while still stepping
  CPU/VIC-II/audio at full rate every frame underneath — the emulated
  machine's own timing must never be slowed down to accommodate slow
  drawing.

**Done.** `render_frame()` in `sdl_frontend.c` blits `VicII.framebuffer`
(via `VIC_PALETTE_RGB`) into an `SDL_PIXELFORMAT_RGB24` streaming
texture and presents it once per completed real frame
(`m.total_cycles / (VIC_CYCLES_PER_LINE * VIC_LINES_PER_FRAME)`
changing is the trigger — the emulated cycle count is the source of
truth, exactly as this section asks, not a host timer). No 1-in-N
throttling was needed: rendering every frame at 2x scale measured well
under the audio pacing budget (see "Real-time pacing" below) on the
development machine. Live-verified: the real KERNAL/BASIC boot screen
renders correctly in the window, including the same badline DMA-delay
checkerboard artifact already confirmed in Phase 4's `demo-real-rom`.

**Actually looking at a live picture immediately surfaced a real,
previously-undisclosed VIC-II gap** — the raw framebuffer includes the
real hardware's own horizontal/vertical blanking intervals, which the
compositor was painting as border color instead of true black,
producing a visually oversized, wrapped-looking border on first live
view. Diagnosed and fixed directly against Bauer's article's own
blanking geometry table (`docs/sources.md`), not guessed — see
`docs/vic-ii.md`'s "Known gaps" entry for the full story. `render_frame()`
also rotates/crops the native, wraparound-numbered 504×312 array into a
contiguous 405×284 visible picture for display, since a real monitor
never shows the array's own internal seam; the raw array itself (and
every internal test) is untouched by this — only the frontend's display
step rotates it. Confirmed by the user directly, live, via screenshot
before and after the fix.

## Audio

- Use SDL2's **pull-based** audio callback API (`SDL_AudioSpec.callback`
  or the newer `SDL_QueueAudio`/callback-stream APIs), not a
  push-and-hope model — the callback is invoked by SDL's own audio
  thread on the device's real schedule and asks for exactly as many
  samples as it needs next; your job is just to have kept a ring buffer
  topped up ahead of that ask (see `docs/machine.md`'s pacing section).
- Sample rate: pick a standard rate (44100 or 48000 Hz) and resample the
  SID's own native output rate to it if they differ, or drive the SID
  itself at the target rate directly (simpler, and fine — the SID's
  documented behavior doesn't require a specific fixed sample rate,
  it's this project's own choice of how often to advance/sample the
  oscillators before feeding the audio device).
- Underrun handling: if the ring buffer runs dry, **pad with silence**,
  don't stall the callback and don't loop/repeat old samples — a click
  or a moment of true silence is a far less confusing artifact to debug
  than a stutter that sounds like a hang.
- Verification target: a known-frequency test tone (see `docs/sid.md`)
  measured correct through the *actual* SDL2 audio pipeline end-to-end,
  not just from the SID module in isolation — pipeline-level bugs
  (buffer starvation, wrong sample rate assumptions) are real and won't
  show up testing the SID alone.

**Done.** `sdl_frontend.c` uses **`SDL_QueueAudio`** (queue-based, not
callback) at 44100Hz mono, `AUDIO_S16SYS`. This is also this project's
real implementation of `docs/machine.md`'s pacing strategy 2 (audio as
the master clock), now that this phase exists to make it possible: the
main loop tops the queue up to a fixed 50ms target every iteration by
running exactly as many real PHI2 cycles as the needed sample count
requires (a fractional-cycles-per-sample accumulator, `cycle_accum` in
`main()`, avoids drift the same way `docs/cia.md`'s TOD integer-
arithmetic rule does); nothing here uses a host wall-clock timer at
all — the audio device's own real drain rate is what paces everything,
including video and input. Underrun handling matches the spec (pad
with silence, don't stall/loop) via SDL_QueueAudio's own documented
behavior when its queue runs dry, rather than anything built here.
Live-verified: an active PipeWire sink stream at the correct
`s16le 1ch 44100Hz` format was confirmed while the real KERNAL/BASIC
boot ran, with CPU usage around 20% (not busy-waiting a whole core,
confirming real backpressure-based pacing, not a spin loop). Not yet
verified end-to-end against a specific known-frequency test tone played
through this exact pipeline (see "Known gaps").

## Keyboard

- Map SDL2 key events to the CIA1 keyboard matrix positions from
  `docs/cia.md`. Handle key-repeat by letting the *real* KERNAL's own
  repeat/debounce logic do its job (i.e. just report raw press/release
  events at the real rate SDL delivers them, don't add your own
  synthetic repeat on top) unless testing shows a real problem.
- Modifier keys (Shift, Control, Commodore key) are themselves ordinary
  matrix positions on real hardware, not something SDL/the OS should be
  allowed to intercept/consume before your event handler sees them.
- If you build any convenience input method for reliably typing whole
  program listings (paste-from-clipboard, a "type this file" mode),
  hold each synthetic keypress for long enough that the real KERNAL's
  interrupt-driven keyboard scan (tied to the jiffy IRQ, ~60Hz-ish) can
  actually see it — a key held for less than one real scan/debounce
  interval can land between two scans and be silently missed. Verify
  this concretely (type a known string, read it back via the emulated
  screen or a LIST) rather than assuming a chosen hold duration is long
  enough.

**Done.** `scancode_to_c64key()` in `sdl_frontend.c` maps SDL2
scancodes (physical-position-based, not the host layout's produced
character) onto `src/c64/keyboard.h`'s `C64Key` matrix positions;
RESTORE (Page Up) is handled separately via `machine_set_restore_key()`
since it isn't a matrix position on real hardware either (see
`docs/machine.md`). Cursor Left/Up are synthesized as the real Cursor
Right/Down matrix position plus a synthetic LSHIFT, matching how real
hardware's own single physical cursor keys work. Raw press/release
events are forwarded as-is, no synthetic repeat added, per this
section's own guidance.

**This is also where the long-open Phase 3 keyboard-matrix empirical
cross-check finally got closed for real** (flagged since Phase 3,
referenced again in Phases 6: `docs/cia.md`, `docs/machine.md`,
`CLAUDE.md`, `README.md`) — not just wired, but verified concretely
exactly as this section's own guidance demands: `tests/integration/
test_keyboard_input.c` types a known 8-key string ("A1Z5MP ,", spanning
matrix rows 0,1,2,4,5,7 and columns 0,1,2,4,5,7) through the real
`KeyboardMatrix -> CIA1 -> IRQ-driven KERNAL scan`, holding each key
for 3 real jiffy periods (per this section's own hold-duration
warning), and confirms the exact expected screen-code sequence appears
in the real KERNAL's own screen memory. PASSES against the user's
staged ROMs — a representative sample of the 64 positions, not an
exhaustive sweep, but genuine, primary-hardware-verified confirmation
that `src/c64/keyboard.h`'s community-sourced layout table is actually
correct, not just plausible. Not independently verified live through
actual SDL2 keyboard events (no input-simulation tool like `xdotool`
was available in the development environment) — see "Known gaps".

## Joystick

- SDL2's joystick/game-controller API, or a keyboard-key fallback (e.g.
  numpad directions + a fire key), coupled to the digital joystick model
  from `docs/cia.md`. Support switching which of the two C64 joystick
  ports a single physical input device controls if you only have one
  real input device to dedicate (a real, common situation) — a runtime
  hotkey to flip which port is being driven is a reasonable, simple
  answer.

**Done, keyboard-fallback variant only** (see "Known gaps"): numpad
8/2/4/6/0 drive up/down/left/right/fire on whichever of the two C64
ports is currently selected (`handle_joystick_key()` in
`sdl_frontend.c`); F9 toggles which port, exactly this section's own
suggested answer for "one physical input device, two possible ports."

## Verification target

The real boot screen renders live in a window at the correct pace, a
typed BASIC command round-trips correctly through real SDL2 keyboard
events, and a test tone is audible at the correct pitch through the
real SDL2 audio pipeline — all three exercised together, not just each
in isolation, since pipeline-level integration bugs are real.

**Partially done.** The real boot screen renders live at the correct
pace (`src/frontend/sdl_frontend.c`, live-verified via screenshot both
before and after the border/blanking fix above) and a typed string
round-trips correctly through the real KeyboardMatrix -> CIA1 -> IRQ-
driven KERNAL scan (`tests/integration/test_keyboard_input.c`, see
"Keyboard" above) — genuinely verified, not assumed. **Not yet done**:
this exact string round-trip driven through *actual* SDL2 keyboard
events specifically (rather than calling `keyboard_matrix_set_key()`
directly, which is what the integration test above does) — no input-
simulation tool (`xdotool`/`ydotool`/`wtype`) was available in the
development environment to drive real SDL2 key events programmatically,
so this specific sub-target is a manual check left for the user: type a
line at the real "READY." prompt in `make run` and confirm it echoes
correctly. Also not yet done: a known-frequency test tone measured
through the real SDL2 audio pipeline specifically (the pipeline itself
was live-verified to produce an active, correctly-formatted audio
stream — see "Audio" above — but pitch-correctness end-to-end through
it, as opposed to through the SID module in isolation per
`docs/sid.md`, wasn't separately re-checked).

## Known gaps to disclose as you build

- Which SDL version (2 vs. 3) and audio API (callback vs. queue-based)
  you actually used. **SDL2, `SDL_QueueAudio`** (queue-based) — see
  "Audio" above for why.
- Any input devices/edge cases (multiple simultaneous joysticks, RS-232
  user port peripherals) explicitly not supported. **Real SDL2 joystick/
  game-controller hardware support is not implemented** — only the
  numpad keyboard fallback (no physical joystick/gamepad was available
  to test against in the development environment, and the docs
  explicitly allow this as a first pass). RS-232 user-port peripherals:
  out of scope, matching `docs/cia.md`'s own existing disclosure.
- F2/F4/F6/F8 (real hardware: SHIFT+F1/F3/F5/F7) aren't separately
  mapped in `scancode_to_c64key()` — only F1/F3/F5/F7 are wired.
- Holding both emulated Cursor-Left and Cursor-Right (or both Up/Down)
  at once can misbehave (releasing one clears the shared CRSR_LR/CRSR_UD
  matrix position even if the other is still logically held) — real
  hardware has only one physical key per pair, so this is an artifact
  unique to faking the shift-toggle with two separate host keys, not a
  real hardware scenario; low-impact, not reference-counted.
- Live SDL2 keyboard-event verification (as opposed to the direct
  `KeyboardMatrix` API verification `test_keyboard_input.c` does) is a
  manual check, not automated — see "Verification target" above.
- A known-frequency test tone through the actual SDL2 audio pipeline
  specifically (as opposed to the pipeline's own format/activity, which
  was live-verified, and the SID module in isolation, which
  `docs/sid.md` already verifies) is not yet separately re-checked.
