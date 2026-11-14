;-----------------------------------------------------------------------
; SIMCLK.COM: show or set the processor clock rate.
;
;	SIMCLK			show the rate
;	SIMCLK 2000000		run the processor at 2 MHz
;	SIMCLK 0		run the processor flat out
;
; Part of the altairsim Host Bridge. Assemble it in the machine:
;
;	ASM SIMCLK		-> SIMCLK.HEX
;	LOAD SIMCLK		-> SIMCLK.COM
;
; Revisions:
;	2026-11-14  First version (issue #918).
;
; The rate is in Hz, as decimal digits. The CPU board checks the range,
; as it does for SET cpu0 CLOCK_HZ=n at the monitor. SIMCLK prints the
; board's own error text when the CPU board refuses the rate.
;
; A SUBMIT file can assemble flat out and then go back to 2 MHz:
;
;	SIMCLK 0
;	ASM FOO
;	LOAD FOO
;	SIMCLK 2000000
;
; SIMCLK makes no BDOS file calls. It uses the console and ports B0H and
; B1H only.
;-----------------------------------------------------------------------

	ORG	100H

;---- CP/M ------------------------------------------------------------
BDOS	EQU	0005H
TAIL	EQU	0080H		; the command tail: length, then the text

CONOUT	EQU	2		; E = char
PRSTR	EQU	9		; DE = string, ends in '$'

IDMAX	EQU	32		; room for the board's signature
RTMAX	EQU	20		; room for a rate in decimal
CR	EQU	0DH
LF	EQU	0AH

;---- The Host Bridge (HB.INC has the full list) ----------------------
HBCMD	EQU	0B0H		; OUT = command   IN = status
HBDATA	EQU	0B1H		; data, both ways

RDY	EQU	01H		; clear while the machine applies a rate
HERR	EQU	10H		; the last operation failed

CIDENT	EQU	0
CERROR	EQU	7
CRESET	EQU	8
CSETCK	EQU	0AH		; then the rate in Hz and a NUL
CGETCK	EQU	0BH		; -> the rate as decimal text and a NUL

;=======================================================================
; MAIN
;=======================================================================
START:	LXI	SP,STACK

	MVI	A,CRESET	; one byte puts the board in a known
	OUT	HBCMD		; state

	CALL	IDENT		; exits if there is no board

	LXI	H,TAIL
	MOV	B,M		; the length of the tail
	INX	H
	CALL	SKIPSP
	MOV	A,B
	ORA	A
	JZ	SHOWRT		; no argument: show the rate

;---- set the rate -----------------------------------------------------
; The digits go to the board as they are in the tail. The board checks
; them, so SIMCLK has no buffer to overflow.
	MVI	A,CSETCK
	OUT	HBCMD
SET1:	MOV	A,M
	CPI	' '
	JZ	SET2		; the end of the word
	OUT	HBDATA
	INX	H
	DCR	B
	JNZ	SET1
SET2:	XRA	A
	OUT	HBDATA		; the NUL commits the request

; Wait for RDY. The machine applies the rate within a few thousand
; instructions. Send no command before RDY is set: any command drops a
; request that the machine has not applied yet.
SET3:	IN	HBCMD
	ANI	RDY
	JZ	SET3

	IN	HBCMD
	ANI	HERR
	JZ	SHOWRT		; the rate is set
	CALL	REPERR		; bad digits, or the CPU board refused it
	JMP	0		; warm boot

;---- show the rate ----------------------------------------------------
SHOWRT: MVI	A,CGETCK
	OUT	HBCMD
	IN	HBCMD
	ANI	HERR
	JZ	SHW1
	CALL	REPERR		; an older altairsim: no such command
	JMP	0

SHW1:	LXI	H,RATE
	MVI	B,RTMAX
	CALL	RECVZ

	LXI	D,MPFX
	MVI	C,PRSTR
	CALL	BDOS

	LXI	H,RATE		; "0" is flat out
	MOV	A,M
	CPI	'0'
	JNZ	SHW2
	INX	H
	MOV	A,M
	ORA	A
	JNZ	SHW2
	LXI	D,MFLAT
	MVI	C,PRSTR
	CALL	BDOS
	JMP	0

SHW2:	LXI	H,RATE
	CALL	PSTR
	LXI	D,MHZ
	MVI	C,PRSTR
	CALL	BDOS
	JMP	0

;=======================================================================
; THE HOST BRIDGE
;=======================================================================

;-----------------------------------------------------------------------
; IDENT: check that the board is there before anything else.
;
; An IN from a port that no board decodes reads 0FFH, and 0FFH has RDY
; and HERR set. Without this check, SIMCLK would print a host error.
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
	RZ			; all of IDWANT matched
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

	LXI	D,MPFX
	MVI	C,PRSTR
	CALL	BDOS

RPE1:	IN	HBDATA		; then the text, one character at a time
	ORA	A
	JZ	RPE2
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

; SKIPSP: step HL past spaces.  B = characters left in the tail.
SKIPSP: MOV	A,B
	ORA	A
	RZ
	MOV	A,M
	CPI	' '
	RNZ
	INX	H
	DCR	B
	JMP	SKIPSP

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

;=======================================================================
; DATA
;=======================================================================
IDWANT: DB	'ALTAIRSIM HOSTBRIDGE',0

MNOHB:	DB	CR,LF,'SIMCLK: no host bridge at B0H.',CR,LF
	DB	'This machine has no `hostbridge` board in it, or it is at'
	DB	' another port.',CR,LF,'$'

MPFX:	DB	'SIMCLK: $'
MFLAT:	DB	'flat out',CR,LF,'$'
MHZ:	DB	' Hz',CR,LF,'$'
MCRLF:	DB	CR,LF,'$'

;---- variables --------------------------------------------------------
RATE:	DS	RTMAX+1		; the rate from the board, as text
IDBUF:	DS	IDMAX+1

	DS	32		; the stack
STACK	EQU	$

	END	START
