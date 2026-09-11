#!/usr/bin/env bash
#
# Fetches Klaus Dormann's 6502 functional test suite on demand -- the
# community-standard correctness gate for a from-scratch 6502 core (see
# docs/6502-reference.md and docs/testing-strategy.md).
#
# This suite is GPLv3 and genuinely openly redistributable -- a
# different, clearer license situation than the Commodore ROMs (see
# CLAUDE.md's license discipline section) -- but it's still fetched on
# demand rather than vendored into this repo, matching this project's
# general "don't check in things you didn't author" convention.
#
# Destination: gitignored tests/vendor/6502_functional_tests/
#
# After fetching, consult that directory's OWN README/build instructions
# to assemble the test program (it needs an external 6502 assembler,
# e.g. ca65 from the cc65 suite, or the as65 assembler some forks of
# this suite bundle -- don't assume one specific toolchain without
# checking what the fetched copy actually expects, since this can
# differ between forks/versions). Verify the documented success trap
# address from the fetched copy's own source/comments rather than
# trusting a number written down elsewhere -- it can differ by build
# option.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST_DIR="$ROOT_DIR/tests/vendor/6502_functional_tests"
REPO_URL="https://github.com/Klaus2m5/6502_65C02_functional_tests.git"

if [[ -d "$DEST_DIR/.git" ]]; then
    echo "Already fetched at $DEST_DIR -- pulling latest instead of re-cloning."
    git -C "$DEST_DIR" pull --ff-only
else
    mkdir -p "$(dirname "$DEST_DIR")"
    git clone --depth 1 "$REPO_URL" "$DEST_DIR"
fi

echo
echo "Fetched into $DEST_DIR (gitignored, never committed)."
echo "Next: read that directory's own README/build instructions to assemble"
echo "6502_functional_test.a65 with an external assembler, and confirm the"
echo "documented success trap address from its own source before wiring up"
echo "the test harness in docs/6502-reference.md's Phase 1 checklist."
