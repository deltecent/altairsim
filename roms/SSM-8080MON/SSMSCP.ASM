        .8080
;       SYSTEM CONFIGURATION PACKAGE


;       LOGICAL DEV1CE/DEVICE DRIVER TABLES
;
;       EACH 4 ENTRY TABLE LISTS THE ADDRESSES
;       OF THE DRIVER ROUTINES TO BE USED FOR
;       THE PHYSICAL DEVICES WHICH HAY ASSIGNED
;       TO THAT LOGICAL DEVICE.

IOTAB:

;       CONSOLE STATUS

;       RETURN WITH REGISTER A = 0 IF NO
;       CONSOLE CHARACTER AVAILABLE.

CSTAB:  DW      TTST        ;0
        DW      KYST        ;1
        DW      KYST        ;2
        DW      KYST        ;3

;       CONSOLE INPUT

;        RETURN CONSOLE INPUT CHARACTER
;       IN REGISTER A.

CITA8:  DW      TTI         ;0
        DW      KYBD        ;1
        DW      KYBD        ;2
        DW      KYBD        ;3

;        CONSOLE OUTPUT

;       OUTPUT BYTE IN REGLSTER C
;       TO CONSOLE OUTPUT DEV1CE.

COTAB:  DW      TTO         ;0
        DW      TTO         ;1
        DW      THRM        ;2
        DW      CRT         ;3

;       READER INPUT

;       RETURN READER INPUT BYTE IN
;       REGISTER A, CARRY OFF. SET
;       CARRY IF NO BYTE AVAILABLE.

RITAB:  DW      TTR         ;0
        DW      RDR         ;1
        DW      KYBD        ;2
        DW      0B8F0H      ;3 DISK READ

;       PUNCH OUTPUT

;       OUTPUT BYTE IN REGISTER C
;       TO PUNCH DEVICE.

POTAB:  DW      TTO         ;0
        DW      PUNCH       ;1
        DW      CRT         ;2
        DW      0B973H      ;3 DISK WRITE

;       LISTING OUTPUT

;       OUTPUT BYTE IN REGISTER C
;       TO LISTING DEVICE.

LOTAB:  DW      TTO         ;0
        DW      CRT         ;1
        DW      THRM        ;2
        DW      TTO         ;3

;       SPECIAL SUBROUTINE TO LOCATE MONITOR
;       SCRATCH RAM
;
;       THE ADDRESS OF THE TOP OF THE SCRATCH
;       RAM AREA USED BY THE MONITOR IS RETURNED
;       IN REGISTERS D,E.
;       NOTE: THIS SUBROUTINE IS NOT CALLED IN THE
;       USUAL WAY: INSTEAD, THE RETURN ADDRESS
;       IS PLACED IN REGISTERS, D,E AND THE
;       SUBROUTINE IS ENTERED BY A ;UMP INSTRUCTION.
;       RETURN IS DONE BY PLACING THE RETURN
;       ADDRESS IN H,L AND EXECUTING A PCHL INST.

ADSCS:  JMP     ADS2

;       SUBROUTINE TO LOCATE MONITOR SCRATCH
;       RAM
;
;       THE ADDRESS OF THE TOP OF THE 64 BYTES
;       OF RAM TO BE USED BY THE MONITOR FOR
;       SCRATCHPAD IS RETURNED IN REGISTERS
;       H,L.

ADSCR:  JMP     ADS1

;       SUBROUTINE TO SET ADDRESS
;       OF IOBYT
;
;       THE ADDRESS OF THE BYTE USED TO
;       RECORD THE CURRENT PHYSICAL/LOGICAL
;       DEVICE ASSIGNMENTS IS RETURNED IN
;       REGISTERS H,L.

ADIOB:  JMP     ADS1

;       SUBROUTINE TO SET THE USER STACK
;       ADDRESS.
;
;       THE ADDRESS TO BE USED AS THE
;       DEFAULT VALUE OF THE USER STACK
;       ADDRESS IS RETURNED IN REGISTERS H,L.

ADUST:  LXI     H,0100H
        RET

ADS2:   LXI     H,0
ADS3:   INR     H
        MOV     A,M
        CMA
        DI
        MOV     M,A
        CMP     M
        CMA
        EI
        MOV     M,A
        JZ      ADS3
        DCX     H
        XCHG
        PCHL

ADS1:   PUSH    D
        LXI     D,$+6
        JMP     ADS2
        XCHG
        POP     D
        RET

;       PHYSICAL DEVICE DRIVER ROUTINES

;       REQUIREMENTS
;               MAINTAIN CONTENTS OF ALL
;               REGISTEHS EXCEPT A AND F.
;               EXIT BY RETURN INST.

;        VIDEO DRIVER

CRT:    MOV     A,C
        ORA     A       ;CHECK FOR NULL
        RZ
        PUSH    H
        CALL    ADIOB
        DCX     H
        DCX     H
        DCX     H
        JMP     0F704H

;       KEY80ARD DRIVER

KYBD:   IN      2
        ANI     1
        JNZ     KYBD
        IN      3
        ANI     7FH
        CPI     61H     ;LOWER CASE A
        JC      KB1
        CPI     7AH+1   ;LOWER CASE Z +1
        JNC     KB1
        ANI     0DFH    ;DELETE ONE BIT
KB1:    ORA     A
        RET

;       KEYBOARD STATUS DRIVER

KYST:   IN      2
        ANI     1
        SUI     1
        SBB     A
        RET

;       READER DRIVER

RDR:    PUSH    H
        LXI     H,0
RD:     IN      4
        ANI     1
        JZ      RD2
        DCX     H
        MOV     A,H
        ORA     L
        JNZ     RD
        STC
        POP     H
        RET
RD2:    IN      5
        ORA     A
        POP     H
        RET

;       TELETYPE STATUS DRIVER

TTST:   IN      0
        ANI     1
        SUI     1
        SBB     A
        RET

;       TELETYPE INPUT DRIVER

TTI:    XRA     A
        OUT     0
TTI1:   IN      0
        ANI     1
        JNZ     TTI1
        IN      1
        ANI     7FH
        RET

;       TELETYPE OUTPUT DRIVER

TTO:    IN      0
        ANI     080H
        JNZ     TTO
        MOV     A,C
        OUT     1
        RET

;       TELETYPE READER DRIVER

TTR:    MVI     A,1
        OUT     0
        MVI     A,0
        OUT     0
TTR1:   IN      0
        ANI     1
        JNZ     TTR1
        IN      1
        RET

;       THERMAL PRINTER DRIVER

THRM:   IN      2
        ANI      80H
        JNZ     THRM
        MOV     A,C
        OUT     3
        RET

;       PUNCH DRIVER

PUNCH:  IN      4
        ANI     80H
        JNZ     PUNCH
        MOV     A,C
        OUT     5
        RET

END
