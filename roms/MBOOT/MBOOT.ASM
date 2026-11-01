;**************************************************************************
;
;  Micropolis FDC Boot ROM
;
;  This ROM loads sector 0 on track 0 of drive 0 into RAM and
;  jumps to it.
;
;  The ROM is position independent so the ROM does not have to be rebuilt
;  if the board is jumpered to a different address. Upon entry, the ROM
;  does a position independent copy of the boot code to a fixed location
;  in RAM (A2h) and then executes the boot code from RAM. 
;
;  Prior to jumping to the loaded boot sector, this ROM places the
;  address of the FDC register space in memory location A2h. Also,
;  register DE points to the ROM base address when control is passed
;  to the loaded code. The boot loaders for typical operating systems
;  (e.g., MDOS, VG CP/M 2.2, Lifeboat CP/M 2.2) expect these parameters
;  to have been set. Further, the boot loaders (e.g. the VG CP/M loader)
;  expect the RAM image code to be in place and may make calls to the
;  rdSec routine at A6h to perform disk I/O.
;
;  Disassembled by Mike Douglas, November 2016
;
;***************************************************************************

SCLEN 	equ	268		;sector len without sync byte and checksum
PTROFF	equ	010Ah		;offset of load address ptr within sector 0

; FDC Status Register

; Sector register is at FDCBASE+0
;     0-3 sector count
;	4 spare
;	5 spare
;	6 sector interrupt flag
;	7 sector flag

SIFLG	equ	040h		;sector interrupt flag
SFLG	equ	080h		;sector start flag
DTMR	equ	020h		;2mhz/4mhz jumper for timers

; Status register is at FDCBASE+1
;     0-1 unit address
;	2 unit selected (low true)
;	3 track 0
;	4 write protect
;	5 disk ready
;	6 pinte
;	7 transfer flag

TFLG	equ	80h		;transfer flag
INTE	equ	40h		;interrupt enable
RDY	equ	20h		;disk ready
WPT	equ	10h		;write protect
TK0	equ	08h		;track zero
USLT	equ	04h		;unit selected

; Command register at FDCBASE+0 (and again at FDCBASE+1)
;	bits 1-0 command modifier
;	bits 7-5 command

SLUN	equ	020h		;select unit
;  Modifier contains unit address

SINT	equ	040h		;set interrupt
;  Modifier = 1 enable interrupt
;  	      0 disable interrupt

STEP	equ	060h		;step carriage
STEPO	equ	000h		;step out modifier
STEPI	equ	001h		;step in modifier

WTCMD	equ	080h		;enable write
;  No modifier used

RESET	equ	0A0h		;reset controller
;  No modifier used


; An ORG of 6Bh is used so the section of this PROM that ends
;   up in RAM at A2h (RAMIMG) is also at A2h when this file
;   is assembled. The PROM code prior to RAMIMG is position
;   independent, so its assembled address does not matter.
;   Note that the code output by this assembly should NOT be
;   offset when burned into the PROM. 

	org	06Bh

;--------------------------------------------------------------------
; ROM entry point (romEnt)
;    Position independent code to copy the boot code to a fixed
;    location in RAM for execution and to save the address found
;    for the FDC.
;--------------------------------------------------------------------
romEnt:	di			;disable interrupts

; Call a "RET" instruction to determine and save the ROM base address
;   in ROMADR

	lxi	h,RAMIMG	;HL=SP=eventual destination in RAM
	sphl

	mvi	m,ret		;create and call a return instruction
	call	RAMIMG		;puts address of "xchg" in ROMADR
	xchg			;DE->RAMIMG
	lhld	ROMADR		;HL->"xchg" instruction (above) in ROM
	mvi	l,0		;HL->base address of ROM
	push	h		;ROMADR=base address of boot ROM

; Put the address of the copy loop below in stack memory so we can
;   use an rz instruction instead of a jz instruction for looping.
;   This allows the copy loop to be position independent.

	lxi	b,(copy-romEnt) AND 0FFh     ;BC=offset of copy loop below
	dad	b		;HL=address of copy loop in ROM
	push	h		;save in RAM for "jz" via rz
	pop	h		;restore stack pointer

	mvi	c,(RAMIMG-copy) AND 0FFh    ;BC=add'l offset to RAMIMG
	dad	b		;HL=address of RAMIMG in ROM

	mvi	b,(IMGEND-RAMIMG) AND 0FFh  ;B=number of bytes to move

	xchg			;DE->ROM source, HL->RAM dest
	
; Position independent loop to copy data starting at RAMIMG in ROM to
;   RAM starting at A2h

copy:	dcx	sp		;(SP)->copy in ROM for jz via rz
	dcx	sp

	ldax	d		;copy byte from ROM to RAM
	mov	m,a
	cmp	m		;verify value in RAM matches
	rnz 	;jz copy	;memory move failed, try again

	inx	h		;next RAM location
	inx	d		;next ROM location
	dcr	b		;decrement byte count
	rnz	;jz copy	;loop until done

; Boot code has been copied to RAM at A2h. Determine the FDC address and save
;   it at fdcAdr for use by boot code. Pass control to the code in RAM.

	pop	h		;fix stack pointer

	lhld	ROMADR		;HL->base of boot ROM
	lxi	d,200H		;DE=offset of FDC registers from boot ROM
	dad	d		;HL->FDC registers
	shld	fdcAdr		;save FDC register base

	mvi	m,RESET		;reset controller
	jmp	ramEnt		;pass control to RAM

;********************************************************************
; RAM Image
;    The following code from RAMIMG to IMGEND gets copied to RAM
;    starting at location A2h. The entry point is further down
;    at ramEnt.
;********************************************************************
RAMIMG	equ	$
ROMADR	equ	RAMIMG-2	;ROM address saved here

fdcAdr:	dw	0		;FDC register address placed here
lodAdr:	dw	0		;sector load address placed here

;--------------------------------------------------------------------
; rdSec - Read sector into RAM
;
; On Entry:
;   DE->RAM buffer
;   B = sector to read
;   C = number of bytes to read/2
;
; On Exit:
;   Z set = read successful
;   Z clear = checksum error
;   A = checksum from disk
;   B = computed checksum
;   C = 0
;   HL->one past last byte of sector
;   DE->FDC Data Register
;--------------------------------------------------------------------
rdSec:	lhld	fdcAdr		;HL->FDC registers (sector register)

wtSec:	mov	a,m		;sector status
	ani	SFLG		;wait for sector true
	jz	wtSec

	mov	a,m		;re-read sector register to be safe
	ani	0Fh		;get sector number alone
	xra	b		;right sector number?
	jnz	wtSec		;no, keep looking

	inx	h		;HL->status register

wtXfer:	ora	m		;wait for transfer flag (bit 7)
	jp	wtXfer

	inx	h		;HL->data register
	mov	a,m		;read data (pull off sync byte)
	xra	a		;start with carry clear
	xchg			;DE->data register

	mvi	b,0		;B=checksum, init to zero
	nop			;timing for FDC stall logic
	nop

; Read sector data from disk into RAM two bytes at a time

movSec:	ldax	d		;read data byte
	mov	m,a		;save in buffer
	inx	h
	adc	b		;update checksum
	mov	b,a

	ldax	d		;repeat for 2nd byte
	mov	m,a
	inx	h
	adc	b
	mov	b,a

	dcr	c		;decrement byte count
	jnz	movSec		;repeat until done

; Read and verify checksum

	ldax	d		;A=checksum from disk
	cmp	b		;compare to computed checksum	
	ret			;return result

;--------------------------------------------------------------------
; RAM entry point (ramEnt)
;    ramEnt is jumped to by the ROM after copying RAMIMG to RAM
;    at A2h. This code selects the drive, restores to track zero,
;    reads sector zero into RAM at a fixed buffer address, then
;    looks for a load address specified at offset 10Ah in the sector.
;    Sector zero is then re-read into the specified RAM address.
;    Finally, control is passed to the loaded code.
;--------------------------------------------------------------------
ramEnt:	lhld	fdcAdr		;HL->FDC command register
	mvi	m,SLUN+0	;select drive zero

; Verify the drive is selected and ready

	inx	h		;HL->status register
	mov	a,m		;A=drive status
	dcx	h		;restore HL->command register
	ani	RDY+USLT	;isolate ready and selected bits
	xri	RDY		;verify ready and selected (USLT negative)
	jnz	ramEnt		;not ready, start over

; Delay 250ms to allow drive spin-up time

	mvi	c,94		;delay 250ms (94*2.68ms) for drive spin-up
	call	delay

; Verify the drive is still selected and ready

	inx	h		;HL->status register
	mov	a,m		;A=drive status
	dcx	h		;restore HL->command register
	ani	RDY+USLT	;isolate ready and selected bits
	xri	RDY		;verify ready and selected (USLT negative)
	jnz	ramEnt		;not ready anymore, start over

; Seek to track zero. If already on track zero, step in 8 tracks and then
;    seek to track zero.

	inx	h		;HL->status register
	mov	a,m		;A=drive status
	ani	TK0		;at track zero?
	dcx	h		;restore HL->command register
	jz	seek0		;not track zero, go seek to zero

	mvi	b,8		;step in 8 tracks

seekIn:	mvi	m,STEP+STEPI	;step command with step in modifier
	mvi	c,15		;delay 40 ms (15*2.68ms)
	call	delay

	dcr	b		;decrement step counter
	jnz	seekIn		;complete all steps

; Execute the seek to track zero

seek0:	inx	h		;HL->status register
	mov	a,m
	ani	TK0		;at track zero?
	dcx	h		;restore HL->command register
	jnz	doRead		;at track zero, go do the read

	mvi	m,STEP+STEPO	;step command with step out modifier
	mvi	c,15		;delay 40 ms (15*2.68ms)
	call	delay
	jmp	seek0		

; At track zero. Read sector zero into a fixed location in RAM (IMGEND),
;   then look at offset 10Ah in the sector just read to get the load
;   address specified within the boot sector. Re-read sector zero
;   into the specified address and jump to it.

doRead:	lxi	h,IMGEND	;HL->fixed location in RAM for sector 
	call	read0		;read sector 0	
	jnz	ramEnt		;failed, start over

	lhld	IMGEND+PTROFF	;HL->requested RAM address for disk's loader
	shld	lodAdr		;save the final load address

	call	read0		;re-read sector zero into specified address
	jnz	ramEnt		;failed, start over

	lhld	lodAdr		;HL->specified load address
	lxi	d,0Ch		;code starts 12 bytes into the sector
	dad	d		;HL->specified entry address
	pop	d		;DE->ROM base address
	pchl			;jump to the boot loader read from disk

;--------------------------------------------------------------------
; read0 - Read sector zero
;
; On Entry:
;   HL->RAM buffer
;
; On Exit:
;   Z set = read successful
;   Z clear = track or sector number error
;   HL->RAM buffer
;
; Clobbers all other registers
;--------------------------------------------------------------------
read0:	push	h		;save buffer address
	xchg			;DE->sector buffer

	lxi	b,SCLEN SHR 1	;B=sector 0, C=bytes to read/2
	call	rdSec		;read the sector
	pop	h		;restore buffer address
	jnz	read0		;retry if read error

; Verify track and sector on disk is track 0, sector 0

	push	h		;save buffer address
	mov	a,m		;A=track number from disk
	inx	h		;HL->sector number from disk
	ora	m		;verify both track and sector are zero
	pop	h		;restore buffer address
	ret			;return with comparison result

;--------------------------------------------------------------------
; delay - Delay 2.68ms times value specified in C. Looks at
;    2mhz/4mhz jumper on the FDC to determine CPU speed.
;
; On Entry:
;   C = delay counter (2.68ms per bit)
;
; On Exit:
;   A = 0
;   C = 0
;--------------------------------------------------------------------
delay:	mov	a,m		;A=sector register from FDC
	ani	DTMR		;see if 4mhz jumper in place	
	mov	a,c		;A=requested delay
	jnz	not4m		;not 4mhz
	rlc			;double the count for 4mhz CPU

not4m:	mov	c,a		;save possibly adjusted delay count

dLoop1:	mvi	a,255		;255 times through the inner loop

dLoop2:	sui	1
	ora	a
	jnz	dLoop2

	dcr	c
	jnz	dLoop1
	ret

IMGEND	equ	$

	nop			;match remaining bytes in ROM
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	jmp	rdSec		;matches ROM, but not a great jump to use

	end

