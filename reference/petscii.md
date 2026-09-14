# PETSCII / screen codes (real hardware facts, for writing programs)

See `reference/README.md` for how this differs from `docs/`. This file
is about the real character-encoding facts a BASIC program (or anything
poking screen memory directly) needs, independent of any emulator.

## Three different codes for "the same character"

1. **PETSCII** -- what `PRINT`, `CHR$()`, and the keyboard actually
   produce/consume. Digits and most letters/punctuation match ASCII,
   but it is **not** ASCII.
2. **Screen codes** -- what's actually stored in screen RAM
   ($0400-$07E7) for the VIC-II to render. These are a *different*
   numbering from PETSCII for letters: uppercase `A`-`Z` are screen
   codes 1-26 (not 65-90). A common, easy-to-verify trick for the
   default uppercase/graphics charset: screen code = `ASCII_LETTER -
   64` (this project's own `tests/integration/test_keyboard_input.c`
   uses exactly this: `'R' - 64` for the screen code of `R`).
3. **Physical key position** -- what a host keyboard's scancode
   actually corresponds to on a *real* C64 keyboard layout, which
   doesn't line up 1:1 with a modern keyboard's printed legends.

## Keys that don't map where you'd expect

- The physical key printed with a pound-sterling-ish symbol on a real
  C64 keyboard types **`£`** (screen code `$1C`), not `$`. Real `$` is
  **SHIFT+4** (screen code `$24`), following the same shifted-digit-row
  convention as `(` (SHIFT+8) and `)` (SHIFT+9). Confirmed directly
  against the real KERNAL while debugging this project's own paste-as-
  typing feature (`docs/peripherals.md`'s Known Gaps,
  `programs/basic/sid_diagnostic.bas`'s own notes -- its `WN$(...)`
  variable names were the first real pasted use of a literal `$` in
  this project and immediately caught the bug).
- `<` and `>` are **SHIFT+comma** and **SHIFT+period** respectively --
  they share physical keys with `,`/`.`, not separate keys.
- There is no real lowercase letter set in the default (uppercase/
  graphics) character mode -- lowercase input has nowhere real to go
  except the same physical key as its uppercase form.

## A few PETSCII control codes actually used so far

- `CHR$(147)` -- clear screen (and home cursor). Used in
  `programs/basic/sprite_test2.bas`. Widely known convention; not yet
  independently re-verified against a primary source in this project
  the way the register-level facts above were -- flagged here so that
  distinction doesn't get lost.

## Practical implication for writing test programs

If a `.bas` listing will be *pasted* into the running emulator (rather
than typed by hand), only the characters `char_to_c64key()` in
`src/frontend/sdl_frontend.c` actually maps will come through correctly
-- see that function's own header comment for the current, disclosed-
approximate punctuation coverage. A new symbol not in that list is
silently skipped (not mistyped), so listings should stick to the
letters/digits/covered punctuation it already knows about, or the
mapping needs extending first.
