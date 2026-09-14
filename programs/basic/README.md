# BASIC test programs

Hand-written BASIC listings for manually exercising the real, running
machine (`make run`) end to end -- not a substitute for the unit/
Dormann/integration tiers in `tests/`, but the thing to reach for when
you want to *look and listen* to a real chip's behavior through the
whole real pipeline (KERNAL -> keyboard matrix -> VIC-II/SID -> SDL2),
the same way `tools/demos/` exists for a lower-level, no-KERNAL sanity
check. See `docs/testing-strategy.md` for how this tier fits alongside
the others.

## Running one

1. `make run` (needs real ROMs staged under `roms/c64/` first --
   `scripts/stage_roms.sh`).
2. Once the real KERNAL/BASIC "READY." prompt appears, paste the
   listing in (Ctrl+V or Shift+Insert -- see `docs/peripherals.md`'s
   Keyboard section for how the paste-as-typing feature works), then
   type `RUN` and press Return. `sound_test.bas` includes its own
   trailing `RUN` line so it starts itself.

## `sid_diagnostic.bas`

Cycles all three SID voices through all four waveforms (triangle,
sawtooth, pulse, noise) in turn, gating each on/off briefly per note
across the same 8-note scale `sound_test.bas` uses. Exercises: register
addressing across all three voice blocks (`$D400`/`$D407`/`$D40E`),
waveform-select bits, and gate on/off timing per `docs/sid.md`.

**Found two real bugs the first time this was pasted in and run.** First,
its own `WN$(...)` string-array variable names are this project's first
real pasted use of a literal `$` character -- which came in as `£`
instead: `char_to_c64key()` mapped `$` to the physical POUND key, which
actually types `£` on real hardware (verified against the real KERNAL:
unshifted POUND produces screen code `$1C`, the £ glyph). Real `$` is
SHIFT+4 (screen code `$24`) -- fixed in `char_to_c64key()`; see that
function's comment and `docs/peripherals.md`'s Known Gaps.

Second, once it actually ran: the noise waveform was completely silent
on all three voices, every time, even though triangle/sawtooth/pulse
all played the scale correctly. Root cause: `sid_init()` zeroed the whole `Sid`
struct, including each voice's 23-bit noise LFSR -- and all-zero is a
genuine fixed point of that LFSR (both feedback taps read 0, so the
fed-back bit stays 0 forever without ever being clocked back to
nonzero). Real noise-locked-at-zero is a real, documented hardware
hazard (recoverable by strobing the TEST bit), but real hardware
doesn't normally power on already stuck there, and this program (like
most real BASIC/ML code that just selects the noise waveform) never
strobes TEST first. Fixed in `sid_init()` by seeding the LFSR to
`0x7FFFFF` (23 ones) instead of 0 -- the same "charged" value this
project's own TEST-bit modeling already produces -- see that function's
comment and `docs/sid.md`'s Known Gaps for the full account, and
`tests/unit/test_sid.c`'s `test_noise_alone_is_not_silent_from_cold_init`
for the regression test.

## `sound_test.bas`

Plays a one-octave C-major scale on voice 1 with a triangle waveform
and a short attack/decay/sustain/release envelope -- the simplest
possible real-hardware-through-real-audio-pipeline smoke test. Good
first thing to run after any SID or audio-pipeline change.

## `sprite_test2.bas`

All 8 hardware sprites at once, exercising real VIC-II/SID capability
this project's other test programs don't touch:

- **Shapes and sizes**: 2 shapes (a solid square, a filled round disc --
  the disc built procedurally at init via a row-by-row half-width
  calculation, not hand-authored `DATA`) times 4 size combinations
  (normal, X-stretched, Y-stretched, both), covering exactly the 8
  sprites via `SH=N MOD 2` (shape), `XE`/`YE` (stretch flags) derived
  from `N`'s bit pattern -- see the file's own comments.
- **Border bounce past X=255**: sprite X is tracked as a full logical
  coordinate (up to ~320) in a BASIC array, split into the low byte
  (`$D000`-family registers) plus the `$D010` MSB bitmask every frame
  -- the concrete real-hardware mechanic this task was written to
  exercise.
- **Real hardware sprite-sprite collision**: `PEEK(53278)` (`$D01E`)
  each frame -- reading it both reports which sprites were touched by
  another sprite since the last read *and* clears it, a real, easy-to-
  get-backwards register semantic (`docs/vic-ii.md`). Because it's
  actual per-pixel hardware collision (not a bounding-box check), two
  round sprites' bounding boxes can overlap without triggering a
  collision until their opaque pixels actually touch.
- **A tone per hit**: each sprite has its own fixed pitch (reusing the
  same 8-note scale as `sound_test.bas`/`sid_diagnostic.bas`), triggered
  on every border or sprite-sprite collision. **Disclosed simplification**:
  the SID only has 3 real voices, so a sprite's tone actually sounds on
  voice `(sprite number) MOD 3` -- two sprites can end up sharing a
  physical voice and cut each other off if they collide at the same
  moment. Each note is a short attack/decay pluck with sustain 0 (fades
  out on its own; no release/gate-off bookkeeping needed).
- **Collision response teleports, rather than just reversing direction**
  (see "A real stuck-forever bug" below for why) -- `$D01E` only reports
  *which* sprites were touched, not *by what*, so there's no hardware-
  given pair information to bounce off of realistically anyway; a fresh
  random position + velocity (`RND(1)`) both guarantees separation and
  gives the real BASIC ROM's random number generator something to do.
- **White border, black background**: distinct colors on purpose, so a
  border hit is visually obvious against the background rather than
  blending into an all-black screen.

**A real stuck-forever bug, found by actually watching this run**: the
first version just reversed a colliding sprite's own `DX`/`DY` in place
without moving it anywhere. If two sprites started already overlapped
(or a slow bounce hadn't fully separated them within one frame), the
hardware collision register stayed set on *every subsequent frame* they
remained in contact -- and re-flipping an already-flipped direction
every frame just flips it right back, so two overlapped sprites could
sit and jitter in place forever, endlessly re-triggering the collision
tone and never visibly moving. Fixed by having a collision teleport the
hit sprite to a fresh random position and velocity instead (subroutine
6000) -- guarantees real separation on the very next frame, not just a
direction change that assumes the sprites will drift apart. The
starting positions were also spread into a 4x2 grid far enough apart
that no two sprites (even the largest, 48x42) start already touching.

**A second, subtler stuck-loop bug, found the same way**: even the
teleport fix could get re-triggered by a *stale* `$D01E` reading -- the
real VIC-II keeps checking collisions continuously against whatever
position is actually poked into hardware right now, not what a BASIC
variable says it will become; in the gap between clearing the register
and the fix's own `POKE` landing, the hardware can latch one more
collision against the *old*, still-overlapping position, which then
surfaces on the *next* poll as if it were new -- even though the
sprites have, by then, genuinely separated. Fixed with a per-sprite
cooldown (`CD()`, subroutine 4000): for a few ticks after a teleport,
`$D01E` is still read (to keep clearing it) but deliberately ignored,
so a stale echo can't trigger a spurious re-teleport before the real
state has settled. See `reference/sprites.md` for the full account.

Verified with VICE's `petcat -w2` (tokenizes cleanly, confirming valid
BASIC V2 syntax) and by careful manual cross-check against this
project's own already-verified register map and the real BASIC V2
"only the first 2 characters of a variable name are significant" rule
(every name in this file was chosen to stay unique that way -- see the
file's own header comment).

**Pasting this file in for real found two more genuine bugs** the
`petcat` check above couldn't catch (it only validates syntax, not the
paste pipeline or real BASIC V2 editor limits):

- `^` (used in this file's `2^N`/`2^(7-BP)` shape-building math) was
  silently dropped by the paste feature entirely -- `char_to_c64key()`
  had no mapping for it at all, corrupting the expression into a real
  `SYNTAX ERROR`. Fixed by mapping `^` to the physical UP-ARROW key
  (real BASIC V2's actual, unshifted exponentiation-operator key). The
  live/typed keyboard path had the identical gap independently (no
  scancode reached it at all) -- fixed by mapping the otherwise-unused
  Insert key to it, since no standard keyboard has a key at the C64's
  real up-arrow position. See `docs/peripherals.md`'s Known Gaps and
  `reference/basic-v2-quirks.md`.
- Several of this file's own `REM`-heavy lines were originally over 80
  characters long, which real BASIC V2's screen editor can't accept
  for one logical line (typed *or* pasted) at all -- not an emulator
  bug, genuine real-hardware behavior. Fixed by shortening/splitting
  those lines; see `reference/basic-v2-quirks.md`.

Still **not independently confirmed end-to-end for gameplay/audio
correctness** (i.e. that the sprites/sizes/colors/collisions/tones all
actually look and sound right) -- please paste it in via `make run`
and report back what you see/hear; if something's off, it's a
BASIC-listing bug to fix, not (necessarily) a sign of an emulator
regression.

## `sprite_test2_pair.bas`

A deliberately simpler sibling of `sprite_test2.bas`: just 2 round,
unstretched sprites, each with its own dedicated SID voice (no `MOD 3`
sharing needed with only 2 sprites). Still covers the full playfield
including X>255 via the `$D010` MSB register, same as the 8-sprite
version -- the only things simplified away are shape/size variety and
voice-sharing.

**Collision response is a real bounce, not a random teleport** --
unlike the 8-sprite version, `$D01E`'s "who collided" bitmask doesn't
need to be checked bit-by-bit here: with only 2 sprites enabled, any
collision *is* the two of them, so both simply reverse direction, as
you'd naturally expect from 2 things bouncing off each other. To avoid
the "stuck re-triggering forever" bug documented in `reference/
sprites.md` (a bare direction-flip alone doesn't guarantee separation
before the very next frame checks again), the move/border-bounce
subroutine is re-run immediately after the reversal, so the pair
actually steps apart in the same frame the collision was detected
rather than trusting next frame's movement to do it.

**That alone still wasn't enough** -- a *stale* `$D01E` reading (latched
against the old, pre-fix position in the gap before the fix's own
`POKE` takes effect) could echo back one tick later as a second,
spurious reversal, sending the pair back into each other and repeating
indefinitely (visible as "collide, bounce apart a couple of steps, then
reverse and collide again"). Fixed with a cooldown counter (`CD`,
subroutine 4000): for a few ticks after a real bounce, `$D01E` is still
read (to keep clearing it) but deliberately ignored, giving the actual
separation time to take effect before the next real collision is
honored. See `reference/sprites.md` for the general lesson.

## `sprite_test.bas`

A single hardware sprite (not a software-drawn shape) bouncing off all
four screen edges. Exercises: sprite data pointer (`$07F8`), sprite
enable (`$D015`), sprite color (`$D027`), sprite X/Y position registers,
and the border-collision boundaries used here (`X` 24-220, `Y` 50-229)
match this project's own visible-area geometry (`docs/vic-ii.md`).
**This exact file is what first caught a real paste-as-typing bug**:
its own `IF X<24 OR X>220 THEN DX=-DX` line silently dropped both
comparison operators before `<`/`>` were added to `char_to_c64key()`'s
mapping (see `docs/peripherals.md`'s Known Gaps) -- corrupting the
bounds check into an infinite scroll instead of a bounce.

## `sprite_move.bas`

A single sprite under direct keyboard control instead of automatic
movement -- W/A/S/D move it up/left/down/right one pixel per keystroke,
clamped to the real border (full width, including X>255 via the
`$D010` MSB register, same as the other sprite tests), starting
centered on screen. Uses `GET K$` -- real BASIC V2's non-blocking
single-keystroke read (returns `""` immediately if nothing's waiting,
unlike `INPUT`, which blocks for a whole line) -- in a tight poll loop;
holding a direction down moves continuously via the real KERNAL's own
key-repeat, not any custom timing/debounce logic here. See
`reference/basic-v2-quirks.md` for the general `GET` technique.
