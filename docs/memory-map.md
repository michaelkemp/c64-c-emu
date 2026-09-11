# Memory map + PLA bank-switching (Phase 2)

## Why this is its own phase before any chip work

The C64's 64KB address space is not one flat block of RAM. Depending on
three control lines, the CPU sees RAM, BASIC ROM, KERNAL ROM, Character
ROM, or memory-mapped I/O registers at the *same* addresses at different
times. Every later chip (CIA, VIC-II, SID, cartridge) is just "another
region this bus dispatches to" — get the dispatch logic right here once,
and every later phase is a small, additive change to it.

## The 6510 I/O port (`$00`/`$01`)

The 6510 (not a plain 6502) has a built-in 2-bit-plus-control-lines I/O
port memory-mapped at `$00` (data direction register) and `$01` (port
data). Bits 0-2 of `$01` are `LORAM`, `HIRAM`, `CHAREN` — the three
lines that drive the bank-switching truth table below. Bits 3-5 drive
the datasette (motor/write/sense — low priority, can stub these).

**Model this as a bus-level memory-mapped device, exactly like any
other chip** — `bus_read8`/`bus_write8` dispatch `$00`/`$01` to this
device's own read/write handlers, the same pattern used for the CIAs and
VIC-II. The CPU core from Phase 1 needs **zero changes** for this to
work; it has no idea `$00`/`$01` are special, it just calls
`bus_read8`/`bus_write8` like for any other address.

## Bank-switching truth table

Three bits (`LORAM`, `HIRAM`, `CHAREN`) select what's visible in three
address ranges. Verify this exact table against a primary technical
reference before implementing (the C64 Programmer's Reference Guide's
memory map chapter, or an equivalent primary source) rather than
trusting a paraphrase — a wiki-style summary getting the ROML/ROMH
cartridge asymmetry backwards is a documented real mistake worth
avoiding twice:

| LORAM | HIRAM | CHAREN | `$A000-$BFFF` | `$D000-$DFFF` | `$E000-$FFFF` |
|---|---|---|---|---|---|
| 1 | 1 | 1 | BASIC ROM | I/O | KERNAL ROM |
| 1 | 1 | 0 | BASIC ROM | Character ROM | KERNAL ROM |
| 1 | 0 | 1 | RAM | I/O | RAM |
| 1 | 0 | 0 | RAM | Character ROM | RAM |
| 0 | 1 | 1 | RAM | I/O | KERNAL ROM |
| 0 | 1 | 0 | RAM | Character ROM | KERNAL ROM |
| 0 | 0 | x | RAM | I/O (if CHAREN=1) / Char ROM (if 0) | RAM |

Everywhere else (`$0000-$9FFF` minus the zero page port, `$C000-$CFFF`)
is always plain RAM regardless of these bits. The zero page and stack
(`$0000-$01FF`) are always RAM (other than the `$00`/`$01` port itself).

**Underlying RAM always exists and is always written through**, even
when a ROM or I/O view is currently switched in on top of it — switching
`CHAREN`/`LORAM`/`HIRAM` back to RAM must reveal whatever was last
written, not a stale/zeroed view. Implement this as "RAM is the real
backing store everywhere; ROM/I/O reads intercept before touching it,
writes to a ROM-shadowed region still land in the RAM underneath except
where a chip's own register write semantics say otherwise" — this one
sentence is the whole trick.

Cartridge `EXROM`/`GAME` lines extend this same table in Phase 8 — don't
build cartridge support into this table now, just leave the design able
to add two more inputs later without a rewrite (i.e., don't hardcode
"three-bit index," use named boolean conditions).

## I/O space (`$D000-$DFFF`) sub-map

When switched in, this 4KB region itself further subdivides (this part
never changes regardless of the bank-switching bits above, as long as
I/O is selected at all):

| Range | Device |
|---|---|
| `$D000-$D3FF` | VIC-II registers (mirrored every 64 bytes within this range on real hardware — decide whether to model the mirroring; it's a real, low-risk-to-skip simplification if you don't) |
| `$D400-$D7FF` | SID registers (also mirrored every 32 bytes) |
| `$D800-$DBFF` | Color RAM (only the low nibble of each byte is wired to real hardware — the high nibble reads back as open-bus/undefined; a common simplification is to just mask to 4 bits on read and ignore the high nibble, which is fine to do and worth stating as a disclosed simplification) |
| `$DC00-$DCFF` | CIA 1 |
| `$DD00-$DDFF` | CIA 2 |
| `$DE00-$DEFF` | I/O area 1 (cartridge-specific, mostly unused for a generic cartridge) |
| `$DF00-$DFFF` | I/O area 2 (cartridge-specific, mostly unused for a generic cartridge) |

## ROM staging

`scripts/stage_roms.sh` copies the user's own already-legally-acquired
KERNAL, BASIC, and Character ROM dumps into gitignored `roms/c64/`. This
project never fetches or vendors them — see `CLAUDE.md`'s license
discipline section. The three real files you're looking for, by their
standard sizes: KERNAL (8KB), BASIC (8KB), Character ROM (4KB).

## Verification target

With real staged ROMs and the CPU core from Phase 1 wired to this bus:
reset the CPU (real vector fetch at `$FFFC`/`$FFFD`, which — with
`HIRAM=1` — reads through to genuine KERNAL ROM) and confirm the CPU
starts executing real KERNAL reset-routine code. "It doesn't crash" is
not sufficient verification — trace the first few thousand instructions
and confirm the PC visits addresses that correspond to the real KERNAL's
documented reset routine (RAM test, I/O chip init, screen init, jump
into BASIC's cold-start) rather than wandering into garbage. If VIC-II/
CIA registers aren't implemented yet, KERNAL init code touching them
should at minimum not crash the bus dispatch (stub reads as 0, ignore
writes, until their own phases land).

## Implementation status (Phase 2, done)

`src/c64/memory.c` implements this file's truth table exactly, as a
second `Bus` implementation the Phase 1 CPU core plugs into unmodified
(`c64memory_as_bus()`). Verified by 40 hand-written unit tests
(`tests/unit/test_memory.c`), including all 8 rows of the bank-switching
truth table exhaustively, RAM write-through under every ROM view, and
that a write to `$D000-$DFFF` while I/O is switched in does *not* reach
the RAM underneath (the one real asymmetry vs. the ROM-shadow case).

The real-ROM verification target (Phase 2's own "reaches the genuine
KERNAL reset routine, not just doesn't crash") lives in
`tests/integration/test_boot.c` (`make integration`) — it traces the
first 5,000 real instructions after reset and reports the addresses
visited, but **does not yet assert they match the genuine KERNAL reset
routine**, since this session had no real, legally-staged ROM dump to
verify against (this project never fetches/vendors one itself — see
`CLAUDE.md`). Whoever next runs this with real staged ROMs should
confirm the traced addresses against the actual KERNAL disassembly (RAM
test, I/O init, screen init, cold-start into BASIC) and tighten that
test's assertion accordingly — right now it's a placeholder that proves
"didn't crash and didn't execute an illegal opcode," which the doc above
explicitly calls insufficient on its own.

## Known gaps / deliberate simplifications

- **VIC-II/SID/CIA1/CIA2 registers are stubbed** (reads return 0, writes
  ignored) since those chips aren't implemented yet (Phases 3-5).
  Register mirroring within each chip's own I/O window is deferred to
  that chip's own phase/doc, which frame it as their decision to make —
  moot for now since the stub ignores which register within the window
  was addressed anyway.
- **Color RAM's undefined high nibble**: reads return the stored low
  nibble with the high nibble forced to 0 (writes discard the incoming
  high nibble too) — the simplification `docs/memory-map.md` itself
  flagged as acceptable, rather than modeling real open-bus behavior.
- **Open-bus behavior generally** (reading an address with nothing
  mapped to it on real hardware returns the last byte that was on the
  data bus) is not modeled — unimplemented I/O registers fixed-return 0
  instead. Acceptable disclosed simplification unless/until specific
  software is found that depends on real open-bus behavior.
- **Datasette (bits 3-5 of the `$01` port) and the cassette-sense input
  aren't modeled.** Every unwritten (input) port bit floats high via
  `c64memory_effective_port()`, which is enough for correct
  LORAM/HIRAM/CHAREN behavior (the only bits that matter to this file)
  but doesn't reflect a real cassette player's actual sense line.
- **RAM powers on zeroed**, not the unpredictable pattern real hardware
  exhibits — a deliberate, low-risk simplification for determinism.
