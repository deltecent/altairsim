;-----------------------------------------------------------------------
; W.COM -- copy a file from CP/M out to the HOST.
;
;       W <cpmfile> [hostfile] [B|T]
;       W <pattern>                     e.g.  W *.ASM     W B:*.*
;
; Part of the altairsim Host Bridge (src/boards/hostbridge.h,
; docs/boards/hostbridge.md). Assemble it INSIDE the machine:
;
;       ASM W                   -> W.HEX
;       LOAD W                  -> W.COM
;
; THE NAME IS AltairZ80'S. THE CODE IS NOT -- see the same paragraph at the
; top of R.ASM. Our W talks to a card of our own at 0B0H; theirs talks to a
; "SIMH pseudo device" at 0FEH, which in a real Altair is the 88-VI/RTC's
; control register.
;
;-----------------------------------------------------------------------
; EVERY DISK OPERATION IN THIS PROGRAM IS A BDOS CALL, AND THAT IS THE
; WHOLE DESIGN. No BIOS entry points, no IN or OUT to a disk controller, no
; assumptions about a DPB, a sector size, a skew table or which card the
; drive is on. The only ports this program touches are the bridge's own
; two. That is what lets the SAME W.COM run against an 88-DCDD 8" disk, an
; 88-MDS minidisk and an 8 MB image without being reassembled.
;
; AND BDOS CLOBBERS EVERY REGISTER (A,B,C,D,E,H,L -- only SP survives).
;-----------------------------------------------------------------------
; B OR T, AND B IS THE DEFAULT. THIS IS DELIBERATE AND IT IS THE OPPOSITE
; OF AltairZ80'S CHOICE.
;
; CP/M stores whole 128-byte records and keeps NO BYTE COUNT anywhere, so a
; file's true length is simply not recorded: the last record is padded, by
; convention with ^Z. Coming back out to the host, those pad bytes are
; indistinguishable from real data -- ONLY THE HUMAN KNOWS whether the file
; was text.
;
;   B (default)  every byte of every record, exactly. A .COM comes out
;                byte-for-byte identical, and a text file comes out with up
;                to 127 trailing ^Z bytes -- which every editor tolerates.
;
;   T            stop at the first ^Z. A text file comes out clean, and a
;                binary file that happens to contain a 1AH byte comes out
;                TRUNCATED, which is why this is not the default.
;
; AltairZ80's W defaults to text and keeps an exception list of extensions
; (.COM, .REL, .DAT...) that it treats as binary instead. A file whose name
; is not on the list and whose contents contain a 1AH is then silently
; truncated. Defaulting to EXACT costs a few pad bytes. Defaulting to text
; costs your data.
;-----------------------------------------------------------------------

        ORG     100H

;---- CP/M ------------------------------------------------------------
BDOS    EQU     0005H           ; ALWAYS here. Never call the BIOS directly.
TAIL    EQU     0080H           ; the command tail: length, then the text.
                                ; The CCP has already FOLDED IT TO UPPER
                                ; CASE -- we cannot see lower case, ever.
TFCB    EQU     005CH           ; the FCB the CCP parsed for us. W CAN use
                                ; this and R cannot, and the reason is the
                                ; whole difference between the two programs:
                                ; W's first argument IS an 8.3 CP/M name, so
                                ; the CCP's 8.3 parse is exactly right for it
                                ; -- INCLUDING its expansion of `*` into the
                                ; `?`s that BDOS search wants. R's first
                                ; argument is a HOST name, which is not 8.3
                                ; and which that parse would destroy.

CONOUT  EQU     2               ; E = char
PRSTR   EQU     9               ; DE = string, '$'-terminated
FOPEN   EQU     15
FREAD   EQU     20              ; read sequential
FSRCHF  EQU     17              ; search for first
FSRCHN  EQU     18              ; search for next
FSETDMA EQU     26

RECLEN  EQU     128             ; a CP/M record. Everything is a multiple.
HNMAX   EQU     64              ; a host name may be a PATH, so it is not 8.3
IDMAX   EQU     32              ; room for the card's signature
MAXN    EQU     64              ; how many wildcard matches we will hold
CPMEOF  EQU     1AH             ; ^Z
CR      EQU     0DH
LF      EQU     0AH

;---- The Host Bridge (see HB.INC -- ASM.COM has no INCLUDE) -----------
HBCMD   EQU     0B0H            ; OUT = command   IN = status
HBDATA  EQU     0B1H            ; data, both ways

HERR    EQU     10H             ; the last operation failed

CIDENT  EQU     0
CVOPENW EQU     2
CCLOSE  EQU     3
CERROR  EQU     7
CRESET  EQU     8

;=======================================================================
; MAIN
;=======================================================================
START:  LXI     SP,STACK

; RESET first -- ONE byte, not the 128-times-in-a-row ritual AltairZ80's
; utilities open with. This card has no undefined state to climb out of.
        MVI     A,CRESET
        OUT     HBCMD

        CALL    IDENT           ; is there even a card there? (exits if not)
        CALL    PARSE           ; the tail -> CNAME, HNAME, MODE
        CALL    INIFCB          ; the CCP's FCB at 5CH -> ours

        LDA     CNAME
        ORA     A
        JZ      USAGE           ; no arguments at all

        CALL    HASWLD          ; does the FCB name hold a '?' ...
        ORA     A
        JNZ     MANY            ; ...yes: it is a pattern
        CALL    ONEF            ; ...no: just the one file
        JMP     0               ; warm boot

;=======================================================================
; MANY -- a wildcard. Copy every CP/M file that matches.
;=======================================================================
; WE COLLECT ALL THE NAMES FIRST, AND THEN COPY THEM. That is not the
; obvious shape -- R interleaves its enumeration with its transfers -- and
; the asymmetry is forced by CP/M, not chosen:
;
; BDOS SEARCH NEXT (18) HAS NO CURSOR OF ITS OWN. It resumes from state
; that BDOS keeps in its ONE shared work area, and ANY other file call --
; the OPEN and the READs we are about to do -- overwrites it. Search first,
; open, read, search next is a documented way to walk off the end of a
; directory into someone else's file. So: take the whole list while nothing
; else is touching BDOS, then do the work.
;
; The HOST bridge has no such problem, which is why R can interleave: its
; enumerator is a real cursor that a transfer does not disturb.
;
; MAXN names is the cap, and hitting it is an ERROR, not a silent stop --
; a backup that quietly copied the first 64 of your 200 files would be
; worse than one that refused.
;=======================================================================
MANY:   LDA     HNAME
        ORA     A
        JNZ     EWILD           ; "W *.ASM FOO.TXT" cannot mean anything

        XRA     A
        STA     NCNT
        LXI     H,NAMES
        SHLD    NPTR            ; where the next match gets stashed
        SHLD    CPTR            ; ...and where the copy phase will read from

        LXI     D,BUF           ; the search results land in the DMA buffer
        MVI     C,FSETDMA
        CALL    BDOS

        LXI     D,FCB
        MVI     C,FSRCHF
        CALL    BDOS

MN1:    CPI     0FFH
        JZ      MN2             ; 0FFH = no (more) matches
        CALL    STASH           ; A holds the directory code -- see STASH
        JC      ETOOMNY
        LXI     D,FCB
        MVI     C,FSRCHN
        CALL    BDOS
        JMP     MN1

MN2:    LDA     NCNT
        ORA     A
        JZ      ENOMAT

; ---- and now the copying, with BDOS's search state safely irrelevant ----
        XRA     A
        STA     DONE

MN3:    LDA     DONE
        MOV     B,A
        LDA     NCNT
        CMP     B
        JZ      EXIT            ; every match done

        CALL    SETFCB          ; the next stashed name -> FCB
        XRA     A
        STA     HNAME           ; a FRESH host name is derived for each file:
                                ; the previous one is still sitting there
        CALL    ONEF            ; (it reports its own failures and carries on)

        LDA     DONE
        INR     A
        STA     DONE
        JMP     MN3

EXIT:   JMP     0               ; warm boot

;=======================================================================
; ONEF -- copy the CP/M file described by FCB out to the host.
;
; The FCB is already built -- by INIFCB from the command line, or by SETFCB
; from a directory entry. HNAME is the host name, or empty to derive one.
;=======================================================================
ONEF:   LXI     H,0
        SHLD    RECS

; A FRESH FCB FOR EVERY OPEN. Extent, the two system bytes, the record
; count and the current record must all be zero, and after a wildcard's
; second file the FCB we are handed is the one BDOS left behind.
        XRA     A
        STA     FCB+12          ; ex
        STA     FCB+13          ; s1
        STA     FCB+14          ; s2
        STA     FCB+15          ; rc
        STA     FCB+32          ; cr -- the sequential record cursor

; OPEN THE CP/M FILE FIRST, BEFORE TOUCHING THE HOST.
;
; The order is load-bearing, exactly as it is in R (for the mirror-image
; reason). OPEN_WRITE on the card truncates an existing host file, so
; creating it first would mean that `W TYPPO.TXT` -- a name that is not on
; the disk -- DESTROYS the host file of the same name and leaves an empty
; one behind. Fail first, destroy nothing.
        LXI     D,FCB
        MVI     C,FOPEN
        CALL    BDOS
        CPI     0FFH
        JZ      ENOCPM

        XRA     A
        STA     FCB+32          ; BDOS OPEN does not promise to clear cr

        LDA     HNAME
        ORA     A
        JNZ     OF1             ; the user said what to call it
        CALL    FCB2HN          ; ...or we name it after the CP/M file
OF1:

;---- Open the host file ----------------------------------------------
        MVI     A,CVOPENW
        OUT     HBCMD
        LXI     H,HNAME
        CALL    SENDZ           ; the NUL is what commits it

        IN      HBCMD
        ANI     HERR
        JNZ     ONEBAD          ; read-only card, outside the sandbox, bad name

        CALL    SAYXFR          ; "W: FOO.ASM -> FOO.ASM"

        LXI     D,BUF
        MVI     C,FSETDMA
        CALL    BDOS            ; where BDOS puts each record it reads

        CALL    COPY            ; <-- the actual transfer
        RC                      ; carry = it failed and has already said so

;---- CLOSE IS WHAT COMMITS THE HOST FILE ------------------------------
; Not politeness: the card holds the whole file in memory and writes it in
; one go here. An unclosed write is DISCARDED -- which is precisely why a
; program that dies half way through a transfer cannot leave a truncated
; file on your host disk.
        MVI     A,CCLOSE
        OUT     HBCMD
        IN      HBCMD
        ANI     HERR
        JNZ     ONEBAD          ; the host disk is full, or is read-only

        CALL    SAYREC          ; "  3 records"
        RET

ONEBAD: CALL    REPERR
        RET

;-----------------------------------------------------------------------
; COPY -- CP/M records -> the host stream. Returns CY on failure.
;
; NO POLLING LOOP ON TBE, AND THAT IS NOT AN OVERSIGHT. The card is
; accumulating the file in host memory, not pushing it down a wire: it can
; always take the next byte, and a wait loop would be waiting for something
; that has already happened.
;-----------------------------------------------------------------------
COPY:   LXI     H,0
        SHLD    RECS

CP0:    LXI     D,FCB
        MVI     C,FREAD
        CALL    BDOS
        ORA     A
        JNZ     CPEND           ; 1 = end of file. Anything else is also an
                                ; end, and there is nothing useful to say
                                ; about it that CP/M has not already said.

        LHLD    RECS            ; count it now: in text mode we may stop half
        INX     H               ; way through this record, and it was still
        SHLD    RECS            ; read

        LXI     H,BUF
        MVI     B,RECLEN

CP1:    MOV     A,M
        MOV     C,A             ; stash the byte -- LDA is about to eat A
        LDA     MODE
        CPI     'T'
        MOV     A,C             ; ...and give it back
        JNZ     CP2             ; binary: send everything, always
        CPI     CPMEOF
        JZ      CPEND           ; text: the FIRST ^Z ends the file

CP2:    OUT     HBDATA
        INX     H
        DCR     B
        JNZ     CP1

; ASK ONCE PER RECORD, NOT ONCE PER BYTE. The only way a write can fail
; while it is still streaming is the card's 8 MB size cap, and a CP/M
; volume cannot be pushed past that in less than 65,536 records. Checking
; here costs one IN per 128 bytes and catches it long before we have
; shovelled a whole disk into a buffer that is never going to be written.
        IN      HBCMD
        ANI     HERR
        JNZ     CPERR
        JMP     CP0

CPEND:  XRA     A               ; ...which also CLEARS CARRY: success
        RET

CPERR:  CALL    REPERR
        STC
        RET

;=======================================================================
; THE CP/M DIRECTORY
;=======================================================================

;-----------------------------------------------------------------------
; INIFCB -- the CCP's parsed FCB at 005CH -> our own FCB.
;
; We copy it rather than use it in place. The CCP put a SECOND FCB at
; 006CH, twelve bytes in, and the moment BDOS opens the first one it writes
; an extent and a record count straight through the second. Copying is one
; loop and it makes the overlap somebody else's problem.
;
; Bytes 0..11 are all we want: the drive, and the eleven characters of the
; 8.3 name -- with `*` already expanded to `?` by the CCP, which is exactly
; the form BDOS SEARCH wants. The rest of the FCB must start at zero.
;-----------------------------------------------------------------------
INIFCB: LXI     H,FCB
        MVI     B,36
IF0:    MVI     M,0
        INX     H
        DCR     B
        JNZ     IF0

        LXI     H,TFCB
        LXI     D,FCB
        MVI     B,12
IF1:    MOV     A,M
        STAX    D
        INX     H
        INX     D
        DCR     B
        JNZ     IF1

        LDA     FCB             ; remember the drive: a wildcard rebuilds the
        STA     DRIVE           ; FCB for each match and must put it back
        RET

;-----------------------------------------------------------------------
; HASWLD -- A=1 if the FCB name holds a '?', else A=0.
;
; We look at the FCB, not at the text, because the CCP has already done the
; expansion: it turned `*.ASM` into `????????ASM` before we ever ran. So
; `?` is the only wildcard character that still exists by the time we look.
;-----------------------------------------------------------------------
HASWLD: LXI     H,FCB+1
        MVI     B,11
HW1:    MOV     A,M
        CPI     '?'
        JZ      HWYES
        INX     H
        DCR     B
        JNZ     HW1
        XRA     A
        RET
HWYES:  MVI     A,1
        RET

;-----------------------------------------------------------------------
; STASH -- copy the name of the directory entry BDOS just found.
;
; BDOS SEARCH returns 0..3 in A: the entry is at BUF + A*32. (It hands back
; the whole 128-byte sector and tells you which of the four 32-byte entries
; in it was the hit. It does not hand you the entry.)
;
; Returns CY if the table is full.
;-----------------------------------------------------------------------
STASH:  MOV     C,A             ; the directory code, before anything eats it

        LDA     NCNT
        CPI     MAXN
        JNC     STFULL
        INR     A
        STA     NCNT

        MOV     A,C
        ANI     3               ; belt and braces: it is documented as 0..3
        ADD     A               ; *2
        ADD     A               ; *4
        ADD     A               ; *8
        ADD     A               ; *16
        ADD     A               ; *32 -- at most 96, so it stays in 8 bits
        MOV     E,A
        MVI     D,0
        LXI     H,BUF
        DAD     D
        INX     H               ; -> the eleven name characters (past the
                                ; user-number byte at offset 0)
        XCHG                    ; DE = source
        LHLD    NPTR            ; HL = where it goes
        MVI     B,11
ST1:    LDAX    D
        ANI     7FH             ; STRIP THE ATTRIBUTE BITS. CP/M keeps R/O,
                                ; SYSTEM and ARCHIVE in the HIGH BIT of three
                                ; of the name characters, so a read-only
                                ; FOO.COM has an 0C3H where its 'M' should be.
                                ; Hand that to OPEN unmasked and it does not
                                ; find the file it just told you about.
        MOV     M,A
        INX     D
        INX     H
        DCR     B
        JNZ     ST1
        SHLD    NPTR
        ORA     A               ; ORA also CLEARS CARRY -- that is the point
        RET

STFULL: STC
        RET

;-----------------------------------------------------------------------
; SETFCB -- the next stashed name -> FCB, and advance the cursor.
;-----------------------------------------------------------------------
SETFCB: LXI     H,FCB
        MVI     B,36
SF0:    MVI     M,0
        INX     H
        DCR     B
        JNZ     SF0

        LDA     DRIVE           ; the drive the user asked for. The directory
        STA     FCB             ; entry does not carry one -- it is a fact
                                ; about the disk it was found on, and the
                                ; entry has a USER NUMBER in that byte instead

        LHLD    CPTR
        XCHG                    ; DE = the stashed name
        LXI     H,FCB+1
        MVI     B,11
SF1:    LDAX    D
        MOV     M,A
        INX     D
        INX     H
        DCR     B
        JNZ     SF1
        XCHG                    ; HL = one past the name...
        SHLD    CPTR            ; ...which is the next one
        RET

;-----------------------------------------------------------------------
; FCB2HN -- name the host file after the CP/M one: FOO.ASM -> "FOO.ASM".
;
; The FCB pads both fields with SPACES, and no CP/M name may contain one,
; so a blank simply ends the field.
;-----------------------------------------------------------------------
FCB2HN: LXI     H,FCB+1
        LXI     D,HNAME
        MVI     B,8
        CALL    FLD

        LDA     FCB+9
        CPI     ' '
        JZ      F2Z             ; no extension: no dot either
        MVI     A,'.'
        STAX    D
        INX     D
        LXI     H,FCB+9
        MVI     B,3
        CALL    FLD

F2Z:    XRA     A
        STAX    D               ; NUL-terminated: that is what SENDZ wants
        RET

; Copy up to B characters from (HL) to (DE), stopping at the first blank.
FLD:    MOV     A,M
        CPI     ' '
        RZ
        STAX    D
        INX     D
        INX     H
        DCR     B
        JNZ     FLD
        RET

;=======================================================================
; THE HOST BRIDGE
;=======================================================================

;-----------------------------------------------------------------------
; IDENT -- prove there is a card there before we do anything else.
;
; THIS IS NOT CEREMONY. An IN from a port that no card decodes FLOATS THE
; BUS and the 8080 reads 0FFH. W's failure mode without this is quieter
; than R's and nastier: every OUT would go into the void, every status read
; would come back 0FFH -- which has HERR set -- and the program would
; announce a host error for a card that is not there. "Permission denied"
; is a much worse thing to tell someone than "there is no bridge".
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
;
; The NUL goes too, AND IT IS THE NUL THAT COMMITS THE COMMAND. Until it
; lands the card has created nothing, so we could still change our mind by
; writing a different command byte.
;-----------------------------------------------------------------------
SENDZ:  MOV     A,M
        OUT     HBDATA
        ORA     A
        RZ                      ; that was the terminator: done
        INX     H
        JMP     SENDZ

;-----------------------------------------------------------------------
; REPERR -- print what actually went wrong.
;
; The card hands back a code byte and then a MESSAGE in the host's own
; words ("FOO.TXT: permission denied"). We print the message and drop the
; code: a number the user has to go and look up is not an error report.
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
; PARSE -- pull up to three words out of the command tail at 0080H.
;
;       W FOO.TXT                 CNAME=FOO.TXT
;       W FOO.TXT T               CNAME=FOO.TXT              MODE=T
;       W FOO.TXT BAR.TXT         CNAME=FOO.TXT HNAME=BAR.TXT
;       W FOO.TXT SRC/BAR.TXT T   ...and a subdirectory of hostdir
;
; A LONE `B` OR `T` IN THE SECOND SLOT IS THE SWITCH, NOT A HOST NAME, and
; that is an ambiguity we are choosing rather than one we are missing: a
; host file really called `T` exists somewhere. Write it as `./T` and the
; card drops the `.` component -- so the escape hatch costs two characters
; and no one has to learn a new syntax.
;
; The FCB comes from the CCP (see TFCB), so CNAME here is only ever used to
; answer "did the user type anything at all".
;-----------------------------------------------------------------------
; EMPTY THE HOST NAME BEFORE WE START, and this is not belt and braces.
; HNAME is a DS, so it is not in the .COM file at all -- LOAD stops writing
; at the last DB, and what is at that address when we run is whatever the
; PREVIOUS program left there. Every other buffer here is filled by TOKEN,
; which always writes its NUL; HNAME is the one that is only written when
; the user gave a second word. Leave it and `W *.HEX` reads the corpse of
; the last program's data as a host name and refuses the wildcard.
PARSE:  XRA     A
        STA     HNAME

        LXI     H,TAIL
        MOV     B,M             ; length of the tail
        INX     H               ; -> the first character

        CALL    SKIPSP
        LXI     D,CNAME
        MVI     C,HNMAX
        CALL    TOKEN

        CALL    SKIPSP
        LXI     D,TOK2
        MVI     C,HNMAX
        CALL    TOKEN

        CALL    SKIPSP
        LXI     D,TOK3
        MVI     C,HNMAX
        CALL    TOKEN

        LDA     TOK3
        ORA     A
        JZ      PA2             ; only two words (or fewer)

; Three words: the second is the host name and the third is the switch.
        LXI     H,TOK2
        CALL    CPYHN
        LXI     H,TOK3
        JMP     SETMOD

PA2:    LDA     TOK2
        ORA     A
        RZ                      ; one word: nothing more to work out

        LDA     TOK2+1
        ORA     A
        JNZ     PA3             ; longer than one character -> a host name
        LDA     TOK2
        CPI     'B'
        JZ      PA2S
        CPI     'T'
        JNZ     PA3
PA2S:   LXI     H,TOK2
        JMP     SETMOD

PA3:    LXI     H,TOK2
        JMP     CPYHN           ; tail call

SETMOD: MOV     A,M
        CPI     'B'
        JZ      SM1
        CPI     'T'
        JNZ     EBADSW
SM1:    STA     MODE
        RET

CPYHN:  LXI     D,HNAME
CH1:    MOV     A,M
        STAX    D
        ORA     A
        RZ                      ; the NUL is copied too
        INX     H
        INX     D
        JMP     CH1

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
;   B = characters left in the tail   C = room at DE, including the NUL
; Always writes the NUL -- so "no word here" comes out as an empty string,
; which is exactly what the callers test for.
TOKEN:  MOV     A,C
        CPI     1
        JZ      TOKEND          ; only room for the terminator
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

; "W: FOO.ASM -> SRC/FOO.ASM"
SAYXFR: LXI     D,MPFX
        MVI     C,PRSTR
        CALL    BDOS
        CALL    PRFCB
        LXI     D,MARROW
        MVI     C,PRSTR
        CALL    BDOS
        LXI     H,HNAME
        CALL    PSTR
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
; Built backwards from the end of a buffer, so no digit reversal is needed,
; and terminated with '$' so BDOS 9 prints the whole thing in one call.
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
        JNZ     PD1             ; ...until nothing is left
        MVI     C,PRSTR
        JMP     BDOS            ; DE already points at the first digit

; DIV10 -- HL = HL/10, A = the remainder. The 8080 has no divide, so this
; is repeated subtraction. A record count is small and this is not a hot
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
; THE ONES BELOW THAT WARM-BOOT ARE REACHED FROM `START` OR `MANY`, WHICH
; ARE JUMP TARGETS AND NOT SUBROUTINES. The stack is empty there, so a RET
; would pop garbage and execute it. ENOCPM is the only one reached from
; inside ONEF -- which WAS called -- so it is the only one that may return.
USAGE:  LXI     D,MUSAGE
        MVI     C,PRSTR
        CALL    BDOS
        JMP     0

EWILD:  LXI     D,MEWILD
        MVI     C,PRSTR
        CALL    BDOS
        JMP     0

EBADSW: LXI     D,MEBADSW
        MVI     C,PRSTR
        CALL    BDOS
        JMP     0

ENOMAT: LXI     D,MENOMAT
        MVI     C,PRSTR
        CALL    BDOS
        JMP     0

ETOOMNY:LXI     D,METOOM
        MVI     C,PRSTR
        CALL    BDOS
        JMP     0

; Print the message, return with CARRY SET so a wildcard walk can go on to
; the next file instead of giving up on all of them.
ENOCPM: LXI     D,MENOCPM
        MVI     C,PRSTR
        CALL    BDOS
        STC
        RET

;=======================================================================
; DATA
;=======================================================================
IDWANT: DB      'ALTAIRSIM HOSTBRIDGE',0

MODE:   DB      'B'             ; B = every byte, exactly. See the top.
DRIVE:  DB      0

MNOHB:  DB      CR,LF,'W: no host bridge at B0H.',CR,LF
        DB      'This machine has no `hostbridge` card in it, or it is at'
        DB      ' another port.',CR,LF,'$'

MUSAGE: DB      CR,LF,'W -- copy a file from CP/M out to the host.',CR,LF,CR,LF
        DB      '   W <cpmfile> [hostfile] [B|T]',CR,LF
        DB      '   W <pattern>',CR,LF,CR,LF
        DB      'Examples',CR,LF
        DB      '   W FOO.ASM            CP/M FOO.ASM -> host FOO.ASM',CR,LF
        DB      '   W FOO.ASM SRC/F.ASM  ...into a subdirectory of hostdir',CR,LF
        DB      '   W FOO.ASM T          stop at the first ^Z -- for TEXT',CR,LF
        DB      '   W *.ASM              every match, each under its own name'
        DB      CR,LF,CR,LF
        DB      'B (the default) copies every byte of every record, so a .COM'
        DB      ' survives',CR,LF
        DB      'the trip and a text file arrives padded with ^Z. T stops at'
        DB      ' the first',CR,LF
        DB      '^Z, which cleans up text and TRUNCATES anything binary.'
        DB      CR,LF
        DB      'HDIR lists what is on the host. R copies the other way.'
        DB      CR,LF,'$'

MPFX:   DB      'W: $'
MARROW: DB      ' -> $'
MRECS:  DB      ' records',CR,LF,'$'
MERR:   DB      'W: $'
MCRLF:  DB      CR,LF,'$'

MEWILD: DB      'W: a wildcard copies each file under its own name -- so a'
        DB      ' second name',CR,LF,'   cannot mean anything. Drop it.'
        DB      CR,LF,'$'
MEBADSW:DB      'W: the switch is B (every byte, the default) or T (stop at'
        DB      ' the first ^Z).',CR,LF,'$'
MENOMAT:DB      'W: no CP/M file matches.',CR,LF,'$'
METOOM: DB      'W: more than 64 files match. Narrow the pattern.',CR,LF
        DB      '   Nothing was copied -- half a backup is worse than none.'
        DB      CR,LF,'$'
MENOCPM:DB      'W: no such CP/M file.',CR,LF,'$'

;---- Variables -------------------------------------------------------
; THE DMA BUFFER -- one CP/M record. BDOS reads each record INTO here (we
; point it at this with FSETDMA), and COPY hands it to the card a byte at a
; time. BDOS SEARCH also uses it, for the directory sector it found.
BUF:    DS      RECLEN

RECS:   DS      2               ; records read from CP/M
FCB:    DS      36              ; ours, copied out of the CCP's -- see INIFCB
HNAME:  DS      HNMAX+1         ; the host name (given, or derived)
CNAME:  DS      HNMAX+1         ; the first word of the tail, as typed
TOK2:   DS      HNMAX+1
TOK3:   DS      HNMAX+1
IDBUF:  DS      IDMAX+1         ; what the card said its name was
NUMBUF: DS      6
NUMEND: DS      1               ; PRDEC builds backwards from here

NCNT:   DS      1               ; how many wildcard matches we found
DONE:   DS      1               ; how many of them we have copied
NPTR:   DS      2               ; the collect cursor into NAMES
CPTR:   DS      2               ; the copy cursor into NAMES
NAMES:  DS      MAXN*11         ; eleven characters each, no dot, no drive

        DS      64              ; stack: BDOS is the deepest thing we call
STACK   EQU     $

        END     START
