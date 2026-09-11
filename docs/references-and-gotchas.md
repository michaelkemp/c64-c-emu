# References and gotchas

Read this after `docs/roadmap.md` and before starting Phase 1. It's the
"things that will bite you if nobody told you" doc — sharper and more
specific than the individual chip docs, which stay focused on register
maps and architecture. Everything below is a real, verifiable technical
fact, not a style preference.

## Primary-source references worth going to directly

Don't rely on a summarized/paraphrased version of any of these for a
behavior question that actually matters (see "read the primary source,
not a summary" below) — go to the source itself:

- **VIC-II**: Christian Bauer's cycle-by-cycle reverse-engineering
  article — https://www.cebix.net/VIC-Article.txt — is the single most
  valuable document for Phase 4. It's long; the sections on display
  priority (sprite-vs-border), badlines, and the exact per-cycle sprite
  DMA sequence are the ones you'll go back to repeatedly. Also the
  official preliminary MOS 6567 datasheet:
  https://6502.org/documents/datasheets/mos/mos_6567_vic_ii_preliminary.pdf
- **CIA 6526**: official preliminary datasheet —
  https://6502.org/documents/datasheets/mos/mos_6526_cia_preliminary_nov_1981.pdf
  — the ICR read-clears-all-flags behavior and the four timer run modes
  are both in here precisely.
- **SID**: reSID (https://github.com/daglem/reSID) is the reference
  implementation for filter/combined-waveform fidelity — GPL, read-only,
  see `CLAUDE.md`. The datasheet's own ADSR rate tables are directly,
  safely implementable without touching reSID at all — don't reach for
  reSID until you actually need the transistor-level stuff.
- **KERNAL error codes**: sta.c64.org/cbm64krnerr.html documents the
  real, narrow (1-9) KERNAL-level error code range — useful the moment
  you're tempted to invent a code for something that "feels like" it
  should be an error (file-exists, disk-full — see `docs/disk.md`).
- **`.d64` format**: ist.uwaterloo.ca/~schepers/formats/D64.TXT is the
  standard reference for the byte layout in `docs/disk.md`.
- **General community references** (useful as an index/starting point,
  *not* as a trusted primary source for a disputed fact):
  codebase64.org and c64-wiki.com. Both are good for "what's this
  feature called" and demoscene-technique background; treat any
  specific register-behavior claim on either as something to
  cross-check against a datasheet, not to cite directly.
- **VICE's own source** (github, search "VICE-Team") is worth reading
  when a specific behavior is genuinely ambiguous after checking the
  datasheet/article (e.g. an exact PLA truth table edge case) — it's
  GPL, so this is "read for understanding, then implement your own
  version," never "copy the logic verbatim." A useful gut check either
  way: it's a mature emulator, so if your own conclusion disagrees with
  its behavior on some real edge case, that's a signal to double-check
  your own reasoning before assuming VICE is wrong.

## The single biggest methodology lesson: read the primary source text yourself

When a specific hardware-behavior question actually matters (sprite/
border display priority is a real example), **fetch the actual primary
source and read the raw text directly** rather than asking an AI tool
to summarize it or answering from general/trained recall. The same
underlying question, asked different ways, can produce confidently
stated but contradictory answers from an AI summary of the same
document — the raw text settles it; a paraphrase of it might not. This
matters more, not less, when the one doing the reading is itself an AI
agent picking up this project.

## Concrete gotchas worth knowing before you hit them

- **Frame rate is ≈50.1245Hz, not 50** — compute
  `PAL_CLOCK_HZ / cycles_per_frame` yourself and use the exact value
  everywhere pacing matters. Even that "50.125" rounding used elsewhere
  in these docs is itself an approximation — compute it, don't copy a
  rounded figure written down anywhere, this file included.
- **Synthetic key input needs to be held for a real minimum duration.**
  If you ever build a "type this program in" convenience feature, a
  keypress shorter than roughly one full jiffy-IRQ interval can land
  between two of the KERNAL's own interrupt-driven keyboard scans and
  be silently missed entirely — not garbled, just never seen. The real
  jiffy IRQ period is driven by CIA1 Timer A's reload value, on the
  order of ~16,400 PHI2 cycles (confirm the exact reload value the real
  KERNAL programs once ROMs are staged, rather than trusting this
  approximate figure) — hold synthetic keys for comfortably longer than
  that, not for some arbitrarily-chosen short duration that merely
  looks plausible.
- **A "verification" that calls a chip module's internals directly
  (rather than going through the real KERNAL routine) can structurally
  miss real bugs.** This is called out in `docs/testing-strategy.md`
  but worth restating with the sharpest version of the lesson: a keyboard
  test that asks the `Cia`/`KeyboardMatrix` object for a character
  directly bypasses the actual interrupt/debounce timing path
  entirely, so it cannot catch a bug that only manifests through *that*
  path (like the debounce-duration issue above). Prefer testing through
  the real KERNAL call, even when a direct unit test would be easier to
  write.
- **A "weird"-sounding audio result isn't automatically a bug.** If SID
  audio sounds choppy/staccato, check whether it's actually correct
  chip behavior given the specific register values the test program
  used (e.g. a short release time combined with a delay loop between
  notes that's shorter than the release actually needs) before assuming
  the emulator itself is wrong. Verify by rendering a reference WAV
  directly from the SID module with no audio-pipeline involvement at
  all and inspecting the raw envelope/waveform — if the "weird" sound is
  present there too, it's the program's own real behavior, not a
  pipeline bug.
- **Audio pacing: prefer a pull model, and watch for a "single next-slot"
  race if you ever build anything push-based.** SDL2's callback API
  already sidesteps this by construction (see `docs/peripherals.md`),
  but if you ever end up with any queue/double-buffer style audio
  hand-off, verify there's an actual check for "is the next slot free"
  before handing off a new chunk — a naive version that always accepts
  the newest chunk can silently overwrite/drop one that hadn't been
  consumed yet, especially once real screen rendering is also
  competing for the same frame's time budget.
- **Rendering every frame at full detail can compete with audio pacing
  for real CPU time once both are running together** — this only shows
  up once you measure the *combined* per-frame cost (CPU step + video
  render + draw), not each piece in isolation. If your measured combined
  cost exceeds the real frame budget (≈19.95ms at the real PAL rate),
  throttling actual screen draws to 1-in-N frames while still stepping
  CPU/VIC-II/audio every frame is a legitimate, disclosed trade-off —
  see `docs/machine.md`. Don't assume you have headroom; measure it.
- **If you ever write your own 6502 assembler for hand-built test
  programs** (not required — `docs/roadmap.md` recommends an external
  assembler like `ca65`/`vasm` instead): a table built by inverting the
  CPU's own opcode table, keyed on `(mnemonic, addressing mode)`, breaks
  the moment you add undocumented/"illegal" opcodes in Phase 10 — several
  of them share a `(mnemonic, mode)` pair with each other (e.g. multiple
  distinct illegal NOP-implementing opcodes), so the pair is no longer a
  unique key once those exist. Only matters if you build an assembler at
  all; irrelevant if you stick with an external one throughout.
- **`JMP ($xxFF)` and the "B flag is not a real stored flag" point** in
  `docs/6502-reference.md` are both easy to silently "fix" because they
  look like bugs in a fresh implementation — don't. They're real,
  documented NMOS behavior.

## What's still genuinely open / not resolved by any doc here

- The exact keyboard matrix-to-key mapping (see `docs/cia.md`) — at
  least two commonly-cited community layouts disagree on some
  positions, and neither has been verified against real hardware by
  this project. Budget real time for the empirical verification method
  described in `docs/cia.md`, don't just pick whichever layout you find
  first.
- Whether per-cycle or per-scanline VIC-II granularity is the right
  starting point (see `docs/vic-ii.md`) is a real, undecided trade-off —
  make the call explicitly and document which you chose rather than
  drifting into one without deciding.
- No specific commercial software has been chosen yet as a Phase 9b
  ("does true drive emulation actually work") test target — picking one
  early (ideally something small and well-understood using a simple,
  well-documented fastloader) will make that phase much easier to
  validate than trying to bring up the IEC bus protocol against
  something complex first.
