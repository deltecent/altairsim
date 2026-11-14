;-----------------------------------------------------------------------
; HDIR.COM: list the host directory.
;
;       HDIR                    everything in hostdir
;       HDIR *.ASM              only what matches
;       HDIR SRC/               a subdirectory of hostdir
;
; Part of the altairsim Host Bridge. Assemble it in the machine:
;
;       ASM HDIR                -> HDIR.HEX
;       LOAD HDIR               -> HDIR.COM
;
; Revisions:
;       1.0  2026-07-14  First version.
;       1.1  2026-07-28  The long listing: size and date.
;       1.2  2026-11-14  The no-board message says what to do.
;
; HDIR prints each host name as it is on the host, in its own case and
; at its full length. R and W change a name to fit 8.3.
;
; HDIR makes no BDOS file calls. It uses the console and ports B0H and
; B1H only.
;-----------------------------------------------------------------------

	ORG	100H

;---- CP/M ------------------------------------------------------------
BDOS	EQU	0005H
TAIL	EQU	0080H		; the command tail: length, then the text.
				; The CCP folds it to upper case. The board
				; folds host names to match, and prints
				; each name in its own case.

CONOUT	EQU	2		; E = char
PRSTR	EQU	9		; DE = string, ends in '$'

HNMAX	EQU	64		; a host name can be a path
IDMAX	EQU	32		; room for the board's signature
SZMAX	EQU	20		; a size in decimal (a uint64 is 20 digits)
DTMAX	EQU	12		; room for a date ("MM/DD/YY")
SIZEW	EQU	8		; the width of the size column
CR	EQU	0DH
LF	EQU	0AH

;---- The Host Bridge (HB.INC has the full list) ----------------------
HBCMD	EQU	0B0H		; OUT = command   IN = status
HBDATA	EQU	0B1H		; data, both ways

HEOF	EQU	08H		; the listing has no more names
HERR	EQU	10H		; the last operation failed

CIDENT	EQU	0
CDIRL	EQU	9		; the long listing: name<0> size<0> date<0>
CDIRN	EQU	5		; the next entry. HEOF at the end.
CERROR	EQU	7
CRESET	EQU	8

;=======================================================================
; MAIN
;=======================================================================
START:	LXI	SP,STACK

	MVI	A,CRESET	; one byte puts the board in a known
	OUT	HBCMD		; state

	CALL	IDENT		; exits if there is no board
	CALL	PARSE		; the tail -> PAT. The board reads an
				; empty pattern as `*`.

	LXI	H,0		; a 16-bit count: a host directory can
	SHLD	COUNT		; hold more than 255 files

	MVI	A,CDIRL
	OUT	HBCMD
	LXI	H,PAT
	CALL	SENDZ		; the NUL commits the command

;---- the listing loop -------------------------------------------------
DIR1:	IN	HBCMD
	ANI	HERR
	JNZ	DIRBAD		; a bad pattern, or no such hostdir

	IN	HBCMD
	ANI	HEOF
	JNZ	DIRE		; no more names. With no matches at all,
				; HDIR comes here on the first pass.

; Each entry is three strings: the name, the size ("<DIR>" for a folder,
; else a decimal byte count) and the date.
	LXI	H,NAME
	MVI	B,HNMAX
	CALL	RECVZ
	LXI	H,SIZE
	MVI	B,SZMAX
	CALL	RECVZ
	LXI	H,DATE
	MVI	B,DTMAX
	CALL	RECVZ

	CALL	SHOW		; one line: <size>  <date>  <name>

	LHLD	COUNT
	INX	H
	SHLD	COUNT

	MVI	A,CDIRN
	OUT	HBCMD
	JMP	DIR1

;---- the count --------------------------------------------------------
DIRE:	LHLD	COUNT
	MOV	A,H
	ORA	L
	JZ	DIRNON

	CALL	CRLF
	LHLD	COUNT
	CALL	PRDEC
	LXI	D,MFILES
	MVI	C,PRSTR
	CALL	BDOS
	JMP	0		; warm boot

DIRNON: LXI	D,MNONE
	MVI	C,PRSTR
	CALL	BDOS
	JMP	0

DIRBAD: CALL	REPERR
	JMP	0

;-----------------------------------------------------------------------
; SHOW: print one entry as a line:  <size>  <date>  <name>
;
;       1792  07/14/26  R.COM
;      <DIR>  07/20/26  SRC/
;
; The size is right-justified in SIZEW columns. The date from the board
; is 8 characters. The name goes last, because it has no fixed length.
; A folder name ends in '/', and its size is "<DIR>".
;-----------------------------------------------------------------------
SHOW:	LXI	H,SIZE
	MVI	C,SIZEW
	CALL	RJUST
	CALL	SPC2		; two spaces between the columns
	LXI	H,DATE
	CALL	PSTR
	CALL	SPC2
	LXI	H,NAME
	CALL	PSTR
	JMP	CRLF		; tail call: end the line

;-----------------------------------------------------------------------
; RJUST: print the string at HL right-justified in C columns. It prints
;        (C - length) spaces, then the string. A string of C characters
;        or more gets no spaces.
;-----------------------------------------------------------------------
RJUST:	PUSH	H
	CALL	STRLEN		; B = the length
	POP	H
	MOV	A,C
	SUB	B		; A = the number of spaces
	JC	RJ2		; wider than the field: no spaces
	JZ	RJ2
	MOV	B,A
RJ1:	MVI	A,' '
	PUSH	B
	PUSH	H
	CALL	PUTCH		; BDOS changes B and HL
	POP	H
	POP	B
	DCR	B
	JNZ	RJ1
RJ2:	JMP	PSTR		; tail call: the string

;-----------------------------------------------------------------------
; SPC2: two spaces, the gap between columns.
;-----------------------------------------------------------------------
SPC2:	MVI	A,' '
	CALL	PUTCH
	MVI	A,' '
	JMP	PUTCH		; tail call

;-----------------------------------------------------------------------
; STRLEN: B = the length of the string at HL.
;-----------------------------------------------------------------------
STRLEN: MVI	B,0
SL1:	MOV	A,M
	ORA	A
	RZ
	INR	B
	INX	H
	JMP	SL1

;=======================================================================
; THE HOST BRIDGE
;=======================================================================

;-----------------------------------------------------------------------
; IDENT: check that the board is there before anything else.
;
; An IN from a port that no board decodes reads 0FFH, and 0FFH has HEOF
; and HERR set. Without this check, HDIR would print a host error or an
; empty listing.
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
	RZ			; all of IDWANT matched. This is a prefix
				; compare, so "... 2" matches too.
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
	RZ
	INX	H
	JMP	SENDZ

;-----------------------------------------------------------------------
; RECVZ: read a string from the data port into HL.
;        B = the room, with the NUL.
;-----------------------------------------------------------------------
RECVZ:	IN	HBDATA
	MOV	M,A
	ORA	A
	RZ
	INX	H
	DCR	B
	JNZ	RECVZ
	MVI	M,0		; no room left: cut the string here
	RET

;-----------------------------------------------------------------------
; REPERR: print the board's error message, in the host's words.
;-----------------------------------------------------------------------
REPERR: MVI	A,CERROR
	OUT	HBCMD
	IN	HBDATA		; the code byte: skip it

	LXI	D,MERR
	MVI	C,PRSTR
	CALL	BDOS

RPE1:	IN	HBDATA		; then the text, one character at a time
	ORA	A
	JZ	RPE2
	MOV	E,A
	MVI	C,CONOUT
	CALL	BDOS
	JMP	RPE1

RPE2:	JMP	CRLF		; tail call

;=======================================================================
; THE COMMAND TAIL
;=======================================================================

;-----------------------------------------------------------------------
; PARSE: the first word of the tail at 0080H -> PAT.
;
; HDIR does not use the CCP's FCB at 005CH. The CCP parses the word as
; 8.3, so `SRC/` would lose its slash.
;-----------------------------------------------------------------------
PARSE:	LXI	H,TAIL
	MOV	B,M		; the length of the tail
	INX	H

	CALL	SKIPSP
	LXI	D,PAT
	MVI	C,HNMAX
	CALL	TOKEN
	RET

SKIPSP: MOV	A,B
	ORA	A
	RZ
	MOV	A,M
	CPI	' '
	RNZ
	INX	H
	DCR	B
	JMP	SKIPSP

; Copy one word from (HL) to (DE), and end it with a NUL.
;   B = characters left in the tail   C = room at DE, with the NUL
; With no word, the result is an empty string.
TOKEN:	MOV	A,C
	CPI	1
	JZ	TOKEND
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
; MESSAGES TO THE USER
;=======================================================================

; PSTR: print the string at HL. HL is pushed across the BDOS call.
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

PUTCH:	MOV	E,A
	MVI	C,CONOUT
	JMP	BDOS

CRLF:	LXI	D,MCRLF
	MVI	C,PRSTR
	JMP	BDOS

;-----------------------------------------------------------------------
; PRDEC: print HL in decimal. The digits go into a buffer from the end.
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
	JNZ	PD1
	MVI	C,PRSTR
	JMP	BDOS		; DE points at the first digit

; DIV10: HL = HL/10, A = the remainder, by repeated subtraction.
DIV10:	PUSH	B
	LXI	B,0
DV1:	MOV	A,H
	ORA	A
	JNZ	DV2
	MOV	A,L
	CPI	10
	JC	DV3
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
	MOV	L,C
	POP	B
	RET

;=======================================================================
; DATA
;=======================================================================
IDWANT: DB	'ALTAIRSIM HOSTBRIDGE',0

MNOHB:	DB	CR,LF,'HDIR: no hostbridge board at port B0H.',CR,LF
	DB	'Add a hostbridge board to the machine, or set its port'
	DB	' to B0.',CR,LF,'$'

MERR:	DB	'HDIR: $'
MCRLF:	DB	CR,LF,'$'
MFILES: DB	' files.',CR,LF,'$'
MNONE:	DB	'HDIR: nothing matches.',CR,LF,'$'

;---- variables --------------------------------------------------------
PAT:	DS	HNMAX+1		; the pattern as typed. Empty = everything.
NAME:	DS	HNMAX+1		; the name from the board. +1: RECVZ can
				; write its NUL one byte past the count.
SIZE:	DS	SZMAX+1		; its size as decimal text, or "<DIR>"
DATE:	DS	DTMAX+1		; its date, MM/DD/YY
IDBUF:	DS	IDMAX+1
COUNT:	DS	2		; the names printed
NUMBUF: DS	6
NUMEND: DS	1		; PRDEC ends here

	DS	64		; the stack
STACK	EQU	$

	END	START
