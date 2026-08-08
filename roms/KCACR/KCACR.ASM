*******************************************************************
*
*  KCACR.ASM - PROM for the Altair 680 to support the 680-KCACR
*     cassette interface board
*
*  Disassembled by Mike Douglas, July 2022
*
*******************************************************************

* 680 monitor equates

BADDR	equ	$FF62		;input address to X
OUTCH	equ	$FF81		;display character from B
OUTS	equ	$FF82		;display a space
XHIB2X	equ	$FF68		;load XHI:B into X
MONITR	equ	$FFAB		;re-enter monitor

* Cassette equates

SIOSR	equ	$F010		;cassette UART status register
SIODR	equ	$F011		;data register
CR	equ	$0D
LF	equ	$0A

* Page zero data

	org	$F4		
endAddr	rmb	2		;end address to write
	rmb	2
temp	rmb	1
rdCount	rmb	1		;byte count in a line when reading
XHI	rmb	1		;XHI:XLO from 680 monitor
XLO	rmb	1
	rmb	1
curAddr	rmb	2		;current write address
wrCount	rmb	1		;byte count in a line when writing

*----------------------------------------------------------------
*  LOAD entry point is $FD00. Load S-Record data from cassette
*     into memory.
*----------------------------------------------------------------
	org	$FD00

load	bsr	casRead		;look for an S record
	subb	#'S
	bne	load

	bsr	casRead		;read next to see if S1 or S9
	cmpb	#'9
	beq	s9Rec		;S9
	cmpb	#'1		;S1?
	bne	load		;no, ignore

	clra			;accumulate checksum in A
	bsr	rdByte		;get the byte count
	subb	#2		;take out the address bytes
	stab	rdCount		;save the read counter
	bsr	rdWord		;read the address and put in X

rdLine	bsr	rdByte		;read a data byte
	dec	rdCount		;repeat for all bytes on the line
	beq	valCsum		;done, go validate the checksum

	stab	0,x		;save the byte
	cmpb	0,x
	bne	memErr		;didn't save properly
	inx
	bra	rdLine

* valCsum - Validate the checksum. If good, go look for another line

valCsum	inca			;validate checksum
	beq	load		;good - keep going

* hexErr, memErr - Die in a loop displaying the error code

hexErr	ldab	#'C		;indicate hex/checksum error
	fcb	$8C		;CPX skip trick
memErr	ldab	#'M

dieLoop	jsr	OUTCH		;die here sending error code
	bra	dieLoop

* rdNibl - Read an ASCII hex nibble into B in binary

rdNibl	bsr	casRead		;get next character
	subb	#'0		;convert to binary
	bmi	hexErr		;too small
	cmpb	#9
	ble	rnExit		;valid 0-9
	cmpb	#'A-'0		;below 'A'?
	bmi	hexErr		;yes, error
	cmpb	#'F-'0		;above 'F'?
	bgt	hexErr		;yes
	subb	#7		;adjust for A to F
rnExit	rts

* rdByte - Read an ASCII hex byte into B in binary

rdByte	bsr	rdNibl		;get msn in B
	aslb			;put in MSN
	aslb
	aslb
	aslb
	stab	temp
	bsr	rdNibl		;get lsn in B
	addb	temp		;form final byte
	aba			;update checksum
	rts

* rdWord - Read an ASCII hex word into X in binary

rdWord	bsr	rdByte		;read MSB into XHI
	stab	XHI
	bsr	rdByte		;read LSB into B
	jmp	XHIB2X		;load XBI:B into X

* casRead - Read a character from the cassette serial port into B

casRead	ldab	SIOSR		;wait for new character
	rorb
	bcs	casRead

	ldab	SIODR		;load and return char
	andb	#$7F
	rts

* s9Rec - Load complete, exit to the 680 monitor

s9Rec	bra	exitMon		;exit to monitor

*----------------------------------------------------------------
*  SAVE entry point is $FD74. Write a range of memory to the
*     cassette in S-Record format.
*----------------------------------------------------------------
mS1	fcb	CR,LF,'S,'1+$80

save	bsr	getAddr		;get start addr from user
	stx	curAddr
	bsr	getAddr		;get end address from user
	stx	endAddr
	bsr	wrNulls		;write leader nulls

* wrLine - Write and S-record line of data

wrLine	ldx	#mS1-1		;write “S1”
wrS1	inx
	ldab	0,x
	bsr	casWrt
	bpl	wrS1

	ldaa	endAddr+1	;compute end-current
	suba	curAddr+1
	ldab	endAddr
	sbcb	curAddr
	bne	wrFull		;not last 256 bytes

	cmpa	#@16		;partial line? (octal vs decimal
	bcs	wrPart		;  16 is original mistake)

wrFull	ldaa	#@16-1		;else, write full line (octal vs
				;  decimal 16 is original mistake)
wrPart	staa	wrCount
	adda	#4		;A=length field of the srec
	bsr	wrByte

	ldx	#curAddr	;write the address bytes
	bsr	wrNext
	bsr	wrNext

	ldx	curAddr		;X->data to write

wrData	bsr	wrNext		;write line of data bytes
	dec	wrCount
	bpl	wrData

	stx	curAddr		;save current address
	coma			;compute and write checksum
	bsr	wrByte

	dex			;remove last increment for checksum
	cpx	endAddr		;reach end?
	bne	wrLine		;no, repeat

	ldab	#'S		;write the S9 record
	bsr	casWrt
	ldab	#'9
	bsr	casWrt
	bsr	wrNulls		;follow with nulss

* exitMon - Jump back into the 680 monitor

exitMon	jmp	MONITR

* wrNulls - Write 40 nulls (documentation shows 50)

wrNulls	ldaa	#@50		;octal 50 is original mistake
	clrb
nullLp	bsr	casWrt
	deca
	bne	nullLp
	rts

* wrNext - Write the next byte from memory as pointed to by X to the
*   cassette. Checksum is accumlated in A.

wrNext	ldab	0,x		;B=next byte to write
	aba			;update checksum in A
	psha
	tba
	bsr	wrByte		;write the byte
	pula
	inx
	rts

* getAddr - Get an address from the operator

getAddr	jsr	OUTS
	ldab	#'?
	jsr	OUTCH
	jmp	BADDR

* wrByte - Write a byte from A in ASCII hex format to the cassette

wrByte	tab			;work in B
	lsrb			;put MSN into LSN
	lsrb
	lsrb
	lsrb
	bsr	wrNibl		;write MSN

	tab			;LSN back in B

* wrNibl - Write a nibble from B in ASCII hex format to the cassette

wrNibl	andb	#$0F		;get LSN alone
	addb	#'0		;convert 0-F to ASCII
	cmpb	#'9
	bls	casWrt
	addb	#7

* casWrt - Write byte from B to the cassette UART

casWrt	pshb
wrWait	ldab	SIOSR
	bmi	wrWait

	pulb
	stab	SIODR
	rts

	end
