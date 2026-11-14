;-----------------------------------------------------------------------
; W.COM: copy a file from CP/M to the host.
;
;       W <cpmfile> [hostfile] [B|T]
;       W <pattern> [hostfolder] [B|T]  e.g.  W *.ASM     W B:*.* OUT
;
; Part of the altairsim Host Bridge. Assemble it in the machine:
;
;       ASM W                   -> W.HEX
;       LOAD W                  -> W.COM
;
; Revisions:
;       1.0  2026-08-27  Version banner. Byte count after each file.
;       1.1  2026-09-23  Attribute bits do not go into a host name.
;       1.2  2026-10-12  With no switch, the extension selects T or B.
;       1.3  2026-10-21  A wildcard can copy into a host folder.
;       1.4  2026-11-14  The no-board message says what to do.
;
; W uses only BDOS calls for the disk, and only ports B0H and B1H. It
; does not use the BIOS or a disk controller, so the same W.COM runs on
; every CP/M disk. BDOS changes every register except SP.
;
; B copies every byte of every record. T stops at the first ^Z. CP/M
; keeps no byte count, so the last record of a text file holds the ^Z
; and up to 126 bytes of old data. With no switch, W looks at the
; extension of the CP/M name (PICKMD):
;
;   ASM MAC Z80 HEX PRN LST SYM INC SUB TXT     -> T, and W says so
;   anything else                               -> B
;
; The table lists text types, so a file that is not on it is copied
; byte for byte. .BAS and .DOC are not on it, because MBASIC saves
; tokens by default and a word processor file can hold a 1AH. A switch
; always wins over the extension.
;-----------------------------------------------------------------------

	ORG	100H

;---- CP/M ------------------------------------------------------------
BDOS	EQU	0005H
TAIL	EQU	0080H		; the command tail: length, then the text.
				; The CCP folds it to upper case.
TFCB	EQU	005CH		; the FCB that the CCP parsed. W's first
				; word is a CP/M name, so this parse is
				; correct for it, and * is already ?.

CONOUT	EQU	2		; E = char
PRSTR	EQU	9		; DE = string, ends in '$'
FOPEN	EQU	15
FREAD	EQU	20		; read sequential
FSRCHF	EQU	17		; search for first
FSRCHN	EQU	18		; search for next
FSETDMA EQU	26

RECLEN	EQU	128		; a CP/M record
HNMAX	EQU	64		; a host name can be a path
IDMAX	EQU	32		; room for the board's signature
MAXN	EQU	64		; the most wildcard matches W holds
CPMEOF	EQU	1AH		; ^Z
CR	EQU	0DH
LF	EQU	0AH

;---- The Host Bridge (HB.INC has the full list) ----------------------
HBCMD	EQU	0B0H		; OUT = command   IN = status
HBDATA	EQU	0B1H		; data, both ways

HERR	EQU	10H		; the last operation failed

CIDENT	EQU	0
CVOPENW EQU	2
CCLOSE	EQU	3
CDIRF	EQU	4
CERROR	EQU	7
CRESET	EQU	8

;=======================================================================
; MAIN
;=======================================================================
START:	LXI	SP,STACK

	MVI	A,CRESET	; one byte puts the board in a known state
	OUT	HBCMD

	CALL	IDENT		; exits if there is no board
	CALL	PARSE		; the tail -> CNAME, HNAME, MODE
	CALL	INIFCB		; the CCP's FCB at 5CH -> FCB

	LDA	CNAME
	ORA	A
	JZ	USAGE		; no arguments

	CALL	HASWLD		; a '?' in the FCB name?
	ORA	A
	JNZ	MANY		; yes: a pattern
	CALL	ONEF		; no: one file
	JMP	0		; warm boot

;=======================================================================
; MANY: a wildcard. Copy each CP/M file that matches.
;
; W collects all the names first, then copies them. BDOS SEARCH NEXT
; keeps its place in the BDOS work area, and an OPEN or a READ
; overwrites it.
;
; More than MAXN matches is an error, and W copies nothing.
;
; A second word is a host folder: `W *.ASM SRC` writes SRC/FOO.ASM. The
; folder must exist. SETDIR checks it once, before the first file.
;=======================================================================
MANY:	CALL	SETDIR		; HNAME -> HDIRP, "SRC/" (exits if bad)

	XRA	A
	STA	NCNT
	LXI	H,NAMES
	SHLD	NPTR		; where the next match goes
	SHLD	CPTR		; where the copy reads from

	LXI	D,BUF		; the search results go in the DMA buffer
	MVI	C,FSETDMA
	CALL	BDOS

	LXI	D,FCB
	MVI	C,FSRCHF
	CALL	BDOS

MN1:	CPI	0FFH
	JZ	MN2		; 0FFH = no more matches
	CALL	STASH		; A = the directory code
	JC	ETOOMNY
	LXI	D,FCB
	MVI	C,FSRCHN
	CALL	BDOS
	JMP	MN1

MN2:	LDA	NCNT
	ORA	A
	JZ	ENOMAT

;---- copy each match --------------------------------------------------
	XRA	A
	STA	DONE

MN3:	LDA	DONE
	MOV	B,A
	LDA	NCNT
	CMP	B
	JZ	EXIT		; every match done

	CALL	SETFCB		; the next name -> FCB
	CALL	DIRHN		; HNAME = the folder, then the CP/M name
	CALL	ONEF		; reports its own errors and returns

	LDA	DONE
	INR	A
	STA	DONE
	JMP	MN3

EXIT:	JMP	0		; warm boot

;-----------------------------------------------------------------------
; SETDIR: HNAME -> HDIRP, the folder that a wildcard writes into.
;
; With no second word, HDIRP is empty. Otherwise W adds a `/` if the
; name does not end in `/` or `\`. Then DIR_FIRST lists the folder. It
; fails if the folder is not there or is a file, and it changes nothing
; on the host. On a failure W prints the host's message and stops.
;
; The folder must leave room in HNAME for a 12-character CP/M name,
; "FILENAME.EXT". W refuses a longer folder.
;-----------------------------------------------------------------------
SETDIR: LXI	H,HNAME
	LXI	D,HDIRP
	MVI	B,0		; B counts the characters copied
SD1:	MOV	A,M
	ORA	A
	JZ	SD2
	STAX	D
	INX	H
	INX	D
	INR	B
	JMP	SD1

SD2:	MOV	A,B
	ORA	A
	JZ	SD9		; no folder: HDIRP is only the NUL
	DCX	H		; the last character typed
	MOV	A,M
	CPI	'/'
	JZ	SD3
	CPI	'\'
	JZ	SD3
	MVI	A,'/'
	STAX	D
	INX	D
	INR	B
SD3:	MOV	A,B
	CPI	HNMAX-12+1
	JNC	ELONG		; no room for FILENAME.EXT
	XRA	A
	STAX	D

	MVI	A,CDIRF
	OUT	HBCMD
	LXI	H,HDIRP
	CALL	SENDZ
	IN	HBCMD
	ANI	HERR
	RZ			; the folder is there
	CALL	REPERR		; "W: NOPE: no such file"
	JMP	0

SD9:	STAX	D		; A is 0
	RET

;-----------------------------------------------------------------------
; DIRHN: HNAME = the folder in HDIRP, then the name in the FCB.
;-----------------------------------------------------------------------
DIRHN:	LXI	H,HDIRP
	LXI	D,HNAME
DH1:	MOV	A,M
	ORA	A
	JZ	FCB2HN		; tail call. DE is where the name goes.
	STAX	D
	INX	H
	INX	D
	JMP	DH1

;=======================================================================
; ONEF: copy the CP/M file in FCB to the host.
;
; INIFCB or SETFCB built the FCB. HNAME is the host name, or empty to
; use the CP/M name.
;=======================================================================
ONEF:	CALL	PICKMD		; B or T for this file
	LXI	H,0
	SHLD	RECS

; Clear the FCB fields after the name before each OPEN. After the first
; match of a wildcard, they hold what BDOS left.
	XRA	A
	STA	FCB+12		; ex
	STA	FCB+13		; s1
	STA	FCB+14		; s2
	STA	FCB+15		; rc
	STA	FCB+32		; cr, the record cursor

; Open the CP/M file before the host file. OPEN_WRITE truncates a host
; file, so a CP/M name that is not on the disk must fail first.
	LXI	D,FCB
	MVI	C,FOPEN
	CALL	BDOS
	CPI	0FFH
	JZ	ENOCPM

	XRA	A
	STA	FCB+32		; BDOS OPEN does not always clear cr

	LDA	HNAME
	ORA	A
	JNZ	OF1		; the user gave a host name
	LXI	D,HNAME
	CALL	FCB2HN		; or use the CP/M name
OF1:

;---- Open the host file ----------------------------------------------
	MVI	A,CVOPENW
	OUT	HBCMD
	LXI	H,HNAME
	CALL	SENDZ		; the NUL commits the command

	IN	HBCMD
	ANI	HERR
	JNZ	ONEBAD		; read-only board, outside the sandbox, bad name

	CALL	SAYXFR		; "W: FOO.ASM -> FOO.ASM"

	LXI	D,BUF
	MVI	C,FSETDMA
	CALL	BDOS		; BDOS reads each record into BUF

	CALL	COPY
	RC			; CY = it failed and printed why

;---- CLOSE commits the host file -------------------------------------
; The board holds the file in memory and writes it to the host at
; CLOSE. A write that is not closed is discarded.
	MVI	A,CCLOSE
	OUT	HBCMD
	IN	HBCMD
	ANI	HERR
	JNZ	ONEBAD		; the host disk is full or read-only

	CALL	SAYREC		; "  3 records, 384 bytes"
	RET

ONEBAD: CALL	REPERR
	RET

;-----------------------------------------------------------------------
; COPY: CP/M records -> the host. Returns CY on failure.
;
; There is no wait for TBE. The board stores the file in memory, so it
; always takes the next byte.
;-----------------------------------------------------------------------
COPY:	LXI	H,0
	SHLD	RECS
	SHLD	BYTES		; the true byte count, 24 bits. T mode can
	XRA	A		; stop in a record, so RECS*128 is wrong.
	STA	BYTES+2

CP0:	LXI	D,FCB
	MVI	C,FREAD
	CALL	BDOS
	ORA	A
	JNZ	CPEND		; 1 = end of file. Any other code ends it too.

	LHLD	RECS		; count the record now. T mode can stop
	INX	H		; in this record, and it was read.
	SHLD	RECS

	LXI	H,BUF
	MVI	B,RECLEN

CP1:	MOV	A,M
	MOV	C,A		; keep the byte: LDA changes A
	LDA	XMODE
	CPI	'T'
	MOV	A,C		; and get it back
	JNZ	CP2		; B: send every byte
	CPI	CPMEOF
	JZ	CPEND		; T: the first ^Z ends the file

CP2:	OUT	HBDATA
	CALL	INCBYT		; count the byte sent
	INX	H
	DCR	B
	JNZ	CP1

; Check HERR once for each record. While it streams, a write fails only
; at the board's 8 MB limit, so one IN for each record finds it soon.
	IN	HBCMD
	ANI	HERR
	JNZ	CPERR
	JMP	CP0

CPEND:	XRA	A		; also clears CY: success
	RET

CPERR:	CALL	REPERR
	STC
	RET

;-----------------------------------------------------------------------
; PICKMD: set XMODE, the mode for the file in FCB.
;
; A switch decides. With no switch, W looks for the extension in
; EXTTAB. On the list is T, and AUTO is set so SAYXFR says so. Off the
; list is B. The compare ignores bit 7 (see STASH).
;-----------------------------------------------------------------------
PICKMD: XRA	A
	STA	AUTO
	LDA	MODSET
	ORA	A
	LDA	MODE		; (LDA does not change the flags)
	JNZ	PM9		; a switch was given

	LXI	D,EXTTAB
PM1:	LDAX	D
	ORA	A
	MVI	A,'B'		; (MVI does not change the flags)
	JZ	PM9		; the end of the table: B

	LXI	H,FCB+9		; the extension
	MVI	B,3
	MVI	C,0		; C collects the bits that differ
PM2:	LDAX	D
	XRA	M
	ANI	7FH
	ORA	C
	MOV	C,A
	INX	H
	INX	D
	DCR	B
	JNZ	PM2		; all three, so DE ends on the next entry
	MOV	A,C
	ORA	A
	JNZ	PM1

	MVI	A,1
	STA	AUTO
	MVI	A,'T'
PM9:	STA	XMODE
	RET

;=======================================================================
; THE CP/M DIRECTORY
;=======================================================================

;-----------------------------------------------------------------------
; INIFCB: copy the CCP's FCB at 005CH to FCB.
;
; The CCP's second FCB starts at 006CH, and an OPEN of the first writes
; into it. W uses a copy for this reason. Bytes 0-11 are the drive and
; the 8.3 name, with * already changed to ?. The rest starts at zero.
;-----------------------------------------------------------------------
INIFCB: LXI	H,FCB
	MVI	B,36
IF0:	MVI	M,0
	INX	H
	DCR	B
	JNZ	IF0

	LXI	H,TFCB
	LXI	D,FCB
	MVI	B,12
IF1:	MOV	A,M
	STAX	D
	INX	H
	INX	D
	DCR	B
	JNZ	IF1

	LDA	FCB		; keep the drive. SETFCB puts it back
	STA	DRIVE		; for each match.
	RET

;-----------------------------------------------------------------------
; HASWLD: A=1 if the FCB name holds a '?', else A=0. The CCP has
; already changed * to ?, so '?' is the only wildcard left.
;-----------------------------------------------------------------------
HASWLD: LXI	H,FCB+1
	MVI	B,11
HW1:	MOV	A,M
	CPI	'?'
	JZ	HWYES
	INX	H
	DCR	B
	JNZ	HW1
	XRA	A
	RET
HWYES:	MVI	A,1
	RET

;-----------------------------------------------------------------------
; STASH: copy the name of the directory entry that BDOS found.
;
; BDOS SEARCH returns 0-3 in A. The entry is at BUF + A*32. Returns CY
; if the table is full.
;
; CP/M keeps R/O, SYSTEM and ARCHIVE in bit 7 of three name characters.
; STASH clears bit 7, because OPEN does not find a name that has it
; set. FLD, PICKMD and PRFCB also ignore bit 7, because a CP/M 3 OPEN
; writes the bits back into the FCB.
;-----------------------------------------------------------------------
STASH:	MOV	C,A		; the directory code

	LDA	NCNT
	CPI	MAXN
	JNC	STFULL
	INR	A
	STA	NCNT

	MOV	A,C
	ANI	3		; documented as 0-3
	ADD	A
	ADD	A
	ADD	A
	ADD	A
	ADD	A		; *32: at most 96, so it fits in 8 bits
	MOV	E,A
	MVI	D,0
	LXI	H,BUF
	DAD	D
	INX	H		; the name, after the user number byte
	XCHG			; DE = source
	LHLD	NPTR		; HL = where it goes
	MVI	B,11
ST1:	LDAX	D
	ANI	7FH		; clear the attribute bit
	MOV	M,A
	INX	D
	INX	H
	DCR	B
	JNZ	ST1
	SHLD	NPTR
	ORA	A		; also clears CY
	RET

STFULL: STC
	RET

;-----------------------------------------------------------------------
; SETFCB: the next stashed name -> FCB, and move CPTR on.
;-----------------------------------------------------------------------
SETFCB: LXI	H,FCB
	MVI	B,36
SF0:	MVI	M,0
	INX	H
	DCR	B
	JNZ	SF0

	LDA	DRIVE		; the drive the user gave. A directory
	STA	FCB		; entry has a user number in this byte.

	LHLD	CPTR
	XCHG			; DE = the stashed name
	LXI	H,FCB+1
	MVI	B,11
SF1:	LDAX	D
	MOV	M,A
	INX	D
	INX	H
	DCR	B
	JNZ	SF1
	XCHG			; HL = the next name
	SHLD	CPTR
	RET

;-----------------------------------------------------------------------
; FCB2HN: the CP/M name in FCB -> "FOO.ASM" at DE.
;
; DE is HNAME, or the end of a folder already in it. The FCB pads each
; field with spaces, and a CP/M name has no space, so a space ends the
; field.
;-----------------------------------------------------------------------
FCB2HN: LXI	H,FCB+1
	MVI	B,8
	CALL	FLD

	LDA	FCB+9
	ANI	7FH		; ignore the attribute bit (see STASH)
	CPI	' '
	JZ	F2Z		; no extension: no dot
	MVI	A,'.'
	STAX	D
	INX	D
	LXI	H,FCB+9
	MVI	B,3
	CALL	FLD

F2Z:	XRA	A
	STAX	D		; the NUL that SENDZ needs
	RET

; FLD: copy up to B characters from (HL) to (DE), and stop at a space.
; Bit 7 is cleared (see STASH).
FLD:	MOV	A,M
	ANI	7FH
	CPI	' '
	RZ
	STAX	D
	INX	D
	INX	H
	DCR	B
	JNZ	FLD
	RET

;=======================================================================
; THE HOST BRIDGE
;=======================================================================

;-----------------------------------------------------------------------
; IDENT: check that the board is there before anything else.
;
; An IN from a port that no board decodes reads 0FFH, and 0FFH has HERR
; set. Without this check, W would report a host error.
;-----------------------------------------------------------------------
IDENT:	MVI	A,CIDENT
	OUT	HBCMD

	LXI	H,IDBUF
	MVI	B,IDMAX
ID1:	IN	HBDATA
	MOV	M,A
	ORA	A
	JZ	ID2		; the terminator
	INX	H
	DCR	B
	JNZ	ID1
	MVI	M,0		; no terminator: 0FFH, so no board

ID2:	LXI	H,IDBUF
	LXI	D,IDWANT
ID3:	LDAX	D
	ORA	A
	RZ			; a match: the board is there. This is a
				; prefix compare, so a later signature
				; such as "... 2" also matches.
	CMP	M
	JNZ	IDBAD
	INX	H
	INX	D
	JMP	ID3

IDBAD:	LXI	D,MNOHB
	MVI	C,PRSTR
	CALL	BDOS
	JMP	0

;-----------------------------------------------------------------------
; SENDZ: send the string at HL to the data port, with its NUL. The NUL
; commits the command.
;-----------------------------------------------------------------------
SENDZ:	MOV	A,M
	OUT	HBDATA
	ORA	A
	RZ			; that was the NUL
	INX	H
	JMP	SENDZ

;-----------------------------------------------------------------------
; REPERR: print the board's error message. The board sends a code byte,
; then the host's message as text. W prints the text.
;-----------------------------------------------------------------------
REPERR: MVI	A,CERROR
	OUT	HBCMD
	IN	HBDATA		; the code byte: skip it

	LXI	D,MERR
	MVI	C,PRSTR
	CALL	BDOS

RPE1:	IN	HBDATA		; then the text, one character at a time.
	ORA	A		; No register is kept across the BDOS
	JZ	RPE2		; call: each byte comes from the port.
	MOV	E,A
	MVI	C,CONOUT
	CALL	BDOS
	JMP	RPE1

RPE2:	LXI	D,MCRLF
	MVI	C,PRSTR
	JMP	BDOS		; tail call

;=======================================================================
; THE COMMAND TAIL
;=======================================================================

;-----------------------------------------------------------------------
; PARSE: get up to three words from the command tail at 0080H.
;
;       W FOO.TXT                 CNAME=FOO.TXT
;       W FOO.TXT T               CNAME=FOO.TXT              MODE=T
;       W *.* B                   a wildcard takes the switch too
;       W FOO.TXT BAR.TXT         CNAME=FOO.TXT HNAME=BAR.TXT
;       W FOO.TXT SRC/BAR.TXT T   a subdirectory of hostdir
;       W *.ASM SRC               with a wildcard, HNAME is a folder
;
; A lone B or T as the second word is the switch. Write a host file
; named T as ./T, and a folder named B or T as B/.
;
; The FCB comes from the CCP (TFCB). CNAME only shows that the user
; typed something.
;
; PARSE clears HNAME first. HNAME is a DS, so it is not in the .COM and
; holds old data at the start. TOKEN puts a NUL in every other buffer,
; but HNAME gets one only from a second word.
;-----------------------------------------------------------------------
PARSE:	XRA	A
	STA	HNAME

	LXI	H,TAIL
	MOV	B,M		; the length of the tail
	INX	H		; the first character

	CALL	SKIPSP
	LXI	D,CNAME
	MVI	C,HNMAX
	CALL	TOKEN

	CALL	SKIPSP
	LXI	D,TOK2
	MVI	C,HNMAX
	CALL	TOKEN

	CALL	SKIPSP
	LXI	D,TOK3
	MVI	C,HNMAX
	CALL	TOKEN

	LDA	TOK3
	ORA	A
	JZ	PA2		; two words or fewer

; Three words: the host name, then the switch.
	LXI	H,TOK2
	CALL	CPYHN
	LXI	H,TOK3
	JMP	SETMOD

PA2:	LDA	TOK2
	ORA	A
	RZ			; one word

	LDA	TOK2+1
	ORA	A
	JNZ	PA3		; more than one character: a host name
	LDA	TOK2
	CPI	'B'
	JZ	PA2S
	CPI	'T'
	JNZ	PA3
PA2S:	LXI	H,TOK2
	JMP	SETMOD

PA3:	LXI	H,TOK2
	JMP	CPYHN		; tail call

SETMOD: MOV	A,M
	CPI	'B'
	JZ	SM1
	CPI	'T'
	JNZ	EBADSW
SM1:	STA	MODE
	MVI	A,1
	STA	MODSET		; a switch was given, so PICKMD does
	RET			; not look at the extension

CPYHN:	LXI	D,HNAME
CH1:	MOV	A,M
	STAX	D
	ORA	A
	RZ			; the NUL is copied too
	INX	H
	INX	D
	JMP	CH1

SKIPSP: MOV	A,B
	ORA	A
	RZ			; nothing left
	MOV	A,M
	CPI	' '
	RNZ
	INX	H
	DCR	B
	JMP	SKIPSP

; TOKEN: copy one word from (HL) to (DE), and end it with a NUL.
;   B = characters left in the tail   C = room at DE, with the NUL
; No word gives an empty string, which the callers test for.
TOKEN:	MOV	A,C
	CPI	1
	JZ	TOKEND		; room for the NUL only
	MOV	A,B
	ORA	A
	JZ	TOKEND
	MOV	A,M
	CPI	' '
	JZ	TOKEND
	STAX	D
	INX	H
	INX	D
	DCR	B
	DCR	C
	JMP	TOKEN
TOKEND: XRA	A
	STAX	D
	RET

;=======================================================================
; TALKING TO THE USER
;=======================================================================

; "W: FOO.ASM -> SRC/FOO.ASM"
SAYXFR: LXI	D,MPFX
	MVI	C,PRSTR
	CALL	BDOS
	CALL	PRFCB
	LXI	D,MARROW
	MVI	C,PRSTR
	CALL	BDOS
	LXI	H,HNAME
	CALL	PSTR
	LDA	AUTO
	ORA	A
	JZ	SX1
	LXI	D,MBYEXT	; " (text, by extension)"
	MVI	C,PRSTR
	CALL	BDOS
SX1:	LXI	D,MCRLF
	MVI	C,PRSTR
	JMP	BDOS

; "  12 records, 1493 bytes"
SAYREC: LHLD	RECS
	CALL	PRDEC
	LXI	D,MRECS		; " records, "
	MVI	C,PRSTR
	CALL	BDOS
	CALL	PRBYT		; the bytes sent to the host
	LXI	D,MBYTES	; " bytes",CR,LF
	MVI	C,PRSTR
	JMP	BDOS

;-----------------------------------------------------------------------
; INCBYT: add one to the 24-bit count at BYTES. Keeps A, B, C and HL.
;-----------------------------------------------------------------------
INCBYT: PUSH	H
	LXI	H,BYTES
	INR	M
	JNZ	IB1		; no carry: done
	INX	H
	INR	M
	JNZ	IB1
	INX	H
	INR	M
IB1:	POP	H
	RET

;-----------------------------------------------------------------------
; PRBYT: print the 24-bit count at BYTES in decimal, as PRDEC does for
; HL. It divides BYTES by ten in place.
;-----------------------------------------------------------------------
PRBYT:	LXI	D,NUMEND
	MVI	A,'$'
	STAX	D
PRB1:	DCX	D
	PUSH	D		; DIV10T changes DE
	CALL	DIV10T		; BYTES = BYTES/10, A = the remainder
	POP	D
	ADI	'0'
	STAX	D
	LDA	BYTES		; anything left?
	LXI	H,BYTES+1
	ORA	M
	INX	H
	ORA	M
	JNZ	PRB1
	MVI	C,PRSTR
	JMP	BDOS

; DIV10T: BYTES = BYTES/10, and A = the remainder. Long division from
; the top byte. The remainder r is below 10, so r*256+byte < 2560 fits
; in DE.
DIV10T: XRA	A		; the remainder starts at 0
	LXI	H,BYTES+2	; the top byte first
	CALL	DT1
	DCX	H
	CALL	DT1
	DCX	H
	JMP	DT1		; tail call: A = the remainder

; DT1: acc = A*256 + (HL). (HL) = acc/10 and A = acc MOD 10. HL does
; not change.
DT1:	MOV	D,A		; DE = acc, D below 10
	MOV	E,M
	MVI	B,0		; the quotient byte
DT2:	MOV	A,D
	ORA	A
	JNZ	DT3		; D is set: acc >= 256
	MOV	A,E
	CPI	10
	JC	DT4		; acc < 10: E is the remainder
DT3:	MOV	A,E
	SUI	10
	MOV	E,A
	MOV	A,D
	SBI	0
	MOV	D,A
	INR	B
	JMP	DT2
DT4:	MOV	M,B		; store the quotient byte
	MOV	A,E		; the remainder, for the next byte
	RET

; PSTR: print the string at HL, up to its NUL. HL is kept across BDOS.
PSTR:	MOV	A,M
	ORA	A
	RZ
	PUSH	H
	MOV	E,A
	MVI	C,CONOUT
	CALL	BDOS
	POP	H
	INX	H
	JMP	PSTR

; PRFCB: print the FCB as "d:NAME.EXT".
PRFCB:	LDA	FCB
	ORA	A
	JZ	PF1		; drive 0 is the current drive: no letter
	ADI	'A'-1
	CALL	PUTCH
	MVI	A,':'
	CALL	PUTCH
PF1:	LXI	H,FCB+1
	MVI	B,8
	CALL	PFLD
	LDA	FCB+9
	ANI	7FH		; ignore the attribute bit (see STASH)
	CPI	' '
	RZ			; no extension
	MVI	A,'.'
	CALL	PUTCH
	LXI	H,FCB+9
	MVI	B,3
PFLD:	MOV	A,M		; print B characters at HL, but not
	ANI	7FH		; the spaces that pad an FCB. Bit 7
	CPI	' '		; is ignored (see STASH).
	JZ	PFL2
	PUSH	B
	PUSH	H
	CALL	PUTCH
	POP	H
	POP	B
PFL2:	INX	H
	DCR	B
	JNZ	PFLD
	RET

PUTCH:	MOV	E,A
	MVI	C,CONOUT
	JMP	BDOS

;-----------------------------------------------------------------------
; PRDEC: print HL in decimal. The digits go into NUMBUF from the end,
; and a '$' ends them, so one BDOS 9 call prints them.
;-----------------------------------------------------------------------
PRDEC:	LXI	D,NUMEND
	MVI	A,'$'
	STAX	D
PD1:	DCX	D
	CALL	DIV10		; HL = HL/10, A = the remainder
	ADI	'0'
	STAX	D
	MOV	A,H
	ORA	L
	JNZ	PD1		; until HL is 0
	MVI	C,PRSTR
	JMP	BDOS		; DE points at the first digit

; DIV10: HL = HL/10, A = the remainder, by repeated subtraction.
DIV10:	PUSH	B
	LXI	B,0
DV1:	MOV	A,H
	ORA	A
	JNZ	DV2		; H is not 0: HL >= 10
	MOV	A,L
	CPI	10
	JC	DV3		; HL < 10: HL is the remainder
DV2:	MOV	A,L
	SUI	10
	MOV	L,A
	MOV	A,H
	SBI	0
	MOV	H,A
	INX	B
	JMP	DV1
DV3:	MOV	A,L		; the remainder, before L changes
	MOV	H,B
	MOV	L,C		; HL = the quotient
	POP	B
	RET

;=======================================================================
; ERRORS
;
; Each error prints its message and does a warm boot, except ENOCPM.
; ENOCPM comes from inside ONEF and returns with CY set, so a wildcard
; continues with the next file.
;=======================================================================
USAGE:	LXI	D,MUSAGE
	MVI	C,PRSTR
	CALL	BDOS
	JMP	0

ELONG:	LXI	D,MELONG
	MVI	C,PRSTR
	CALL	BDOS
	JMP	0

EBADSW: LXI	D,MEBADSW
	MVI	C,PRSTR
	CALL	BDOS
	JMP	0

ENOMAT: LXI	D,MENOMAT
	MVI	C,PRSTR
	CALL	BDOS
	JMP	0

ETOOMNY:LXI	D,METOOM
	MVI	C,PRSTR
	CALL	BDOS
	JMP	0

ENOCPM: LXI	D,MENOCPM
	MVI	C,PRSTR
	CALL	BDOS
	STC
	RET

;=======================================================================
; DATA
;=======================================================================
IDWANT: DB	'ALTAIRSIM HOSTBRIDGE',0

MODE:	DB	'B'		; the switch, when one was given
MODSET: DB	0		; 1 = a switch was given. A DB, because it
				; must start at 0 (a DS is not in the .COM)

; EXTTAB: the text extensions (see the header). Three characters each,
; and a 0 ends the table.
EXTTAB: DB	'ASM','MAC','Z80','HEX','PRN'
	DB	'LST','SYM','INC','SUB','TXT'
	DB	0
DRIVE:	DB	0

MNOHB:	DB	CR,LF,'W: no hostbridge board at port B0H.',CR,LF
	DB	'Add a hostbridge board to the machine, or set its port'
	DB	' to B0.',CR,LF,'$'

MUSAGE: DB	CR,LF,'W -- copy a file from CP/M out to the host.  (1.4)',CR,LF,CR,LF
	DB	'   W <cpmfile> [hostfile] [B|T]',CR,LF
	DB	'   W <pattern> [hostfolder] [B|T]',CR,LF,CR,LF
	DB	'Examples',CR,LF
	DB	'   W FOO.ASM            CP/M FOO.ASM -> host FOO.ASM',CR,LF
	DB	'   W FOO.ASM SRC/F.ASM  ...into a subdirectory of hostdir',CR,LF
	DB	'   W FOO.DAT T          stop at the first ^Z',CR,LF
	DB	'   W *.*                every match, each under its own name'
	DB	CR,LF
	DB	'   W *.ASM SRC          ...into folder SRC, which must exist'
	DB	CR,LF,CR,LF
	DB	'B copies every byte of every record, so a text file arrives'
	DB	' with',CR,LF
	DB	'the rest of its last record: a ^Z and whatever follows it.'
	DB	CR,LF
	DB	'T stops at the first ^Z, which TRUNCATES anything binary.'
	DB	CR,LF
	DB	'With neither, the extension decides. These are text:',CR,LF
	DB	'   ASM MAC Z80 HEX PRN LST SYM INC SUB TXT',CR,LF
	DB	'Every other file, a .COM included, is copied byte for byte.'
	DB	CR,LF
	DB	'HDIR lists what is on the host. R copies the other way.'
	DB	CR,LF,'$'

MPFX:	DB	'W: $'
MARROW: DB	' -> $'
MBYEXT: DB	' (text, by extension)$'
MRECS:	DB	' records, $'
MBYTES: DB	' bytes',CR,LF,'$'
MERR:	DB	'W: $'
MCRLF:	DB	CR,LF,'$'

MELONG: DB	'W: the folder name is too long.',CR,LF,'$'
MEBADSW:DB	'W: the switch is B (every byte) or T (stop at the first ^Z).'
	DB	CR,LF,'$'
MENOMAT:DB	'W: no CP/M file matches.',CR,LF,'$'
METOOM: DB	'W: more than 64 files match. Narrow the pattern.',CR,LF
	DB	'   Nothing was copied -- half a backup is worse than none.'
	DB	CR,LF,'$'
MENOCPM:DB	'W: no such CP/M file.',CR,LF,'$'

;---- Variables -------------------------------------------------------
; BUF: the DMA buffer, one CP/M record. BDOS READ and SEARCH put their
; data here.
BUF:	DS	RECLEN

XMODE:	DS	1		; B or T for this file (PICKMD)
AUTO:	DS	1		; 1 = the extension chose T
RECS:	DS	2		; records read from CP/M
BYTES:	DS	3		; the true byte count. 8 MB needs 24 bits.
FCB:	DS	36		; W's copy of the CCP's FCB (INIFCB)
HNAME:	DS	HNMAX+1		; the host name, given or made
HDIRP:	DS	HNMAX+2		; a wildcard's host folder, "SRC/" (SETDIR)
CNAME:	DS	HNMAX+1		; the first word of the tail
TOK2:	DS	HNMAX+1
TOK3:	DS	HNMAX+1
IDBUF:	DS	IDMAX+1		; the board's signature
NUMBUF: DS	8		; 8 digits: 16777215 is the largest count
NUMEND: DS	1		; PRDEC and PRBYT build back from here

NCNT:	DS	1		; how many matches W found
DONE:	DS	1		; how many it has copied
NPTR:	DS	2		; the collect cursor into NAMES
CPTR:	DS	2		; the copy cursor into NAMES
NAMES:	DS	MAXN*11		; 11 characters each, no dot, no drive

	DS	64		; the stack. BDOS is the deepest call.
STACK	EQU	$

	END	START
