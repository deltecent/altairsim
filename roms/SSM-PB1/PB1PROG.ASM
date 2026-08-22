; ============================================================================
; PB1PROG -- SSM PB1 2708 EPROM programmer, ready to run under altairsim.
;
; This is the "2708 Programming Software" from the SSM PB1 instruction manual
; (section 4.2, copyright Solid State Music 1978), transcribed byte-for-byte
; from the manual's object listing -- with ONE change: the manual ends with
;   JMP MONIT   (a jump to the SSM 8080 monitor at F021H, "set by user")
; and here that exit is a HLT, so the routine hands control back to altairsim
; when the burn is done. The manual documents this exit as user-customizable.
;
; It burns the erased 2708 in socket U22: arm the board for 2708 timing, then
; copy 1K (1024 bytes) from RAM at 4000H into the programming window at D000H,
; and finally read the socket once to reset the board (LED off). A 1K test
; pattern (byte = low byte of its address) is assembled in at 4000H so the
; whole demo is self-contained:
;
;     altairsim pb1
;     LOAD roms/SSM-PB1/PB1PROG.HEX
;     G 100
;     SAVE eprom.hex D000 D3FF     ; the burned 2708, as an Intel HEX file
;
; To burn a 2716 instead, use MVI A,02 / MVI B,01 / MVI C,07 (section 4.3 of
; the manual) and a 2K source -- the socket window then maps to U23.
;
; See reference/SSM PB1 EPROM Programmer.md and docs/boards/ssm-pb1.md.
; ============================================================================

PROM    EQU     0D000H          ; the PB1 programming-socket window
RAM     EQU     4000H           ; the data to be copied into the EPROM
CPORT   EQU     10H             ; the PB1 control port

        ORG     0100H
; --- initialize the board ---
PROG0:  MVI     A,01H           ; 01 = 2708 mode (D0 = 1)
        OUT     CPORT           ; arm the board and latch the type (LED on)
; --- set up the parameters ---
        MVI     B,0FFH          ; 256 programming passes for the 2708
        MVI     C,03H           ; size: 256 * (03 + 1) = 1024 bytes
; --- copy RAM -> EPROM ---
PROG1:  LXI     D,PROM          ; DE -> the programming socket
        LXI     H,RAM           ; HL -> the source data
PROG2:  MOV     A,M             ; fetch a source byte
        STAX    D               ; write it into the socket -> one programming pulse
        INX     D
        INX     H
        MOV     A,D             ; done with this pass?  (D & C) == 0 and E == 0
        ANA     C
        ORA     E
        JNZ     PROG2
        DCR     B               ; repeat the whole 1K, B times
        JNZ     PROG1
        DCX     D
        LDAX    D               ; read the socket once -> RESET the board (LED off)
        HLT                     ; manual: JMP MONIT (F021H) -- exit to the monitor

; ----------------------------------------------------------------------------
; The 1K source pattern at 4000H: byte N = N AND 0FFH.  (Emitted as a data
; block by the .HEX; shown here as a directive for the reader.)
; ----------------------------------------------------------------------------
        ORG     4000H
        ; DB 00,01,02, ... ,0FF  repeated four times  (1024 bytes)

        END     PROG0
