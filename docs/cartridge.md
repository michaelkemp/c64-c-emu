# Cartridge (`.crt`) support (Phase 8)

## Scope: generic/type-0 hardware only, initially

Real `.crt` files can declare 100+ distinct hardware "types," most of
which need their own bespoke bank-switching register emulation (each is
effectively its own small chip). Building all of them is out of scope
for an initial pass. **Generic/type-0** — plain static ROM, no
bank-switching registers at all, just a fixed image mapped into
`$8000-$9FFF` (ROML) and/or `$A000-$BFFF`/`$E000-$FFFF` (ROMH, depending
on 8K/16K/Ultimax mode) — is the only hardware type this phase targets.
Any other declared type should raise a clear, specific
"unsupported cartridge hardware type N" error rather than silently
misbehaving or guessing.

The `.crt` container format itself is an openly published,
non-proprietary format (unlike the ROM *contents* of a real commercial
cartridge, which are copyrighted the same way KERNAL/BASIC are) — safe
to implement directly from its public specification. Standard shape:
an ~64-byte file header (magic string, version, hardware type, EXROM/
GAME line states, cartridge name) followed by one or more CHIP packets
(each with its own small header: load address, bank number, size, ROM
vs. RAM, and the raw data).

## Bus integration

Extend Phase 2's PLA-driven bank-switching table with the cartridge's
own `EXROM`/`GAME` line states as two more inputs feeding the *same*
LORAM/HIRAM-gated logic — don't build a separate, parallel cartridge-
specific memory path. Verify the exact resulting truth table from a
primary technical reference before implementing: in particular, the
real, **asymmetric** relationship between ROML and ROMH is a common
point of confusion — ROML (`$8000-$9FFF`) requires `LORAM` **and**
`HIRAM` both set; ROMH in 16K mode requires only `HIRAM` — don't assume
symmetry between the two without checking a primary source, since a
plausible-sounding paraphrase getting this backwards is a real, documented
mistake other implementers have made.

## Ultimax mode (`GAME=0`, `EXROM=1`) — optional follow-up

A real, distinct hardware configuration some cartridges use: RAM is
only genuinely available at `$0000-$0FFF`; `$1000-$7FFF` and
`$A000-$CFFF` are open bus (no RAM chip-select at all in this mode);
`$8000-$9FFF` is ROML; `$D000-$DFFF` is always I/O regardless of
`CHAREN`; `$E000-$FFFF` is ROMH, replacing the KERNAL entirely. Writes
to the ROML/ROMH address ranges in this mode are genuine no-ops for a
generic cartridge (not a write to hidden RAM underneath, unlike the
non-Ultimax ROM-overlay case) — verify this specific asymmetry from a
primary technical reference rather than assuming every ROM-shadowed
region behaves the same way.

## Autostart

No cartridge-specific boot code should be needed: the genuine,
unmodified KERNAL's own reset routine already checks for the CBM80
signature bytes at the start of ROML and jumps into cartridge code
itself when present, as real hardware does. If autostart doesn't work
once Phase 2's real KERNAL is staged, look for a bug in the memory-map/
PLA logic first, not in "missing cartridge boot code" — there shouldn't
be any needed.

## Verification target

A synthetic, hand-built `.crt` (generic/type-0, built directly by this
project's own code — not a vendored real cartridge dump, which would be
copyrighted software) round-trips through parse → load → the CBM80
autostart check. If you have access to a cartridge dump you personally
and legitimately own for further testing, that's a reasonable
supplementary check, but the checked-in automated tests should use only
synthetic fixtures this project generates itself.

## Known gaps to disclose as you build

- Every non-generic/type-0 hardware type (bank-switching cartridges of
  all kinds) — explicitly out of scope until a specific need arises.
- Ultimax mode, if deferred past the initial cut.
