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
article (widely available, search for "VIC-II Article" — cite the exact
section when pulling a specific fact from it, since it's long and
sections cover very different sub-behaviors) and the official
preliminary MOS 6567 VIC-II datasheet.

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

## Modes to implement, in order

1. **Standard character mode** (`ECM=0, BMM=0, MCM=0`) — verify against
   the real boot screen first; this is the concrete, unambiguous
   "did I get the basics right" checkpoint.
2. **Multicolor character mode** (`MCM=1, BMM=0`) — real per-cell
   behavior: each screen-RAM cell's *own* color-RAM entry's bit 3
   decides whether that specific cell renders hi-res or multicolor, not
   a single global switch. Get this per-cell nuance right; it's a real,
   verifiable fact from testing against real multicolor-text software.
3. **Bitmap mode** (`BMM=1, MCM=0`) and **multicolor bitmap mode**
   (`BMM=1, MCM=1`).
4. **Extended color mode** (`ECM=1`) — lower priority, rarely used by
   real software; fine to defer if nothing you're testing against needs
   it, but document the deferral.

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

## Raster IRQ

`$D012` (bits 0-7) + `$D011` bit 7 (bit 8) together form a 9-bit raster
compare value. When the current raster line matches it, set the
corresponding bit in `$D019` and assert IRQ if enabled in `$D01A` — this
must happen at the exact scanline (Phase 6's IRQ delivery then fires
between instructions at the correct cycle, same as any other IRQ
source). This is the single most important concrete capability this
whole phase's architecture exists to deliver correctly.

## Verification targets

1. Real boot screen (Phase 2's ROMs + standard character mode) —
   pixel-correct against the known, iconic real output.
2. A hand-assembled test program that changes `$D020` (border color)
   partway down the screen from a raster IRQ handler, producing a
   visibly split-color frame when rendered scanline-by-scanline — the
   concrete test a frame-snapshot design structurally cannot pass.
3. Sprite priority/collision against a hand-assembled test moving a
   sprite across the border and over background content.

## Known gaps to disclose as you build

- NTSC timing (65 cycles/line, different frame rate) — PAL-only unless/
  until NTSC is explicitly wanted; document which you built.
- Extended color mode, if deferred.
- Full per-cycle (vs. per-scanline) accuracy, if you took the coarser
  shortcut described above — say so explicitly, and name which specific
  real-software timing effects are known not to work as a result.
