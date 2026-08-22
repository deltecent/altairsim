        .8080
;OCR'ed by B Beech Apr 2014
;       8080 MONITOR VL0
        ;PROGRAMMER: C. E. OHME
;       (415)657-8326


;
;       SYSTEM CONFIGURATION INTERFACE

SCP     EQU     0F600H
IOTAS   EQU     SCP
ADSCS   EQU     SCP+48
ADSCR   EQU     SCP+51
ADIOB   EQU     SCP+54
ADUST   EQU     SCP+57

; ASCII CHARACTERS

CR      EQU     0DH
LF      EQU     0AH

        ORG     0F000H

; EXTERNALLY REFERENCED SUBROUTINE
; JUMP TABLE

        JMP     BEGIN
        JMP     CI
        JMP     RI
        JMP     CO
        JMP     PO
        JMP     LO
        JMP     CSTS
        JMP     IOCHK
        JMP     IOSET
        JMP     MEMCK
        JMP     STRNG
REENT:  MVI     C,0
        LXI     H,0
        ORG     $-2

BEGIN:  MVI     C,1
        LXI     D,$+6
        JMP     ADSCS
        XCHG
        MVI     B,ENDX-EXIT-1
        LXI     D,ENDX-1
BG1:    DCX     D
        LDAX    D
        DCX     H
        MOV     M,A
        DCR     B
        JNZ     BG1
        SPHL
        CALL    ADUST
        PUSH    H
        MVI     H,0
        PUSH    H
        PUSH    H
        PUSH    H
        MOV     A,C
        ORA     A
        JZ      BG2
        CALL    ADIOB
        MVI     M,0
BG2:    LXI     H,VERS
        CALL    STRNG
        ORA     A

; COMMAND RETURN POINT

CMNDR:  JNC     START

; ERROR RETURN

LER:    CALL    ADSCR
        LXI     D,EXIT-ENDX-7
        DAD     D
        SPHL
        LXI     H,ERM
        CALL    STRNG

;       INPUT AND EXECUTE NEXT COMMAND

START:  EI
        CALL    CRLF
        MVI     C,'.'
        CALL    CO
        CALL    TI
        SUI     'A'
        JM      LER
        CPI     'X'-'A'+1
        JP      LER
        ADD     A
        LXI     H,CMNDR
        PUSH    H
        LXI     H,TBL
        MVI     D,0
        MOV     E,A
        DAD     D
        MOV     A,M
        INX     H
        MOV     H,M
        MOV     L,A
        MVI     C,2
        PCHL
;VERS:   DB      CR,LF,'MONITOR V1.','0' OR 80H
VERS:   DB      CR,LF,'MONITOR V1.','0' | 80H
;ERM:    DB      LF,'*' OR 80H
ERM:    DB      LF,'*' | 80H

;       COMMAND JUMP TABLE

TBL:    DW      ASSIGN
        DW      BIN
        DW      LER
        DW      DISP
        DW      LER
        DW      FILL
        DW      GOTO
        DW      HEXN
        DW      LER
        DW      LER
        DW      KOPY
        DW      LOAD
        DW      MOVE
        DW      NULL
        DW      LER
        DW      LER
        DW      LER
        DW      READ
        DW      SUBS
        DW      LER
        DW      LER
        DW      LER
        DW      WRITE
        DW      X

;    UTILITY SUBROUTINES

BLK:    MVI     C,' '

CO:     CALL    IOBR
        DB      1,10H

CI:     CALL    IOBR
        DB      1,8

CSTS:   CALL    IOBR
        DB      1,0

RI:     CALL    IOBR
        DB      4,18H

PO:     CALL    IOBR
        DB      3,20H

LO:     CALL    IOBR
        DB      2,28H

IOBR:   XTHL
        PUSH    B
        MOV     B,M
        INX     H
        MOV     C,M
        CALL    ADIOB
        MOV     A,M
        RRC
IOB1:   RLC
        RLC
        DCR     B
        JNZ     IOB1
        ANI     6
        ADD     C
        MOV     C,A
        LXI     H,IOTAB
        DAD     B
        MOV     A,M
        INX     H
        MOV     H,M
        MOV     L,A
        POP     B
        XTHL
        RET
IOCHK:
        PUSH    H
        CALL    ADIOB
        MOV     A,M
        POP     H
        RET
IOSET:
        PUSH    H
        PUSH    PSW
        CALL    ADIOB
        MOV     M,C
        POP     PSW
        POP     H
        RET
STRNG:  
        MOV     A,M
        ANI     7FH
        RZ
        MOV     C,A
        MOV     A,M
        ORA     A
        JM      CO
        CALL    CO
        INX     H
        JMP     STRNG
TI:
        CALL    CI
        ANI     7FH
        PUSH    B
        MOV     C,A
        CALL    CO
        MOV     A,C
        POP     B
        RET
CONV:
        ANI     0FH
        ADI     90H
        DAA
        ACI     40H
        DAA
        MOV     C,A
        RET
CRLF:
        MVI     C,CR
        CALL    CO
        MVI     C,LF
        JMP     CO
EXPR1:
        MVI     C,1
EXPR:
        LXI     H,0000H
EX0:    CALL    TI
EX1:    MOV     B,A
        CALL    NIBBL
        JC      EX2
        DAD     H
        DAD     H
        DAD     H
        DAD     H 
        ORA     L
        MOV     L,A
        JMP     EX0
EX2:    XTHL
        PUSH    H
        MOV     A,B
        CALL    P2C
        JNC     EX3
        DCR     C
        JNZ     LER
        RET
EX3:    JNZ     LER
        DCR     C
        JNZ     EXPR
        RET
EXF:
        MVI     C,1
        LXI     H,0000H
        JMP     EX1
HILO:
        INX     H
        MOV     A,H
        ORA     L
        STC
        RZ
        MOV     A,E
        SUB     L
        MOV     A,D
        SBB     H
        RET
LADR:
        MOV     A,H
        CALL    LBYTE
        MOV     A,L
LBYTE:
        PUSH    PSW
        RRC
        RRC
        RRC
        RRC
        CALL    HXD
        POP     PSW
HXD:
        CALL    CONV
        JMP     CO
LEADS:
        MVI     B,4
LEAD:
        MVI     C,0
        CALL    PO
        DCR     B
        JNZ     LEAD
        ORA     A
        RET
MEMCK:
        PUSH    H
        PUSH    D
        CALL    ADSCR
        XCHG
        LXI     H,0
MEM0:   INR     H
        MOV     A,M
        CMA
        MOV     M,A
        CMP     M
        CMA
        MOV     M,A
        JZ      MEM0
        DCX     H
        MOV     B,H
        MOV     A,H
        CMP     D
        MVI     A,0C0H
        POP     D
        POP     H
        RZ
        MVI     A,0FFH
        RET
NIBBL:
        SUI     '0'
        RC
        ADI     '0'-'G'
        RC
        ADI     6
        JP      NI0
        ADI     7
        RC
NI0:    ADI     10
        ORA     A
        RET
PCHK:
        CALL    TI
P2C:
        CPI     ' '
        RZ
        CPI     ','
        RZ
        CPI     CR
        STC
        RZ
        CMC
        RET

; BREAKPOINT ENTRY POINT

RESTRT: PUSH    H
        PUSH    D
        PUSH    B
        PUSH    PSW
        CALL    ADSCR
        LXI     D,EXIT-ENDX+1
        DAD     D
        XCHG
        LXI     H,000AH
        DAD     SP
        MVI     B,4
        XCHG
RST0:   DCX     H
        MOV     M,D
        DCX     H
        MOV     M,E
        POP     D
        DCR     B
        JNZ     RST0
        POP     B
        DCX     B
        SPHL
        LXI     H,TLOC
        DAD     SP
        MOV     A,M
        SUB     C
        INX     H
        JNZ     RST1
        MOV     A,M
        SUB     B
        JZ      RST3
        INX     H
        INX     H
        MOV     A,M
        SUB     C
        JNZ     RST2
        INX     H
        MOV     A,M
        SUB     B
        JZ      RST3
RST2:   INX     B
RST3:   LXI     H,LLOC
        DAD     SP
        MOV     M,E
        INX     H
        MOV     M,D
RST1:   INX     H
        INX     H
        MOV     M,C 
        INX     H
        MOV     M,B
        PUSH    B
        LXI     H,ERM
        CALL    STRNG
        POP     H
        CALL    LADR
        LXI     H,TLOC
        DAD     SP
        MVI     D,2
RST4:   MOV     C,M
        MVI     M,0
        INX     H
        MOV     B,M
        MVI     M,0
        INX     H
        MOV     A,C
        ORA     B
        JZ      RST5
        MOV     A,M
        STAX    B
RST5:   INX     H
        DCR     D
        JNZ     RST4
        JMP     START

;       SCRATCHPAD TEMPLATE

EXIT:   POP     D
        POP     B
        POP     PSW
        POP     H
        SPHL
        EI
        LXI     H,0
HLX     EQU     $-2
        JMP     0
PCX     EQU     $-2
T1A:    DW      0       ;TRAP 1 ADDR
        DB      0       ;TRAP 1 INST
        DW      0       ;TRAP 2 ADDR
        DB      0       ;TRAP 1 INST
        DW      0       ;VIDEO PNTR
        DB      0       ;VIDEO HOLD
        DB      0       ;IOBYT
ENDX:
ALOC    EQU     5
BLOC    EQU     3
CLOC    EQU     2
DLOC    EQU     1
ELOC    EQU     0
FLOC    EQU     4
HLOC    EQU     HLX-EXIT+9
LLOC    EQU     HLX-EXIT+8
PLOC    EQU     PCX-EXIT+9
SLOC    EQU     7
TLOC    EQU     T1A-EXIT+8

;       COMMAND IMPLEMENTATION

;       ASSIGN COMMAND

ASSIGN: CALL    TI
        MVI     B,0
        CPI     'C'
        JZ      AS1
        INR     B
        CPI     'R'
        JZ      AS1
        INR     B
        CPI     'P'
        JZ      AS1
        INR     B
        CPI     'L'
        JNZ     EREXT
AS1:    CALL    TI
        CPI     '='
        JNZ     AS1
        CALL    TI
        SUI     '0'
        MOV     L,A
        JM      EREXT
        CPI     4
        JP      EREXT
        MVI     H,3
AS2:    DCR     B
        JM      AS3
        DAD     H
        DAD     H
        JMP     AS2
AS3:    XCHG
        CALL    ADIOB
        MOV     A,M
        ORA     D
        XRA     D
        ORA     E
        MOV     M,A
        RET
EREXT:  STC
        RET

;       BINARY COMMAND

BIN:    CALL    EXPR
        CALL    CRLF
        POP     D
        POP     H
BIN0:   MOV     A,D
        ORA     E
        JNZ     B0
        CALL    LEADS
        MVI     C,78H
        CALL    PHL
        ORA     A
        RET
B0:     MOV     A,E
        SUB     L
        MOV     A,D
        SBB     H
        RC
B1:     MOV     A,E
        SUB     L
        MOV     C,A
        MOV     A,D
        SBB     H
        CMC
        RNC
        INR     C
        JNZ     B2
        MVI     C,0FFH
B2:     PUSH    D
        MOV     E,C
        CALL    LEADS
        MVI     C,3CH
        CALL    PO
        MOV     C,E
        CALL    PHL
        MOV     A,H
        ADD     L
        MOV     D,A
B3:     MOV     C,M
        INX     H
        MOV     A,D
        ADD     C
        MOV     D,A
        CALL    PO
        DCR     E
        JNZ     B3
        MOV     C,D
        CALL    PO
        POP     D
        MOV     A,L
        ORA     H
        RZ
        JMP     B1
PHL:    CALL    PO
        MOV     C,L
        CALL    PO
        MOV     C,H
        JMP     PO

;       DISPLAY COMMAND

DISP:   CALL    EXPR
        POP     D
        POP     H
DI0:    CALL    CRLF
        CALL    LADR
DI1:    CALL    BLK
        MOV     A,M
        CALL    LBYTE
        CALL    HILO
        CMC
        RNC
        MOV     A,L
        ANI     0FH
        JNZ     DI1
        JMP     DI0

;       FILL COMMAND

FILL:   INR     C
        CALL    EXPR
        POP     B
        POP     D
        POP     H
FI0:    MOV     M,C
        CALL    HILO
        JNC     FI0
        ORA     A
        RET

;       GOTO COMMAND

GOTO:   POP     H
        CALL     PCHK
        JC      GO3
        JZ      GO0
        CALL    EXF
        POP     D
        LXI     H,PLOC
        DAD     SP
        MOV     M,D
        DCX     H
        MOV     M,E
        MOV     A,B
        CPI     CR
        JZ      GO3
;        MVI     A,(JMP 0)
        MVI     A,0C3H
        STA     8
        LXI     H,RESTRT
        SHLD    9
GO0:    MVI     D,2
        LXI     H,TLOC
        DAD     SP
GO1:    PUSH    H
        CALL    EXPR1
        MOV     E,B
        POP     B
        POP     H
        MOV     A,B
        ORA     C
        JZ      GO2
        MOV     M,C
        INX     H
        MOV     M,B
        INX     H
        LDAX    B
        MOV     M,A
        INX     H
;        MVI     A,(RST 1)
        MVI     A,0CFH
        STAX    B
GO2:    MOV     A,E
        CPI     CR
        JZ      GO3
        DCR     D
        JNZ     GO1
GO3:    CALL    CRLF
        LXI     H,0008H
        DAD     SP
        PCHL

;       HEXAD£CIMAL COMMAND

HEXN:   CALL    EXPR
        POP     D
        POP     H
        CALL    CRLF
        PUSH    H
        DAD     D
        CALL    LADR
        CALL    BLK
        POP     H
        MOV     A,L
        SUB     E
        MOV     L,A
        MOV     A,H
        SBB     D
        MOV     H,A
        CALL    LADR
        ORA     A
        RET

;       COPY COMMAND

KOPY:   CALL    RI
        MOV     C,A
        CALL    PO
        JMP     KOPY
        
;       LOAD COMMAND

LOAD    CALL    EXPR1
        POP     B
L1:     CALL    RI
        RC
        CPI     3CH
        JZ      L2
        CPI     78H
        JNZ     L1
        CALL    LHL
        RC
        ORA     L
        RZ
        PCHL
L2:     CALL    RI
        RC
        MOV     E,A
        CALL    LHL
        RC
        ADD     L
        MOV     D,A
        DAD     B
L3:     CALL    RI
        RC
        MOV     M,A
        ADD     D
        MOV     D,A
        INX     H
        DCR     E
        JNZ     L3
        CALL    RI
        RC
        CMP     D
        JZ      L1
        STC
        RET
LHL:    CALL    RI
        RC
        MOV     L,A
        CALL    RI
        RC
        MOV     H,A
        RET

;       MOVE COMMAND

MOVE:   INR     C
        CALL    EXPR
        POP     B
        POP     D
        POP     H
MV0:    MOV     A,M
        STAX    B
        INX     B
        CALL    HILO
        JNC     MV0
        ORA     A
        RET

;       NULL COMMAND

NULL:   MVI     B,60
        JMP     LEAD

;       READ COMMAND

READ:   CALL    EXPR1
        POP     H
RED0:   CALL    RI
        RC
        ANI     7FH
        SUI     ':'
        JNZ     RED0
        MOV     D,A
        PUSH    H
        CALL    BYTE
        JZ      RED2
        MOV     E,A
        CALL     BYTE
        MOV     B,A
        CALL    BYTE
        MOV     C,A
        DAD     B
        CALL    BYTE
RED1:   CALL    BYTE
        MOV     M,A
        INX     H
        DCR     E
        JNZ     RED1
        CALL    BYTE
        POP     H
        JZ      RED0
        STC
        RET
RED2:   CALL    BYTE
        MOV     H,A
        CALL    BYTE
        POP     B
        MOV     L,A
        ORA     H
        RZ
        PCHL
BYTE:   CALL    RNBBL
        RLC
        RLC
        RLC
        RLC
        MOV     C,A
        CALL    RNBBL
        ORA     C
        MOV     C,A
        ADD     D
        MOV     D,A
        MOV     A,C
        RET
RNBBL:  CALL    RI
        JC      RNBER
        ANI     7FH
        CALL    NIBBL
        JC      RNBER
        RET
RNBER:  POP     H
        POP     H
        POP     H
        RET

;       SUBSTITUTE COMMAND

SUBS:   CALL    EXPR1
        CALL    P2C
        POP     H
        RC
SU0:    MOV     A,M
        CALL    LBYTE
        MVI     C,'-'
        CALL    CO
        CALL    PCHK
        CMC
        RNC
        JZ      SU1
        PUSH    H
        CALL    EXF
        POP     D
        POP     H
        MOV     M,E
        MOV     A,B
        CPI     CR
        RZ
SU1:    INX     H
        JMP     SU0

;       WRITE COMMAND

WRITE:  CALL    EXPR
        CALL    CRLF
        POP     D
        POP     H
WRIT0:  MOV     A,D
        ORA     E
        JNZ     W0
        CALL    PEOL
        MVI     C,':'
        CALL    PO
        XRA     A
        MOV     D,A
        CALL    PBYTE
        CALL    PADR
        MVI     A,1
        CALL    PBYTE
        XRA     A
        SUB     D
        CALL    PBYTE
        JMP     NULL
W0:     MOV     A,E
        SUB     L
        MOV     A,D
        SBB     H
        RC
WRI0:   MOV     A,E
        SUB     L
        MOV     C,A
        MOV     A,D
        SBB     H
        CMC
        RNC
        MOV     A,C
        ANI     0FH
        INR     A
        PUSH    D
        MOV     E,A
        MVI     D,0
        CALL    PEOL
        MVI     C,':'
        CALL    PO
        MOV     A,E
        CALL    PBYTE
        CALL    PADR
        XRA     A
        CALL    PBYTE
WRI3:   MOV     A,M
        INX     H
        CALL    PBYTE
        DCR     E
        JNZ     WRI3
        XRA     A
        SUB     D
        CALL    PBYTE
        POP     D
        MOV     A,L
        ORA     H
        RZ
        JMP     WRI0
PADR:   MOV     A,H
        CALL    PBYTE
        MOV     A,L
PBYTE:  PUSH    PSW
        RRC
        RRC
        RRC
        RRC
        CALL    CONV
        CALL    PO
        POP     PSW
        PUSH    PSW
        CALL    CONV
        CALL    PO
        POP     PSW
        ADD     D
        MOV     D,A
        RET
PEOL:   MVI     C,CR
        CALL    PO
        MVI     C,LF
        JMP     PO

;       REGISTER COMMAND

X:      CALL    TI
        LXI     H,ACTBL
        CPI     CR
        JZ      X6
        MOV     B,A
X0:     CMP     M
        JZ      X1
        MOV     A,M
        RAL
        RC
        INX     H
        INX     H
        INX     H
        MOV     A,B
        JMP     X0
X1:     CALL    BLK
X2:     INX     H
        MOV     A,M
        XCHG
        MOV     L,A
        MVI     H,0
        DAD     SP
        XCHG
        INX     H
        MOV     B,M
        INX     H
        LDAX    D
        CALL    LBYTE
        DCR     B
        JZ      X3
        DCX     D
        LDAX    D
        CALL    LBYTE
X3:     INR     B
        MVI     C,'-'
        CALL    CO
        CALL    PCHK
        CMC
        RNC
        JZ      X5
        PUSH    H
        PUSH    B
        CALL    EXF
        POP     H
        POP     PSW
        PUSH    B
        PUSH    PSW
        MOV     A,L
        STAX    D
        POP     B
        DCR     B
        JZ      X4
        INX     D
        MOV     A,H
        STAX    D
X4:     POP     B
        POP     H
X5:     MOV     A,M
        ORA     A
        RM
        MOV     A,B
        CPI     CR
        RZ
        JMP     X2
X6:     CALL    CRLF
X7:     CALL    BLK
        MOV     A,M
        INX     H
        ORA     A
        RM
        MOV     C,A
        CALL    CO
        MVI     C,'='
        CALL    CO
        MOV     A,M
        INX     H
        XCHG
        MOV     L,A
        MVI     H,0
        DAD     SP
        XCHG
        MOV     B,M
        INX     H
        LDAX    B
        CALL    LBYTE
        DCR     B
        JZ      X7
        DCX     D
        LDAX    D
        CALL    LBYTE
        JMP     X7
ACTBL:  DB      'A',    ALOC+2, 1
        DB      'B',    BLOC+2, 1
        DB      'C',    CLOC+2, 1
        DB      'D',    DLOC+2, 1
        DB      'E',    ELOC+2, 1
        DB      'F',    FLOC+2, 1
        DB      'H',    HLOC+2, 1
        DB      'L',    LLOC+2, 1
        DB      'M',    HLOC+2, 2
        DB      'P',    PLOC+2, 2
        DB      'S',    SLOC+2, 2
        DB      -1

;        END

;       SYSTEM CONFIGURATION PACKAGE

        ORG     0F600H

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

;       OUTPUT BYTE IN REGlSTER C
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

;        END

;       VIDEO BOARD DRIVER


;       THIS SUBROUTINE FACILITATES THE USE
;       OF THE SOLID STATE MUSIC VBl BOARD
;       AND A VIDEO DISPLAY DEVICE AS A
;       CONSOLE OUTPUT DEVICE.
;       ASCII CHARACTERS PRESENTED TO THE
;       SUBROUTINE IN THE C REGISTER ARE
;       DISPLAYED ON THE SCREEN. CERTAIN
;       CHARACTERS, LISTED BELOW, RECEIVE
;       SPECIAL TREATMENT. ALL REGISTERS
;       ARE PRESERVED BY THIS SUBROUTINE.

;       LOC IS THE BEGINNING ADDRESS OF THE
;       SUBROUTINE. IT MAY BE IN RAM OR ROM.

LOC     EQU     0F700H

;       VID IS THE BEGINNING ADDRESS ASSIGNED
;       TO THE DISPLAY RAM LOCATED ON THE VB1
;       BOARD.

VID     EQU     0EC00H

;       THREE BYTES OF RAM ARE REQUIRED FOR
;       HOUSEKEEPING. THESE BYTES MUST BE
;       IN AN AREA UNUSED BY OTHER PROGRAMS.

VDPTR   EQU     0BC44H  ;CURSOR POINTER
VDHLD   EQU     VDPTR+2 ;CHARACTER HOLD


;       NON-DISPLAYABLE CHARACTERS

FF      EQU     0CH     ;FORM FEED
                        ;CLEAR SCREEN, HOME CURSOR
LF      EQU     0AH     ;LINE FEED
                        ;DOWN ONE LINE, CLEEAR LINE
CR      EQU     0DH     ;CARRIAGE RETURN
                        ;MOVE CURSOR TO LEFT MARGIN

;       NORMAL ENTRY POINT

        ORG     LOC

VDTTY:  PUSH    H       ;SAVE HL
        LXI     H,VDPTR ;ADDR OF CURSOR POINTER

;       ALTERNATE ENTRY POINT

;       THIS ENTRY POINT t1AY BE USED IF
;       THE CURSOR POINTER AND CHARACTER
;       HOLD ARE AT LOCATIONS OTHER THAN
;       THOSE SPECIFIED ON THIS LISTING.
;       THE USER MUST SUPPLY SUBROUTINE
;       ENTRY CODE AS FOLLOWS.
;ENTR:  PUSH    H       ;SAVE HL
;       LXI     H,PNTR  ;AODR OF CURSOR POINTER
;       JMP     ALTVD   ;;OIN THIS CODE

ALTVD:  PUSH    D       ;SAVE DE
        PUSH    B       ;SAVE BC
        MOV     E,M     ;LPTR
        INX     H       ;
        MOV     A,M     ;HPTR
        ANI     3       ;CONVERT TO VIDEO
;        ADI     VID SHR 8 ;RAM ADDRESS
        ADI     VID >> 8 ;RAM ADDRESS
        MOV     D,A     ;
        INX     H       ;
        MOV     B,M     ;CHAR UNDER CURSOR
        XCHG            ;PNTR TO HL
        MOV     M,B     ;RESTORE PREV CHAR

;       IDENTIFY INPUT CHAR

        MOV     A,C     ;NEW CHAR
        CPI     FF      ;
        JZ      VIDFF   ;FORM FEED
        CPI     CR      ;
        JZ      VIDCR   ;CARRIAGE RETURN
        CPI     LF      ;
        JZ      VIDLF   ;LINE FEED

        MOV     M,C     ;
CRRT:   LXI     B,1

;       AD;UST CURSOR POINTER

CRAD:   DAD     B       ;

;       CHECK FOR OVERFLOW

        MOV     A,H     ;
;        CPI     (VID+1024) SHR 8 ;
        CPI     (VID+1024) >> 8 ;
        JNZ     VIDRT   ;
        LXI     H,VID+960 ;
        CALL    ROLL0   ;
        JMP     VIDR1   ;

;       COMMON EXIT CODE
;       NORMALIZE CURSOR POINTER

;VIDRT:  MVI     H,(VID+960) SHR 8 ;
VIDRT:  MVI     H,(VID+960) >> 8 ;
        MOV     A,L     ;
        ORI     0C0H    ;
        MOV     L,A     ;
VIDR1:  MOV     A,M     ;CHAR UNDER CURSOR
        MVI     M,7FH   ;CURSOR
        XCHG            ;PNTR TO DE
        MOV     M,A     ;CHAR UNDER CURSOR
        DCX     H       ;
        MOV     M,D     ;HPTR
        DCX     H       ;
        MOV     M,E     ;LPTR

;       RESTORE REG ISTERS, EXIT

        POP     B       ;
        POP     D       ;
        POP     H       ;
        MOV     A,C     ;
        ORA     A
        RET             ;

;       PROCESS FORM FEED
;       FILL SCREEN WITH SPACES ,
;       MOV£ CURSOR TO TOP LEFT

VIDFF:  LXI     H,VID   ;
        PUSH    H       ;
VIDFC:  MVI     M,' '   ;
        INX     H       ;
        MOV     A,H     ;
;        CPI     (VID+1024) SHR 8 ;
        CPI     (VID+1024) >> 8 ;
        JC      VIDFC   ; 
        POP     H       ;

;       PROCESS CARRIAGE RETURN
;       MOVE CURSOR TO BEGINNING
;       OF LINE

VIDCR:  MOV     A,L     ;
        ANI     0C0H    ;
        MOV     L,A     ;
        JMP     VIDRT   ;

;       PROCESS LINE FEED 
;       MOVE CURSOR DOWN ONE LINE,
;       FILL NEW LINE WITH SPACES

VIDLF:  PUSH    D       ;
        LXI     D,64    ;
        DAD     D       ;
        MOV     A,H     ;
;        CPI     (VID + 1024) SHR 8 ;
        CPI     (VID + 1024) >> 8 ;
        POP     D       ;
        JNZ     VIDRT   ;

;       THE FOLLOWING INSTRUCTION
;       (MARKED XXXX) MAY BE REMOVED
;       IF SENSE SWITCHES ARE NOT
;       TO BE USED.

;       WAIT UNTIL SENSE SWITCH 1 IS ON
;       BEFORE ROLLING UP ONE LINE.

VDLF2:  IN      0FFH    ;XXXX
        ANI     1       ;XXXX
        JZ      VDLF2   ;XXXX

;       ROLL THE WHOLE DISPLAY UP ONE
;       LINE.

        CALL    ROLL0   ;
        MOV     A,L     ;
        ORI     0C0H    ;
        MOV     L,A     ;
;        MVI     H,(VID+960) SHR 8 ;
        MVI     H,(VID+960) >> 8 ;
        JMP     VIDRT   ;
;
;       ROLL SUBROUTINE

ROLL0:  PUSH    D       ;
        PUSH    H       ;
        LXI     D,VID   ;
        LXI     H,VID+64 ;
ROLL1:  MOV     A,M     ;
        STAX    D
        MVI     M,20H   ;
        INX     D       ;
        INX     H       ;
        MOV     A,H     ;
;        CPI     (VID+1024) SHR 8 ;
        CPI     (VID+1024) >> 8 ;
        JNZ     ROLL1   ;
        POP     H       ;

ROLL2:  POP     D       ;
        RET             ;

;       GRAPHICS INTERFACE SUBROUTINES

;       THESE SUBROUTINES FACILITATE THE
;       USE OF THE SOLID STATE MUSIC VBI

;       BOARD AND A VIDEO DISPLAY DEVICE
;       AS A GRAPHICS DISPLAY DEVICE.

;       THESE SUBROUTINES TREAT THE DISPLAY
;       SCREEN AS A MATRIX OF DOTS,48 DOTS
;       HIGH BY 128 DOTS WIDE. EACH DOT IS
;       SPECIFIED IN TERMS OF ITS VERTICAL
;       COORDINATE(0-47) AND ITS HORIZONTAL
;       COORDINATE(0-127). DOT 0,0 IS AT
;       THE LOWER LEFT CORNER OF THE SCREEN.

;       THE SUBROUTINES HAVE SIMILIAR
;       INTERFACES WITH THEIR CALLING
;       PROGRAHS. REGISTER B IS PRESERVED.
;       ENTRY CONDITIONS:
;               H = VERTICAL COORDINATE
;               L = HORIZONTAL COORDINATE
;       EXIT CONDITIONS
;               A = DIFFERS BY SUBROUTINE
;               B = PRESERVED
;               C = BIT MASK FOR SPECIFIED DOT
;               DE= MEMORY ADDRESS OF DOT
;               H = VERTICAL COORDINATE
;               L = HORIZONTAL COORDINATE
;       H AND L ARE CONVERTED(IF NECESSARY)
;       MODULO 48 AND 128 RESPECTIVELY.

;       THE CHECK SUBROUTINE SETS THE ZERO
;       FLAG TO INDICATE WHETHER THE SPECIFIED
;       DOT IS WHITE OR BLACK. IF THE DOT
;       IS CURRENTLY WHITE THE ZERO FLAG IS
;       SET ON, IF THE DOT IS BLACK THE FLAG
;       IS SET OFF. THE A REGISTER CONTAINS
;       ZERO IF THE DOT IS WHITE, THE BIT
;       MASK IF IT IS BLACK.
        CALL    CNVRT   ;
        ANA     C       ;
        RET             ;

;       THE WHITE SUBROUTINE SETS THE
;       SPECIFIED DOT WHITE. REGISTER
;       A CONTAINS THE NEW CONTENTS OF
;       THE MEMORY LOCATION.
WHITE:  CALL    CNVRT   ;CONVERT
        ANI     0BFH    ;CLEAR UNUSED BIT
        ORI     80H     ;SET GRAPHICS BIT
        ORA     C       ;SET THIS DOT
        XRA     C       ;CLEAR THIS DOT
        STAX    D       ;UPDATE BYTE
        RET             ;

;       THE BLACK SUBROUTINE SETS THE
;       SPECIFIED DOT BLACK. REGISTER
;       A CONTAINS THE NEW CONTENTS OF
;       THE MEMORY LOCATION.

BLACK:  CALL    CNVRT   ;CONVERT
        ANI     0BFH    ;CLEAR UNUSED BIT
        ORI     80H     ;SET GRAPHICS BIT
        ORA     C       ;SET THIS DOT
        STAX    D       ;UPDATE BYTE
        RET             ;

;       THE CNVRT SUBROUTINE PERFORMS
;       THE COORDINATE TO ADDRESS - 
;       BIT MASK CONVERSION. REGISTER
;       A CONTAINS THE CURRENT CONTENTS
;       OF THE MEMORY LOCATION.

CNVRT:  PUSH    B       ;

;       NORMALIZE THE COORDINATES

        MOV     A,L     ;
        ANI     7FH     ;
        MOV     L,A     ;
        MOV     A,H     ;
D1:     SUI     48      ;
        JP      D1      ;
D2:     ADI     48      ;
        JM      D2      ;
        MOV     H,A     ;
        PUSH    H       ;

;       CONVERT COORDINATES TO ADDRESS
;       IN DE

        MOV     B,H     ;
        MOV     C,L     ;
        MOV     E,H     ;
        MVI     D,0     ;
        LXI     H,1     ;
        DAD     D       ;
        DAD     H       ;
        DAD     H       ;
        DAD     D       ;
        DAD     H       ;
        DAD     H       ;
        DAD     D       ;
        MOV     D,H     ;
        MOV     A,L     ;
        ANI     0C0H    ;

        MOV     E,A     ;
        DAD     D       ;
        DAD     D       ;
        DAD     H       ;
        DAD     H       ;
        MOV     A,B     ;
        SUB     H       ;
        MOV     B,A     ;
;        MVI     A,(VID+960) AND 0FFH
        MVI     A,(VID+960) & 0FFH
        SUB     E       ;
        MOV     E,A     ;
;        MVI     A,(VID+960) SHR 8 
        MVI     A,(VID+960) >> 8 
        SBB     D       ;
        MOV     D,A     ;
        MOV     A,C     ;
        RAR             ;
        ORA     E       ;
        MOV     E,A     ;

;       GENERATE BIT MASK

        MOV     A,C     ;
        RAR             ;
        MOV     A,B     ;
        RAL
        MOV     C,A     ;
        MVI     B,0     ;
        LXI     H,DTAB  ;
        DAD     B
        MOV     A,M

;       PREPARE FOR EXIT

        POP     H       ;
        POP     B       ;
        MOV     C,A     ;
        LDAX    D       ;
        RET

DTAB:   DB      04H
        DB      20H
        DB      02H
        DB      10H
        DB      01H
        DB      08H

        END
