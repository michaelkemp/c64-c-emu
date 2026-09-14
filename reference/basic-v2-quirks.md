# BASIC V2 language quirks (real facts, for writing programs)

See `reference/README.md` for how this differs from `docs/`. This file
is about Commodore BASIC 2.0 the *language*, independent of any chip.

## Variable names are only 2 characters significant

BASIC V2 only distinguishes a variable's identity by its **first two
characters** (plus a `$`/`%` type suffix). `MINX` and `MINY` are the
*same variable* to the interpreter -- both truncate to `MI`. This is a
real, easy-to-get-bitten-by fact, not a style preference: every name
in `programs/basic/sprite_test2.bas` was deliberately chosen so no two
names share their first two characters. Arrays and simple (scalar)
variables of the same spelled name are in *separate* namespaces, so
`X` (scalar) and `X()` (array) don't collide with each other even
though they share every character.

## No `ELSE`

Real Commodore BASIC 2.0 has no `ELSE` keyword at all. Two-branch logic
needs either: chained `IF` statements (one for each branch's own
condition), a `GOTO` past the "then" branch, or -- often cleanest --
arithmetic that avoids branching entirely, e.g. `W = 24 + 24*XE` instead
of an if/else assigning 24 or 48 based on a flag `XE` that's already
0/1.

## `IF...THEN` line semantics

`IF cond THEN stmt1 : stmt2 : stmt3` -- if `cond` is false, the
**entire rest of that logical line** (every colon-separated statement,
not just the first) is skipped and execution continues at the next
line number. If `cond` is true, every one of those statements executes
in sequence, including a `GOSUB` among them (a subroutine call inside a
conditional line works fine and returns to the statement after it on
that same line, or the next line number if it was last).

## `AND` / `OR` are bitwise, not just boolean

Both operands are converted to 16-bit integers first, then combined
bit-by-bit. This makes them directly useful for hardware register
bitmask tests, e.g. `IF (CB AND 2^N) <> 0 THEN ...` to check whether
bit `N` of a byte just read from a hardware register is set -- not just
`IF cond1 AND cond2` boolean-style (though that also works, since a
true/false relational result is already an all-1s/all-0s bit pattern).

## `^` is real exponentiation

`2^N` works and is exact for the small integer powers this kind of
register-bitmask code needs (up to `2^15`ish before floating-point
precision would start to matter, far beyond an 8-bit register's needs).

## `DATA`/`READ` is one continuous stream

Every `DATA` statement in the whole program is logically concatenated,
in ascending line-number order, into a single sequential stream --
`READ` just pulls the next value(s) off that stream regardless of which
subroutine or line issued the `READ`, or where physically the `DATA`
statements sit in the listing (a `REM` or blank-ish line in between
does nothing to the pointer). This means `DATA` blocks feeding
different subroutines just need to appear in the *same relative order*
the corresponding `READ`s will consume them in, not be interleaved with
the code that reads them.

## A logical line is capped at 80 characters

The screen editor (used for both typing *and* pasting a program in --
pasting is just synthetic typing, see `docs/peripherals.md`) only
accepts up to **80 characters** (two 40-column screen rows) for one
logical program line. A line longer than that produces a real `SYNTAX
ERROR` -- this is genuine BASIC V2 editor behavior, not a bug in any
particular terminal, emulator, or paste feature. Found by pasting an
early version of `programs/basic/sprite_test2.bas`, several of whose
comment-heavy lines were originally over 80 characters; fixed by
shortening/splitting them, since there's nothing to "fix" about the
real 80-character limit itself. When writing a new listing, keep every
line comfortably under 80 (this project now targets ~78 or less as a
safety margin) rather than relying on the exact boundary.

## The exponentiation operator is a real, separate key

`^` (real Commodore BASIC V2's exponentiation operator, e.g. `2^N`) is
the physical **up-arrow** key on a real C64 keyboard -- a dedicated key
with no shift needed, not a shifted-digit trick. No standard PC
keyboard has a key at that same physical position, which is exactly
the kind of gap this project's paste-as-typing and live-keyboard
mappings can silently drop -- see `docs/peripherals.md`'s Known Gaps
for the real bug this caused (every `^` in a pasted listing was
silently dropped, corrupting the expression into a `SYNTAX ERROR`) and
how it was fixed for both the paste path and live typing (a bare
Insert-key press, since no natural physical analogue exists on a
standard keyboard).

## `POKE`/`PEEK` value ranges

`POKE address, value` takes `address` 0-65535 and `value` 0-255
(rounded/truncated from whatever numeric expression is given -- a
float like `192.0` POKEs fine). `PEEK(address)` always returns 0-255.

## `GET` for real-time, non-blocking keyboard input

`GET K$` reads at most one character already waiting in the keyboard
buffer and returns immediately -- `K$=""` if nothing's been typed since
the last `GET`, rather than blocking like `INPUT` does for a whole
line-plus-Return. This is the standard, real BASIC V2 idiom for
"control something live from the keyboard" (a moving sprite, a menu
cursor, anything that needs to keep running between keystrokes) --
just `GET K$` once per pass through the main loop and act on whatever
key (if any) came back. Holding a key down repeats it via the real
KERNAL's own key-repeat/debounce logic automatically; no custom timing
is needed on top, matching `docs/peripherals.md`'s own "let the real
KERNAL's repeat logic do its job" convention for this project's
keyboard handling in general. Used in `programs/basic/sprite_move.bas`
for direct WASD sprite control.
