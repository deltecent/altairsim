;----------------------------------------------------------------------
;  HELLO.ASM -- a symbolic-debugger demo for altairsim.
;
;  Prints "HELLO, WORLD" to the console through the 88-2SIO, then halts.
;  Assembled at 0100H. Small on purpose: every label, every EQU and every
;  kind of operand is here to be looked at under the debugger.
;----------------------------------------------------------------------

TTYS	EQU	10H	;2SIO status port (channel A)
TTYD	EQU	11H	;2SIO data port
TXRDY	EQU	02H	;status bit: transmitter empty
CR	EQU	0DH
LF	EQU	0AH

	ORG	0100H

START:	LXI	SP,STACK	;set up a stack
	LXI	H,MSG		;HL -> the string
LOOP:	MOV	A,M		;fetch the next character
	ORA	A		;zero terminator?
	JZ	DONE		;yes -- we are finished
	CALL	PUTC		;no  -- send it
	INX	H		;step to the next character
	JMP	LOOP
DONE:	HLT		;stop; the debugger takes over

;  PUTC -- send the character in A, waiting for the transmitter first.
PUTC:	PUSH	PSW
PWAIT:	IN	TTYS		;read 2SIO status
	ANI	TXRDY		;transmitter ready?
	JZ	PWAIT		;no -- keep polling
	POP	PSW
	OUT	TTYD		;yes -- send the byte
	RET

MSG:	DB	'HELLO, WORLD',CR,LF,0
	DS	16		;room for the stack to grow down into
STACK	EQU	$		;top of stack (grows downward from here)

	END	START
