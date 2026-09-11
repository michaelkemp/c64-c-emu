#ifndef C64EMU_C64_PALETTE_H
#define C64EMU_C64_PALETTE_H

#include <stdint.h>

/* The VIC-II's 16-color palette, as RGB8 triples. See
 * src/c64/palette.c and docs/sources.md: the real chip generates an
 * analog composite color signal, not RGB, so there is genuinely no
 * single canonical RGB value per color (confirmed directly from
 * Christian Bauer's article, section 3.3, which names the 16 colors
 * but explicitly does not give RGB numbers) -- this is a commonly-
 * cited community approximation (Philip "Pepto" Timmermann's palette),
 * not a primary-sourced fact, and is disclosed as such. */
extern const uint8_t VIC_PALETTE_RGB[16][3];

#endif /* C64EMU_C64_PALETTE_H */
