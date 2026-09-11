/* Sourced from https://www.pepto.de/projects/colorvic/2001/ (read as
 * raw page text, not an AI summary -- see docs/sources.md), a widely-
 * cited derivation of the VIC-II's analog composite output. There is
 * no canonical RGB palette for this chip -- see palette.h. */

#include "palette.h"

const uint8_t VIC_PALETTE_RGB[16][3] = {
    {0x00, 0x00, 0x00}, /* 0  black */
    {0xFF, 0xFF, 0xFF}, /* 1  white */
    {0x68, 0x37, 0x2B}, /* 2  red */
    {0x70, 0xA4, 0xB2}, /* 3  cyan */
    {0x6F, 0x3D, 0x86}, /* 4  pink/purple */
    {0x58, 0x8D, 0x43}, /* 5  green */
    {0x35, 0x28, 0x79}, /* 6  blue */
    {0xB8, 0xC7, 0x6F}, /* 7  yellow */
    {0x6F, 0x4F, 0x25}, /* 8  orange */
    {0x43, 0x39, 0x00}, /* 9  brown */
    {0x9A, 0x67, 0x59}, /* 10 light red */
    {0x44, 0x44, 0x44}, /* 11 dark grey */
    {0x6C, 0x6C, 0x6C}, /* 12 medium grey */
    {0x9A, 0xD2, 0x84}, /* 13 light green */
    {0x6C, 0x5E, 0xB5}, /* 14 light blue */
    {0x95, 0x95, 0x95}, /* 15 light grey */
};
