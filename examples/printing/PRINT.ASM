; PRINT.ASM -- send a banner to the 88-C700 line printer and halt.
;
; The C700 lives at port 02: status/control at 02, data at 03 (printer.toml).
; The status byte's bit 0 (ACK) is SET when the card will accept a byte, so we poll
; it before every character -- exactly what a real printer driver does, and what
; keeps the last byte from being dropped if the far end is momentarily busy.
;
; The message ends with CR, LF, and a FORM FEED (0Ch). The form feed matters: a
; real printer holds the current page until something tells it the page is done, so
; without it a laser printer would keep the sheet and print nothing. 00h ends the
; string.
;
; Assembles at 0100h. There is no assembler in the tree; PRINT.HEX beside this file
; is the assembled image, and this listing is here to be read.

        ORG  0100H

C700S   EQU  02H            ; C700 status/control port
C700D   EQU  03H            ; C700 data port
ACK     EQU  01H            ; status bit 0: card will accept a byte

START:  LXI  H, MSG         ; HL -> the message
LOOP:   MOV  A, M           ; next character
        ORA  A              ; is it the 00h terminator?
        JZ   DONE           ; yes -> stop
        MOV  B, A           ; hold the character
WAIT:   IN   C700S          ; read the printer status
        ANI  ACK            ; ready to take a byte?
        JZ   WAIT           ; no -> spin
        MOV  A, B           ; recover the character
        OUT  C700D          ; ...and send it to the printer
        INX  H              ; next character
        JMP  LOOP
DONE:   HLT

MSG:    DB   'ALTAIRSIM 8800', 0DH, 0AH, 0CH, 00H
