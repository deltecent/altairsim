;----------------------------------------------------------------------
;  HELLO.ASM -- print a greeting through CP/M, one character at a time.
;
;  Assembled on the machine with CP/M's own ASM.COM.  It is meant to
;  print HELLO, WORLD and return to the A> prompt.
;
;  IT DOES NOT.  There is ONE deliberate bug in the loop below, and the
;  whole point of this example is that an AI assistant finds it and fixes
;  it over MCP -- build it, run it, watch what it prints, single-step the
;  loop under DDT to see why, correct the source, and reassemble. Read the
;  README beside this file; do not read past this line if you would rather
;  hunt the bug yourself.
;----------------------------------------------------------------------

BDOS	EQU	0005H		;CP/M entry point
CONOUT	EQU	2		;BDOS function 2: console output, character in E
CR	EQU	0DH
LF	EQU	0AH

	ORG	0100H		;the CP/M transient program area

START:	LXI	H,MSG		;HL -> the first character of the string
LOOP:	INX	H		;advance to the next character
	MOV	A,M		;fetch it
	ORA	A		;the 0 terminator?
	JZ	DONE		;yes -- nothing left to print
	MOV	E,A		;no  -- the character goes in E for BDOS
	MVI	C,CONOUT
	PUSH	H		;BDOS is free to clobber HL, so save it
	CALL	BDOS
	POP	H		;...and get it back
	JMP	LOOP
DONE:	RET			;back to CP/M

MSG:	DB	'HELLO, WORLD',CR,LF,0

	END	START
