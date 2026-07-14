;  2SIO echo routine

	org	0
	mvi	a,03h	;reset UART
	out	10h
	mvi	a,15h	;no RI, no XI, RTS Low, 8n1, /16
	out	10h

wtRcv	in	10h	;character received?
	rrc		;receive flag in lsb
	jnc	wtRcv
	in	11h	;read the character
	out	11h	;echo it back out
	jmp	wtRcv
	end

