; A SIMPLE ROUTINE FOR CHECKING IF THE EPROM IS ERASED.
; PRINTS P = PASS, F = FAIL.
; COPYRIGHT BY SOLID STATE MUSIC, 1978.
; SSM PB1 manual section 4.4 -- transcribed verbatim.
;
; REGISTERS USED:
;   REG.A .... test and pass a character
;   REG.B .... not used
;   REG.C .... size
;   REG.DE ... PROM card address
;   REG.HL ... not used
;
; CO (F009H) and MONIT (F021H) are the SSM 8080 monitor's console-output and
; entry addresses (manual section 3.3). BE SURE TO CHANGE THESE TO MEET YOUR
; NEEDS. altairsim does not ship the SSM monitor yet, so the P/F character is
; not printed until it is added -- see the README.
;
LOC     EQU     140H
PROM    EQU     0D000H          ; programming socket
MONIT   EQU     0F021H          ; exit to user
CO      EQU     0F009H          ; console out routine

        ORG     LOC
; CHECKING EPROM START
ERASE:  MVI     C,03            ; 03 = 2708, 07 = 2716
        LXI     D,PROM          ; EPROM address
ER1:    LDAX    D               ; read EPROM
        INX     D
        CPI     0FFH            ; test for erase
        MVI     A,'F'           ; fail character
        JNZ     ERROR
        MOV     A,D
        ANA     C
        ORA     E               ; done yet?
        JNZ     ER1
        MVI     A,'P'           ; pass character
; OUTPUT ROUTINE CALLS USER MONITOR ENTRY
; POINT FOR CONSOLE OUTPUT.  DATA IS IN REG.C.
; BE SURE TO CHANGE THIS TO MEET YOUR NEEDS.
ERROR:  MOV     C,A
        CALL    CO
        JMP     MONIT
        END
