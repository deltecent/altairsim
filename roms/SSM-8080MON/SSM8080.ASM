        .8080
;OCR'ED BY B BEECH APR 2014
;       8080 MONITOR VL0
        ;PROGRAMMER: C. E. OHME
;       (415)657-8326


;
;       SYSTEM CONFIGURATION INTERFACE

SCP     EQU     0F600H
IOTAB   EQU     SCP
ADSCS   EQU     SCP+48
ADSCR   EQU     SCP+51
ADIOB   EQU     SCP+54
ADUST   EQU     SCP+57

; ASCII CHARACTERS

CR      EQU     0DH
LF      EQU     0AH


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
VERS:   DB      CR,LF,'MONITOR V1.','0' OR 80H
ERM:    DB      LF,'*' OR 80H

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

;       HEXADECIMAL COMMAND

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

LOAD:   CALL    EXPR1
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

END
