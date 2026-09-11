# Demos

Ad-hoc smoke-test tools, not permanent project deliverables. Built
because there was otherwise no way to *see* whether the VIC-II
(Phase 4) actually renders anything correct — Phases 1-4 only produce
chip modules and unit tests, with no display and no way to load a
program until Phase 6 (real machine loop) and Phase 7 (SDL2
peripherals) exist for real. This is a deliberately small, early,
partial slice of both, built to unblock visual sanity-checking sooner.

- `hello_c64.s` / `hello_c64.cfg` — a small, self-contained 6502 program
  (assembled with `ca65`/`ld65` from the cc65 suite). It needs no
  KERNAL/BASIC ROM at all: it defines its own 8×8 font for the letters
  it needs, pokes "HELLO C64" directly into screen RAM and Color RAM,
  and busy-waits on `$D012` (no interrupts — Phase 6 doesn't exist yet)
  to change the border color at three different raster lines, as a
  concrete demonstration of real per-scanline timing.
- `framebuffer_dump.c` — loads that binary directly at `$C000` (skips
  `cpu6502_reset()` entirely — there's no ROM to fetch a reset vector
  from), wires a real `C64Memory` + `VicII` together via
  `c64memory_attach_vic()`, runs a plain CPU/VIC-II cycle-interleaved
  loop for a few frames, and dumps the resulting framebuffer as a PPM
  image using `src/c64/palette.c`'s RGB approximation.

Run with `make demo` from the repo root (needs `ca65`/`ld65` — see
`scripts/fetch_dormann_tests.sh`'s neighbor comment for the same
external-assembler convention used in Phase 1). Convert the resulting
`build/hello_c64.ppm` to PNG with `convert build/hello_c64.ppm
build/hello_c64.png` (ImageMagick) if you want something more
universally viewable; `hello_c64_screenshot.png` in this directory is a
checked-in reference of what a correct run looks like.

**What this does and doesn't prove**: it exercises real cycle-accurate
CPU+VIC-II interleaving, register writes taking effect at the right
raster line, standard character-mode rendering with a custom font, and
the border/background color logic — genuinely useful end-to-end
evidence that Phase 4 works. It does **not** exercise CIAs, interrupts,
real-time pacing, audio, or input, and it doesn't boot a real KERNAL —
none of that exists yet. Don't mistake this for Phase 6 or Phase 7
themselves; both still need to be built for real.
