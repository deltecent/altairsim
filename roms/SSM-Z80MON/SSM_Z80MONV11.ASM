        .Z80
;OCR'ed by B Beech Apr 2014
;*******SSM Z80 MONITOR V1.10********

;       PROGRAMMER:
;       C. E. OHME,1977

;       TECHNICAL SUPPORT:
;       P. DENNIS,4-21-80

;       MODIFIED BY:
;       M. T. WRIGHT,4-27-80
;       D. M. FISCHLER, 9-15-80

;       COPYRIGHT 19S0 BY SSM MICROCOMPUTER PRODUCTS
;       ALL RIGHTS RESERVED

TRUE    EQU     0FFFFH
;FALSE   EQU     NOT TRUE
FALSE   EQU     ~TRUE

;       STARTING ADDRESS OF MONITOR
LOC     EQU     0F000H

;       SIZE OF BREAKPOINT STACK
;       WARNING:
;       DON'T CHANGE THESE VALUES UNLESS YOU
;       REALLY UNDERSTAND THE MONITOR.
BSTK    EQU     22              ;22 BYTES DEEP

;       ASCII CHARACTERS
CR      EQU     0DH
LF      EQU     0AH
BS      EQU     08H
FF      EQU     0CH

        ORG     LOC

;       EXTERNALLY REFERENCED SUBROUTINE
;       JUMP TABLE
        JP      BEGIN
        JP      CI
        JP      RI
        JP      CO
        JP      PR
        JP      LD
        JP      CSTS
        JP      IOCHK
        JP      IOSET
        JP      MEMCK
        JP      STRNG
        JP      REENT
        JP      JVTR
REENT:  LD      C,0
        DEFB    21H             ;"LXI H" TRICK
BEGIN:  LD      C,1
        LD      DE,BG0
        JP      ADSCS
BG0:    LD      A,C             ;SAVE COLD BOOT FLAG
        LD      HL,VX3-ENDX
        ADD     HL,DE           ;POINT TO (VX3-1)
        LD      BC,VX3-EXIT     ;NUMBER OF BYTES
        LD      DE,VX3-1        ;TOP 3 OF TEMPLATE
BG1:    EX      DE,HL
        LDDR                    ;MOVE TEMPLATE INTO RAM
        EX      DE,HL
        INC     HL
        LD      C,A             ;RESTORE COLD BOOT FLAG
        LD      SP,HL
        CALL    ADUST
        PUSH    HL
        LD      HL,0
; ******************************
;        LD      B,(BSTK-2)/2
        LD      B,10
; ******************************
BG1A:   PUSH    HL              ;CLEAR BREAK PT. STACK
        DEC     B
        JR      NZ,BG1A
        LD      A,C
        OR      A
        JR      Z,BG4
;SET-UP 1/0 BYTE FOR MONITOR
BG2:    LD      A,C             ;GET CONSOLE VALUE
        DEC     A               ;TRY NEXT CONSOLE
        AND     3               ;ONLY FOUR!
        LD      C,A
        CALL    IOSET           ;SET FOR a THRU 3
        CALL    CSTS            ;TEST FOR INPUT
        JR      Z,BG2           ;NO STATUS?KEEP LOOKING
        CALL    CI              ;GET 1ST CHAR.
        CP      CR              ;MUST BE A 'CR'.
        JR      NZ,BG2          ;IF ERR, KEEP TRYING.
        CALL    VB3MZ           ;INITIALIZE VB3
        CALL    ADSCR           ;FIND TOP OF RAM
        DEC     HL              ;SKIP 1/0 BYTE
        LD      DE,NROWS*256+NCOLS
        LD      (HL),D          ;SET NO. OF RDWS
        DEC     HL
        LD      (HL),E          ;SET NO. OF COLUMNS
        LD      C,FF            ;CLEAR SCREEN
        CALL    CO              ;
BG4:    LD      HL,VERS
        CALL    STRNG
        OR      A

; COMMAND RETURN POINT
CMNDR:  JP      NC,START

;       ERROR RETURN
LER:    CALL    ADSCR
; ******************************
        LD      DE,EXIT-ENDX+1-BSTK
; ******************************
        ADD     HL,DE
        LD      SP,HL
        LD      HL,ERM
        CALL    STRNG
;       INPUT AND EXECUTE NEXT COMMAND
START:  EI
        CALL    CRLF
        LD      C,'.'
        CALL    CO
        CALL    TI
        OR      A               ;IS IT A BREAK?
        JP      Z,LER
        LD      B,A             ;SAVE CMD LETTER
        LD      HL,CMNDR
        PUSH    HL
        LD      HL,TBL
S1:     LD      A,(HL)          ;GET POSSIBLE CMD.
        INC HL
        CP      B               ;IS THERE A MATCH?
        JR      Z,S2            ;JUMP IF YES.
        OR      A               ;END OF CMD TABLE?
        JP      Z,LER
        INC     HL
        INC     HL
        JR      S1
S2:     LD      A,(HL)
        INC     HL
        LD      H,(HL)
        LD      L,A
        LD      C,2
        JP (HL)
VERS:   DEFB    CR,LF,'SSM Z80 MONITOR V1.1'
;        DEFB    '0' OR 80H
        DEFB    '0' | 80H
;ERM:    DEFB    LF,'*' OR 80H
ERM:    DEFB    LF,'*' | 80H
;       COMMAND JUMP TABLE
TBL:    DEFB    'A'
        DEFW    ASSIGN
        DEFB    'D'
        DEFW    DISP
        DEFB    'F'
        DEFW    FILL
        DEFB    'G'
        DEFW    GOTO
        DEFB    'H'
        DEFW    HEXN
        DEFB    'I'
        DEFW    ZINP            ;Z80 INPUT CMD
        DEFB    'M'
        DEFW    MOVE
        DEFB    'O'
        DEFW    ZOUT            ;Z80 OUTPUT CMD
        DEFB    'S'
        DEFW    SUBS
        DEFB    'X'
        DEFW    X
        DEFB    0               ;FUTURE CMD.
        DEFW    0               ;FUTURE ROUTINE ADDR.
        DEFB    0               ;DITTO
        DEFW    0
        DEFB    0               ;END OF TABLE

;       UTILITY SUBROUTINES
BLK:    LD      C,' '
CO:     CALL    IOBR
        DEFB    1,10H
CI:     CALL    IOBR
        DEFB    1,8
CSTS:   CALL    IOBR
        DEFB    1,0
RI:     CALL    IOBR
        DEFB    4,18H
PR:     CALL    IOBR
        DEFB    3,20H
LD:     CALL    IOBR
        DEFB    2,28H
IOBR:   EX      (SP),HL
        PUSH    BC
        LD      B,(HL)
        INC     HL
        LD      C,(HL)
        CALL    ADIOB
        LD      A,(HL)
        RRCA
IOB1:   RLCA
        RLCA
        DEC     B
        JR      NZ,IOB1
        AND     6
        ADD     A,C
        LD      C,A
        LD      HL,IOTAB
        ADD     HL,BC
        LD      A,(HL)
        INC     HL
        LD      H,(HL)
        LD      L,A
        POP     BC
        EX      (SP),HL
        RET

IOCHK:  PUSH    HL
        CALL    ADIOB
        LD      A,(HL)
        POP     HL
        RET

IOSET:  PUSH    HL
        PUSH    AF
        CALL    ADIOB
        LD      (HL),C
        POP     AF
        POP HL
        RET

STRNG:  LD      A,(HL)
        AND     7FH
        RET     Z
        LD      C,A
        LD      A,(HL)
        OR      A
        JP      M,CO
        CALL    CO
        INC     HL
        JP      STRNG

TI:     CALL    CI
        AND     7FH
        PUSH    BC
        LD      C,A
        CALL    CO
        LD      A,C
        POP     BC
        CP      61H             ;LDWER CASE -AF16A
        CCF
        RET     NC              ;RET IF LESS THAN
        CP      7AH+1           ;LDWER CASE -Z-
        RET     NC              ;RET IF GREATER THAN
        AND     5FH             ;CONVERT TO UPPER CASE
        RET

CONV:   AND     0FH
        ADD     A,90H
        DAA
        ADC     A,40H
        DAA
        LD      C,A
        RET

CRLF:   LD      C,CR
        CALL    CO
        LD      C,LF
        JP      CO

EXPR1:  LD      C,1
EXPR:   LD      HL,0000H
EX0:    CALL    TI
EX1:    LD      B,A                 ;SAVE CHARACTER
        CALL    NIBBL               ;CREATE NYBBLE
        JP      C,EX2               ;JUMP IF ERROR
        ADD     HL,HL               ;SHIFT HL LEFT 4
        ADD     HL,HL
        ADD     HL,HL
        ADD     HL,HL
        OR      L                   ;COMBINE WITH NYBBLE
        LD      L,A
        JP      EX0                 ;GO BACK FOR MORE.
EX2:    EX      (SP),HL
        PUSH    HL
        LD      A,B
        CALL    P2C
        JP      NC,EX3
        DEC     C
        JP      NZ,LER
        RET
EX3:    JP      NZ,LER
        DEC     C
        JP      NZ,EXPR
        RET
EXF:    LD      C,1
        LD      HL,0000H
        JP      EX1

HILO:   INC     HL
        LD      A,H
        OR      L
        SCF
        RET     Z
        LD      A,E
        SUB     L
        LD      A,D
        SBC     A,H
        RET

LADR:   LD      A,H
        CALL    LBYTE
        LD      A,L
LBYTE:  PUSH    AF
        RRCA
        RRCA
        RRCA
        RRCA
        CALL    HXD
        POP     AF
HXD:    CALL    CONV
        JP      CO
MEMCK:  PUSH    HL
        PUSH    DE
        CALL    ADSCR
        EX      DE,HL
        LD      HL,0
MEM0:   INC     H
        LD      A,(HL)
        CPL
        LD      (HL),A
        CP      (HL)
        CPL
        LD      (HL),A
        JR      Z,MEM0
        DEC     HL
        LD      B,H
        LD      A,H
        CP      D
        LD       A,100H-BSTK-20-(ENDX-EXIT)
        ;BSTK=BREAKPT. STACK
        ;ENDX-EXIT=TEMPL. SIZE
        ;20=SOME MONIT. RAM
        POP     DE
        POP     HL
        RET     Z
        LD      A,0FFH
        RET
NIBBL:  SUB     '0'
        RET     C
;        ADD     A,'0'-'G' AND 0FFH
        ADD     A,'0'-'G' & 0FFH
        RET     C
        ADD     A,6
        JP      P,NI0
        ADD     A,7
        RET     C
NI0:    ADD     A,10
        OR      A
        RET
PCHK:   CALL    TI
P2C:    CP      ' '
        RET     Z
        CP      ','
        RET     Z
        CP      CR
        SCF
        RET Z
        CCF
        RET

;       BREAKPOINT ENTRY POINT
RESTRT: PUSH    HL                  ;PUT REGISTERS ON TO
        PUSH    DE                  ;   USER'S STACK
        PUSH    BC
        PUSH    AF
        CALL    ADSCR
        LD      DE,EXIT-ENDX+1
        ADD     HL,DE               ;DELTA EQUIV. TO 'EXIT'
        EX      DE,HL
        LD      HL,10
        ADD     HL,SP               ;SET TO TOP OF USER STX
        LD      B,4                 ;
        EX      DE,HL               ;DE=USER STX,HL='EXIT'

RST0:   DEC     HL
        LD      (HL),D              ;SAVE DATA IN MONT.RAM
        DEC     HL
        LD      (HL),E              ;SAVE USER STX ADDR.
        POP     DE                  ; THEN 'PSW',NEXT'BC'
        DEC     B                   ; LAST 'DE'
        JR      NZ,RST0
        POP     BC                  ;DE='HL' ,BC=RET.ADDR.
        DEC     BC                  ;MOVE RET.ADDR. BACK
        LD      SP,HL               ;START NEW STACK
        EX      AF,AF'
        EXX
        PUSH    HL                  ;PUSH HL
        PUSH    DE                  ;PUSH DE
        PUSH    BC                  ;PUSH BC
        PUSH    AF                  ;PUSH AF
        PUSH    IX
        PUSH    IY
        LD      A,I
        LD      B,A                 ;LD B,A
        LD      A,R
        LD      C,A                 ;LD C,A
        PUSH    BC                  ;PUSH BC
        EXX
        LD      HL,TLOC
        ADD     HL,SP
        LD      A,(HL)              ;DOES TRAP #1 ADDRESS
        SUB     C                   ;EQUAL RET. ADDRESS
        INC     HL
        JR      NZ,RST1             ;NO?
        LD      A,(HL)
        SUB     B
        JR      Z,RST3              ;YES?
RST1:   INC     HL
        INC     HL                  ;SKIP TRAP#1 INSTRUC.
        LD      A,(HL)              ;DOES TRAP#2 ADDRESS
        SUB     C                   ; EQUAL RET. ADDRESS
        JR      NZ,RST2             ;NO?
        INC     HL
        LD      A,(HL)
        SUB     B
        JR      Z,RST3              ;YES?
RST2:   INC     BC                  ;MUST BE FALSE BREAK PT
RST3:   LD      HL,LLOC
        ADD     HL,SP
        LD      (HL),E              ;SAVE HL IN TEMPLATE
        INC     HL                  ;  IN 'LXI'INSTRUCTION
        LD      (HL),D
        LD      HL,PLOC-1
        ADD     HL,SP
        LD      (HL),C              ;SAVE RET.ADDRESS IN
        INC     HL                  ;  TEMPLATE 'JMP'INSTR.
        LD      (HL),B
        PUSH    BC                  ;SAVE BREAK AODR.
        LD      HL,ERM              ;
        CALL    STRNG               ;PRINT '*'
        POP     HL                  ;GET BREAK ADOR.
        CALL    LADR                ;PRINT ADDRESS

;RESTORE BREAK PT. BYTES IN PROGRAM
        LD      HL,TLOC
        ADD     HL,SP               ;PT. TO TRAP ADOR. 1
        LD      D,2
RST4:   LD      C,(HL)              ;GET LDW HALF
        LD      (HL),0
        INC     HL
        LD      B,(HL)              ;GET HIGH HALF
        LD      (HL),0
        INC     HL
        LD      A,C
        OR      B                   ;TRAP ADDR.=O?
        JR      Z,RST5              ;SKIP TRAP IF YES.
        LD      A,(HL)              ;GET OLD INSTR. BYTE
        LD      (BC),A              ;RESTORE PROGRAM BYTE
RST5:   INC     HL                  ;NEXT TRAP
        DEC     D
        JR      NZ,RST4
        JP      START

;       SCRATCHPAD TEMPLATE
EXIT:   POP     BC
        LD      A,C
        LD      R,A
        LD      A,B
        LD      I,A
        POP     IY
        POP     IX
        POP     AF
        POP     BC
        POP     DE
        POP     HL
        EXX
        EX      AF,AF'
        POP     DE
        POP     BC
        POP     AF
        POP     HL
        LD      SP,HL
        LD      HL,0
HLX     EQU     $-2
        EI
        JP      0
PCX     EQU     $-2
T1A:    DEFW    0                   ;TRAP 1 ADDR
        DEFB    0                   ;TRAP 1 INST
        DEFW    0                   ;TRAP 2 ADDR
        DEFB    0                   ;TRAP 2 INST
VX1:    DEFW    0                   ;VIDEO POINTER
        DEFB    0                   ;VIDEO HOLD
VX3:    DEFB    NCOLS               ;VB3 COLUMN CONSTANT
        DEFB    NROWS               ;VB3 ROW CONSTANT
        DEFB    0                   ;IOBYT
ENDX:
ALOC    EQU     13H
BLOC    EQU     11H
CLOC    EQU     10H
DLOC    EQU     0FH
ELOC    EQU     0EH
FLOC    EQU     12H
HLOC    EQU     HLX-EXIT+BSTK+1
LLOC    EQU     HLX-EXIT+BSTK
PLOC    EQU     PCX-EXIT+BSTK+1
SLOC    EQU     15H
TLOC    EQU     T1A-EXIT+BSTK
;
APLOC   EQU     07H
BPLOC   EQU     09H
CPLOC   EQU     08H
DPLOC   EQU     0BH
EPLOC   EQU     0AH
FPLOC   EQU     06H
HPLOC   EQU     0DH
LPLOC   EQU     0CH
XLOC    EQU     05H
YLOC    EQU     03H
ILOC    EQU     01H
RLOC    EQU     00H
;       COMMAND IMPLEMENTATION

;       ASSIGN COMMAND
ASSIGN: CALL    TI
        LD      B,0
        CP      'C'
        JR      Z,AS1
        INC     B
        CP      'R'
        JR      Z,AS1
        INC     B
        CP      'P'
        JR      Z,AS1
        INC     B
        CP      'L'
        JR      NZ,EREXT
AS1:    CALL    TI
        CP      '='
        JR      NZ,AS1
        CALL    TI
        SUB     '0'
        LD      L,A
        JP      M,EREXT
        CP      4
        JP      P,EREXT
        LD      H,3
AS2:    DEC     B
        JP      M,AS3
        ADD     HL,HL
        ADD     HL,HL
        JR      AS2
AS3:    EX      DE,HL
        CALL    ADIOB
        LD      A,(HL)
        OR      D
        XOR     D
        OR      E
        LD      (HL),A
        RET
EREXT:  SCF
        RET

;       DISPLAY COMMAND
DISP:   CALL    EXPR
        POP     DE
        POP     HL
DI0:    CALL    CRLF
        CALL    LADR
DI1:    CALL    BLK
        LD      A,(HL)
        CALL    LBYTE
        CALL    HILO
        CCF
        RET     NC
        LD      A,L
        AND     0FH
        JR      NZ,DI1
        CALL    CSTS                ;TEST KEYBOARD STATUS
        JR      Z,DI0               ;JUMP IF NO DATA
        CALL    CI                  ;GET CHARACTER
        CP      'S'                 ;
        RET     Z                   ;RET, IF COMMANDED
        CALL    CI                  ;WAIT. GET CHARACTER
        CP      'S'
        JR      NZ,DI0              ;CONTINUE IF NO 's'
        RET

;       FILL COMMAND
FILL:   INC     C
        CALL    EXPR
        POP     BC
        POP     DE
        POP     HL
FI0:    LD      (HL),C
        CALL    HILO
        JR      NC,FI0
        OR      A
        RET

;       GOTO COMMAND
GOTO:   POP     HL
        CALL    PCHK
        JR      C,GO3
        JR      Z,GO0
        CALL    EXF
        POP     DE
        LD      HL,PLOC
        ADD     HL,SP
        LD      (HL),D
        DEC     HL
        LD      (HL),E
        LD      A,B
        CP      CR
        JR      Z,GO3
        LD      A,0C3H              ;C3 = JMP CODE
        LD      (8),A
        LD      HL,RESTRT
        LD      (9),HL
GO0:    LD      D,2
        LD      HL,TLOC
        ADD     HL,SP
GO1:    PUSH    HL
        CALL    EXPR1
        LD      E,B
        POP     BC
        POP     HL
        LD      A,B
        OR      C
        JR      Z,GO2
        LD      (HL),C
        INC     HL
        LD      (HL),B
        INC     HL
        LD      A,(BC)
        LD      (HL),A
        INC     HL
        LD      A,0CFH              ;CF = RST1 CODE
        LD      (BC),A
GO2:    LD      A,E
        CP      CR
        JR      Z,GO3
        DEC     D
        JR      NZ,GO1
GO3:    CALL    CRLF
; ********************
        LD      HL,BSTK
; ********************
        ADD HL,SP
        JP (HL)

;       HEXADECIMAL COMMAND
HEXN:   CALL    EXPR
        POP     DE
        POP     HL
        CALL    CRLF
        PUSH    HL
        ADD     HL,DE
        CALL    LADR
        CALL    BLK
        POP     HL
        LD      A,L
        SUB     E
        LD      L,A
        LD      A,H
        SBC     A,D
        LD      H,A
        CALL    LADR
        OR      A
        RET

;       INPUT COMMAND
;       SUGGESTED BY DAN FISCHLER,1980
ZINP:   CALL    EXPR1               ;GET PORT NUMBER
        POP     BC                  ;PLACE NO. IN REG.C
        IN      B,(C)
        CALL    BLK                 ;PRINT BLANK
        LD      A,B                 ;GET OATA
        CALL    LBYTE               ;PRINT DATA
        OR      A                   ;CLEAR ERR FLAG
        RET

;       MOVE COMMAND
MOVE:   INC     C
        CALL    EXPR
        POP     BC
        POP     DE
        POP     HL
MV0:    LD      A,(HL)
        LD      (BC),A
        INC     BC
        CALL    HILO
        JP      NC,MV0
        OR      A
        RET

;       OUTPUT COMMAND
ZOUT:   CALL    EXPR                ;GET PORT & OATA
        POP     DE                  ;PUT DATA IN REG.E
        POP     BC                  ;PUT PORT NO. IN REG.C
        OUT     (C),E
        OR      A                   ;CLEAR ERR FLAG
        RET

;       SUBSTITUTE COMMAND
SUBS:   CALL    EXPR1
        CALL    P2C
        POP     HL
        RET     C
        LD      B,8                 ;SUBST. EIGHT PER LINE
SU0:    PUSH    BC                  ;SAVE COUNT
        LD      A,(HL)
        CALL    LBYTE
        LD      C,'-'
        CALL    CO
        CALL    PCHK
        CCF
        POP     BC
        RET     NC
        JR      Z,SU1
        PUSH    BC                  ;SAVE COUNT
        PUSH    HL
        CALL    EXF
        POP     DE
        POP     HL
        LD      (HL),E
        LD      A,B
        CP      CR
        POP     BC
        RET     Z
SU1:    INC     HL
        CALL    SU2                 ;CHK FOR 8 SUBST.
        JR      SU0
SU2:    DEC     B                   ;COUNT SUBSTITUTES
        RET     NZ
        CALL    CRLF
        CALL    BLK
        CALL    BLK
        CALL    BLK
        LD      B,8
        RET

;       REGISTER COMMAND
X:      CALL    TI
        LD      HL,ACTBL
        CP      CR
        JP      Z,X6
        CP      27H
        JR      NZ,XA
        LD      HL,PRMTB
        CALL    TI
        CP      CR
        JP      Z,X6
XA:
        LD      B,A
X0:     LD      A,(HL)
        AND     7FH
        CP      B
        JR      Z,X1
        LD      A,(HL)
        OR      A
        RET     Z
        INC     HL
        INC     HL
        LD      A,B
        JR      X0
X1:     CALL    BLK
X2:     LD      A,(HL)
        AND     80H
        RLCA
        LD      B,A
        INC     B
        INC     HL
        LD      A,(HL)
        EX      DE,HL
        LD      L,A
        LD      H,0
        ADD     HL,SP
        EX      DE,HL
        INC     HL
        LD      A,(DE)
        CALL    LBYTE
        DEC     B
        JR      Z,X3
        DEC     DE
        LD      A,(DE)
        CALL    LBYTE
X3:     INC     B
        LD      C,'-'
        CALL    CO
        CALL    PCHK
        CCF
        RET     NC
        JR      Z,X5
        PUSH    HL
        PUSH    BC
        CALL    EXF
        POP     HL
        POP     AF
        PUSH    BC
        PUSH    AF
        LD      A,L
        LD      (DE),A
        POP     BC
        DEC     B
        JR      Z,X4
        INC     DE
        LD      A,H
        LD      (DE),A
X4:     POP     BC
        POP     HL
X5:     LD      A,(HL)
        OR      A
        RET     Z
        LD      A,B
        CP      CR
        RET     Z
        JP      X2
X6:     CALL    CRLF
        EX      DE,HL
        INC     DE
        LD      A,(DE)
        LD      L,A
        LD      H,0
        ADD     HL,SP
        LD      B,(HL)
        LD      HL,FLGTBL
X7:     CALL    BLK
X75:    LD      A,(HL)
        INC     HL
        OR      A
        LD      C,A
        LD      A,B
        RLCA
        LD      B,A
        JR      Z,X8
        JP      M,X75
        CALL    CO
        LD      C, ':'
        CALL    CO
        LD      A,B
        AND     1
        ADD     A,'0'
        LD      C,A
        CALL    CO
        JR      X7
X8:     EX      DE,HL
        INC     HL
        CALL    CRLF
X9:     CALL    BLK
        LD      A,(HL)
        INC     HL
        OR      A
        RET     Z
        LD      B,A
        AND     7FH                 ;REMOVE MSB FOR A
                                    ;CLEAN DISPLAY ON
                                    ;THE VB1B/C & VB3
        LD      C,A
        CALL    CO
        LD      C,'='
        CALL    CO
        LD      A,(HL)
        INC     HL
        EX      DE,HL
        LD      L,A
        LD      H,0
        ADD     HL,SP
        EX      DE,HL
        LD      A,(DE)
        CALL    LBYTE
        LD      A,B
        AND     80H
        JR      Z,X9
        DEC     DE
        LD      A,(DE)
        CALL    LBYTE
        JR      X9

ACTBL:  DEFB    'F',            FLOC+2
        DEFB    'A',            ALOC+2
        DEFB    'B',            BLOC+2
        DEFB    'C',            CLOC+2
        DEFB    'D',            DLOC+2
        DEFB    'E',            ELOC+2
        DEFB    'H',            HLOC+2
        DEFB    'L',            LLOC+2
;        DEFB    'M' OR 80H,     HLOC+2
        DEFB    'M' | 80H,     HLOC+2
;        DEFB    'P' OR 80H,     PLOC+2
        DEFB    'P' | 80H,     PLOC+2
;        DEFB    'S' OR 80H,     SLOC+2
        DEFB    'S' | 80H,     SLOC+2
        DEFB    'I',            ILOC+2
        DEFB    0

PRMTB:  DEFB    'F',            FPLOC+2
        DEFB    'A',            APLOC+2
        DEFB    'B',            BPLOC+2
        DEFB    'C',            CPLOC+2
        DEFB    'D',            DPLOC+2
        DEFB    'E',            EPLOC+2
        DEFB    'H',            HPLOC+2
        DEFB    'L',            LPLOC+2
;        DEFB    'M' OR 80H,     HPLOC+2
        DEFB    'M' | 80H,     HPLOC+2
;        DEFB    'X' OR 80H,     XLOC+2
        DEFB    'X' | 80H,     XLOC+2
;        DEFB    'Y' OR 80H,     YLOC+2
        DEFB    'Y' | 80H,     YLOC+2
        DEFB    'R',            RLOC+2
        DEFB    0

FLGTBL: DEFB    'S'
        DEFB    'Z'
        DEFB    'X'
        DEFB    'H'
        DEFB    'X'
        DEFB    'V'
        DEFB    'N'
        DEFB    'C'
        DEFB    0

; ***** SYSTEM CONFIGURATION PACKAGE *****
;       LOGICAL DEVICE/DEVICE DRIVER TABLES
;
;       EACH 4 ENTRY TABLE LISTS THE ADDRESSES
;       OF THE DRIVER ROUTINES TO BE USED FOR
;       THE PHYSICAL DEVICES WHICH MAY ASSIGNED
;       TO THAT LOGICAL DEVICE.

IOTAB:
;       CONSOLE STATUS
;
;       RETURN WITH REGISTER A = 0 IF NO
;       CONSOLE CHARACTER AVAILABLE.
CSTAB:  DEFW    TTSA    ;0 STATUS, SERIAL-A
        DEFW    TTSB    ;1 STATUS,SERIAL-B
        DEFW    KYS1    ;2 STATUS, PAR. KYBD
        DEFW    KYS2    ;3 STATUS,VB3 KYBD
;       CONSOLE INPUT
;       RETURN CONSOLE INPUT CHARACTER
;       IN REGISTER A.
CITAB:  DEFW    TTIA    ;0 OATA-IN,SERIAL-A
        DEFW    TTIB    ;1 DATA-IN,SERIAL-B
        DEFW    KYI1    ;2 DATA-IN, PAR. KYBO
        DEFW    KYI2    ;3 DATA-IN,VB3 KYBD
;       CONSOLE OUTPUT
;       OUTPUT BYTE IN REGISTER C
;       TO CONSOLE OUTPUT DEVICE.
COTAB:  DEFW    TTOA    ;0 DATA-OUT, SERIAL-A
        DEFW    TTOB    ;1 DATA-OUT,SERIAL-B
        DEFW    CRT     ;2 DATA-OUT,VBIB
        DEFW    VB30    ;3 DATA-OUT,VB3
;       READER INPUT
;       RETURN READER INPUT BYTE IN
;       REGISTER A, CARRY OFF. SET
;       CARRY IF NO BYTE AVAILABLE.
RITAB:  DEFW    TTIA    ;0 READER-IN, SERIAL-A
        DEFW    TTIB    ;1 READER-IN,SERIAL-B
        DEFW    RDR     ;2 READER-IN,EXT RDR
        DEFW    KYI2    ;3 READER-IN
;       PUNCH OUTPUT
;       OUTPUT BYTE IN REGISTER C
;       TO PUNCH DEVICE.
POTAB:  DEFW    TTOA    ;O PUNCH-OUT, SERIAL-A
        DEFW    TTOB    ;1 PUNCH-OUT,SERIAL-B
        DEFW    PUNCH   ;2 PUNCH-OUT,EXT PUNCH
        DEFW    VB30    ;3 PUNCH-OUT
;       LISTING OUTPUT
;       OUTPUT BYTE IN REGISTER C
;       TO LISTING DEVICE.
LOTAB:  DEFW    TTOB    ;0 LIST DEV. , SERIAL-B
        DEFW    TTOA    ;1 LIST DEV.,SERIAL-A
        DEFW    VB30    ;2 LIST DEV., VB3
        DEFW    CRT     ;3 LIST DEV.,VB1
;       SET ASIDE SCRATCH RAM FOR MONITOR DATA.
;
;       SET THE TOP OF MONITOR SCRATCH TO BE USED ON
;       PDWER-UP. THE ADDRESS IS RETURNED IN REGISTERS
;       D&E. **NOTE** THIS ROUTINE IS NOT CALLED
;       INSTEAD THE RETURN ADDRESS IS PLACED IN REG.
;       D&E AND THE SUBROUTINE IS ENTERED BY A JUMP
;       INSTRUCTION. THE ROUTINE RETURNS BY PLACING
;       THE ADDRESS IN H&L REG. AND EXECUTING A PCHL
;       INSTRUCTION. REGISTER C MUST EQUAL A ZERO OR
;       A ONE.
ADSCS:  JP      ADS2

;
;       LOCATE THE 84 BYTES OF RAM USED BY THE
;       MONITOR. ADDRESS IS RETURNED IN H&L.
ADSCR:  JP      ADS1
;
;       LOCATE THE IOBYT ADDRESS. ADDRESS IS RETURNED
;IN H&L.
ADIOB:  JP      ADS1
;
;       SET UP USER DEFAULT STACK. ADDRESS IS
;       RETURNED IN H&L.
ADUST:  LD      HL,100H
        RET

;       SPECIAL JUMP VECTOR. REGISTER A=INDEX.
JVTR:   JP      JENTRY
ADS1:   PUSH    BC
        PUSH    DE
        LD      C,0
        LD      DE,ADS1A
        JR      ADS2
ADS1A:  EX      DE,HL
        POP     DE
        POP     BC
        RET
M2K     EQU     2*1024
M48K    EQU     48*1024
LIMIT   EQU     M48K    ;UPPER 16K FOR USER.
ADS2:   LD      HL,0
;        LD      B,M2K SHR 8
        LD      B,M2K >> 8
ADS3:   ADD     HL,BC
        LD      A,H     ;GET HIGH ADDR.
;        CP      (LIMIT+M2K) SHR 8
        CP      (LIMIT+M2K) >> 8
        JP      C,ADS4
;        LD      H,LIMIT SHR 8
        LD      H,LIMIT >> 8
        JR      ADS5
        ;
ADS4:   LD      A,(HL)  ;GET MEMORY BYTE
        DI
        CPL
        LD      (HL),A  ;TRY TO CHG. IT
        CP      (HL)    ;DID WE CHG. IT?
        CPL
        LD      (HL),A  ;RESTORE IT
        EI
        JP      Z,ADS3  ;IF RAM, TRY AGAIN
ADS5:   LD      L,0
        DEC     HL
        EX      DE,HL
        JP      (HL)

;******* GENERAL PURPOSE 1/O ROUTINES *******
;       THREE ROUTINES FOR CHECKING STATUS, INPUTTING
;       DATA AND OUTPUTTING DATA. REGISTERS A & C
;       ARE USED.
;       ENTER WITH:
;           REG.-A = STATUS PORT ADDRESS.
;           REG.-C = OUTPUT DATA (GPRT ONLY)
;       EXITS WITH:
;           REG.-A = DATA READ (GDAT ONLY)
;           REG.-A = STATUS CONDITION (GSTS ONLY)

;       GET STATUS CONDITION OF INPUT DEVICE
;       EXITS WITH REG.-A SET TO ZERO IF NO DATA YET.
GSTS:   PUSH    BC
        LD      C,A     ;SET STATUS ADDRESS
        IN      A,(C)   ;INPUT STATUS
        AND     1       ;TEST LSB
        SUB     1
        SBC     A,A
        POP     BC
        RET

;       GET INPUT DATA.
;       EXITS WITH REG.-A SET WITH THE DATA
GDAT:   PUSH    BC
        LD      C,A     ;SET STATUS ADDRESS
GD:     IN      A,(C)   ;INPUT STATUS
        AND     1       ;TEST LSB
        JR      NZ,GD   ;IF NO DATA,GO BACK
        INC     C       ;SET DATA ADDRESS
        IN      A,(C)   ;GET DATA
        AND     7FH     ;REMOVE PARITY
        POP     BC
        RET

;       PRINT OUT DATA.
;       ENTER WITH DATA IN REG.-C
GPRT:   PUSH    BC
        LD      B,C
        LD      C,A     ;SET PORT ADDRESS
GP:     IN      A,(C)   ;INPUT STATUS
        AND     80H     ;PRINTER BUSY?
        JR      NZ,GP   ;BUSY?IF TRUE, JUMP BACK
        INC     C       ;SET OUTPUT PORT
        LD      A,B 
        OUT     (C),A   ;PRINT!
        POP     BC
        RET

;********** STATUS ROUTINES **********
;       ALL THE MONITOR'S STATUS ROUTINES ARE HERE.
;       SERIAL-A
TTSA:   LD      A,0     ;PORT 0
        JP      GSTS

;       SERIAL-B
TTSB:   LD      A,2     ;PORT 2
        JP      GSTS

;       PARALLEL KEYBOARD
KYS1:   LD      A,6     ;PORT 6
        JP      GSTS

;       VB3 KEYBOARD
KYS2:   LD      A,0E0H  ;PORT EO
        JP      GSTS

;********** INPUT ROUTINES **********
;       ALL THE MONITOR'S INPUT ROUTINES ARE HERE.
;       SERIAL-A
TTIA:   LD      A,0     ;PORTS 0&1
        JP      GDAT

;       SERIAL-B
TTIB:   LD      A,2     ;PORTS 2&3
        JP      GDAT

;       PARALLEL KEYBOARD
KYI1:   LD A,6          ;PORTS 6&7
        JP      GDAT

;       VB3 KEYBOARD
KYI2:   LD      A,0E0H  ;PORTS E0 & E1
        JP      GDAT

;       READER ROUTINE
;       THIS ROUTINE HAS A BUILT-IN TIMER AND WILL
;       RETURN WITH THE CARRY SET IF NO DATA IS
;       FOUND IN A SET PERIOD OF TIME.
RDR:    PUSH    HL      ;SAVE H&L
        LD      HL,0    ;SET TIMER
RD1:    IN      A,(0EH) ;GET STATUS
        AND     1       ;TEST STATUS
        JR      Z,RD2   ;JUMP OUT, IF DATA
        DEC     HL
        LD      A,H
        OR      L       ;TEST TIMER
        JR      NZ,RD1  ;IF STILL TIME,GO BACK
        SCF             ;TIME OUT, SET ERROR
        POP     HL
        RET
RD2:    IN      A,(0FH) ;GET DATA
        OR      A       ;CLEAR ANY CARRYS
        POP     HL
        RET

;********** OUTPUT ROUTINES **********
;       ALL THE MONITOR'S OUTPUT ROUTINES ARE HERE.
;        SERIAL-A
TTOA:   LD      A,0     ;PORTS 0&1
        JP      GPRT

;       SERIAL-B
TTOB:   LD      A,2     ;PORTS 2&3
        JP      GPRT

;       PUNCH DEVICE
PUNCH:  LD      A,0EH   ;PORTS E&F
        JP      GPRT

;       VBIB OUTPUT
CRT:    LD      A,C     ;GET DATA
        OR      A       ;CHECK FOR NULL
        RET     Z       ;RET IF TRUE
        CP      7FH     ;CHECK FOR RUB-OUT
        RET     Z       ;RET IF TRUE
        PUSH    HL
        CALL    ADSCR   ;GET ADDR. OF SCRATCH
        DEC     HL
        DEC     HL      ;VIDEO POINTER
        DEC     HL
        DEC     HL
        DEC     HL      ;PNT TO VB1B SCRATCH
        JP      VDTTY

;**********VB3 OUTPUT**********
;       "VBIOS"; REV 0
;       WRITTEN BY BEN L GEE; DEC 9, 1979
;       MODIFIED BY MALCOLM T. WRIGHT,4-27-80
;
;       VBIOS IS A DRIVER FOR THE SSM VB3 VIDEO BOARD. 
;       VBIOS CONTAINS ONLY ENOUGH CODE TO RUN CP/M
;       AND SOME CP/M PROGRAMS.
;
;**     HARDWARE CONFIGURATION
;       16 MHZ DOT CLOCK
;       9 DOTS PER CHARACTER
;       80 CHARACTER PROMS
;**     DEFINE SYSTEM EQUATES.
VIDEO   EQU     0C0H    ;ADDRESS OF THE VB3
OFFSET  EQU     01000H  ;OFFSET TO ATTRIBUTE
NORMAL  EQU     3       ;NORMAL ALPHA ATTRIB.
CODE    EQU     5       ;SET 80 CHAR. MODE
VTAC    EQU     0D0H    ;1/0 ADDR. OF CRT CTRL
KSTAT   EQU     0E0H    ;KEYBOARD STATUS
KDATA   EQU     KSTAT+1 ;KEYBOARD DATA
NCOLS   EQU     80      ;SCREEN SIZE
NROWS   EQU     18      ;NO. OF TEXT LINES
SKEW    EQU     0
SCANF   EQU     262     ;SCANS PER FRAME
SCANR   EQU     12      ;SCANS PER DATA ROW
HCOUNT  EQU     113     ;SET HORIZ SCAN LINE
INTERL  EQU     FALSE   ;INTERLACED?

;**     VIDEO DRIVER SUBROUTINES
;
;       ENTRY POINTS ARE:
;
;               VB3MZ - TO INITALIZE THE VB3
;               VB30 - TO OUTPUT A CHARACTER
;       CALLING SEQUENCES:
;
;               CALL    VB3MZ   ;INITIALIZE VB3
;
;
;               LD      A,DATA  ;GET CHARACTER
;               MOV     C,A     ;SAVE IN REG.C
;               CALL    VB30    ;OUTPUT IT
;
;       VECTOR TO SPECIAL SOBROUTINES.
JENTRY: OR      A
        JP      Z,ADSCR ;TOP OF SCRATCH RAM
        DEC     A
        JP      Z,RWCL  ;GET NCOL/NROW ADDR.
        DEC     A
        JP      Z,VB3IZ ;INITIALIZE VB3
        DEC     A
        JP      Z,VB30UT;OUTPUT TO VB3
        DEC     A
        JP      Z,VDTTY ;OUTPUT TO VB1
        RET

;       FIND SCRATCH CONSTANTS FOR NCOLS & NROWS.
;       HL = ADDRESS OF NCOLS CONSTANT.
RWCL:   CALL    ADSCR
        PUSH    DE
        LD      DE,VX3-ENDX+1
        ADD     HL,DE   ;HL PNTS TO NCOLS
        POP     DE
        RET
VB3MZ:
        OUT     (KSTAT),A ; ENABLE THE VB3 BOARD
        CALL    VB3I    ;INITIALIZE CHIP
        OUT     (KDATA),A ;DISABLE VB3
        RET

;       INITALIZE ALL OF THE CONTROL REGISTERS
VB3I:   LD      HL,X80X18
VB3IZ:  OUT     (VTAC+14) ,A
        OUT     (VTAC+10),A     ;RESET THE VTAC
        ;SEND HCOONT
        LD      A,(HL)
        OUT     (VTAC+0),A
        INC     HL
        ;SEND SYNC POSITION
        LD      A,(HL)
        OUT     (VTAC+1),A
        INC     HL
        ;SEND NO. OF CHAR.
        LD      A,(HL)
        OUT     (VTAC+2),A
        INC     HL
        ;SEND NO. OF ROWS
        LD      A,(HL)
        OUT     (VTAC+3),A
        INC     HL
        ;SEND RASTER FILL-IN
        LD      A,(HL)
        OUT     (VTAC+4),A
        INC     HL
        ;SEND TOP MARGIN
        LD      A,(HL)
        OUT     (VTAC+5),A
        INC     HL
        ;SEND BOTTOM LINE NO.
        LD      A,(HL)
        OUT     (VTAC+6),A
        OUT     (VTAC+14),A ;START CRT CHIP
        RET

;       VB3 PARAMETER TABLE
X80X18: DEFB    HCOUNT-1    ;HCOUNT-1
        ;INTERLACE + SYNC POSITION
        DEFB    (INTERL*128)+53H
        ;RASTER LINES PER CHAR. + NO. OF CHAR.
;        IF      NOT INTERL
        IF      ~INTERL
        DEFB    (SCANR-1)*8+CODE
        ENDIF
;        IF      INTERL
;        DEFB    (SCANR-2)*8+CODE
;        ENDIF
        ;NO. OF ROWS DOWN THE SCREEN
        DEFB    SKEW*64+NROWS-1
        ;FILL-IN THE RASTER FOR A FRAME
;        IF      NOT INTERL
        IF      ~INTERL
        DEFB    (SCANF-256)/2
        ENDIF
;        IF INTERL
;        DEFB    (SCANF-513)/2
;        ENDIF
        ;SET THE TOP MARGIN
        DEFB    (SCANF-NROWS*SCANR)/4+9
        ;SET WHICH LINE IS BOTTOM
        DEFB    NROWS-1

;       VB30 IS THE MAIN DISPLAY DRIVER FOR THE VB3.
VB30:   OUT     (KSTAT),A ;TURN-ON VB3
        PUSH    HL
        CALL    RWCL    ;PNT TO CONSTANTS
        CALL    VB30UT  ;OUTPUT TO VB3
        POP     HL
        OUT     (KDATA),A ;TURN-OFF VB3
        RET
VB30UT: PUSH    IX      ;SAVE IX REGISTER
        PUSH    HL      ;SWITCH HL WITH IX
        POP     IX      ;
        PUSH    DE      ;SAVE D,E,B,C REG.'S
        PUSH    BC      ;
        CALL    CUROFF  ;TURN OFF THE CURSOR
        CALL    PROCESS ;PROCESS THE CHARACTER
        CALL    CURON   ;TURN CURSOR BACK ON
        POP     BC      ;RESTORE THE REGISTERS
        POP     DE
        POP     IX
        RET

;       ON ENTRY, REG HL = LOGICAL CURSOR ADDRESS
;       AND REG C = DATA.
PROCESS:
        LD      A,C     ;MOVE DATA TO REG A.
        AND     7FH     ;STRIP PARITY/SET FLAGS
        RET     Z       ;SKIP NULLS.
        CP      0FFH    ;RUB-OUT
        RET     Z
        CP      CR      ;CHECK FOR CTRL-CHAR.
        JP      Z,DOCR  ;
        CP      LF
        JP      Z,DOLF
        CP      BS
        JP      Z,LEFT  ;DO A BACKSPACE
        CP      FF
        JP      Z,DOFF

;       MUST BE A DATA CHARACTER.
        PUSH    HL      ;SAVE LOGIC CUR. ADDR.
        CALL    GETBA   ;GET PHYSICAL CUR/ADDR.
        LD      (HL),C  ;PUT THE DATA THERE.
        LD      DE,OFFSET
        ADD     HL,DE   ;ADDRESS OF ATTRIBUTE
        POP     HL      ;RESTORE LOGICAL CURSOR
        CALL    RIGHT   ;MOVE CURSOR RIGHT.
        RET     NZ      ;

;       PUT THE CURSOR ON COL.1 OF THE CURRENT LINE.
DOCR:   LD      L,0     ;PUT CURSOR ON COL 1
        RET

;       PUT CURSOR ON THE NEXT LINE, BLANK THE LINE.
DOLF:   CALL    DOWN    ;MOVE CURSOR DOWN
        PUSH    HL      ;SAVE CURSOR ADDRESS.
        LD      L,0     ;PUT CURSOR ON COL 1.
        CALL    EEOL    ;ERASE TO END OF LINE.
        POP     HL      ;RESTORE CURSOR ADDR.
        RET             ;RETURN TO CALLER.

DOFF:   LD      HL,0    ;START AT UPPER-LEFT
                        ;AND CLEAR SCREEN.

;       ERASE FROM THE CURSOR POSITION TO THE END
;       OF THE SCREEN.
EEOS:   LD      A,(IX)  ;NCOLS
        SUB     L
        LD      B,A
        LD      A,(IX+1)
        SUB     H
        LD      C,A
        JR      ERASE

;       ERASE FROM THE CURSOR TO THE END OF THE
;       CURRENT LINE.
EEOL:   LD      A,NCOLS
        SUB     L
        LD      B,A
        LD      C,1

;       ERASE THE SCREEN BEGINNING AT THE CURSOR
;       POSITION. THE NUMBER OF CHARACTERS TO
;       ERASE IS IN REG BC.
ERASE:
        PUSH    HL
ERASE1: PUSH    HL
        CALL    GETBA
        LD      DE,OFFSET
        EX      DE,HL
        ADD     HL,DE
        EX      DE,HL
        LD      A,NORMAL ;USE NORMAL ATTRIBUTE
ERASE2: LD      (HL),' '
        LD      (DE),A  ;
        INC     HL
        INC     DE
        DEC     B
        JR      NZ,ERASE2
        POP     HL
        LD      L,0
        CALL    DOWN
        LD      B,NCOLS
        DEC     C
        JR      NZ,ERASE1
        POP     HL
        RET

;       MOVE THE CURSOR UP.
UP:     LD      A,H
        DEC     H
        OR      A
        RET     NZ
        LD      H,(IX+1) ;GET NROWS VALUE
        DEC     H       ;NROWS-1
        RET

;       MOVE THE CURSOR DOWN.
DOWN:   INC     H
        LD      A,(IX+1) ;NROWS
        CP      H
        RET     NZ
        LD      H,0
        RET

;       MOVE THE CURSOR LEFT.
LEFT:   LD      A,L
        DEC     L
        OR      A
        RET     NZ
        LD      L,(IX)  ;NCOLS
        DEC     L       ;NCOLS-1
        RET

;       MOVE THE CURSOR RIGHT.
RIGHT:  INC     L
        LD      A,(IX)  ;NCOLS
        CP      L
        RET     NZ
        LD      L,0
        RET

;       TURN THE CURSOR ON.
;       THE LOGICAL ADDRESS OF THE CURSOR MUST BE
;       IN REG HL.
CURON:  LD      A,L
        OUT     (VTAC+12),A
        LD      A,H
        OUT     (VTAC+13),A
        OUT     (VTAC+6),A ;SET CURSOR AT BOTTOM.
        RET

;       TURN THE CURSOR OFF.
;       THE LOGICAL ADDRESS OF THE CURSOR IS RETURN;
;       ED IN REG HL.
CUROFF: IN      A,(VTAC+9) ;READ COL. REGISTER
        LD      L,A     ;TO REG L
        IN      A,(VTAC+8) ;READ ROW REGISTER
        LD      H,A     ;TO REG H
        LD      A,0FFH  ;TURN CURSOR OFF
        OUT     (VTAC+ 12),A
        RET

;       CONVERT A LOGICAL ADDRESS TO A PHYSICAL
;       ADDRESS.
;       ON ENTRY, HL CONTAINS THE LOGICAL ADDRESS.
;       ON EXIT, HL CONTAINS THE PHYSICAL ADDRESS.
;       A LOGICAL ADDRESS IS IN THE FORM (ROW,
;       COLUMN) WITH ROW IN REG H AND COLUMN
;       IN REG L. ROW MUST BE IN THE RANGE OF 0
;       TO NROWS-1 AND COLUMN MUST BE IN
;       THE RANGE OF 0 TO NCOLS-1.
;       A PHYSICAL ADDRESS IS THE ACTUAL MEMORY
;       ADDRESS IN THE VIDEO MEMORY.
GETBA:  PUSH    DE
        PUSH    BC
        EX      DE,HL
        INC     D
        LD      B,0
        LD      C,(IX)  ;NCOLS
        LD      HL,0
        OR      A       ;CLEAR CARRY
        SBC     HL,BC   ;BC=NCOLS,HL=-NCOLS
GETBA2: ADD     HL,BC
        DEC     D
        JR      NZ,GETBA2
        ADD     HL,DE
        LD      DE,VIDEO
        ADD     HL,DE
        POP     BC
        POP     DE
        RET
    
;       VIDEO BOARD DRIVER
;       THIS SUBROUTINE FACILITATES THE
;       USE OF THE SSM VB1B/C VIDEO BOARD
;       AND A VIDEO DISPLAY DEVICE AS A
;       CONSOLE OUTPUT DEVICE.
;       ASCII CHARACTERS PRESENTED TO THE
;       SUBROUTINE IN THE C REGISTER ARE
;       DISPLAYED ON THE SCREEN. CERTAIN
;       CHARACTERS, LISTED BELOW, RECEIVE
;       SPECIAL TREATMENT. ALL REGISTERS
;       ARE PRESERVED BY THIS SUBROUTINE.
;       VID IS THE BEGINNING ADDRESS ASSIGNED
;       TO THE DISPLAY RAM LOCATED ON THE VB1
;       BOARD.

VID     EQU     0E000H  ;START OF VIDEO

;       THREE BYTES OF RAM ARE REQUIRED FOR
;       HOUSEKEEPING. THESE BYTES MUST BE
;       IN AN AREA UNUSED BY OTHER PROGRAMS.
;       MAIN ENTRY POINT
;       THIS ENTRY POINT MAY BE USED IF
;       THE CURSOR POINTER AND CHARACTER
;       HOLD ARE AT LOCATIONS SPECIFIED
;       IN REGISTERS H & L.
;       THE USER MUST SUPPLY SUBROUTINE
;       ENTRY CODE AS FOLLOWS:

;ENTR:  PUSH    HL      ;SAVE HL
;       LD      HL,PNTR ;ADDR OF CURSOR PNTR
;       JP      VDTTY   ;JOIN THIS CODE
VDTTY:  PUSH    DE      ;SAVE DE
        PUSH    BC      ;SAVE BC
        LD      E,(HL)  ;LPTR
        INC     HL      ;
        LD      A,(HL)  ;HPTR
        AND     3       ;CONVERT TO VIDEO
;        ADD     A,VID SHR 8 ;RAM ADDRESS
        ADD     A,VID >> 8 ;RAM ADDRESS
        LD      D,A     ;
        INC     HL      ;
        LD      B,(HL)  ;CHAR UNDER CURSOR
        EX      DE,HL   ;PNTR TO HL
        LD      (HL),B  ;RESTORE PREV CHAR
 
;           IDENTIFY INPUT CHAR
        LD      A,C     ;NEW CHAR
        CP      FF      ;
        JP      Z,VIDFF ;FORM FEED
        CP      CR      ;
        JP      Z,VIDCR ;CARRIAGE RETURN
        CP      LF      ;
        JP      Z,VIDLF ;LINE FEED
        CP      BS      ;
        JP      Z,CRLT  ;BACKSPACE
        LD      (HL),C  ;
CRRT:   LD      BC,1

;       ADJUST CURSOR POINTER
CRADJ:  ADD     HL,BC   ;
;       CHECK FOR OVERFLOW
        LD      A,H     ;
;        CP      (VID+1024) SHR 8
        CP      (VID+1024) >> 8
        JP      NZ,VIDRT
        LD      HL,VID+960
        CALL    ROLL0
        JR      VIDR1   ;

;       COMMON EXIT CODE
;       NORMALIZE CURSOR POINTER
;VIDRT:  LD      H,VID+960 SHR 8
VIDRT:  LD      H,VID+960 >> 8
        LD      A,L     ;
        OR      0C0H
        LD      L,A     ;
VIDR1:  LD      A,(HL)  ;CHAR UNDER CURSOR
        LD      (HL),7FH ;CURSOR
        EX      DE,HL   ;PNTR TO DE
        LD      (HL),A  ;CHAR UNDER CURSOR
        DEC     HL      ;
        LD      (HL),D  ;HPTR
        DEC     HL      ;
        LD      (HL),E  ;LPTR

;       RESTORE REGISTERS, EXIT
        POP     BC      ;
        POP     DE      ;
        POP     HL      ;
        LD      A,C     ;
        OR      A
        RET

;       PROCESS BACKSPACE
;       MOVE CURSOR LEFT ONE CHARACTER.
CRLT:   LD      BC,-1
        JP      CRADJ

;       PROCESS FORM FEED
;       FILL SCREEN WITH SPACES,
;       LDE CURSOR TO TOP LEFT
VIDFF:  LD      HL,VID  ;
        PUSH    HL      ;
VIDFC:  LD      (HL),' ' ;
        INC     HL      ;
        LD      A,H     ;
;        CP      (VID+1024) SHR 8 ;
        CP      (VID+1024) >> 8 ;
        JR      C,VIDFC ;
        POP     HL      ;

;       PROCESS CARRIAGE RETURN
;       MOVE CURSOR TO BEGINNING
;       OF LINE
VIDCR:  LD      A,L     ;
        AND     0C0H    ;
        LD      L,A     ;
        JP      VIDRT   ;

;       PROCESS LINE FEED
;       MOVE CURSOR DOWN ONE LINE,
;       FILL NEW LINE WITH SPACES
VIDLF:  PUSH    DE      ;
        LD      DE,64   ;
        ADD     HL,DE   ;
        LD      A,H     ;
;        CP      (VID+1024) SHR 8
        CP      (VID+1024) >> 8
        POP     DE      ;
        JP      NZ,VIDRT    ;

;       ROLL THE WHOLE DISPLAY UP ONE
;       LINE.
        CALL    ROLL0
        LD      A,L     ;
        OR      0C0H    ;
        LD      L,A     ;
;        LD      H,VID+960 SHR 8
        LD      H,VID+960 >> 8
        JP      VIDRT   ;

;       ROLL SUBROUTINE
ROLL0:  PUSH    DE
        PUSH    HL
        LD      DE,VID  ;
        LD      HL,VID+64;
ROLL1:  LD      A,(HL)
        LD      (DE),A  ;
        LD      (HL),' ' ;
        INC     DE
        INC     HL      ;
        LD      A,H     ;
;        CP      (VID+1024) SHR 8
        CP      (VID+1024) >> 8
        JR      NZ,ROLL1    ;
        POP     HL      ;
ROLL2:  POP     DE      ;
        RET

        END
