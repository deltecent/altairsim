; VDM1DEMO -- write a banner into a Processor Technology VDM-1 and stop.
;
; The smallest thing that proves the VDM-1 is in the machine and working: it
; copies a string into the card's memory-mapped screen RAM at 0CC00H (row 0,
; column 0) and halts. The characters appear on the VDM-1's display -- there is no
; console I/O and no monitor involved, which is the whole point of a memory-mapped
; video terminal: you make text appear by WRITING MEMORY.
;
; Loaded at 0F800H by machines/vdm1.toml (mount = "builtin:vdm1demo") and run at
; boot (startup = ["RUN F800"]). Assembles to roms/VDM1DEMO/VDM1DEMO.HEX.
;
; See reference/Processor Technology VDM-1.md and docs/boards/proctech-vdm1.md.

VRAM    EQU     0CC00H          ; VDM-1 screen RAM: 16 rows x 64 cols, row 0 at top

        ORG     0F800H

START:  LXI     H,VRAM          ; HL -> top-left of the screen
        LXI     D,MSG           ; DE -> the message
MLOOP:  LDAX    D               ; A = *DE
        ORA     A               ; NUL terminator?
        JZ      DONE            ; yes -- we are finished
        MOV     M,A             ; *HL = A   (a byte written into screen RAM shows)
        INX     H               ; next screen cell
        INX     D               ; next source byte
        JMP     MLOOP
DONE:   HLT                     ; stop; the banner stays on the display

MSG:    DB      'PROCESSOR TECHNOLOGY VDM-1  READY'
        DB      0

        END
