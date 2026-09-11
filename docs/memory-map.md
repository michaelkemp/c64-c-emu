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

## Known gaps / deliberate simplifications to disclose here as you make them

- VIC-II/SID register mirroring within their I/O ranges — decide and
  write down whether you modeled it.
- Color RAM's undefined high nibble — decide and write down what you
  return for it.
- Open-bus behavior generally (reading an address with nothing mapped to
  it on real hardware returns the last byte that was on the data bus,
  not a fixed 0/`$FF`) — a fully accurate open-bus model is a real,
  deep rabbit hole; returning a fixed value (document which) is an
  acceptable disclosed simplification unless/until specific software is
  found that depends on real open-bus behavior.
