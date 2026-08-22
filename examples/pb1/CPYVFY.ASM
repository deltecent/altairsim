; A SIMPLE ROUTINE FOR VERIFYING THE ORIGINAL SOURCE DATA IN MEMORY
; AGAINST THE EPROM COPY.  PRINTS P = PASS, F = FAIL.
; COPYRIGHT BY SOLID STATE MUSIC, 1978.
; SSM PB1 manual section 4.5 -- transcribed verbatim.
;
; REGISTER USAGE:
;   REG.A .... test and pass characters
;   REG.B .... original data
;   REG.C .... size.  size = 256*(REG.C+1)
;   REG.DE ... PROM card address
;   REG.HL ... address of original
;
; CO (F009H) and MONIT (F021H) are the SSM 8080 monitor's console-output and
; entry addresses (manual section 3.3). BE SURE TO CHANGE THESE TO MEET YOUR
; NEEDS. altairsim does not ship the SSM monitor yet, so the P/F character is
; not printed until it is added -- see the README.
;
LOC     EQU     180H
PROM    EQU     0D000H          ; programming socket
RAM     EQU     4000H           ; original data addr.
MONIT   EQU     0F021H          ; exit to user
CO      EQU     0F009H          ; console output

        ORG     LOC
; START OF VERIFY
VERF:   MVI     C,03            ; 03 = 2708, 07 = 2716
        LXI     D,PROM          ; socket addr. of copy
        LXI     H,RAM           ; point to RAM master
VERF1:  MOV     B,M             ; get original data
        LDAX    D               ; get ROM copy
        INX     D
        INX     H
        CMP     B               ; are they alike?
        MVI     A,'F'
        JNZ     ERROR
        MOV     A,D
        ANA     C
        ORA     E
        JNZ     VERF1           ; go back for next byte
        MVI     A,'P'
; OUTPUT ERROR CHARACTER
ERROR:  MOV     C,A
        CALL    CO              ; output it
        JMP     MONIT
        END
