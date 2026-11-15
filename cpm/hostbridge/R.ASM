;-----------------------------------------------------------------------
; R.COM: copy a file from the host into CP/M.
;
;       R <hostfile> [cpmfile]
;       R <pattern>                     e.g.  R *.ASM     R SRC/*.MAC
;
; Part of the altairsim Host Bridge. Assemble it in the machine:
;
;       ASM R                   -> R.HEX
;       LOAD R                  -> R.COM
;
; Revisions:
;       1.0  2026-08-27  Version banner. Byte count after each file.
;       1.1  2026-09-20  The CP/M name is folded to upper case.
;       1.2  2026-10-02  A space does not go into a CP/M name.
;       1.3  2026-11-14  The no-board message says what to do.
;
; R uses only BDOS calls for the disk, and only ports B0H and B1H. It
; does not use the BIOS or a disk controller, so the same R.COM runs on
; every CP/M disk. BDOS changes every register except SP.
;-----------------------------------------------------------------------

	ORG	100H

;---- CP/M ------------------------------------------------------------
BDOS	EQU	0005H
TAIL	EQU	0080H		; the command tail: length, then the text.
				; The CCP folds it to upper case. The board
				; folds host names to match, so
				; `R readme.txt` finds readme.txt.

CONOUT	EQU	2		; E = char
PRSTR	EQU	9		; DE = string, ends in '$'
FCLOSE	EQU	16
FDELET	EQU	19
FWRITE	EQU	21		; write sequential
FMAKE	EQU	22
FSETDMA EQU	26

RECLEN	EQU	128		; a CP/M record
HNMAX	EQU	64		; a host name can be a path
IDMAX	EQU	32		; room for the board's signature
CPMEOF	EQU	1AH		; ^Z
CR	EQU	0DH
LF	EQU	0AH

;---- The Host Bridge (HB.INC has the full list) ----------------------
HBCMD	EQU	0B0H		; OUT = command   IN = status
HBDATA	EQU	0B1H		; data, both ways

DAV	EQU	02H		; a byte is waiting
HEOF	EQU	08H		; the stream has no more data
HERR	EQU	10H		; the last operation failed

CIDENT	EQU	0
CVOPENR EQU	1
CDIRF	EQU	4
CDIRN	EQU	5
CERROR	EQU	7
CRESET	EQU	8

;=======================================================================
; MAIN
;=======================================================================
START:	LXI	SP,STACK

	MVI	A,CRESET	; one byte puts the board in a known state
	OUT	HBCMD

	CALL	IDENT		; exits if there is no board
	CALL	PARSE		; the tail -> HNAME, CNAME

	LDA	HNAME
	ORA	A
	JZ	USAGE		; no arguments

	CALL	HASWLD		; a * or ? in the host name?
	ORA	A
	JNZ	MANY		; yes: walk the host directory
	CALL	ONE		; no: one file
	JMP	EXIT

;-----------------------------------------------------------------------
; MANY: a wildcard. Copy each host file that matches.
;
; DIR_FIRST and DIR_NEXT keep their place across an OPEN_READ, so R
; copies each match as the board lists it.
;-----------------------------------------------------------------------
MANY:	LDA	CNAME
	ORA	A
	JNZ	EWILD		; a wildcard takes no CP/M name

	MVI	A,CDIRF
	OUT	HBCMD
	LXI	H,HNAME		; the pattern
	CALL	SENDZ

MANY1:	IN	HBCMD
	ANI	HERR
	JNZ	MANYE		; a bad pattern or a bad folder

	IN	HBCMD
	ANI	HEOF
	JNZ	EXIT		; no more names. With no matches at all,
				; R comes here on the first pass.

	LXI	H,HNAME		; the match replaces the pattern. The
	MVI	B,HNMAX		; board keeps the pattern.
	CALL	RECVZ

	CALL	ISDIR		; a folder ends in '/'
	JZ	MANY2		; and is not copied

	CALL	ONE

MANY2:	MVI	A,CDIRN
	OUT	HBCMD
	JMP	MANY1

MANYE:	CALL	REPERR
	JMP	EXIT

EXIT:	JMP	0		; warm boot

;=======================================================================
; ONE: copy the host file named in HNAME into CP/M.
;
; It runs once for one file, and once for each match of a wildcard.
;=======================================================================
ONE:	LXI	H,0
	SHLD	RECS		; records written
	SHLD	BYTES		; the true host byte count, 24 bits. The
	XRA	A		; last record is padded, so RECS*128 is wrong.
	STA	BYTES+2

; R opens the host file before it changes the CP/M directory. If the
; host file is not there, the CP/M file of that name stays as it was.
	MVI	A,CVOPENR
	OUT	HBCMD
	LXI	H,HNAME
	CALL	SENDZ

	IN	HBCMD
	ANI	HERR
	JNZ	ONEBAD		; no such host file, outside the sandbox, ...

;---- the CP/M name ----------------------------------------------------
	LDA	CNAME
	ORA	A
	JZ	ONEDER		; no second word: use the host name
	LXI	H,CNAME		; the name the user gave
	JMP	ONEFCB
ONEDER: CALL	BASENM		; SRC/FOO.ASM -> FOO.ASM
ONEFCB: CALL	MKFCB

	CALL	SAYXFR		; "R: SRC/FOO.ASM -> FOO.ASM"

;---- make the CP/M file -----------------------------------------------
; Delete the old file first. BDOS MAKE on a name that exists makes a
; second directory entry with the same name.
	LXI	D,FCB
	MVI	C,FDELET
	CALL	BDOS		; 0FFH = there was no file. That is fine.

	LXI	D,FCB
	MVI	C,FMAKE
	CALL	BDOS
	CPI	0FFH
	JZ	ENODIR		; directory full

	LXI	D,BUF
	MVI	C,FSETDMA
	CALL	BDOS		; BDOS writes each record from BUF

	CALL	COPY
	RC			; CY = the copy failed and said so

	LXI	D,FCB
	MVI	C,FCLOSE
	CALL	BDOS
	CPI	0FFH
	JZ	ECLOSE

	CALL	SAYREC		; "3 records, 300 bytes"
	RET

ONEBAD: CALL	REPERR
	RET

;-----------------------------------------------------------------------
; COPY: the host stream -> CP/M records. Returns CY on failure.
;
; The board reads the whole file into memory when OPEN_READ commits.
; DAV clear means the end of the data, so R does not wait for DAV.
;-----------------------------------------------------------------------
COPY:	IN	HBCMD
	ANI	DAV
	RZ			; no more data: done, no carry. An empty
				; host file makes an empty CP/M file.

	LXI	H,BUF		; start a record
	MVI	B,RECLEN

CP1:	IN	HBCMD
	ANI	DAV
	JZ	CPPAD		; the data ended in the record: pad it
	IN	HBDATA
	MOV	M,A
	INX	H
	DCR	B
	JNZ	CP1
	MVI	A,RECLEN	; a full record: count 128 bytes
	CALL	ADDBYT
	JMP	CPWRIT

; Pad the rest of the last record with ^Z. CP/M keeps no byte count, and
; TYPE and ED stop at the ^Z.
CPPAD:	MVI	A,RECLEN	; count only the bytes from the host:
	SUB	B		; 128 - the slots left.
	CALL	ADDBYT		; ADDBYT keeps B and HL for the pad below.
	MVI	A,CPMEOF
CPPAD1: MOV	M,A
	INX	H
	DCR	B
	JNZ	CPPAD1

CPWRIT: LXI	D,FCB
	MVI	C,FWRITE
	CALL	BDOS
	ORA	A
	JNZ	EDISK		; disk full, or read-only

	LHLD	RECS
	INX	H
	SHLD	RECS
	JMP	COPY		; the next record

;=======================================================================
; THE HOST BRIDGE
;=======================================================================

;-----------------------------------------------------------------------
; IDENT: check that the board is there before anything else.
;
; An IN from a port that no board decodes reads 0FFH, and 0FFH has DAV
; set. Without this check, R would fill the disk with 0FFH bytes.
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
; REPERR: print the board's error message. The board sends a code byte,
; then the host's message as text. R prints the text.
;-----------------------------------------------------------------------
REPERR: MVI	A,CERROR
	OUT	HBCMD
	IN	HBDATA		; the code byte: skip it

	LXI	D,MERR
	MVI	C,PRSTR
	CALL	BDOS

RPE1:	IN	HBDATA		; then the text, one character at a time.
	ORA	A		; No register is kept across the BDOS
	JZ	RPE2		; call, because the next byte comes
	MOV	E,A		; from the port.
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
; PARSE: get two words from the command tail at 0080H.
;
; R does not use the CCP's FCBs at 005CH and 006CH. The CCP parses the
; first word as 8.3, so `readme.markdown` would become `README  MAR`.
;-----------------------------------------------------------------------
PARSE:	LXI	H,TAIL
	MOV	B,M		; the length of the tail
	INX	H		; the first character

	CALL	SKIPSP
	LXI	D,HNAME
	MVI	C,HNMAX+1
	CALL	TOKEN

	CALL	SKIPSP
	LXI	D,CNAME
	MVI	C,HNMAX+1
	CALL	TOKEN
	RET

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
; No word gives an empty string. A word that does not fit stops R with
; an error. R does not cut the word: the rest would go past the buffer,
; and a cut host name can name a different file.
TOKEN:	MOV	A,B
	ORA	A
	JZ	TOKEND
	MOV	A,M
	CPI	' '
	JZ	TOKEND
	MOV	A,C
	CPI	1
	JZ	EWORD		; a character, and room for the NUL only
	MOV	A,M
	STAX	D
	INX	H
	INX	D
	DCR	B
	DCR	C
	JMP	TOKEN
TOKEND: XRA	A
	STAX	D
	RET

EWORD:	LXI	D,MEWORD
	MVI	C,PRSTR
	CALL	BDOS
	JMP	0

;-----------------------------------------------------------------------
; HASWLD: A=1 if HNAME has a * or a ?, else A=0.
;-----------------------------------------------------------------------
HASWLD: LXI	H,HNAME
HW1:	MOV	A,M
	ORA	A
	JZ	HWNO
	CPI	'*'
	JZ	HWYES
	CPI	'?'
	JZ	HWYES
	INX	H
	JMP	HW1
HWNO:	XRA	A
	RET
HWYES:	MVI	A,1
	RET

;-----------------------------------------------------------------------
; ISDIR: Z if HNAME ends in '/'. The board marks a folder that way.
; A wildcard can match a folder, and R does not try to open it.
;-----------------------------------------------------------------------
ISDIR:	LXI	H,HNAME
	MOV	A,M
	ORA	A
	RNZ			; empty name: not a folder (and NZ)
IS1:	MOV	A,M
	ORA	A
	JZ	IS2
	INX	H
	JMP	IS1
IS2:	DCX	H		; the last character
	MOV	A,M
	CPI	'/'		; Z if it is a folder
	RET

;-----------------------------------------------------------------------
; BASENM: HL = the last part of the path in HNAME.
; SRC/FOO.ASM -> FOO.ASM. CP/M has no folders.
;-----------------------------------------------------------------------
BASENM: LXI	H,HNAME
	LXI	B,HNAME		; BC = the start of the last part so far
BN1:	MOV	A,M
	ORA	A
	JZ	BN9
	CPI	'/'
	JZ	BN2
	CPI	5CH		; backslash. The board accepts both
	JZ	BN2		; separators on every host.
	INX	H
	JMP	BN1
BN2:	INX	H		; the character after the separator
	MOV	B,H
	MOV	C,L
	JMP	BN1
BN9:	MOV	H,B
	MOV	L,C
	RET

;=======================================================================
; THE FCB
;=======================================================================

;-----------------------------------------------------------------------
; MKFCB: build FCB from the name at HL.
;
; It reads a drive prefix ("B:FOO.ASM"), cuts the name to 8.3, and
; drops each character that CP/M cannot store in a name. A host name
; such as `my-notes(2).txt` becomes MY-NOTES.TXT.
;
; It also folds the name to upper case. The CCP folds every name that
; the user types, so a lower case CP/M name cannot be typed. A lower case
; name comes from the host, as a wildcard match or through BASENM.
;-----------------------------------------------------------------------
MKFCB:	PUSH	H

	LXI	H,FCB		; zero the FCB
	MVI	B,36
MF0:	MVI	M,0
	INX	H
	DCR	B
	JNZ	MF0

	LXI	H,FCB+1		; then fill the name and type with
	MVI	B,11		; spaces
MF1:	MVI	M,' '
	INX	H
	DCR	B
	JNZ	MF1

	POP	H

; A drive letter: a ':' in the second position.
	MOV	A,M
	ORA	A
	RZ			; empty name. The caller checked.
	INX	H
	MOV	A,M
	CPI	':'
	JNZ	MF2		; no colon, no drive
	DCX	H
	MOV	A,M		; the letter
	CALL	UPCASE
	SUI	'A'-1		; 'A' -> 1, 'B' -> 2. 0 = the current drive.
	STA	FCB
	INX	H
	INX	H		; step over "B:"
	JMP	MF3
MF2:	DCX	H		; back to the first character

; The name: up to 8 characters, to the '.' or the end.
MF3:	LXI	D,FCB+1
	MVI	B,8
MF4:	MOV	A,M
	ORA	A
	JZ	MF9		; the end: no extension
	CPI	'.'
	JZ	MF5
	CALL	UPCASE
	CALL	LEGAL		; CY = CP/M cannot store this character
	JC	MF4N
	MOV	C,A		; keep the character
	MOV	A,B
	ORA	A
	JZ	MF4N		; the field is full: skip the rest
	MOV	A,C
	STAX	D
	INX	D
	DCR	B
MF4N:	INX	H
	JMP	MF4

; The type: up to 3 characters after the '.'.
MF5:	INX	H		; step over the '.'
	LXI	D,FCB+9
	MVI	B,3
MF6:	MOV	A,M
	ORA	A
	RZ
	CALL	UPCASE
	CALL	LEGAL
	JC	MF6N
	MOV	C,A
	MOV	A,B
	ORA	A
	JZ	MF6N
	MOV	A,C
	STAX	D
	INX	D
	DCR	B
MF6N:	INX	H
	JMP	MF6

MF9:	RET

;-----------------------------------------------------------------------
; UPCASE: fold a lower case letter in A to upper case. Other values do
;         not change. Changes only A and the flags.
;-----------------------------------------------------------------------
UPCASE: CPI	'a'
	RC
	CPI	'z'+1
	RNC
	SUI	'a'-'A'
	RET

;-----------------------------------------------------------------------
; LEGAL: CY set if CP/M cannot put the character in A in a name.
;        A is kept. Changes C.
;
; A space or a control character ends a name for the CCP. DEL and a
; byte with bit 7 set are bad too, because CP/M keeps the file
; attributes in bit 7. R checks these ranges first, then ILLCHR.
;-----------------------------------------------------------------------
LEGAL:	PUSH	H
	MOV	C,A
	CPI	21H		; a space, or below
	JC	LGBAD
	CPI	7FH		; DEL, or bit 7 set
	JNC	LGBAD
	LXI	H,ILLCHR
LG1:	MOV	A,M
	ORA	A
	JZ	LGOK		; the end of the table: it is legal
	CMP	C
	JZ	LGBAD
	INX	H
	JMP	LG1
LGOK:	MOV	A,C
	POP	H
	ORA	A		; ORA clears CY
	RET
LGBAD:	MOV	A,C
	POP	H
	STC
	RET

;=======================================================================
; MESSAGES TO THE USER
;=======================================================================

; "R: SRC/FOO.ASM -> B:FOO.ASM"
SAYXFR: LXI	D,MPFX
	MVI	C,PRSTR
	CALL	BDOS
	LXI	H,HNAME
	CALL	PSTR
	LXI	D,MARROW
	MVI	C,PRSTR
	CALL	BDOS
	CALL	PRFCB
	LXI	D,MCRLF
	MVI	C,PRSTR
	JMP	BDOS

; "12 records, 1493 bytes"
SAYREC: LHLD	RECS
	CALL	PRDEC
	LXI	D,MRECS		; " records, "
	MVI	C,PRSTR
	CALL	BDOS
	CALL	PRBYT		; the bytes from the host
	LXI	D,MBYTES	; " bytes",CR,LF
	MVI	C,PRSTR
	JMP	BDOS

;-----------------------------------------------------------------------
; ADDBYT: add A (0..128) to the 24-bit count at BYTES. Changes A. HL
; and B are kept, so CPPAD keeps its pointer and its count.
;-----------------------------------------------------------------------
ADDBYT: PUSH	H
	LXI	H,BYTES
	ADD	M		; low byte + A
	MOV	M,A
	MVI	A,0
	INX	H
	ADC	M		; carry into the middle byte
	MOV	M,A
	MVI	A,0
	INX	H
	ADC	M		; and the top byte
	MOV	M,A
	POP	H
	RET

;-----------------------------------------------------------------------
; PRBYT: print the 24-bit value at BYTES in decimal, as PRDEC does.
; It divides BYTES by ten in place.
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
	LDA	BYTES		; is any of it left?
	LXI	H,BYTES+1
	ORA	M
	INX	H
	ORA	M
	JNZ	PRB1
	MVI	C,PRSTR
	JMP	BDOS

; DIV10T: BYTES (24-bit) = BYTES/10, the remainder in A. It divides one
; byte at a time from the top. The remainder r is below 10, so
; r*256+byte is below 2560 and fits in DE.
DIV10T: XRA	A		; the remainder starts at 0
	LXI	H,BYTES+2	; the top byte first
	CALL	DT1
	DCX	H
	CALL	DT1
	DCX	H
	JMP	DT1		; tail call: A = the remainder

; DT1: acc = A*256 + (HL). (HL) = acc/10, A = acc MOD 10. HL does not
; change.
DT1:	MOV	D,A		; DE = acc, D the high byte (< 10)
	MOV	E,M
	MVI	B,0		; the quotient byte
DT2:	MOV	A,D
	ORA	A
	JNZ	DT3		; acc >= 256, so >= 10
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
DT4:	MOV	M,B		; the quotient byte goes back in place
	MOV	A,E		; the remainder goes to the next byte down
	RET

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

; PRFCB: print the FCB as "d:NAME.EXT".
PRFCB:	LDA	FCB
	ORA	A
	JZ	PF1		; drive 0 = the current drive: no letter
	ADI	'A'-1
	CALL	PUTCH
	MVI	A,':'
	CALL	PUTCH
PF1:	LXI	H,FCB+1
	MVI	B,8
	CALL	PFLD
	LDA	FCB+9
	CPI	' '
	RZ			; no extension
	MVI	A,'.'
	CALL	PUTCH
	LXI	H,FCB+9
	MVI	B,3
PFLD:	MOV	A,M		; print B characters at HL, and skip
	CPI	' '		; the pad spaces
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
; PRDEC: print HL in decimal. The digits go into a buffer from the end,
; and a '$' ends it, so one BDOS 9 call prints the number.
;-----------------------------------------------------------------------
PRDEC:	LXI	D,NUMEND
	MVI	A,'$'
	STAX	D
PD1:	DCX	D
	CALL	DIV10		; HL = HL/10, A = remainder
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
	JNZ	DV2		; H not 0: HL >= 10
	MOV	A,L
	CPI	10
	JC	DV3		; < 10: L is the remainder
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
;=======================================================================
USAGE:	LXI	D,MUSAGE
	MVI	C,PRSTR
	CALL	BDOS
	JMP	0

; MANY is not called, so the stack is empty there. EWILD does a warm
; boot, because a RET would pop data that is not a return address.
; ENODIR, EDISK and ECLOSE come from inside ONE, which was called, so
; ESAY returns.
EWILD:	LXI	D,MEWILD
	MVI	C,PRSTR
	CALL	BDOS
	JMP	0

ENODIR: LXI	D,MENODIR
	JMP	ESAY
EDISK:	LXI	D,MEDISK
	JMP	ESAY
ECLOSE: LXI	D,MECLOSE
	JMP	ESAY

; Print the message and return with CY set.
ESAY:	MVI	C,PRSTR
	CALL	BDOS
	STC
	RET

;=======================================================================
; DATA
;=======================================================================
IDWANT: DB	'ALTAIRSIM HOSTBRIDGE',0

; The printable characters that CP/M does not store in a name. LEGAL
; checks a space, a control character and DEL and up before this table.
; 5CH is the backslash and 2FH the slash. BASENM removed the path first.
ILLCHR: DB	'<>.,'
	DB	3BH		; ';' is not in a string. ASM takes
				; a ';' in a string as a comment.
	DB	':=?*[]|'
	DB	5CH,2FH,0

MNOHB:	DB	CR,LF,'R: no hostbridge board at port B0H.',CR,LF
	DB	'Add a hostbridge board to the machine, or set its port'
	DB	' to B0.',CR,LF,'$'

MUSAGE: DB	CR,LF,'R -- copy a file from the host into CP/M.  (1.3)',CR,LF,CR,LF
	DB	'   R <hostfile> [cpmfile]',CR,LF
	DB	'   R <pattern>',CR,LF,CR,LF
	DB	'Examples',CR,LF
	DB	'   R FOO.ASM            host FOO.ASM -> CP/M FOO.ASM',CR,LF
	DB	'   R FOO.ASM B:BAR.ASM  ...to B:, under another name',CR,LF
	DB	'   R SRC/FOO.ASM        out of a subdirectory of hostdir',CR,LF
	DB	'   R *.ASM              every match',CR,LF,CR,LF
	DB	'Files are copied BYTE FOR BYTE. The last CP/M record is'
	DB	' padded with ^Z.',CR,LF
	DB	'HDIR lists what is on the host. W copies the other way.'
	DB	CR,LF,'$'

MPFX:	DB	'R: $'
MARROW: DB	' -> $'
MRECS:	DB	' records, $'
MBYTES: DB	' bytes',CR,LF,'$'
MERR:	DB	'R: $'
MCRLF:	DB	CR,LF,'$'

MEWILD: DB	'R: a wildcard copies each file under its own name -- so a'
	DB	' second name',CR,LF,'   cannot mean anything. Drop it.'
	DB	CR,LF,'$'
MENODIR:DB	'R: the CP/M directory is full.',CR,LF,'$'
MEDISK: DB	'R: the CP/M disk is full, or is read-only.',CR,LF,'$'
MEWORD: DB	'R: a name is too long.',CR,LF,'$'
MECLOSE:DB	'R: could not close the CP/M file.',CR,LF,'$'

;---- variables --------------------------------------------------------
BUF:	DS	RECLEN		; the DMA buffer: one CP/M record

RECS:	DS	2		; records written to CP/M
BYTES:	DS	3		; the host byte count, 24 bits. 16 bits
				; cannot count the board's 8 MB limit.
FCB:	DS	36		; R's own FCB, not the CCP's at 5CH
HNAME:	DS	HNMAX+1		; the host name, the pattern, or a match.
				; +1: RECVZ can write its NUL one byte
				; past the count.
CNAME:	DS	HNMAX+1		; the CP/M name, if the user gave one.
				; MKFCB cuts it to 8.3.
IDBUF:	DS	IDMAX+1		; the board's signature (+1: as above)
NUMBUF: DS	8		; 16777215, the largest 24-bit count, is
NUMEND: DS	1		; 8 digits. PRDEC and PRBYT end here.

	DS	64		; the stack
STACK	EQU	$

	END	START
