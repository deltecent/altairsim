; A SIMPLE ROUTINE FOR PROGRAMMING 2708's.
; COPYRIGHT BY SOLID STATE MUSIC, 1978.
; SSM PB1 manual section 4.2 -- transcribed verbatim.
;
; REGISTER USAGE:
;   REG.A .... pass data from memory to the programmer
;   REG.B .... repeated programming cycles
;   REG.C .... size.  size = 256*(REG.C+1)
;   REG.DE ... PROM card address (the programming socket)
;   REG.HL ... data address to be copied
;
; The final JMP goes to the SSM 8080 monitor at F021H (manual section 3.3). To
; adapt it to your own monitor, patch the low byte at 011F and the high byte at
; 0120. altairsim does not ship the SSM monitor yet; see the README -- run this
; with BREAK F021 set, then SAVE the burned socket to a hex file.
;
LOC     EQU     100H
PROM    EQU     0D000H          ; programming socket window
RAM     EQU     4000H           ; data to be copied
CPORT   EQU     10H             ; control port for PB1
MONIT   EQU     0F021H          ; exit addr. -- set by user

        ORG     LOC
; START OF PROGRAM
;   INITIALIZE BOARD
PROG0:  MVI     A,01            ; 01 = 2708 mode
        OUT     CPORT           ; preset board (arm; latch type; LED on)
; SET UP PARAMETERS
;   number of programming cycles
        MVI     B,0FFH          ; 256 cycles for 2708
;   number of bytes = 256*(C+1)
        MVI     C,03            ; 03 = 2708 (1024 bytes)
; SET UP ADDRESSES FOR TRANSFER
PROG1:  LXI     D,PROM
        LXI     H,RAM
; PROGRAM THE EPROM
PROG2:  MOV     A,M
        STAX    D
        INX     D
        INX     H
        MOV     A,D
        ANA     C
        ORA     E
        JNZ     PROG2
        DCR     B
        JNZ     PROG1
        DCX     D
        LDAX    D               ; RESET PB1
        JMP     MONIT           ; BACK TO MONITOR
        END
