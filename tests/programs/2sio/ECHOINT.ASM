; 2SIO echo test using receive interrupts

	org	0
	lxi	sp,0100h	;init stack pointer
	mvi	a,03h		;reset the ACIA
	out	010h
	mvi	a,095h		;8n1 rcv ints on
	out	010h
	ei			;enable 8080 interrupts

; The loop below represents "normal" processing a program may
;    be doing.

loop	nop
	nop
	nop
	jmp	loop

; Interrupt service routine for RST7 is at 038h. This routine
;    saves the accumulator and PSW, then echoes the character received.
;    If this interrupt is shared, then the commented statements to
;    check for a character would be included and used to branch to
;    a 2nd device to check. 

	org	038h		;RST7 entry address
	push	a		;save A and status flags
;	in	010h		;verify a new character is present
;	rrc			;lsb has RDRF flag
;	jnc	nextDev		;no character, try next device
	in	011h		;read the character
	out	011h		;echo it
	pop	a		;restore A and status flags
	ei			;re-enable 8080 interrupts
	ret

