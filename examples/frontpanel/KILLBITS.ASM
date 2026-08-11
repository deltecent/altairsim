;
;  Kill the Bit game by Dean McDaniel, May 15, 1975
;
;  Object: Kill the rotating bit. If you miss the lit bit, another
;	   bit turns on leaving two bits to destroy. Quickly 
;	   toggle the switch, don't leave the switch in the up
;	   position. Before starting, make sure all the switches
;	   are in the down position.	   
;
	org	0
	lxi	h,0		;initialize counter
	mvi	d,080h		;set up initial display bit
	lxi	b,0eh		;higher value = faster
beg:	ldax	d		;display bit pattern on
	ldax	d		;...upper 8 address lights
	ldax	d
	ldax	d
	dad	b		;increment display counter
	jnc	beg
	in	0ffh		;input data from sense switches
	xra	d		;exclusive or with A
	rrc			;rotate display right one bit
	mov	d,a		;move data to display reg
	jmp	beg		;repeat sequence
	end
