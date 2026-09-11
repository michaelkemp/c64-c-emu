# Disk (1541) support (Phase 9)

## Real hardware, briefly

A 1541 is not "a file on a bus" — it's a **complete second computer**:
its own 6502 CPU, its own 2KB RAM, its own 16KB DOS ROM, and two 6522
VIA chips (a different chip from the C64's own CIA) driving the stepper
motor, read/write head, and the serial bus interface. The real C64
talks to it by bit-banging a serial protocol (IEC, derived from
IEEE-488) over CIA2 port A lines (ATN/CLK/DATA) — the KERNAL's
`LISTEN`/`TALK`/`ACPTR`/`CIOUT` routines toggle those lines with real
cycle-level timing, and the drive's own program watches its own VIA and
replies. Both machines run concurrently. Real floppies also store data
GCR-encoded, not as plain bytes — `.d64` files are a convenience format
already decoded to plain sector bytes, sidestepping that layer
regardless of which strategy below is used.

## Two real strategies — build strategy 1 first, attempt strategy 2 as this project's actual stretch goal

**1. KERNAL-trap "fake" loading**: intercept the KERNAL's own LOAD/SAVE
calls at their fixed jump-table addresses, parse/write the `.d64`'s
track-sector-directory structure directly, and hand back bytes as if a
real load happened — no drive CPU, no VIA, no serial bus at all. Fast to
build, no second ROM needed, and ordinary `LOAD`/`SAVE` work immediately
for anything using the stock KERNAL loader.

**2. True drive emulation**: a full second machine, cross-stepped with
the first over an emulated IEC bus, exactly like real hardware. The
Phase 1 CPU core is directly reusable (already validated, chip-
agnostic), and the 1541's own memory map is much simpler than the C64's
(no bank-switching) — what's genuinely new is a VIA chip module (similar
scope to `docs/cia.md`'s CIA), the drive's own tiny bus, the serial-bus
protocol itself (the fiddly, timing-sensitive part), and a real 1541 DOS
ROM (see licensing below).

**The real, unavoidable tradeoff**: strategy 1 cannot run software with
a custom "fastloader" — a large fraction of commercial disk games
replace the KERNAL's stock loader with their own hand-optimized routine
that talks to real drive hardware/timing directly, bypassing the KERNAL
entirely. A trap at the KERNAL level never sees those calls at all; the
game just hangs waiting for a drive that isn't really there. Only
strategy 2 handles that correctly.

This project builds strategy 1 first (Phase 9a — fast, immediately
useful, matches the "document the real gap" convention rather than
pretending it's complete). Strategy 2 (Phase 9b) is the genuine stretch
goal this project's whole "why C, why now" rationale points toward — see
`CLAUDE.md`. Don't start Phase 9b until Phases 1-8 are solid; it's a
comparable-scope undertaking to Phase 4's scanline-accurate VIC-II
rewrite, not a small add-on.

## ROM licensing (for Phase 9b)

Same discipline as the C64 ROMs (see `CLAUDE.md`): **do not fetch or
vendor a 1541 DOS ROM image**. The fact that reference emulators bundle
one is not proof of a verified redistribution license. `scripts/
stage_roms.sh`'s same pattern extends here — stage the user's own
already-legitimately-acquired dump into gitignored `roms/`, never
download one. For the record (a historical fact, not a redistribution
license): the standard 1541 DOS ROM is historically known by Commodore
part numbers 325302-01 + 901229-05 (DOS 2.6), 16,384 bytes.

## `.d64` format

Standard 35-track image, 683 total sectors, 174,848 bytes:

| Track range | Sectors/track | Sectors |
|---|---|---|
| 1-17 | 21 | 357 |
| 18-24 | 19 | 133 |
| 25-30 | 18 | 108 |
| 31-35 | 17 | 85 |

**BAM (Block Availability Map), track 18 sector 0**:

| Offset | Content |
|---|---|
| `$00-01` | First directory sector T/S (always `18/1`) |
| `$02` | DOS version (`$41` = standard 2A) |
| `$04-8F` | Per-track BAM: 4 bytes/track — byte 0 = free-sector count, bytes 1-3 = 24-bit free bitmap (bit=1 means free) |
| `$90-9F` | Disk name, 16 chars, `$A0`-padded |
| `$A2-A3` | Disk ID (2 bytes) |
| `$A5-A6` | DOS type (`"2A"`) |

**Directory entries, track 18 sectors 1+ (8 entries per 256-byte sector)**:

| Offset | Content |
|---|---|
| `$00-01` | Next directory sector T/S (only meaningful on each sector's first entry; `$00/$00` once there's no next sector) |
| `$02` | File type: bits 0-3 = type (0=DEL, 1=SEQ, 2=PRG, 3=USR, 4=REL), bit 6 = locked, bit 7 = closed (unset = a "*"/incomplete file) |
| `$03-04` | First T/S of the file's data chain |
| `$05-14` | Filename, 16 chars, `$A0`-padded |
| `$1E-1F` | File size in sectors (little-endian), a block count, not exact bytes |

**File data chain**: each sector's first two bytes are
`(next_track, next_sector)`; `next_track == 0` marks the last sector,
and `next_sector` then holds the offset of the *last valid byte* in that
sector. Real hardware interleaves sectors to give the physical disk time
to settle between reads; this doesn't matter for correctness in an
emulated context (only for real spinning-disk seek/settle time) — a
disclosed simplification, not a bug, if your allocator doesn't reproduce
it.

**Real-world directory gotcha worth designing for from the start**: some
real commercial disks contain directory entries deliberately typed
`DEL` (file type nibble 0) with the closed bit still set, used as
cosmetic directory separators — these display in a directory listing
but must never be treated as loadable files (real DOS's own file search
skips them). Any exact-name or wildcard file lookup should explicitly
skip `DEL`-type entries, matching real DOS behavior, not just whatever a
naive linear search happens to find first.

## KERNAL trap protocol (Phase 9a)

Traps fire at the **fixed KERNAL jump-table addresses** `$FFD5` (LOAD)
and `$FFD8` (SAVE) — these addresses are stable across every real
KERNAL revision specifically so machine-language programs can rely on
them without needing source-level compatibility. Only calls with device
number 8+ should be trapped; device 0 (keyboard) and 1 (cassette) must
fall through to the real, unmodified KERNAL routine untouched.

Everything *before* the trap point (BASIC tokenizing `LOAD`/`SAVE`,
`SETNAM`/`SETLFS`) and *after* it (BASIC's own post-LOAD relinking of
its variable/program-end pointers) should be genuine, unmodified
KERNAL/BASIC ROM code — the trap only replaces the "transfer bytes over
a serial bus to/from a real spinning disk" part.

**LOAD ($FFD5) on entry** (verify these exact zero-page locations
against the real KERNAL's own documented calling convention, not from
memory alone): filename length, filename pointer, device number,
secondary address are all passed in specific documented zero-page
locations. A PRG file's own first two bytes are *always* its claimed
load address and are *always* stripped from the actual loaded data,
regardless of secondary address — secondary address only picks *which*
address is used as the actual destination (0 = force to X/Y, ignoring
the file's own header value; 1 = use the file's own header value,
needed for machine-language programs with their own absolute load
address). On return: Carry clear + X/Y = end address + 1 on success;
Carry set + an error code in A on failure.

**SAVE ($FFD8) on entry**: a zero-page address holding a pointer to the
start of the data to save; X/Y = the end address (exclusive). On
return: Carry clear on success, set + an error code on failure — but
verify the real KERNAL's own defined error code range (1-9) against a
primary source before fabricating codes for conditions like "file
exists" or "disk full": on real hardware those are disk-error-channel-
only conditions (via `OPEN 15,8,15` + `INPUT#15`), **not** KERNAL-level
Carry-set error codes — a plain `SAVE` to an existing filename should
return Carry clear (success) from the KERNAL's own point of view while
correctly not overwriting the file, matching real hardware's actual
behavior even though it looks surprising on paper.

**Wildcard LOAD (`*`/`?`)**: real, documented KERNAL/DOS usage, not just
plain filenames. `*` matches the rest of the name, and — a real,
verified hardware quirk — everything in the pattern *after* a `*` is
ignored entirely (so `"PIC*.KOA"` behaves exactly like `"PIC*"` and
can't filter by "extension"). `?` matches exactly one character. A
bare `"*"` on real hardware means "whatever was last accessed" (needs
real disk-head-position state) — a reasonable, disclosed simplification
is "the first non-`DEL` directory entry," which is what a real,
freshly-booted `LOAD"*",8,1` actually hits in practice. A leading
drive-unit prefix (`"0:"`/`"1:"`, meaningful only on real dual-unit
drives) should be stripped before matching. Wildcard support is
reasonable to scope to `LOAD` only, not `SAVE` (real hardware's
`@0:*`-style "replace last file" convention is a separate, narrower
feature).

## Phase 9b: VIA 6522 + IEC bus (stretch goal — see roadmap)

Once attempted, document the VIA's own register map/timer semantics
here (or split into its own `docs/via.md`, your call — just keep the
one-doc-per-chip convention either way) with the same rigor as
`docs/cia.md`. The genuinely new, fiddly piece beyond "another timer
chip" is the **IEC bus protocol** itself: ATN (attention, C64-driven,
tells all devices on the bus "pay attention, a command follows"), CLK
and DATA (bit-banged, open-collector-wired-AND lines, both directions),
with real documented timing windows for each handshake step (talk/
listen addressing, byte-by-byte handshaking with EOI signaling on the
last byte). This requires cross-stepping the C64's CPU and the drive's
own CPU cycle-by-cycle — exactly the interleaving `docs/machine.md`'s
main loop is already built around, extended to a second CPU instance.

## Verification targets

- Phase 9a: `LOAD"$",8` / `LOAD"PROGRAM",8,1` / `SAVE"PROGRAM",8`
  round-trip correctly against the real KERNAL/BASIC, including
  wildcard `LOAD` and the `DEL`-entry-skipping behavior above.
- Phase 9b: a real fastloader-using disk game that hangs under Phase 9a
  now loads and runs correctly.

## Known gaps to disclose as you build

- No copy protection support (malformed sectors, half-tracks, weak
  bits) — the plain `.d64` sector model can't represent this regardless
  of strategy.
- No sector interleave modeling — disclosed simplification, doesn't
  affect correctness in an emulated context.
- No wildcard `SAVE`.
- Bare `"*"` LOAD semantics simplified to "first entry" rather than
  real hardware's "last accessed" — only matters for a narrow real
  pattern (load a file by name, then later bare `"*"` expecting to
  reload that same file).
