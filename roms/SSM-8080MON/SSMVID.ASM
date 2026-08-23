        .8080
;       VIDEO BOARD DRIVER


;       THIS SUBROUTINE FACILITATES THE USE
;       OF THE SOLID STATE MUSIC VBL BOARD
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


VDTTY:  PUSH    H       ;SAVE HL
        LXI     H,VDPTR ;ADDR OF CURSOR POINTER

;       ALTERNATE ENTRY POINT

;       THIS ENTRY POINT T1AY BE USED IF
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
        ADI     VID SHR 8 ;RAM ADDRESS
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
        CPI     (VID+1024) SHR 8 ;
        JNZ     VIDRT   ;
        LXI     H,VID+960 ;
        CALL    ROLL0   ;
        JMP     VIDR1   ;

;       COMMON EXIT CODE
;       NORMALIZE CURSOR POINTER

VIDRT:  MVI     H,(VID+960) SHR 8 ;
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
;       MOVE CURSOR TO TOP LEFT

VIDFF:  LXI     H,VID   ;
        PUSH    H       ;
VIDFC:  MVI     M,' '   ;
        INX     H       ;
        MOV     A,H     ;
        CPI     (VID+1024) SHR 8 ;
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
        CPI     (VID + 1024) SHR 8 ;
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
        MVI     H,(VID+960) SHR 8 ;
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
        CPI     (VID+1024) SHR 8 ;
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
        MVI     A,(VID+960) AND 0FFH
        SUB     E       ;
        MOV     E,A     ;
        MVI     A,(VID+960) SHR 8
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
