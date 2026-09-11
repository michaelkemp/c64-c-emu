; Minimal, self-contained 6502 demo -- no KERNAL/BASIC ROM needed.
; Sets up standard character-mode text directly (custom font in RAM,
; since $2000 is outside both hardwired Character-ROM windows), writes
; "HELLO C64" to the screen, and sets border/background colors, then
; loops forever. Used by tools/demos/framebuffer_dump.c as an early,
; pre-Phase-6 smoke test of the VIC-II renderer -- see that file and
; CLAUDE.md's status section.
;
; Assembled with: ca65 hello_c64.s -o hello_c64.o
;                 ld65 -C hello_c64.cfg hello_c64.o -o hello_c64.bin

.segment "CODE"

start:
        ; ---- copy the 7-glyph font (56 bytes) to $2008 (code 0 stays
        ; the RAM's own zeroed default, used here as blank/space) ----
        ldx #0
copy_font:
        lda font_data,x
        sta $2008,x
        inx
        cpx #56
        bne copy_font

        ; ---- write the 9 screen codes for "HELLO C64" at row 12, col 15 ----
        ldx #0
copy_screen:
        lda screen_data,x
        sta $05EF,x             ; $0400 (screen base, VM=1) + 12*40+15
        inx
        cpx #9
        bne copy_screen

        ; ---- color RAM: white text at the same 9 positions ----
        ldx #0
copy_color:
        lda color_data,x
        sta $D9EF,x             ; $D800 (Color RAM) + 495
        inx
        cpx #9
        bne copy_color

        ; ---- VIC-II setup ----
        lda #$18                ; $D018: VM10-13=1 ($0400), CB11-13=4 ($2000)
        sta $D018
        lda #$1B                ; $D011: DEN|RSEL|YSCROLL=3 (25-line alignment)
        sta $D011
        lda #$08                ; $D016: CSEL (40 columns), XSCROLL=0
        sta $D016
        lda #14                 ; $D020: border = light blue
        sta $D020
        lda #6                  ; $D021: background = blue
        sta $D021

        ; ---- a small proof of real per-scanline timing: every single
        ; frame, busy-wait for three specific raster lines and change
        ; the border color at each, without any interrupt delivery
        ; (Phase 6 doesn't exist yet) -- a frame-snapshot renderer
        ; could not show this as scanline-banded colors at all. ----
main_loop:
        lda #14                 ; light blue (top band)
        sta $D020
wait_line_100:
        lda $D012
        cmp #100
        bne wait_line_100
        lda #2                  ; red (middle band)
        sta $D020

wait_line_180:
        lda $D012
        cmp #180
        bne wait_line_180
        lda #5                  ; green (bottom band)
        sta $D020

wait_line_250:
        lda $D012
        cmp #250
        bne wait_line_250
        jmp main_loop

font_data:
        .byte $81,$81,$81,$FF,$81,$81,$81,$00   ; code 1: H
        .byte $FF,$80,$80,$FC,$80,$80,$FF,$00   ; code 2: E
        .byte $80,$80,$80,$80,$80,$80,$FF,$00   ; code 3: L
        .byte $7E,$81,$81,$81,$81,$81,$7E,$00   ; code 4: O
        .byte $7E,$81,$80,$80,$80,$81,$7E,$00   ; code 5: C
        .byte $38,$44,$80,$B8,$C4,$44,$38,$00   ; code 6: 6
        .byte $0C,$14,$24,$44,$FE,$04,$04,$00   ; code 7: 4

screen_data:
        .byte 1,2,3,3,4,0,5,6,7   ; H E L L O ' ' C 6 4

color_data:
        .byte 1,1,1,1,1,1,1,1,1   ; white
