;-----------------------------------------------------------------------
; R.COM -- copy a file from the HOST into CP/M.
;
;       R <hostfile> [cpmfile]
;       R <pattern>                     e.g.  R *.ASM     R SRC/*.MAC
;
; Part of the altairsim Host Bridge (src/boards/hostbridge.h,
; docs/boards/hostbridge.md). Assemble it INSIDE the machine:
;
;       ASM R                   -> R.HEX
;       LOAD R                  -> R.COM
;
; THE NAME IS AltairZ80'S. THE CODE IS NOT.
; AltairZ80 has an R.COM and a W.COM, and the muscle memory is worth
; keeping, so ours are called the same thing. Nothing else is shared: that
; R.COM talks to a "SIMH pseudo device" at port 0FEH, which in a real
; Altair is the 88-VI/RTC's control register, and it was written in SPL.
; This one talks to a card of our own at 0B0H and is 8080 assembler.
; AltairZ80's R.COM will not run here, and this one will not run there.
;
;-----------------------------------------------------------------------
; EVERY DISK OPERATION IN THIS PROGRAM IS A BDOS CALL, AND THAT IS THE
; WHOLE DESIGN.
;
; No BIOS entry points. No IN or OUT to a disk controller. No assumptions
; about a DPB, a sector size, a skew table, or which card the drive is on.
; The only ports this program touches are the Host Bridge's own two.
;
; That is what makes R.COM portable across every CP/M and every disk
; device in the machine -- an 88-DCDD 8", an 88-MDS minidisk, an 8 MB
; image, a Tarbell if one is ever built, and any BIOS anybody writes
; later. The utility knows about FILES; CP/M knows about DISKS; the bridge
; knows about the HOST. No layer reaches past the next one.
;
; AND BDOS CLOBBERS EVERY REGISTER (A,B,C,D,E,H,L -- only SP survives).
; That is not a warning, it is the single most common way 8080 CP/M code
; goes wrong. Nothing may be left live in a register across CALL BDOS.
;-----------------------------------------------------------------------

        ORG     100H

;---- CP/M ------------------------------------------------------------
BDOS    EQU     0005H           ; ALWAYS here. Never call the BIOS directly.
TAIL    EQU     0080H           ; the command tail: length, then the text
                                ; ...and the CCP has already FOLDED IT TO
                                ; UPPER CASE. We cannot see lower case, ever.
                                ; The card knows, and folds host names to
                                ; match -- so `R readme.txt` still works.

CONOUT  EQU     2               ; E = char
PRSTR   EQU     9               ; DE = string, '$'-terminated
FCLOSE  EQU     16
FDELET  EQU     19
FWRITE  EQU     21              ; write sequential
FMAKE   EQU     22
FSETDMA EQU     26

RECLEN  EQU     128             ; a CP/M record. Everything is a multiple.
HNMAX   EQU     64              ; a host name may be a PATH, so it is not 8.3
IDMAX   EQU     32              ; room for the card's signature
CPMEOF  EQU     1AH             ; ^Z
CR      EQU     0DH
LF      EQU     0AH

;---- The Host Bridge (see HB.INC -- ASM.COM has no INCLUDE) -----------
HBCMD   EQU     0B0H            ; OUT = command   IN = status
HBDATA  EQU     0B1H            ; data, both ways

DAV     EQU     02H             ; a byte is waiting
HEOF    EQU     08H             ; the stream has run dry
HERR    EQU     10H             ; the last operation failed

CIDENT  EQU     0
CVOPENR EQU     1
CDIRF   EQU     4
CDIRN   EQU     5
CERROR  EQU     7
CRESET  EQU     8

;=======================================================================
; MAIN
;=======================================================================
START:  LXI     SP,STACK

; RESET first. Not the 128-times-in-a-row ritual AltairZ80's utilities
; need -- ONE byte, because this card has no undefined state to climb out
; of. It costs nothing and it means we start from a known place even if
; the last program to run left a transfer half-finished.
        MVI     A,CRESET
        OUT     HBCMD

        CALL    IDENT           ; is there even a card there? (exits if not)
        CALL    PARSE           ; command tail -> HNAME, CNAME

        LDA     HNAME
        ORA     A
        JZ      USAGE           ; no arguments at all

        CALL    HASWLD          ; does the host name contain * or ? ...
        ORA     A
        JNZ     MANY            ; ...yes: walk the host directory
        CALL    ONE             ; ...no: just the one file
        JMP     EXIT

;-----------------------------------------------------------------------
; MANY -- a wildcard. Walk the matches, copying each.
;
; THE ENUMERATION AND THE TRANSFERS INTERLEAVE, and the card is built for
; it: DIR_FIRST/DIR_NEXT keep their place across the OPEN_READ that
; happens between them. The alternative -- buffering every matching name
; up front -- would cost real memory in a TPA for no reason.
;-----------------------------------------------------------------------
MANY:   LDA     CNAME
        ORA     A
        JNZ     EWILD           ; "R *.ASM FOO.ASM" cannot mean anything

        MVI     A,CDIRF
        OUT     HBCMD
        LXI     H,HNAME         ; the pattern...
        CALL    SENDZ           ; ...NUL-terminated. The NUL commits it.

MANY1:  IN      HBCMD
        ANI     HERR
        JNZ     MANYE           ; the host said no (a bad pattern, a bad dir)

        IN      HBCMD
        ANI     HEOF
        JNZ     EXIT            ; the list ran out -- and an EMPTY list lands
                                ; here on the FIRST pass, which is why there
                                ; is no special case for "no matches"

        LXI     H,HNAME         ; overwrite the pattern with the match; we do
        MVI     B,HNMAX         ; not need it again, the card is holding it
        CALL    RECVZ

        CALL    ISDIR           ; a subdirectory comes back marked with a '/'
        JZ      MANY2           ; ...and is not a file to copy

        CALL    ONE

MANY2:  MVI     A,CDIRN
        OUT     HBCMD
        JMP     MANY1

MANYE:  CALL    REPERR
        JMP     EXIT

EXIT:   JMP     0               ; warm boot

;=======================================================================
; ONE -- copy the host file named in HNAME into CP/M.
;
; Called once for a plain transfer, and once per match for a wildcard.
;=======================================================================
ONE:    LXI     H,0
        SHLD    RECS            ; records written, for the report at the end

; OPEN THE HOST FILE FIRST, BEFORE TOUCHING THE CP/M DIRECTORY.
;
; The order is load-bearing. Creating the CP/M file first would mean that
; `R TYPPO.ASM` -- a name that does not exist on the host -- DELETES the
; CP/M file of the same name (see FDELET below) and leaves an empty one in
; its place. Fail first, destroy nothing.
        MVI     A,CVOPENR
        OUT     HBCMD
        LXI     H,HNAME
        CALL    SENDZ

        IN      HBCMD
        ANI     HERR
        JNZ     ONEBAD          ; no such host file, outside the sandbox, ...

;---- Work out what to call it on this side ----------------------------
        LDA     CNAME
        ORA     A
        JZ      ONEDER          ; no second argument -> derive the name
        LXI     H,CNAME         ; the user said what to call it
        JMP     ONEFCB
ONEDER: CALL    BASENM          ; SRC/FOO.ASM -> FOO.ASM: a CP/M file has no
                                ; directory to live in, so only the last
                                ; component can survive the trip
ONEFCB: CALL    MKFCB

        CALL    SAYXFR          ; "R: SRC/FOO.ASM -> FOO.ASM"

;---- Make the CP/M file ----------------------------------------------
; Delete any existing one first. BDOS MAKE on a name that already exists
; creates a SECOND directory entry for it -- two files, same name, and the
; disk is quietly corrupt. Deleting first is not politeness, it is the
; documented way to avoid that.
        LXI     D,FCB
        MVI     C,FDELET
        CALL    BDOS            ; 0FFH = "there wasn't one", which is fine

        LXI     D,FCB
        MVI     C,FMAKE
        CALL    BDOS
        CPI     0FFH
        JZ      ENODIR          ; directory full

        LXI     D,BUF
        MVI     C,FSETDMA
        CALL    BDOS            ; where BDOS reads each record from

        CALL    COPY            ; <-- the actual transfer
        RC                      ; carry = the copy failed and has reported it

        LXI     D,FCB
        MVI     C,FCLOSE
        CALL    BDOS
        CPI     0FFH
        JZ      ECLOSE

        CALL    SAYREC          ; "  3 records"
        RET

ONEBAD: CALL    REPERR
        RET

;-----------------------------------------------------------------------
; COPY -- host stream -> CP/M records. Returns CY on failure.
;
; NO POLLING LOOP, AND THAT IS NOT AN OVERSIGHT. The card never makes you
; wait: it read the whole file into its own buffer when OPEN_READ
; committed, so every byte is already there. DAV clear means END OF DATA,
; not "not yet" -- so `ANI DAV / JZ` is a complete and correct test, and a
; wait loop around it would spin forever on the last byte.
;-----------------------------------------------------------------------
COPY:   IN      HBCMD
        ANI     DAV
        RZ                      ; nothing (left) at all -- done, no carry.
                                ; An EMPTY host file lands here immediately
                                ; and produces a zero-record CP/M file, which
                                ; is right: it is empty too.

        LXI     H,BUF           ; start a record
        MVI     B,RECLEN

CP1:    IN      HBCMD
        ANI     DAV
        JZ      CPPAD           ; ran out mid-record -> pad the rest
        IN      HBDATA
        MOV     M,A
        INX     H
        DCR     B
        JNZ     CP1
        JMP     CPWRIT          ; a full 128 bytes

; PAD THE TAIL OF THE LAST RECORD WITH ^Z.
;
; CP/M stores whole 128-byte records and keeps NO BYTE COUNT, so a file's
; true length is simply not representable -- the last record is always
; full of something. ^Z is what every CP/M program of the era expects to
; find there, and it is what TYPE and ED stop at.
;
; This is also the exact reason W.COM has a T switch: coming back the
; other way, those pad bytes are indistinguishable from real data, and
; only the human knows whether the file was text.
CPPAD:  MVI     A,CPMEOF
CPPAD1: MOV     M,A
        INX     H
        DCR     B
        JNZ     CPPAD1

CPWRIT: LXI     D,FCB
        MVI     C,FWRITE
        CALL    BDOS
        ORA     A
        JNZ     EDISK           ; out of space, or a read-only disk

        LHLD    RECS
        INX     H
        SHLD    RECS
        JMP     COPY            ; ...and around for the next record

;=======================================================================
; THE HOST BRIDGE
;=======================================================================

;-----------------------------------------------------------------------
; IDENT -- prove there is a card there before we do anything else.
;
; THIS IS NOT CEREMONY. An IN from a port that no card decodes FLOATS THE
; BUS, and the 8080 reads 0FFH. Without this check, R on a machine with no
; bridge in it would read 0FFH forever: 0FFH has DAV set, so COPY would
; happily write a disk full of 0FFH bytes and call it a file.
;
; A program that cannot tell "the card said 0FFH" from "there is no card"
; must ask a question only a real card can answer. This is that question.
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
        RZ                      ; matched all the way to the end: it is ours
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
;
; The NUL goes too, AND IT IS THE NUL THAT COMMITS THE COMMAND. Until it
; lands, the card has opened nothing and looked nothing up, and we could
; still change our mind by writing a different command byte.
;-----------------------------------------------------------------------
SENDZ:  MOV     A,M
        OUT     HBDATA
        ORA     A
        RZ                      ; that was the terminator: done
        INX     H
        JMP     SENDZ

;-----------------------------------------------------------------------
; RECVZ -- read a NUL-terminated string from the data port into HL.
;          B = room available, including the terminator.
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
; REPERR -- print what actually went wrong.
;
; The card hands back a code byte and then a MESSAGE, in the host's own
; words ("FOO.ASM: no such file or directory"). We print the message and
; drop the code: a number the user has to go and look up is not an error
; report. The code is there for a program that wants to branch on it.
;-----------------------------------------------------------------------
REPERR: MVI     A,CERROR
        OUT     HBCMD
        IN      HBDATA          ; the code byte -- read it and discard it

        LXI     D,MERR
        MVI     C,PRSTR
        CALL    BDOS

RPE1:   IN      HBDATA          ; ...then the text, a character at a time.
        ORA     A               ; Nothing is live across the BDOS call below,
        JZ      RPE2            ; because the next byte comes from the PORT.
        MOV     E,A
        MVI     C,CONOUT
        CALL    BDOS
        JMP     RPE1

RPE2:   LXI     D,MCRLF
        MVI     C,PRSTR
        JMP     BDOS            ; tail call: BDOS returns to OUR caller

;=======================================================================
; THE COMMAND TAIL
;=======================================================================

;-----------------------------------------------------------------------
; PARSE -- pull two words out of the command tail at 0080H.
;
; WE PARSE IT OURSELVES rather than use the FCBs the CCP already built at
; 005CH and 006CH. We have to: the CCP's parse is an 8.3 FCB parse, and it
; would take `readme.markdown` and hand us `README  MAR` -- a host name
; mangled beyond recovery before our first instruction ran.
;
; What we CANNOT undo is the upper-casing: the CCP folds the whole tail
; before we see it. The card compensates (it folds host names when looking
; for a match), which is why there is no `L` switch here. AltairZ80's READ
; has one, and it puts the burden on the human to remember which of their
; files are in which case.
;-----------------------------------------------------------------------
PARSE:  LXI     H,TAIL
        MOV     B,M             ; length of the tail
        INX     H               ; -> first character

        CALL    SKIPSP
        LXI     D,HNAME
        CALL    TOKEN

        CALL    SKIPSP
        LXI     D,CNAME
        CALL    TOKEN
        RET

SKIPSP: MOV     A,B
        ORA     A
        RZ                      ; nothing left
        MOV     A,M
        CPI     ' '
        RNZ
        INX     H
        DCR     B
        JMP     SKIPSP

; Copy one space-delimited word from (HL) to (DE), NUL-terminated.
; Always writes the NUL -- so "no word here" comes out as an empty string,
; which is what the callers test for.
TOKEN:  MOV     A,B
        ORA     A
        JZ      TOKEND
        MOV     A,M
        CPI     ' '
        JZ      TOKEND
        STAX    D
        INX     H
        INX     D
        DCR     B
        JMP     TOKEN
TOKEND: XRA     A
        STAX    D
        RET

;-----------------------------------------------------------------------
; HASWLD -- A=1 if HNAME contains * or ?, else A=0.
;-----------------------------------------------------------------------
HASWLD: LXI     H,HNAME
HW1:    MOV     A,M
        ORA     A
        JZ      HWNO
        CPI     '*'
        JZ      HWYES
        CPI     '?'
        JZ      HWYES
        INX     H
        JMP     HW1
HWNO:   XRA     A
        RET
HWYES:  MVI     A,1
        RET

;-----------------------------------------------------------------------
; ISDIR -- Z if HNAME ends in '/', i.e. the card says it is a directory.
;
; HDIR shows directories, and a wildcard can match one. R must not try to
; open it: the card would refuse, and the user would get a puzzling error
; instead of the obvious "that is a folder, not a file".
;-----------------------------------------------------------------------
ISDIR:  LXI     H,HNAME
        MOV     A,M
        ORA     A
        RNZ                     ; empty name: not a directory (and NZ)
IS1:    MOV     A,M
        ORA     A
        JZ      IS2
        INX     H
        JMP     IS1
IS2:    DCX     H               ; -> the last character
        MOV     A,M
        CPI     '/'             ; Z if it IS a directory
        RET

;-----------------------------------------------------------------------
; BASENM -- HL = the last component of HNAME.
;
; SRC/FOO.ASM -> FOO.ASM. A CP/M file has no directory to live in, so the
; path cannot come across; only the name can.
;-----------------------------------------------------------------------
BASENM: LXI     H,HNAME
        LXI     B,HNAME         ; BC = best candidate so far
BN1:    MOV     A,M
        ORA     A
        JZ      BN9
        CPI     '/'
        JZ      BN2
        CPI     5CH             ; backslash. Both separators are legal on
        JZ      BN2             ; every host -- see HB.INC.
        INX     H
        JMP     BN1
BN2:    INX     H               ; the character AFTER the separator...
        MOV     B,H
        MOV     C,L             ; ...is the new candidate
        JMP     BN1
BN9:    MOV     H,B
        MOV     L,C
        RET

;=======================================================================
; THE FCB
;=======================================================================

;-----------------------------------------------------------------------
; MKFCB -- build FCB from the NUL-terminated name at HL.
;
; Handles an optional drive prefix ("B:FOO.ASM"), folds to 8.3, and DROPS
; characters CP/M cannot store in a filename. Dropping rather than
; refusing is the friendly choice for a name that came off a host
; filesystem, where `my-notes(2).txt` is perfectly ordinary.
;-----------------------------------------------------------------------
MKFCB:  PUSH    H

        LXI     H,FCB           ; zero the whole thing...
        MVI     B,36
MF0:    MVI     M,0
        INX     H
        DCR     B
        JNZ     MF0

        LXI     H,FCB+1         ; ...then blank the name and type fields.
        MVI     B,11            ; An FCB pads with SPACES, not NULs.
MF1:    MVI     M,' '
        INX     H
        DCR     B
        JNZ     MF1

        POP     H

; A drive letter? "B:FOO.ASM" -- look for the ':' in the second position.
        MOV     A,M
        ORA     A
        RZ                      ; empty name; caller has already checked
        INX     H
        MOV     A,M
        CPI     ':'
        JNZ     MF2             ; no colon -> no drive; back up
        DCX     H
        MOV     A,M             ; the letter
        SUI     'A'-1           ; 'A' -> 1, 'B' -> 2 ... 0 means "current"
        STA     FCB
        INX     H
        INX     H               ; step over "B:"
        JMP     MF3
MF2:    DCX     H               ; back to the first character

; The name: up to 8 characters, stopping at '.' or the end.
MF3:    LXI     D,FCB+1
        MVI     B,8
MF4:    MOV     A,M
        ORA     A
        JZ      MF9             ; end of string: no extension at all
        CPI     '.'
        JZ      MF5
        CALL    LEGAL           ; CY = CP/M cannot store this character
        JC      MF4N
        MOV     C,A             ; LEGAL gives A back; stash it
        MOV     A,B
        ORA     A
        JZ      MF4N            ; field is full -- truncate, do not overflow
        MOV     A,C
        STAX    D
        INX     D
        DCR     B
MF4N:   INX     H
        JMP     MF4

; The type: up to 3, after the '.'.
MF5:    INX     H               ; step over the '.'
        LXI     D,FCB+9
        MVI     B,3
MF6:    MOV     A,M
        ORA     A
        RZ
        CALL    LEGAL
        JC      MF6N
        MOV     C,A
        MOV     A,B
        ORA     A
        JZ      MF6N
        MOV     A,C
        STAX    D
        INX     D
        DCR     B
MF6N:   INX     H
        JMP     MF6

MF9:    RET

;-----------------------------------------------------------------------
; LEGAL -- CY set if A is a character CP/M cannot put in a filename.
;          A is preserved either way. Clobbers C.
;-----------------------------------------------------------------------
LEGAL:  PUSH    H
        MOV     C,A
        LXI     H,ILLCHR
LG1:    MOV     A,M
        ORA     A
        JZ      LGOK            ; ran off the end of the table: it is fine
        CMP     C
        JZ      LGBAD
        INX     H
        JMP     LG1
LGOK:   MOV     A,C
        POP     H
        ORA     A               ; ORA also CLEARS CARRY -- that is the point
        RET
LGBAD:  MOV     A,C
        POP     H
        STC
        RET

;=======================================================================
; TALKING TO THE USER
;=======================================================================

; "R: SRC/FOO.ASM -> B:FOO.ASM"
SAYXFR: LXI     D,MPFX
        MVI     C,PRSTR
        CALL    BDOS
        LXI     H,HNAME
        CALL    PSTR
        LXI     D,MARROW
        MVI     C,PRSTR
        CALL    BDOS
        CALL    PRFCB
        LXI     D,MCRLF
        MVI     C,PRSTR
        JMP     BDOS

; "  12 records"
SAYREC: LHLD    RECS
        CALL    PRDEC
        LXI     D,MRECS
        MVI     C,PRSTR
        JMP     BDOS

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

; PRFCB -- print the FCB as "d:NAME.EXT".
PRFCB:  LDA     FCB
        ORA     A
        JZ      PF1             ; drive 0 = "the current one"; do not print it
        ADI     'A'-1
        CALL    PUTCH
        MVI     A,':'
        CALL    PUTCH
PF1:    LXI     H,FCB+1
        MVI     B,8
        CALL    PFLD
        LDA     FCB+9
        CPI     ' '
        RZ                      ; no extension: nothing to print
        MVI     A,'.'
        CALL    PUTCH
        LXI     H,FCB+9
        MVI     B,3
PFLD:   MOV     A,M             ; print B characters at HL, skipping the
        CPI     ' '             ; blanks an FCB pads with
        JZ      PFL2
        PUSH    B
        PUSH    H
        CALL    PUTCH
        POP     H
        POP     B
PFL2:   INX     H
        DCR     B
        JNZ     PFLD
        RET

PUTCH:  MOV     E,A
        MVI     C,CONOUT
        JMP     BDOS

;-----------------------------------------------------------------------
; PRDEC -- print HL in decimal.
;
; Built backwards from the end of a buffer, so no digit reversal is
; needed, and terminated with '$' so BDOS 9 can print the whole thing in
; one call.
;-----------------------------------------------------------------------
PRDEC:  LXI     D,NUMEND
        MVI     A,'$'
        STAX    D
PD1:    DCX     D
        CALL    DIV10           ; HL = HL/10, A = remainder
        ADI     '0'
        STAX    D
        MOV     A,H
        ORA     L
        JNZ     PD1             ; ...until nothing is left
        MVI     C,PRSTR
        JMP     BDOS            ; DE already points at the first digit

; DIV10 -- HL = HL/10, A = the remainder. The 8080 has no divide, so this
; is repeated subtraction; a record count is small and this is not a hot
; loop.
DIV10:  PUSH    B
        LXI     B,0
DV1:    MOV     A,H
        ORA     A
        JNZ     DV2             ; H nonzero -> definitely >= 10
        MOV     A,L
        CPI     10
        JC      DV3             ; < 10: what is left is the remainder
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
        MOV     L,C             ; HL = the quotient
        POP     B
        RET

;=======================================================================
; ERRORS
;=======================================================================
USAGE:  LXI     D,MUSAGE
        MVI     C,PRSTR
        CALL    BDOS
        JMP     0

; EWILD IS REACHED FROM `MANY`, WHICH IS A JUMP TARGET, NOT A SUBROUTINE.
; The stack is empty there, so ESAY's RET would pop garbage and execute it.
; This one warm-boots instead. (The three below are all reached from inside
; ONE, which WAS called, so their RET has somewhere to go.)
EWILD:  LXI     D,MEWILD
        MVI     C,PRSTR
        CALL    BDOS
        JMP     0

ENODIR: LXI     D,MENODIR
        JMP     ESAY
EDISK:  LXI     D,MEDISK
        JMP     ESAY
ECLOSE: LXI     D,MECLOSE
        JMP     ESAY

; Print the message and return with CARRY SET. COPY's caller tests it.
ESAY:   MVI     C,PRSTR
        CALL    BDOS
        STC
        RET

;=======================================================================
; DATA
;=======================================================================
IDWANT: DB      'ALTAIRSIM HOSTBRIDGE',0

; The characters CP/M will not store in a filename. Note 5CH is the
; backslash and 2FH the slash -- a path separator has no business in an
; FCB, and BASENM should have removed it long before we get here.
ILLCHR: DB      '<>.,'
        DB      3BH             ; ';' -- NOT in a string: ASM strips
                                ; the comment before it parses the quotes, so a
                                ; semicolon inside one truncates it and the line
                                ; dies with a syntax error.
        DB      ':=?*[]|'
        DB      5CH,2FH,0

MNOHB:  DB      CR,LF,'R: no host bridge at B0H.',CR,LF
        DB      'This machine has no `hostbridge` card in it, or it is at'
        DB      ' another port.',CR,LF,'$'

MUSAGE: DB      CR,LF,'R -- copy a file from the host into CP/M.',CR,LF,CR,LF
        DB      '   R <hostfile> [cpmfile]',CR,LF
        DB      '   R <pattern>',CR,LF,CR,LF
        DB      'Examples',CR,LF
        DB      '   R FOO.ASM            host FOO.ASM -> CP/M FOO.ASM',CR,LF
        DB      '   R FOO.ASM B:BAR.ASM  ...to B:, under another name',CR,LF
        DB      '   R SRC/FOO.ASM        out of a subdirectory of hostdir',CR,LF
        DB      '   R *.ASM              every match',CR,LF,CR,LF
        DB      'Files are copied BYTE FOR BYTE. The last CP/M record is'
        DB      ' padded with ^Z.',CR,LF
        DB      'HDIR lists what is on the host. W copies the other way.'
        DB      CR,LF,'$'

MPFX:   DB      'R: $'
MARROW: DB      ' -> $'
MRECS:  DB      ' records',CR,LF,'$'
MERR:   DB      'R: $'
MCRLF:  DB      CR,LF,'$'

MEWILD: DB      'R: a wildcard copies each file under its own name -- so a'
        DB      ' second name',CR,LF,'   cannot mean anything. Drop it.'
        DB      CR,LF,'$'
MENODIR:DB      'R: the CP/M directory is full.',CR,LF,'$'
MEDISK: DB      'R: the CP/M disk is full, or is read-only.',CR,LF,'$'
MECLOSE:DB      'R: could not close the CP/M file.',CR,LF,'$'

;---- Variables -------------------------------------------------------
; THE DMA BUFFER -- one CP/M record. BDOS reads each record FROM here (we point
; it at this with FSETDMA), and COPY fills it from the host a byte at a time.
BUF:    DS      RECLEN

RECS:   DS      2               ; records written to CP/M
FCB:    DS      36              ; ours, not the CCP's at 5CH -- see PARSE
HNAME:  DS      HNMAX+1         ; the host name (or the pattern, or a match).
                                ; +1: RECVZ's truncate path writes its NUL one
                                ; past the count it was given.
CNAME:  DS      20              ; the CP/M name, if the user gave one
IDBUF:  DS      IDMAX+1         ; what the card said its name was (+1: as above)
NUMBUF: DS      6
NUMEND: DS      1               ; PRDEC builds backwards from here

        DS      64              ; stack: BDOS is the deepest thing we call
STACK   EQU     $

        END     START
