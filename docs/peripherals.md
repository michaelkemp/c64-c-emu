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

## Joystick

- SDL2's joystick/game-controller API, or a keyboard-key fallback (e.g.
  numpad directions + a fire key), coupled to the digital joystick model
  from `docs/cia.md`. Support switching which of the two C64 joystick
  ports a single physical input device controls if you only have one
  real input device to dedicate (a real, common situation) — a runtime
  hotkey to flip which port is being driven is a reasonable, simple
  answer.

## Verification target

The real boot screen renders live in a window at the correct pace, a
typed BASIC command round-trips correctly through real SDL2 keyboard
events, and a test tone is audible at the correct pitch through the
real SDL2 audio pipeline — all three exercised together, not just each
in isolation, since pipeline-level integration bugs are real.

## Known gaps to disclose as you build

- Which SDL version (2 vs. 3) and audio API (callback vs. queue-based)
  you actually used.
- Any input devices/edge cases (multiple simultaneous joysticks, RS-232
  user port peripherals) explicitly not supported.
