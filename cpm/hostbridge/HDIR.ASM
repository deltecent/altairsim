;-----------------------------------------------------------------------
; HDIR.COM -- list the HOST directory.
;
;       HDIR                    everything in hostdir
;       HDIR *.ASM              only what matches
;       HDIR SRC/               inside a subdirectory of hostdir
;
; Part of the altairsim Host Bridge (src/boards/hostbridge.h,
; docs/boards/hostbridge.md). Assemble it INSIDE the machine:
;
;       ASM HDIR                -> HDIR.HEX
;       LOAD HDIR               -> HDIR.COM
;
; THIS ONE HAS NO ANCESTOR. AltairZ80 ships an R and a W and nothing that
; shows you what is there to read -- so you type a host name from memory,
; get it wrong, and cannot tell whether the file is missing or your spelling
; is. HDIR is the answer to "what can I say to R".
;
;-----------------------------------------------------------------------
; IT PRINTS TRUE HOST NAMES, AND THAT IS THE POINT OF IT.
;
; R and W have to squeeze a host name into an 8.3 FCB -- `my-notes(2).txt`
; comes across as `MYNOTES2.TXT`, and a name too long or too strange to map
; is reported and skipped. HDIR does no such thing: it prints what is
; actually on the host disk, in its real case, at its real length. So when
; R says it cannot name something, you can still see what it was.
;
; This program touches NO disk at all -- no BDOS file calls, no FCB. It is
; the console and the two bridge ports, and nothing else.
;-----------------------------------------------------------------------

        ORG     100H

;---- CP/M ------------------------------------------------------------
BDOS    EQU     0005H
TAIL    EQU     0080H           ; the command tail: length, then the text.
                                ; The CCP has already FOLDED IT TO UPPER
                                ; CASE. The card folds host names to match
                                ; when it looks for them -- but what it
                                ; PRINTS is always the true name.

CONOUT  EQU     2               ; E = char
PRSTR   EQU     9               ; DE = string, '$'-terminated

HNMAX   EQU     64              ; a host name may be a PATH, so it is not 8.3
IDMAX   EQU     32              ; room for the card's signature
SZMAX   EQU     20              ; a host size in decimal (a uint64 is <= 20 digits)
DTMAX   EQU     12              ; room for a date field ("MM/DD/YY" and then some)
SIZEW   EQU     8               ; the size column, right-justified to this width
CR      EQU     0DH
LF      EQU     0AH

;---- The Host Bridge (see HB.INC -- ASM.COM has no INCLUDE) -----------
HBCMD   EQU     0B0H            ; OUT = command   IN = status
HBDATA  EQU     0B1H            ; data, both ways

HEOF    EQU     08H             ; the listing has run out
HERR    EQU     10H             ; the last operation failed

CIDENT  EQU     0
CDIRL   EQU     9               ; start the LONG listing: name<0> size<0> date<0>
CDIRN   EQU     5               ; then each next entry; HEOF when it runs out
CERROR  EQU     7
CRESET  EQU     8

;=======================================================================
; MAIN
;=======================================================================
START:  LXI     SP,STACK

        MVI     A,CRESET        ; ONE byte, not the 128 AltairZ80's utilities
        OUT     HBCMD           ; send. This card has no undefined state.

        CALL    IDENT           ; is there even a card there? (exits if not)
        CALL    PARSE           ; the tail -> PAT (which may be empty, and an
                                ; EMPTY PATTERN MEANS EVERYTHING -- the card
                                ; reads it as `*`, so there is no special case
                                ; here for the bare `HDIR`)

        LXI     H,0             ; a 16-bit tally: a host directory is not a
        SHLD    COUNT           ; CP/M one and can easily hold 300 files

        MVI     A,CDIRL         ; the LONG listing -- each entry is three fields
        OUT     HBCMD
        LXI     H,PAT
        CALL    SENDZ           ; the NUL is what commits it

;---- the listing loop -------------------------------------------------
DIR1:   IN      HBCMD
        ANI     HERR
        JNZ     DIRBAD          ; a bad pattern, or a hostdir that is not there

        IN      HBCMD
        ANI     HEOF
        JNZ     DIRE            ; the list ran out -- and an EMPTY list lands
                                ; here on the FIRST pass, which is why "no
                                ; matches" needs no special case either

; Each entry streams THREE NUL-terminated fields, in this order: the name, its
; size ("<DIR>" for a directory, else a decimal byte count) and its date.
        LXI     H,NAME
        MVI     B,HNMAX
        CALL    RECVZ
        LXI     H,SIZE
        MVI     B,SZMAX
        CALL    RECVZ
        LXI     H,DATE
        MVI     B,DTMAX
        CALL    RECVZ

        CALL    SHOW            ; one line: <size>  <date>  <name>

        LHLD    COUNT
        INX     H
        SHLD    COUNT

        MVI     A,CDIRN
        OUT     HBCMD
        JMP     DIR1

;---- and the tally ----------------------------------------------------
DIRE:   LHLD    COUNT
        MOV     A,H
        ORA     L
        JZ      DIRNON

        CALL    CRLF
        LHLD    COUNT
        CALL    PRDEC
        LXI     D,MFILES
        MVI     C,PRSTR
        CALL    BDOS
        JMP     0               ; warm boot

DIRNON: LXI     D,MNONE
        MVI     C,PRSTR
        CALL    BDOS
        JMP     0

DIRBAD: CALL    REPERR
        JMP     0

;-----------------------------------------------------------------------
; SHOW -- print one entry as a line:  <size>  <date>  <name>
;
;       1792  07/14/26  R.COM
;      <DIR>  07/20/26  SRC/
;
; The size is right-justified in an 8-wide field so the numbers line up; the
; date is the card's own MM/DD/YY, already 8 characters; the name comes last,
; where a long one can run to any length without pushing anything out of true.
;
; A DIRECTORY COMES BACK WITH A TRAILING '/' on the name (and "<DIR>" for its
; size), and we leave both on: that is the entire difference between "you can R
; this" and "you cannot", and hiding it to tidy the listing would be hiding the
; one thing the user needs to know.
;-----------------------------------------------------------------------
SHOW:   LXI     H,SIZE          ; the size, right-justified into the column
        MVI     C,SIZEW
        CALL    RJUST
        CALL    SPC2            ; two spaces between the columns
        LXI     H,DATE          ; the date -- already MM/DD/YY
        CALL    PSTR
        CALL    SPC2
        LXI     H,NAME          ; and the name, at its real length
        CALL    PSTR
        JMP     CRLF            ; tail call: end the line

;-----------------------------------------------------------------------
; RJUST -- print the NUL-terminated string at HL right-justified in a field
;          C characters wide: emit (C - length) leading spaces, then the
;          string. A string as wide as the field, or wider, prints unpadded.
;-----------------------------------------------------------------------
RJUST:  PUSH    H
        CALL    STRLEN          ; B = length (and STRLEN walked HL to the NUL)
        POP     H
        MOV     A,C
        SUB     B               ; A = spaces owed
        JC      RJ2             ; wider than the field: no padding
        JZ      RJ2
        MOV     B,A
RJ1:    MVI     A,' '
        PUSH    B
        PUSH    H
        CALL    PUTCH           ; BDOS eats every register, so save B and H
        POP     H
        POP     B
        DCR     B
        JNZ     RJ1
RJ2:    JMP     PSTR            ; tail call: the string itself

;-----------------------------------------------------------------------
; SPC2 -- two spaces, the gap between columns.
;-----------------------------------------------------------------------
SPC2:   MVI     A,' '
        CALL    PUTCH
        MVI     A,' '
        JMP     PUTCH           ; tail call

;-----------------------------------------------------------------------
; STRLEN -- B = the length of the NUL-terminated string at HL.
;-----------------------------------------------------------------------
STRLEN: MVI     B,0
SL1:    MOV     A,M
        ORA     A
        RZ
        INR     B
        INX     H
        JMP     SL1

;=======================================================================
; THE HOST BRIDGE
;=======================================================================

;-----------------------------------------------------------------------
; IDENT -- prove there is a card there before we do anything else.
;
; THIS IS NOT CEREMONY. An IN from a port that no card decodes FLOATS THE
; BUS and the 8080 reads 0FFH -- which has both HEOF and HERR set. Without
; this check, HDIR on a machine with no bridge in it would print a host
; error, or an empty listing, and either one is a lie about your disk.
;-----------------------------------------------------------------------
IDENT:  MVI     A,CIDENT
        OUT     HBCMD

        LXI     H,IDBUF
        MVI     B,IDMAX
ID1:    IN      HBDATA
        MOV     M,A
        ORA     A
        JZ      ID2             ; got the terminator
        INX     H
        DCR     B
        JNZ     ID1
        MVI     M,0             ; never terminated -- 0FFH forever, i.e. no card

ID2:    LXI     H,IDBUF
        LXI     D,IDWANT
ID3:    LDAX    D
        ORA     A
        RZ                      ; matched to the end of what we wanted: ours.
                                ; A PREFIX compare, so a later card that says
                                ; "... 2" still works with this .COM
        CMP     M
        JNZ     IDBAD
        INX     H
        INX     D
        JMP     ID3

IDBAD:  LXI     D,MNOHB
        MVI     C,PRSTR
        CALL    BDOS
        JMP     0               ; nothing else in this program can work

;-----------------------------------------------------------------------
; SENDZ -- send the NUL-terminated string at HL to the data port.
; The NUL goes too, AND IT IS THE NUL THAT COMMITS THE COMMAND.
;-----------------------------------------------------------------------
SENDZ:  MOV     A,M
        OUT     HBDATA
        ORA     A
        RZ
        INX     H
        JMP     SENDZ

;-----------------------------------------------------------------------
; RECVZ -- read a NUL-terminated string from the data port into HL.
;          B = the room available, including the terminator.
;-----------------------------------------------------------------------
RECVZ:  IN      HBDATA
        MOV     M,A
        ORA     A
        RZ
        INX     H
        DCR     B
        JNZ     RECVZ
        MVI     M,0             ; out of room -- truncate rather than run on
        RET

;-----------------------------------------------------------------------
; REPERR -- print what actually went wrong, in the host's own words.
;-----------------------------------------------------------------------
REPERR: MVI     A,CERROR
        OUT     HBCMD
        IN      HBDATA          ; the code byte -- read it and discard it

        LXI     D,MERR
        MVI     C,PRSTR
        CALL    BDOS

RPE1:   IN      HBDATA          ; ...then the text, a character at a time
        ORA     A
        JZ      RPE2
        MOV     E,A
        MVI     C,CONOUT
        CALL    BDOS
        JMP     RPE1

RPE2:   JMP     CRLF            ; tail call

;=======================================================================
; THE COMMAND TAIL
;=======================================================================

;-----------------------------------------------------------------------
; PARSE -- the first word of the tail at 0080H -> PAT.
;
; We parse it ourselves rather than use the FCB the CCP built at 005CH,
; and we have to: that is an 8.3 FCB parse, and it would hand us `SRC/`
; as `SRC     ` with the slash eaten and the pattern gone. A host pattern
; is not an 8.3 name and must never be put through one.
;-----------------------------------------------------------------------
PARSE:  LXI     H,TAIL
        MOV     B,M             ; the length of the tail
        INX     H

        CALL    SKIPSP
        LXI     D,PAT
        MVI     C,HNMAX
        CALL    TOKEN
        RET

SKIPSP: MOV     A,B
        ORA     A
        RZ
        MOV     A,M
        CPI     ' '
        RNZ
        INX     H
        DCR     B
        JMP     SKIPSP

; Copy one space-delimited word from (HL) to (DE), NUL-terminated.
;   B = characters left in the tail   C = room at DE, including the NUL
; Always writes the NUL, so "no word here" is an empty string -- which is
; what the card wants anyway.
TOKEN:  MOV     A,C
        CPI     1
        JZ      TOKEND
        MOV     A,B
        ORA     A
        JZ      TOKEND
        MOV     A,M
        CPI     ' '
        JZ      TOKEND
        STAX    D
        INX     H
        INX     D
        DCR     B
        DCR     C
        JMP     TOKEN
TOKEND: XRA     A
        STAX    D
        RET

;=======================================================================
; TALKING TO THE USER
;=======================================================================

; PSTR -- print the NUL-terminated string at HL.
; HL is pushed around the BDOS call because BDOS EATS EVERY REGISTER.
PSTR:   MOV     A,M
        ORA     A
        RZ
        PUSH    H
        MOV     E,A
        MVI     C,CONOUT
        CALL    BDOS
        POP     H
        INX     H
        JMP     PSTR

PUTCH:  MOV     E,A
        MVI     C,CONOUT
        JMP     BDOS

CRLF:   LXI     D,MCRLF
        MVI     C,PRSTR
        JMP     BDOS

;-----------------------------------------------------------------------
; PRDEC -- print HL in decimal. Built backwards from the end of a buffer,
; so no digit reversal is needed.
;-----------------------------------------------------------------------
PRDEC:  LXI     D,NUMEND
        MVI     A,'$'
        STAX    D
PD1:    DCX     D
        CALL    DIV10           ; HL = HL/10, A = the remainder
        ADI     '0'
        STAX    D
        MOV     A,H
        ORA     L
        JNZ     PD1
        MVI     C,PRSTR
        JMP     BDOS            ; DE already points at the first digit

; DIV10 -- HL = HL/10, A = the remainder. The 8080 has no divide.
DIV10:  PUSH    B
        LXI     B,0
DV1:    MOV     A,H
        ORA     A
        JNZ     DV2
        MOV     A,L
        CPI     10
        JC      DV3
DV2:    MOV     A,L
        SUI     10
        MOV     L,A
        MOV     A,H
        SBI     0
        MOV     H,A
        INX     B
        JMP     DV1
DV3:    MOV     A,L             ; the remainder -- grab it BEFORE clobbering L
        MOV     H,B
        MOV     L,C
        POP     B
        RET

;=======================================================================
; DATA
;=======================================================================
IDWANT: DB      'ALTAIRSIM HOSTBRIDGE',0

MNOHB:  DB      CR,LF,'HDIR: no host bridge at B0H.',CR,LF
        DB      'This machine has no `hostbridge` card in it, or it is at'
        DB      ' another port.',CR,LF,'$'

MERR:   DB      'HDIR: $'
MCRLF:  DB      CR,LF,'$'
MFILES: DB      ' files.',CR,LF,'$'
MNONE:  DB      'HDIR: nothing matches.',CR,LF,'$'

;---- Variables -------------------------------------------------------
PAT:    DS      HNMAX+1         ; the pattern, as typed. Empty = everything.
NAME:   DS      HNMAX+1         ; the name the card just handed us. +1: RECVZ's
                                ; truncate path writes its NUL one past the
                                ; count it was given.
SIZE:   DS      SZMAX+1         ; its size, as decimal text (or "<DIR>")
DATE:   DS      DTMAX+1         ; its date, MM/DD/YY
IDBUF:  DS      IDMAX+1
COUNT:  DS      2               ; how many names we have printed
NUMBUF: DS      6
NUMEND: DS      1               ; PRDEC builds backwards from here

        DS      64              ; stack: BDOS is the deepest thing we call
STACK   EQU     $

        END     START
