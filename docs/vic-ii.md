# MOS 6567/6569 VIC-II — scanline-accurate from the start (Phase 4)

This is the phase where this project makes its central architectural
bet: build the video chip as a cycle/scanline-driven state machine from
day one, not a "run some CPU cycles, then compute a whole frame's pixels
in one function call" renderer. See `CLAUDE.md`'s opening rationale for
why — the short version is that real raster effects (color-bar splits,
mid-frame charset swaps, more-than-8-sprite tricks) all depend on
register writes taking effect at the exact scanline they happen on, and
a once-per-frame snapshot architecturally cannot represent that no
matter how much other fidelity it has.

Primary references: Christian Bauer's cycle-by-cycle reverse-engineering
article — https://www.cebix.net/VIC-Article.txt, fetched and read as
raw text (not an AI summary of it — see docs/sources.md for exactly
what was confirmed and how, including one case where the raw diagram's
own ASCII-art column alignment was genuinely ambiguous and had to be
resolved by finding the article's own prose stating the fact in words
instead of trusting a visual column-count). Rule numbers cited in
`src/c64/vic_ii.c`'s comments refer to that article's own numbering.

## The PAL timing budget

- **63 PHI2 cycles per scanline** (PAL — NTSC differs, 65 cycles/line;
  decide up front whether you support both or PAL-only for now and
  document the choice).
- **312 scanlines per frame** (PAL), of which the visible display area
  is a real, documented subset — the rest is vertical border/blanking.
- That's 63 × 312 = **19,656 PHI2 cycles per frame**, at a PAL dot clock
  giving a real frame rate of PAL_CLOCK_HZ / 19656 ≈ **50.125 Hz** — not
  a round 50Hz. Use the exact computed value wherever frame pacing
  matters (Phase 6/7), not a hardcoded `50`.

## Architecture: what "scanline-accurate" actually means here

The VIC-II and CPU share the same bus and literally contend for cycles
on real hardware — the VIC-II can steal cycles from the CPU (see
Badlines below). This means the main loop (Phase 6) cannot simply "run
the CPU for N cycles, then ask the VIC-II what happened" — it must
interleave at the cycle level: each PHI2 cycle, the VIC-II may (a) do a
memory fetch of its own (stealing the bus from the CPU that cycle), (b)
advance its raster position, (c) latch register writes that landed this
cycle, and (d) produce (or not) a pixel/group of pixels for the current
raster position. The concrete API shape this implies:

```
vic_ii_cycle(&vic, bus);   // advance exactly one PHI2 cycle
// returns whether this cycle is a VIC-II bus-access cycle (CPU stalls)
```

driven by the main loop as: for each PHI2 cycle, call `vic_ii_cycle`
first (it may claim the bus), then either stall the CPU for a cycle or
let it execute if the bus is free that cycle. This is why Phase 1's CPU
core needs a "step exactly one cycle" interface, not "step one whole
instruction" — see `docs/6502-reference.md` and `docs/machine.md`.

A per-scanline (rather than strict per-cycle) granularity is an
acceptable, coarser starting point if per-cycle turns out to be too much
complexity up front — it still gets you correct raster-IRQ timing and
mid-frame register changes, just not perfectly accurate badline/sprite
DMA cycle-stealing timing. If you take this shortcut, **document it
explicitly as a known gap** (this is exactly the kind of thing the
project's own convention exists to catch) and revisit if/when a real
program's timing-sensitive effect doesn't work.

**Decided (Phase 4, done): a middle ground, not either extreme.** The
*timing/bus-access* state machine — bad-line detection, VC/RC/VCBASE/
VMLI, c/g/p/s-access scheduling, the BA "bus stolen" signal, sprite DMA
on/off, the raster-IRQ compare, both border flip-flops' Y-checks — runs
at true **whole-PHI2-cycle** granularity via `vic_ii_cycle()`, called
once per cycle exactly like the CPU core. *Pixel compositing* (border
X-check, graphics color, sprite overlay/expansion/priority/collision)
is computed per **pixel** (8 per cycle) as each cycle's data becomes
available, written into a per-scanline buffer that's final once the
line's 63 cycles complete. This is coarser than literally simulating
the real 24-bit sprite shift registers and 8-bit graphics shift
register cycle-by-cycle (it won't reproduce FLI, hyperscreen,
linecrunch, or sprite stretching/crunch — all genuinely "Effects and
applications" territory in the source article, not core chip behavior)
but it reproduces every verification target below. See
`src/c64/vic_ii.h`'s own header comment for the full reasoning, and
"Known gaps" below for the complete list of what this doesn't cover.

## Register map (`$D000-$D02E`, mirrored through `$D3FF`)

| Range | Purpose |
|---|---|
| `$D000-$D00F` | Sprite 0-7 X/Y position (paired bytes) |
| `$D010` | Sprite X MSBs (9th bit of X position, one per sprite) |
| `$D011` | Control register 1: raster compare bit 8, `ECM`/`BMM`/`DEN`/`RSEL`, Y scroll |
| `$D012` | Raster line (current on read, compare value on write — real dual-purpose register) |
| `$D013`/`$D014` | Light pen X/Y (low priority, stub) |
| `$D015` | Sprite enable bits |
| `$D016` | Control register 2: `MCM`, `RES`, `CSEL`, X scroll |
| `$D017` | Sprite Y expansion |
| `$D018` | Memory pointers: screen RAM base, character set base (relative to the VIC-II's own bank, see below) |
| `$D019` | Interrupt register (which raster/sprite-collision IRQ sources are pending — read clears, same ICR-style gotcha as the CIA) |
| `$D01A` | Interrupt enable mask |
| `$D01B` | Sprite-to-background priority |
| `$D01C` | Sprite multicolor mode select |
| `$D01D` | Sprite X expansion |
| `$D01E` | Sprite-sprite collision (read clears) |
| `$D01F` | Sprite-background collision (read clears) |
| `$D020` | Border color |
| `$D021` | Background color 0 |
| `$D022-$D024` | Background colors 1-3 (multicolor modes) |
| `$D025`/`$D026` | Sprite multicolor 0/1 (shared across all sprites using multicolor mode) |
| `$D027-$D02E` | Per-sprite color |

## VIC-II's own memory view (bank switching)

The VIC-II does **not** see the CPU's bank-switched view from Phase 2 —
it has its own, separate 16KB "bank" selected by CIA2 Port A bits 0-1
(inverted: `00` = bank 3, `$C000-$FFFF`; `01` = bank 2; `10` = bank 1;
`11` = bank 0, `$0000-$3FFF`, the default). Within that 16KB bank,
`$D018` selects screen RAM (in 1KB steps) and character-set data (in
2KB steps, further overridden to always read *Character ROM* instead of
RAM when the selected character-data address falls in either of the two
4KB windows real hardware permanently wires to Character ROM regardless
of what's in RAM there — verify the exact windows from the datasheet;
this is a real, easy-to-miss exception, not a general rule that VIC-II
always reads RAM). Implement `vic_ii_read(&vic, addr)` as its own
function, separate from the CPU-facing `bus_read8`, sourced from the
same underlying RAM/Character-ROM arrays but through this different
address translation.

**Done** — `vic_ii_read()` in `src/c64/vic_ii.c` implements the bank
offset and the `$1000-$1FFF`/`$9000-$9FFF` Character-ROM-override
windows exactly as described above (banks 0 and 2 are the ones that
overlap these windows; banks 1 and 3 never do). One honest caveat: this
specific window-address fact is a **C64 board-wiring fact, not a
VIC-II-chip fact** — Bauer's article (a VIC-II-specific document) says
nothing about it, and this project's only source for it right now is
this doc's own pre-existing text, not independently re-verified against
a primary C64 schematic/board reference. Treat it the same way as the
keyboard matrix layout (`docs/cia.md`): sourced, not yet independently
cross-checked. Color RAM is wired directly into the c-access and is
**not** part of this bank-switched 16KB space at all (confirmed from
the article: c-access reads video-matrix data via `vic_ii_read()`, but
its color nibble comes from a separate, always-present Color RAM,
matching `src/c64/memory.c`'s own separate `color_ram` array from
Phase 2 — `VicII` holds its own pointer to that same array).

## Modes to implement, in order

1. **Standard character mode** (`ECM=0, BMM=0, MCM=0`) — verify against
   the real boot screen first; this is the concrete, unambiguous
   "did I get the basics right" checkpoint. **Done** — see
   `render_pixels_from_byte()`/`do_g_access()` in `src/c64/vic_ii.c`,
   address/data formulas sourced directly from the article (section
   3.7.3.1, confirmed via docs/sources.md). Verified with synthetic
   screen/charset content in `tests/unit/test_vic_ii.c` (no real ROMs
   available to check the actual boot screen — see Known Gaps).
2. **Multicolor character mode** (`MCM=1, BMM=0`) — real per-cell
   behavior: each screen-RAM cell's *own* color-RAM entry's bit 3
   decides whether that specific cell renders hi-res or multicolor, not
   a single global switch. Get this per-cell nuance right; it's a real,
   verifiable fact from testing against real multicolor-text software.
   **Done** — the per-cell MC-flag check (c-data bit 11, which is
   literally color RAM's own bit 3) is implemented exactly as described.
3. **Bitmap mode** (`BMM=1, MCM=0`) and **multicolor bitmap mode**
   (`BMM=1, MCM=1`). **Done**, same file/section, formulas from article
   sections 3.7.3.3/3.7.3.4.
4. **Extended color mode** (`ECM=1`) — lower priority, rarely used by
   real software; fine to defer if nothing you're testing against needs
   it, but document the deferral. **Deferred, disclosed**: `ctrl1`'s ECM
   bit is recognized and renders solid black (matching the article's
   own statement that this project's other unimplemented "invalid" mode
   combinations all produce black) rather than the real four-background-
   colors-per-character behavior. See Known Gaps.

## Sprites

Fetch/render for all 8, X/Y position (including the 9-bit X via
`$D010`), expansion (X and Y independently), multicolor mode, and both
collision types (sprite-sprite via `$D01E`, sprite-background via
`$D01F`). **Border has strictly higher display priority than every
sprite** — a sprite must be clipped by the border, never drawn over it;
verify this specific fact against a primary source (Bauer's article has
a dedicated priority section) rather than a paraphrase, since this is a
genuinely easy detail to get backwards and only shows up as a visible
bug with a real sprite-using program running near the border.

**Done** — `src/c64/vic_ii.c`'s `update_sprite_dma()` implements the
article's DMA on/off rules 1-7 (section 3.8.1) exactly, including the
Y-expansion "advance line" flip-flop and MCBASE/MC bookkeeping;
`composite_sprites_for_line()` implements priority (`MxDP`,
sprite-0-highest) and both collision types (section 3.8.2), including
the real "only the first collision after the register reads as zero
raises the IRQ latch bit" behavior and collision suppression inside the
vertical border. Border-over-sprite priority is enforced structurally:
sprite compositing runs against a background/border buffer that already
has the border color written wherever `main_border` was set, and
`composite_sprites_for_line()` never overwrites it.

**Deliberately not implemented**: rule 7a (the obscure "CPU clears MxYE
in cycle 15" MCBASE-averaging special case used by advanced
sprite-stretching/crunch tricks — article section 3.14.7-equivalent
"Effects and applications" territory). Real sprite DMA/rendering is
modeled at whole-PHI2-cycle granularity, not the real half-cycle p/s-
access phase timing (see "Architecture" above) — this doesn't affect
priority, collision, or on-screen position, only exactly which half of
which cycle the underlying bus access happens in.

## Badlines

A "badline" is a real condition (raster line within the display window,
where the low 3 bits of the raster line match the Y-scroll value in
`$D011`) that makes the VIC-II steal **40 extra cycles** from the CPU on
that line to refresh its internal video matrix line buffer. This is not
optional to model as a real cycle cost if you want real timing-sensitive
software (which includes some totally ordinary programs, not just
demos) to behave correctly — a badline that's only a queryable flag but
doesn't actually cost the CPU cycles will desync any program timing a
loop against real cycle counts across that raster line.

**Done, and the actual number is 43, not 40** — confirmed directly from
the article (section 3.7.2's rule 3, docs/sources.md): `BA` goes low in
cycle 12 and stays low through cycle 54 (43 cycles), even though the
40 c-accesses themselves only span cycles 15-54; `vic_ii_cycle()`
returns `true` (bus stolen) for exactly that window. A genuinely subtle,
directly-confirmed extra detail also implemented: **the first three
c-accesses of any bad line (cycles 15-17) read a forced `$FF`** for the
character/bitmap byte (the color-RAM-sourced bits are unaffected) due to
a real AEC-vs-BA startup delay — this is the documented cause of the
"colorful garbage stripe" visible on the left edge of real, naively-
written custom-charset screens, not a bug to avoid reproducing.

## Raster IRQ

`$D012` (bits 0-7) + `$D011` bit 7 (bit 8) together form a 9-bit raster
compare value. When the current raster line matches it, set the
corresponding bit in `$D019` and assert IRQ if enabled in `$D01A` — this
must happen at the exact scanline (Phase 6's IRQ delivery then fires
between instructions at the correct cycle, same as any other IRQ
source). This is the single most important concrete capability this
whole phase's architecture exists to deliver correctly.

**Done.** One real, easy-to-get-wrong detail confirmed and implemented:
**`$D019` is write-1-to-clear, not read-clears** like the CIA's ICR —
"the processor has to write a 1 there 'by hand'... the VIC doesn't
clear the latch on its own." A CIA-style implementation copy-pasted here
would be a real, silent bug (spurious repeated IRQs, or IRQs that never
clear). The compare check itself runs every cycle 1 (the article's own
noted line-0-uses-cycle-2 exception is a disclosed, very minor gap —
see Known Gaps).

## Verification targets

1. Real boot screen (Phase 2's ROMs + standard character mode) —
   pixel-correct against the known, iconic real output. **Done**: with
   real ROMs staged (`scripts/stage_roms.sh`) and `c64memory_attach_vic()`
   wired up, `make demo-real-rom` (`tools/demos/real_rom_boot_dump.c`)
   genuinely renders the real "\*\*\*\* COMMODORE 64 BASIC V2 \*\*\*\*"
   boot screen and "READY." prompt from real KERNAL/BASIC/Character ROM
   content — visually confirmed, and `tests/integration/test_boot.c`
   independently confirms it behaviorally (the real "READY." screen-code
   sequence appears in screen memory after a real cold-start). This run
   also visibly exhibited the documented "first three c-accesses of a
   bad line read forced `$FF`" DMA-delay quirk (a checkerboard artifact
   on the screen's left edge) with real ROM content, not just in
   synthetic tests. Standard/multicolor text and both bitmap modes'
   *pixel-level* address/data-bit formulas are still primarily verified
   against synthetic content in `tests/unit/test_vic_ii.c`, since
   pixel-exact comparison against a reference image wasn't done here.
2. A hand-assembled test program that changes `$D020` (border color)
   partway down the screen from a raster IRQ handler, producing a
   visibly split-color frame when rendered scanline-by-scanline — the
   concrete test a frame-snapshot design structurally cannot pass.
   **Done** as a direct register-write equivalent (no hand-assembled
   6502 test program exists yet since Phase 6's real machine loop
   doesn't exist to run one against): `test_raster_split_border_color_
   mid_frame` in `tests/unit/test_vic_ii.c` writes `$D020` between two
   scanlines and asserts both scanlines keep their own distinct border
   color in the committed framebuffer.
3. Sprite priority/collision against a hand-assembled test moving a
   sprite across the border and over background content. **Partially
   done**: `tests/unit/test_vic_ii.c` verifies sprite positioning,
   standard and multicolor rendering, sprite-sprite priority, and
   sprite-sprite collision detection (including the auto-clear-on-read
   and first-collision-only-raises-IRQ semantics) directly against the
   `VicII` struct/framebuffer. Not yet exercised via an actual
   hand-assembled 6502 program moving a sprite across the border in
   real time — Phase 6's machine loop now exists (`src/c64/machine.c`)
   so this is unblocked, but the actual hand-assembled test program
   hasn't been written yet.

## Known gaps to disclose as you build

- NTSC timing (65 cycles/line, different frame rate) — **PAL (6569)
  only**, as decided; `VIC_CYCLES_PER_LINE`/`VIC_LINES_PER_FRAME` in
  `src/c64/vic_ii.h` are hardwired to the PAL values.
- Extended color mode (ECM) and the three genuinely-invalid ECM/BMM/MCM
  combinations — deferred, rendered as solid black. See "Modes" above.
- **Granularity, decided**: whole-PHI2-cycle timing/bus-access state,
  per-pixel compositing — not literal per-half-cycle shift-register
  simulation. Concretely NOT reproducible under this model: FLI,
  hyperscreen/border-opening, linecrunch, sprite stretching/crunch, and
  any effect that depends on a register changing mid-character-cell
  (sub-8-pixel) rather than between cycles. Concretely IS reproducible:
  badline CPU-cycle stealing, raster-IRQ-timed register changes (the
  project's own central bet), sprite DMA/priority/collision/movement.
- Sprite DMA rule 7a (the CPU-clears-MxYE-in-cycle-15 special case) is
  not implemented — see "Sprites" above.
- The raster-IRQ compare's line-0-specific "checked in cycle 2 instead
  of cycle 1" exception is not modeled (checked at cycle 1 uniformly) —
  a one-cycle timing difference only observable if a raster IRQ is
  deliberately set for line 0 itself.
- Lightpen (`$D013`/`$D014`) is a plain read/write stub, not connected
  to anything (no lightpen input exists in this project, and CIA1 Port
  B bit 4 — the software-triggered LP line — isn't wired to it yet).
- The real hardware invisible-X-position gap (`$1f8`-`$1ff`) is a
  natural consequence of this project's `% VIC_X_MODULUS` arithmetic
  (values in that range are simply never reached by the per-pixel
  loop), not separately special-cased — confirmed to match the
  article's own statement of this fact, not independently re-derived.
- The rare multi-sprite-with-mixed-`MxDP` "foreground pixel inherits a
  behind-foreground sprite's priority against a different, in-front
  sprite" interaction (article section 3.8.2's own "hard to represent
  consistently" case) is not modeled exactly — each sprite's priority
  decision here is evaluated independently against the background/
  foreground classification, not against other sprites' inherited
  priority. See the comment in `composite_sprites_for_line()`.
- Both border flip-flops default to *set* at `vic_ii_init()` (full
  border until the display window is configured) — a reasonable,
  disclosed initialization choice, not a fact stated by the article
  (which doesn't document a power-on default for this internal,
  non-register state) — see `src/c64/vic_ii.c`.
- **Wired in Phase 6**: `src/c64/machine.c`'s `machine_cycle()` runs
  `vic_ii_cycle()` every PHI2 cycle (stealing the bus from the CPU when
  it must), ORs the VIC-II's raster IRQ into the shared IRQ line
  alongside both CIAs, and re-derives the VIC-II's bank from CIA2 Port
  A every cycle (`tests/unit/test_machine.c`'s
  `test_vic_bank_follows_cia2_port_a` and
  `test_vic_raster_irq_interrupts_running_cpu` cover this).
- **Found and fixed in Phase 7, via the first real live SDL2 display**
  (see `docs/peripherals.md`): the border flip-flops' on/off state was
  being used as the *only* thing controlling whether a pixel got border
  color, with no separate notion of horizontal/vertical blanking at
  all — so the compositor painted border color continuously through the
  real hardware's genuine sync/blanking intervals too, which a real
  monitor shows as pure black (no picture), not border color. Every
  earlier verification of this fact was necessarily blind to it: all of
  Phase 4/6's checks compare specific screen-memory-driven pixel
  positions or border-color-register values, never "does the whole
  picture look like a real photo of a C64." It took someone actually
  looking at a live rendered frame (Phase 7's own stated verification
  target) to notice the border looked wrong at all.

  Fixed by reading section 3.4 of Bauer's article directly (raw
  `.txt`, `docs/sources.md`) for the 6569's own documented blanking
  geometry — a real primary-sourced fact, not a guessed crop margin:
  first/last vblank line 300/15 (so visible lines run 16-299, 284
  lines, exactly matching the article's own separately-stated "Visible
  lines" count for the 6569); first/last visible X coordinate 480/380
  (wrapping through the X=503/0 boundary, since X coordinates are
  numbered from the raster-IRQ reference point at $194/404, not from
  the start of the picture). These are now `VIC_FIRST_VISIBLE_X`/
  `VIC_LAST_VISIBLE_X`/`VIC_FIRST_VISIBLE_LINE`/`VIC_LAST_VISIBLE_LINE`
  in `src/c64/vic_ii.h`, genuinely distinct from the border comparator
  values (`border_left`/`border_right`/`border_top`/`border_bottom` in
  `src/c64/vic_ii.c`) — the border flip-flops keep painting border
  color straight through blanking on real hardware too; it's the video
  *signal* that's separately forced off there, which is what's now
  modeled. `vic_ii_cycle()` forces framebuffer pixels outside this
  window to black (index 0) once each line is committed, overriding
  whatever border/graphics color compositing already computed — see
  `tests/unit/test_vic_ii.c`'s
  `test_vertical_blanking_forces_black_regardless_of_border_color` and
  `test_horizontal_blanking_forces_black_regardless_of_border_color`.
  No existing test regressed: every prior check reads pixels that are
  already inside the visible window.

  This is a genuine, disclosed VIC-II *chip-timing* fix (not just a
  frontend cosmetic crop) — `tools/demos/`'s raw PPM dumps and every
  internal test still see the full, un-cropped, native-X-coordinate
  504×312 array (now correctly black in the blanking region rather
  than border-colored); `src/frontend/sdl_frontend.c`'s
  `render_frame()` separately rotates/crops that same array into a
  contiguous, non-wrapped 405×284 picture for live display, since a
  real monitor never shows the array's own internal wraparound seam —
  see `docs/peripherals.md`'s Screen section for that display-only
  step.
