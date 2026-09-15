# Sprites (real hardware facts, for writing programs)

See `reference/README.md` for how this differs from `docs/vic-ii.md`
(which documents this emulator's own VIC-II implementation/gaps). This
file is about the real chip's sprite hardware as a programmer sees it.

## Registers

| Register | Address (decimal) | Notes |
|---|---|---|
| Sprite `n` data pointer | `$07F8+n` (2040-2047) | `screen_ram_end..+7`, conventionally unused screen-RAM tail bytes. Value = (data address within the VIC-II's current 16KB bank) / 64 -- data must be 64-byte aligned. |
| Sprite `n` X | `$D000+2n` (53248+2n) | Low 8 bits only -- see MSB register below. |
| Sprite `n` Y | `$D001+2n` (53249+2n) | Full range fits in 8 bits; no MSB register exists for Y. |
| Sprite X MSB | `$D010` (53264) | One bit per sprite (bit `n` = sprite `n`'s 9th X bit). Rebuild and POKE the whole byte each frame rather than trying to set/clear individual bits with a read-modify-write. |
| Sprite enable | `$D015` (53269) | One bit per sprite. |
| Sprite Y-expand | `$D017` (53271) | One bit per sprite; doubles each row vertically (21 rows -> 42 display pixels tall). |
| Sprite-to-background priority | `$D01B` (53275) | One bit per sprite: **1 = sprite drawn behind foreground graphics, 0 = in front** (confirmed against this project's own `vic_ii.c`: `behind_foreground = (sprite_priority & (1<<n)) != 0`). Border always wins regardless of this bit -- see "Border" below. |
| Sprite multicolor select | `$D01C` (53276) | One bit per sprite; enables 2-bits-per-pixel multicolor mode using the two shared colors below instead of that sprite's own single hi-res color. |
| Sprite X-expand | `$D01D` (53277) | One bit per sprite; doubles each column horizontally (24 cols -> 48 display pixels wide). |
| Sprite-sprite collision | `$D01E` (53278) | **Read-only, and reading clears it.** Bitmask of every sprite that touched another sprite (real per-*pixel* opaque-pixel overlap, not a bounding-box check) since the last read. Doesn't say *which* other sprite -- only *that* a collision involving this sprite happened. |
| Sprite-background collision | `$D01F` (53279) | Same read-clears semantics, but against background *graphics* (character/bitmap foreground pixels) -- a blank/space-filled screen never triggers this, regardless of screen or border color. |
| Sprite `n` color | `$D027+n` (53287-53294) | 4-bit color (0-15), hi-res (non-multicolor) mode. |
| Sprite multicolor 0/1 | `$D025`/`$D026` (53285/53286) | Shared across *every* sprite using multicolor mode -- not per-sprite. |

## Coordinate system / border

Real visible display window (PAL, 40-column/25-row, the normal case):
first X coordinate 24 ($18), last X coordinate 343 ($157); first line
51 ($33), last line 250 ($FA) -- Christian Bauer's VIC-II article,
section 3.4's own table (already this project's cited primary source
for the emulator itself, `docs/sources.md`).

To keep a sprite of width `W`/height `H` (24/21 unexpanded, 48/42 fully
expanded) fully inside that window: `X` from 24 to `344-W`, `Y` from
~50 to `251-H` (this project's own test programs use 50, one line more
conservative than the article's exact 51, as a small safety margin --
not independently re-verified pixel-for-pixel, and harmless either
way for a bouncing-sprite demo).

**A genuine discrepancy between two authoritative-ish sources, worth
knowing about rather than silently picking one**: the Commodore 64
Programmer's Reference Guide's own MOB-position section (Appendix N,
p.442) states "X locations 23 to 347 ($17-$157) and Y locations 50 to
249 ($32-$F9) are visible." Two things don't line up:
- Its own decimal "347" doesn't match its own hex "$157" -- `$157` is
  343 decimal, not 347. This looks like a genuine erratum in the
  original book itself, since Bauer's article (this project's other
  cited source) agrees with the *hex* figure: last X coordinate 343.
- Its Y upper bound (249, `$F9`) is one line short of Bauer's article's
  250 (`$FA`) -- an unresolved 1-line disagreement between the two
  sources that neither this project nor the book's own text explains.

Since this project's own bounds already sit safely inside *both*
versions (see above), nothing here needed a code change -- just worth
knowing the exact edge value has this small amount of real ambiguity
if a future test ever needs pixel-perfect border-edge precision.

**The border is not part of collision detection at all** -- it's a
separate display-priority mechanism (`docs/vic-ii.md`: "border has
strictly higher display priority than every sprite") that only affects
final on-screen color, never `$D01E`/`$D01F`. Bouncing off the border
has to be a software bounds check against the coordinates above; only
sprite-vs-sprite and sprite-vs-background bouncing can use the real
collision registers.

Any sprite whose X can reach past 255 needs the MSB register --
track a full logical X (up to ~344) in your own variable, split it into
`X MOD 256` (POKE'd normally) plus a bit in `$D010` every frame.

## Shape data

63 bytes per sprite = 21 rows x 3 bytes (24 pixels/row, MSB-first: byte
0 bit 7 = column 0, byte 2 bit 0 = column 23). The 64th byte at the
pointer's target is padding/unused. You don't have to hand-author
`DATA` for a shape -- a filled circle, for instance, is cheap to build
procedurally: for row `R` (0-20), `DR=R-10`, `HW=INT(SQR(RA*RA-DR*DR))`
gives that row's half-width for a radius-`RA` disc; loop columns 0-23,
OR in `2^(7-(C MOD 8))` into byte `INT(C/8)` for each column inside
`[center-HW, center+HW]`. Real C64 pixels aren't quite square, so a
disc built this way (equal math radius in both axes) will look *close*
to round on a real display but not perfectly circular -- a minor,
disclosed approximation, not a bug.

## Practical notes from `programs/basic/sprite_test2.bas`

- 8 sprites, 2 shapes x 4 size combinations, covers every combination
  exactly once by deriving shape/stretch flags from the sprite number's
  own bits (`SH = N MOD 2`; `XE = INT(N/2) MOD 2`; `YE = INT(N/4) MOD
  2`) rather than a lookup table.
- Only 3 real SID voices exist, so "one tone per sprite" for 8 sprites
  means multiplexing (`voice = sprite MOD 3`) -- see `sound.md`.
- `$D01E`'s "who collided" bitmask, with no pair information, means
  there's no hardware-given basis for real pairwise elastic-collision
  physics -- some other response has to stand in for it.
- **A naive "just reverse direction" collision response can get two
  sprites stuck oscillating in place forever.** `$D01E` stays set on
  *every* frame two sprites remain in contact, not just the frame they
  first touch. If a frame's worth of movement isn't enough to fully
  separate them (e.g. they started already overlapped, or they're
  moving slowly relative to their own size), the very next frame's
  collision check fires again and flips the direction *back* --
  forever, with no net movement, just an endless retrigger (and, if a
  collision sound is wired up, an endless tone). Found exactly this way
  in `sprite_test2.bas`: sprites that started too close together froze
  in place indefinitely instead of bouncing apart. A response that
  *guarantees* separation on the next frame -- e.g. teleporting the
  touched sprite to a fresh random position (`RND(1)`) rather than
  trusting a direction reversal to drift it clear in time -- avoids
  this regardless of starting distance or speed.
- **Even a "guaranteed separation" fix like that teleport (or an
  immediate reverse-and-re-move) can still loop, for a subtler reason:
  a *stale* `$D01E` reading.** BASIC polls the register once per (slow)
  loop iteration, but the real VIC-II keeps checking for collisions
  continuously, against whatever position is *actually poked into the
  hardware registers right now* -- not whatever your BASIC array
  variables say it should be next. Between the moment you `PEEK`
  (clearing the register) and the moment your fix's `POKE` actually
  lands the new, separated position, the *old*, still-overlapping
  position is still what the hardware sees, and it can latch a brand
  new collision in that gap. That fresh-but-already-obsolete reading
  then surfaces on your *next* poll, one tick later, triggering a
  second, spurious response even though the sprites have, by then,
  genuinely separated -- which looks exactly like "collide, move apart
  a couple of steps, then collide again," repeating forever. Found in
  `programs/basic/sprite_test2_pair.bas`. Fixed with a small per-
  response cooldown (a few ticks where a fresh `$D01E` reading is still
  read -- to keep clearing it -- but deliberately ignored) so a stale
  echo from before the fix took effect can't trigger a second response
  before the real, current state has had a chance to settle.
