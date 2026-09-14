# Reference

A working knowledge base of **real Commodore 64 programming facts** --
register addresses, coordinate systems, PETSCII/screen-code quirks,
BASIC V2 language gotchas -- accumulated as a side effect of writing
and debugging `programs/basic/` test listings, so that knowledge isn't
re-derived (or re-discovered the hard way) from scratch every time.

**This is not `docs/`.** `docs/` documents *this emulator's own
implementation*: what it does, what it deliberately simplifies, what's
a disclosed known gap versus real silicon. This directory documents
*the real machine being programmed* -- facts a BASIC (or ML) programmer
needs regardless of which emulator or real hardware they're running on.
A fact can live here without `docs/` needing to say anything about it
at all, and vice versa.

## Files

- `sprites.md` -- registers, coordinate ranges, collision detection,
  expansion, priority, shape data layout.
- `sound.md` -- SID registers, waveforms, ADSR, real hardware quirks
  (the noise LFSR lock-up), the reusable 8-note scale.
- `petscii.md` -- PETSCII vs. ASCII vs. screen codes, keys that don't
  map where a modern keyboard user would expect.
- `screen.md` -- screen/color RAM layout, VIC-II bank selection, the
  keyboard buffer, default memory layout a BASIC program needs to work
  around.
- `basic-v2-quirks.md` -- the BASIC V2 language itself: the 2-character
  variable name rule, no `ELSE`, `IF...THEN` line semantics, bitwise
  `AND`/`OR`, `DATA`/`READ` sequencing.

## Convention

- Add to (or correct) these files whenever writing, running, or
  debugging a `programs/basic/` listing turns up a real fact worth
  keeping -- the same "write it down as you learn it" discipline
  `docs/` uses for the emulator itself, just aimed at the real machine.
- State the confidence/source briefly where it matters: a fact
  confirmed against a primary source (a datasheet, Christian Bauer's
  VIC-II article, this project's own already-tested register wiring)
  reads differently from "widely known convention, not independently
  re-verified here." Don't blur the two.
- These are meant to be built *from* -- if asked for a proper writeup
  on, say, sprites, the answer should draw on `sprites.md` rather than
  re-deriving everything from first principles.
