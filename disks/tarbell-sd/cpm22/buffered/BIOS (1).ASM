*****************************************************************************
;
;   CP/M 2.2 BIOS for Tarbell single density FDC and Altair 88-2SIO serial
;	I/O. The console, punch, and reader use the 1st port on the 2SIO
;	and the list device goes through the 2nd port on the 2SIO. After
;	boot, the program IOBYTE.COM can be run which installs full
;	IOBYTE support. See IOBYTE.ASM for details.
;
;	Easily modified for other controllers which use the WD1771/9x FDC
;       chip and for other serial I/O boards.
;
;	This BIOS uses full track buffering on both reads and writes to
;	improve performance. This BIOS performs 20%-70% faster than the
;       non-buffered BIOS for typical disk based operations.
;
;	In order to fit in the boot tracks of a standard SSSD IBM format
;	soft sectored 8" disk, the code/initialized data portion of this
;	BIOS can only be 380h bytes long. This restriction substantially
;	reduces the features that can be provided by this BIOS. This is 
;	why IOBYTE support is installed after boot by IOBYTE.COM. The
;	source file has a conditional assembly statement to detect when
;	this size limit is exceeded. Look for "BIOS IS TO BIG ****" as
;	an error statement when assembling. 
;
;	MODE byte, default IOBYTE, send-null flag locations for patching:
;		33h, 34h, 35h from start of BIOS
;		1FB3h, 1FB4h, 1FB5h in memory after MOVCPM
;
;       To patch an update to this BIOS into a CP/M image created and saved
;	after running MOVCPM, use the following commands:
;
;	   A>DDT CPM56.COM    (specify the saved CP/M image file)
;	   -IBIOS.HEX
;	   -Rxxxx      	      (xxxx = the value of the BIAS equate below)
;	   -G0		      (Go zero, not "oh")
;
;	Ver	  Date		     Description
;	---	----------	------------------------------------------
;	1.0 	03/24/2016	Mike Douglas, Original
;
;	1.1	11/02/2017	Mike Douglas
;				Do not write sectors marked as "bad" when
;				flushing the track buffer. Change secTran
;				to use an algorithm instead of a table to
;				save code space.
;
;	1.2	11/15/2017	Mike Douglas
;				Support IOBYTE for the LST device to allow
;				listing to the 1st or 2nd 2SIO port. To
;				free up code space, a single DPH entry is
;				used for all drives and the ALV and CSV
;				pointers are filled in during drive select.
;
;	1.3	02/28/2019	Mike Douglas
;				Undo DPH update of ver 1.2 and go back to
;				a DPH table per drive. CP/M puts persistent
;				data per drive in the three "scratch" words
;				in the DPH. Due to space constraints, only
;				three drives are now supported instead of
;				four. Invalidate the track buffer during
;				warm boot so that retrys actually re-read
;				the disk. Display "delayed write error" when
;				buffer flush fails due to Read Address Mark
;				error. Change dSeek retry logic to fix bug 
;				that could result in a clobbered retry 
;				counter in register E. Change delay loop to 
;				to delayMS to use millisecond input values.
;				Tweak serial I/O calls to free up a few
;				bytes of code space.
;
;	1.4	11/03/2019	Mike Douglas
;				Eliminate limited IOBYTE support and add 200h
;				bytes of uninitialized space where code for
;				full IOBYTE support is inserted after boot
;				by IOBYTE.COM. Add Extended Function support.
;				This is a hook for non-standard BIOS functions
;				via the select disk (selDsk) entry point. This
;				BIOS supports "Flush Track Buffer." See selDsk
;				for details. Fixed the logic in wrtTrk that
;				skips writing sectors flagged as bad in the
;				track buffer. The code was throwing "Delayed
;				write error" if a bad sector (from reading)
;				was in the track buffer. These sectors should
;				just be skipped when writing - not an error.
;
;	1.5	06/23/2020	Mike Douglas
;				Ensure a 600us delay after writes to better
;				support drives with tunnel erase heads.
;
;****************************************************************************

;  Assembly options

MEMSIZE	equ	56		;memory size in K bytes
IOBENBL	equ	1		;enable reserved space for IOBYTE code
IOBLEN	equ	200h		;length of reserved space for IOBYTE code

;  CP/M location equates

CCPLEN	equ	0800h		;CPM 2.2 fixed length
BDOSLEN	equ	0E00h		;CPM 2.2 fixed length
BIOSLEN	equ	1200h+(IOBENBL*IOBLEN)	;length of this BIOS

CCPBASE	equ	MEMSIZE*1024 - BIOSLEN - BDOSLEN - CCPLEN
BDOSENT	equ	(CCPBASE+CCPLEN+6)	 ;entry address of BDOS
BIOSBAS	equ	(CCPBASE+CCPLEN+BDOSLEN) ;base address of this BIOS

BIAS	equ	0980h-CCPBASE

; CP/M page zero equates

WBOOTV	equ	00h		;warm boot vector location
BDOSV	equ	05h		;bdos entry vector location
CDISK	equ	04h		;CPM current disk
DEFDMA	equ	80h		;default dma address
IOBYTE	equ	03h

; CP/M (logical) disk information

NUMDISK	equ	3		;three drives supported
NUMTRK	equ	77		;number of tracks on the disk
NUMSEC	equ	26		;logical (128 byte) sectors per track
SECLEN	equ	128		;CP/M sector length
WBSECS	equ	(CCPLEN+BDOSLEN)/SECLEN   ;warm boot sectors to read

DSM	equ	242		;max block number (243 blocks of 1024 bytes)
DRM	equ	63		;max directory entry number (64 entries) 
CKS	equ	(DRM+1)/4	;directory check space

; Head settle delay, head reverse delay.
;    Based on the drive used, specify the head settle delay and the head
;    direction reversal delay (both are additional delay after the final 
;    10ms step delay provided by the 1771).

HDSETL	equ	120		;12ms additional head settle after step
REVDIR	equ	120		;12ms delay when reversing step direction

; 2SIO Serial Board Equates

CONCR	equ	10h		;1st port on 2SIO board - control register
CONDR	equ	11h		;1st port on 2SIO board - data register
LSTCR	equ	12h		;2nd port on 2SIO board - control register
LSTDR	equ	13h		;2nd port on 2SIO board - data register
RDRF	equ	01h		;read data register full flag
TDRE	equ	02h		;transmit data register empty flag

; Misc equates

UNDEF	equ	0FFh		;undefined value
RDTRIES	equ	5		;number of read attempts
CR	equ	13		;ascii for carriage return
LF	equ	10		;ascii for line feed

	org	BIOSBAS
;-----------------------------------------------------------------------------
;  BIOS Entry Jump Table. 
;-----------------------------------------------------------------------------
	jmp	boot		;cold start
wboote	jmp	wBoot		;warm start
	jmp	conSt		;console status
	jmp	conIn		;console character in (and disk flush)
	jmp	conOut		;console character out
	jmp	listOut		;list character out
	jmp	conOut		;punch character out
	jmp	conIn		;reader character in
	jmp	home		;move head to home position
	jmp	selDsk		;select disk
	jmp	setTrk		;set track number
	jmp	setSec		;set sector number
	jmp	setDma		;set dma address
	jmp	read		;read disk
	jmp	write		;write disk
	jmp	listSt		;return list status
	jmp	secTran		;sector translate

;---------------------------------------------------------------------------
; MODE byte - At offset 33h from start of BIOS
;---------------------------------------------------------------------------
fCLDCMD	equ	01h		;true = CCP process cmd on cold start
fWRMCMD	equ	02h		;true = CCP process cmd on warm start

mode	db	00h		;don't process command line

;---------------------------------------------------------------------------
; IOBYTE - At offset 34h from start of BIOS. IOBYTE is only supported
;   if IOBENBL is true and IOBYTE.COM is run after cold boot (manually
;   or automatically). If IOBYTE support is not enabled, port assignment
;   is fixed with the defaults shown here. 
;
; CON device (bits 1,0):                  PUN device (bits 5,4):
;   00 - TTY on 88-SIO at 0/1               00 - TTY on 88-SIO at 0/1
;   01 - CRT on 88-2SIO at 10h/11h*         01 - PTP on 88-2SIO at 10h/11h*
;   10 - BAT (indirect through RDR,LST)     10 - UP1 on cassette (SIO at 6/7)
;   11 - UC1 on 88-2SIO at 12h/13h          11 - UP2 on 88-2SIO at 12h/13h
;
;
; RDR device (bits 3,2):                  LST device (bits 7,6):
;   00 - TTY on 88-SIO at 0/1               00 - TTY on 88-SIO at 0/1
;   01 - PTR on 88-2SIO at 10h/11h*         01 - CRT on 88-2SIO at 10h/11h
;   10 - UR1 on cassette (SIO at 6/7)       10 - LPT on 88-2SIO at 12h/13h*
;   11 - UR2 on 88-2SIO at 12h/13h          11 - UL1 on 88-LPC at 2/3
; 
; *Default IOBYTE
;---------------------------------------------------------------------------
defIob	db	95h

;----------------------------------------------------------------------------
; sndNull flag - At offset 35h from start of BIOS.
;    Determines if a NULL should be transmitted following a CR when
;    writing to the console device. The end user can patch this byte
;    to nonzero for Teletype support
;----------------------------------------------------------------------------
sndNull	db	0		;true if null should be sent after CR

;----------------------------------------------------------------------------
; boot - Cold boot BIOS entry. CPM is already loaded in memory. Hand
;    control to the CCP.
;----------------------------------------------------------------------------
boot	lxi	sp,0100h	;init stack pointer

	xra	a
	sta	CDISK		;current CPM disk is drive zero

	mvi	a,03h		;reset 2SIO ports
	out	CONCR
	out	LSTCR
	mvi	a,11h		;select 8N2
	out	CONCR
	out	LSTCR

	lda	defIob		;initialize IOBYTE
	sta	IOBYTE

	call	dispMsg		;display welcome message
	db	CR,LF,LF
	db	MEMSIZE/10 + '0',MEMSIZE mod 10 + '0'
	db	'K CP/M 2.2b v1.5',CR,LF
	db	'SD Tarbell on Altair',CR,LF+80h

; Determine whether or not execution of a cold start command line is
;    requested, then enter CP/M.

	lda	mode		;see if cold start command line requested
	ani	fCLDCMD
	push	psw		;save result

	jmp	coldCpm		;enter CP/M

;----------------------------------------------------------------
; wBoot - Warm boot BIOS entry. Reload CPM from disk up to, but
;    not including the BIOS. Re-enter CPM after loading.
;----------------------------------------------------------------
wbRetry	sta	bfDrive		;force re-read of disk (A=0FFh)

wBoot	lxi	sp,0100h	;init stack pointer

	mvi	c,0		;boot from drive zero
	call	selDsk		;select drive zero

	lxi	b,WBSECS*256+2	;B=secs to read, C=starting sec
	lxi	h,CCPBASE	;HL->CP/M load address
	xra	a		;starting track = 0

wbTrk	sta	trkNum		;save track number to use

; wbLoop - read sector loop for warm boot

wbLoop	shld	dmaAddr		;set read destination
	call	setSec		;set sector from C

	push	b		;save sector count and number
	call	read		;read next sector
	pop	b
	jnz	wbRetry		;failed read, start over
	
	dcr	b		;decrement total sector count
	jz	entCpm		;warm boot is done, enter CP/M
	
	inr	c		;bump to next sector number
	mvi	a,NUMSEC	;A=number of sectors per track
	cmp	c		;compare NUMSEC - next sector
	jnc	wbLoop		;sector is 26 or lower, go read it
	
	mvi	c,1		;resume with C=sector 1 and...
	mov	a,c		;  A=track 1
	jmp	wbTrk

; entCpm - Enter CPM. Set page zero variables, enter CPM with or without
;   command line based on MODE flags.

entCpm	lda	mode		;see if warm start command line requested
	ani	fWRMCMD
	push	psw		;save result

coldCpm	lxi	h,DEFDMA	;set the default dma address (080h)
	shld	dmaAddr

	mvi	a,JMP		;8080 "jump" opcode
	sta	WBOOTV		;store in 1st byte of warm boot vector
	sta	BDOSV		;and 1st byte of bdos entry vector

	lxi	h,wboote	;get the warm boot address
	shld	WBOOTV+1	;and put it after the jump

	lxi	h,BDOSENT	;BDOS entry address
	shld	BDOSV+1		;put it after the jump opcode

	lda	CDISK		;get current disk number
	mov	c,a		;pass it to CPM in register c

	pop	psw		;restore command line test
	jz	CCPBASE+3	;enter CCP and don't run command line
	jmp	CCPBASE		;enter CCP with possible cmd line

;---------------------------------------------------------------------------
; conSt - Console status BIOS entry point. Return FF if character ready,
;    return zero otherwise.
;---------------------------------------------------------------------------
conSt	in	CONCR		;test for serial data present
	ani	RDRF
	rz			;return zero if no data present

	mvi	a,0FFh		;else return FF if data present
	ret

;---------------------------------------------------------------------------
; conIn - Console input byte BIOS entry point. Wait for character
;    and return it
;---------------------------------------------------------------------------
conIn	call	invFlsh		;invalidate and flush track buffer
	
ciLoop	call	conSt		;character present?
	jz	ciLoop		;no, wait

	in	CONDR		;read the character
	ani	07Fh		;clear msb
	ret

;---------------------------------------------------------------------------
; conOut - Console output BIOS entry point. Output the character in C
;    through the serial port.
;---------------------------------------------------------------------------
conOut	in	CONCR		;test ready to transmit
	ani	TDRE
	jz	conOut		;not ready, wait

	mov	a,c		;transmit the passed character
	ani	7Fh		;force MSBit to zero
	out	CONDR
	ret

;---------------------------------------------------------------------------
; listSt - List status BIOS entry point. Return FF if OK to transmit,
;    return zero otherwise.
;---------------------------------------------------------------------------
listSt	in	LSTCR		;2nd 2SIO port ready to xmit?
	ani	TDRE
	rz			;return zero if not ready to send

	mvi	a,0FFh		;else return FF if ready
	ret

;---------------------------------------------------------------------------
; listOut - List output BIOS entry point. Output the character in C
;    through the printer serial port.
;---------------------------------------------------------------------------
listOut	call	listSt		;test for OK to transmit
	jz	listOut		;not ready, wait

	mov	a,c		;transmit the passed character
	out	LSTDR
	ret

;----------------------------------------------------------------------------
; selDsk - Select Disk BIOS entry. C contains the disk number to select.
;    Validate the disk number and return a pointer to the disk parameter
;    header in HL. Zero is returned in HL for invalid drive number. The
;    disk number is stored in drvNum. No drive activity takes place.
;----------------------------------------------------------------------------
; selDsk is also used as the "Extended Function" entry point. If a drive
;    number of FFh is passed in C, then this call is an extended function
;    request and not a disk select request. In this case:
;
; On Entry
;    C = FFh (extended function request)
;    B = extended function number (see list below)
;
; On Exit
;    H = 0, L = number of functions supported. This return value is neither
;	the "invalid drive" response (HL=0) or a valid DPH pointer when a
;    	drive is selected. This allows the caller to detect whether a
;	BIOS supports extended functions or not.
;    Z flag set true if the extended function executed without error, Z flag
;	false otherwise.
;    Clobbers All
;
;    Extended Functions supported by this BIOS:
;	00h - Invalidate and flush track buffer.
;----------------------------------------------------------------------------
selDsk	lxi	h,0		;init HL=0 for invalid drive request

	inr	c		;test for drive number of 0FFh
	jnz	notExt		;not an extended function request

	inr	l		;H=0, L=1 (number of functions supported)
	mov	a,b		;A=extended function code
	ora	a		;code 0 = flush track buffer?
	cz	invFlsh		;yes, invalidate and flush track buffer
	ret			;exit with HL=1 and proper zero status

notExt	mov	a,c		;A=drive number + 1
	dcr	a		;A=drive number
	cpi	NUMDISK		;valid drive number?
	rnc			;invalid drive number, return with HL=0=error

	lxi	h,fNewSel	;set the "new select" flag
	mov	m,h		;(H is not zero)

	sta	drvNum		;save new drive number
	add	a		;compute disk parameter header address
	add	a		;   as (16*drvNum) + dpHead
	add	a
	add	a		;A=16*drvNum

	lxi	h,dpHead  	;HL->start of disk parameter headers
	mov	e,a
	mvi	d,0
	dad	d		;HL->DPH for the passed drive number
	ret

;----------------------------------------------------------------------------
; home - Home BIOS entry. Set trkNum to zero. No drive activity takes place.
;----------------------------------------------------------------------------
home	xra	a		;set trkNum to zero
	sta	trkNum
	ret

;----------------------------------------------------------------------------
; setTrk - Set Track BIOS entry. C contains the desired track number.
;    The track number is saved in trkNum for later use. No actual
;    drive activity takes place.
;----------------------------------------------------------------------------
setTrk	mov	a,c		;save track number passed in C
	sta	trkNum
	ret

;----------------------------------------------------------------------------
; setSec - Set Sector BIOS entry. C contains the desired sector number.
;    The sector number is saved in secNum for later use. No actual
;    drive activity takes place.
;----------------------------------------------------------------------------
setSec	mov	a,c		;save sector number passed in C
	sta	secNum
	ret

;----------------------------------------------------------------------------
; setDma - Set DMA BIOS entry. BC contains the address for reading or
;    writing sector data for subsequent I/O operations. The address is
;    stored in dmaAddr.
;----------------------------------------------------------------------------
setDma	mov	h,b		;save buffer address passed in BC
	mov	l,c
	shld	dmaAddr
	ret

;----------------------------------------------------------------------------
; secTran - Sector translation BIOS entry. Convert zero-indexed logical 
;    sector number in BC to a sector number in HL based on the translate
;    table passed in DE.
;
;    In order to save a few bytes, this routine performs sector translation
;    with an algorithm rather than a table. The table address passed in DE
;    is ignored - all drives in this BIOS use the same translation.
;
;    The standard 8" SSSD translate table shown here is implemented:
;
;	tranTbl	db	01,07,13,19,25,05,11,17,23,03,09,15,21
;		db	02,08,14,20,26,06,12,18,24,04,10,16,22
;----------------------------------------------------------------------------
secTran	mov	a,c		;A=logical sector 0-25

	add	a		;*2 multiply logical sector by 6
	add	c		;*3
	add	a		;*6

	lxi	h,NUMSEC	;H=0, L=26

	cpi	(NUMSEC/2)*6	;is the input logical sector >= 13?
	cmc			;carry now set if A >= 13
	adc	h		;increments A by 1 if sector >= 13

stMod	sub	l		;do MOD 26 (subtract 26)
	jnc	stMod

	adc	l		;add back last 26 + 1 (carry is set)
	mov	l,a		;HL=translated sector out
	ret

;----------------------------------------------------------------------------
; read - Read sector BIOS entry. Read one sector using the drvNum, 
;    trkNum, secNum, and dmaAddr previously specified.
; On Entry:
;    drvNum = drive to read from
;    trkNum = track to read from
;    secNum = physical sector number to read
;    dmaAddr = address of buffer to read into
; On Exit:
;   If read successful:
;      sector read into (dmaAddr)
;      HL->dmaAddr+SECLEN
;      Z flag = 1
;   if read fails:
;      Z flag = 0
;  Clobbers all (see HL note above)
;----------------------------------------------------------------------------
read	lxi	h,exitDio	;RET will jump to exitDio
	push	h

	call	ckFlush		;see if track buffer should be flushed
	rnz			;track flush error (jump to exitDio)

	call	readTrk		;fill track buffer if needed
	rnz			;track read error (jump to exitDio)
	
; move sector from proper offset in trkBuf to (dmaAddr) unless the sector
;   is flagged as bad.

	call	loadHD		;HL=(dmaAddr), DE->sector buffer in trkBuf

	ldax	d		;check 1st byte (sector status byte)
	ora	a
	rnz			;bad sector (jump to exitDio)

	inx	d		;DE->1st data byte in sector
	jmp	moveD2H		;move sector to (dmaAddr), exit thru exitDio

;----------------------------------------------------------------------------
; write - Write sector BIOS entry. Write one sector using the drvNum,
;    trkNum, secNum and dmaAddr specified. 
; On Entry:
;    drvNum = drive to write
;    trkNum = track to write
;    secNum = physical sector number to write
;    dmaAddr = address of buffer to write from
; On Exit:
;   If successful:
;      sector written to trkBuf from (dmaAddr)
;      DE->dmaAddr+SECLEN
;      Z flag = 1
;   if write fails:
;      Z flag = 0
;  Clobbers all (see DE note above)
;----------------------------------------------------------------------------
write	lxi	h,exitDio	;RET will jump to exitDio
	push	h

	call	ckFlush		;see if track buffer should be flushed
	rnz			;error, jump to exitDio

	call	readTrk		;fill track buffer if needed
	rnz			;track read error, jump to exitDio

	call	dChkWp		;write protected?
	rnz			;yes, jump to exitDio

	lxi	h,bfDirty	;set buffer dirty flag true
	mvi	m,0FFh

; move sector from (dmaAddr) to proper offset in trkBuf

	call	loadHD		;HL=(dmaAddr), DE->sector buffer in trkBuf
	xchg			;DE=(dmaAddr), HL->sector buffer in trkBuf

	mvi	m,0		;force sector to "good"
	inx	h		;HL->first data byte in sector
	jmp	moveD2H		;move data to trkBuf, exit through exitDio

; exitDio - exit disk I/O. Restore interrupts and properly set A and
;     the zero flag. Note that moveD2H exits with zero status true.

exitDio	ei			;re-enable interrupts

	mvi	a,0		;if zero status is true, return zero
	rz

	cma			;return A=FF, Z false
	ret

;----------------------------------------------------------------------------
; loadHD - Return HL=(dmaAddr), DE->sector buffer in trkBuf. 
; On Entry:
;    secNum = sector to read or write
;    dmaAddr = address of buffer to read or write
; On Exit:
;   HL=(dmaAddr)
;   DE->sector buffer in trkBuf (status byte)
; Clobbers none
;----------------------------------------------------------------------------
loadHD	lxi	h,secNum	;load requested sector into E
	mov	e,m
	call	dSecAdr		;HL->sector within trkBuf (status byte)
	xchg			;DE->sector within trkBuf

	lhld	dmaAddr		;HL=(dmaAddr)
	ret

;------------------------------------------------------------------------------
; readTrk- read full track into track buffer if the requested 
;    drive (drvNum) or track (trkNum) does not match the buffered
;    drive (bfDrive) or buffered track (bfTrack). The status byte
;    at the start of each sector is zero if the sector is good,
;    non-zero if the sector couldn't be read.
; On Entry:
;    drvNum, trkNum hold drive and track for the requested read
; On Exit:
;    trkNum on drvNum read into trkBuf
;    Z set if track read, Z cleared if error
; Clobbers all
;------------------------------------------------------------------------------
readTrk	lhld	bfDrive		;L=buffered drive, H=buffered track

	lda	drvNum		;A=requested drive
	cmp	l		;same drive buffered?
	jnz	newBuf		;drive doesn't match, need a new buffer

	lda	trkNum		;A=requested track
	cmp	h		;same track buffered?
	rz			;yes, already have this buffer

; newBuf - new buffer (full track) needed

newBuf	mvi	a,UNDEF		;invalidate buffered data
	sta	bfDrive

	call	dSelDrv		;select the drive
	rnz			;error, probably not ready

	call	dSeek		;seek to trkNum, E=sec under head
	rnz			;unrecoverable seek error

	mvi	d,NUMSEC	;D=sector counter

rdLoop	mvi	b,RDTRIES	;restart retry count
	call	dNxtSec		;E=sector, HL->sector buffer

rdRtry	call	dRead		;read the sector
	jz	rdNext		;good read, do next sector

	call	dSecAdr		;HL->sector buffer
	dcr	b		;decrement retry counter
	jnz	rdRtry
				;fall through to do next sector, the bad
				;sector has a non-zero status byte

rdNext	dcr	d		;decrement sector count
	jnz	rdLoop		;loop exits with zero true

; Track read successful. Update bfDrive and bfTrack with the drive and
;    track number present in the track buffer and exit.

	lhld	drvNum		;L=drvNum, H=trkNum
	shld	bfDrive		;bfDrive=drvNum, bfTrack=trkNum

	ret			;return with Z flag set

;------------------------------------------------------------------------------
; invFlush - Invalidate and flush the track buffer.
;    Console input is used as an idle indicator and as a good time
;    to flush a dirty track buffer, so the conIn function calls this
;    subroutine. Even if not flushed, the track buffer is invalidated
;    to force a track read on the next disk I/O request. This helps
;    catch a disk swap.
; On Entry:
;    Same drive is on the same track as buffered in trkBuf
; On Exit:
;    Track buffer written if required and invalidated.
;    Z set for no error, Z cleared if write error occured.
; Clobbers all except HL
;------------------------------------------------------------------------------
invFlsh	mvi	a,UNDEF		;invalidate track buffer
	sta	bfDrive

	push	h		;preserve HL in case conIn caller expects it
	call	ckFlush		;flush the track if needed
	pop	h
	
	ei			;re-enable interrupts
	ret
	
;------------------------------------------------------------------------------
; ckFlush - Check if track buffer should be flushed. This
;    function must be called before any drive selection or seek
;    operation. The buffer is flushed if the bfDirty flag is
;    set and the drive or track number are different than the
;    buffered track.
; On Entry:
;    drvNum, trkNum updated for the new I/O call
; On Exit:
;    Z set for no error, Z cleared if write error occured.
;    Clobbers all
;------------------------------------------------------------------------------
ckFlush	lda	bfDirty		;see if track buffer is dirty
	ora	a
	rz			;no, exit with Z set

	lhld	bfDrive		;L=buffered drive, H=buffered track
	lda	drvNum		;A=requested drive number
	cmp	l		;same drive?
	jnz	wrtTrk		;no, flush

	lda	trkNum		;A=requested track number
	cmp	h		;same track?
	rz			;yes, no need to flush

; wrtTrk - track buffer needs to be written

wrtTrk	xra	a		;clear buffer dirty flag
	sta	bfDirty

	call	dReadAm		;get E=sector under head
	jnz	wrtErr		;couldn't read address mark

	mvi	d,NUMSEC	;D=sector counter

wrtLoop	call	dNxtSec		;E=sector, HL->sector buffer
	
	mov	a,m		;check sector error flag
	ora	a		;only write good sectors (flag=0)
	jnz	wtSkip		;skip bad sector

	call	dWrite		;write the sector if not bad
	jnz	wrtErr		;hard write error

wtSkip	dcr	d		;decrement sector count
	jnz	wrtLoop

	mvi	a,6		;delay 600us for trim erase
	jmp	delay		;call delay and return

; wrtErr - Display error message since CP/M won't for a delayed write
;   called at conIn time.

wrtErr	call	dispMsg		;A is non-zero on return
	db	CR,LF,'Disk Flush Err',CR,LF+80h

	ret			;exit with non-zero status	

;------------------------------------------------------------------------------
; moveD2H - move 128 bytes from (DE) to (HL).
; On Entry:
;    DE->source buffer
;    HL->destination buffer
; On Exit:
;   Z flag set
; Clobbers B. DE and HL updated as buffer moved.
;------------------------------------------------------------------------------
moveD2H	mvi	b,SECLEN	;B=buffer length (128 bytes)

movLoop	ldax	d		;move from (DE) to (J:)
	mov	m,a
	inx	d		;increment both pointers
	inx	h
	dcr	b		;decrement character count
	jnz	movLoop		;loop until count = 0

	ret			;Z is set on exit
	
;------------------------------------------------------------------------------
; dispMsg - display null terminated string on console device. This
;    code assume conOut does not clobber HL in order to save two
;    bytes.
; On Entry: HL->message
; On Exit:
; Clobbers C,H,L
;------------------------------------------------------------------------------
dispMsg	pop	h

dmLoop	mov	c,m		;display next character
	call	conOut

	ora	m		;end of message? (MSBit set)
	inx	h
	jp	dmLoop		;no, repeat with next char

	pchl			;return past message string


;****************************************************************************
;
; Tarbell disk I/O routines (single density controller)
;
;****************************************************************************

; Tarbell Disk Drive Controller Equates

FDCBASE	equ	0F8H		;controller base address
FDCCMD	equ	FDCBASE		;1771 command register
FDCSTAT	equ	FDCBASE		;1771 status register
FDCTRK	equ	FDCBASE+1	;1771 track register
FDCSEC	equ	FDCBASE+2	;1771 sector register
FDCDATA	equ	FDCBASE+3	;1771 data in/out register
FDCWAIT	equ	FDCBASE+4	;FDC stall/wait port
FDCSEL	equ	FDCBASE+4	;FDC drive select port

STPRATE	equ	2		;10ms step rate

; Commands for 1771.

cRESTOR	equ	008h+STPRATE	;restore, head load, no verify
cSEEK	equ	018h+STPRATE	;seek, head load, no verify
cSTPIN	equ	058h+STPRATE	;step in, update track reg, head loaded
cSTPOUT	equ	078h+STPRATE	;step out, update track reg, head loaded
cREAD	equ	088h		;read, head assumed settled
cWRITE	equ	0A8h		;write, head assumed settled
cRDADDR	equ	0C4h		;read address marker, wait head settle
cRESET	equ	0D0h		;reset 1771 to idle

cSTPTGL	equ	020h		;xor'd toggles between step in and out
cWAITHD	equ	004h		;bit for read/write to wait for head settle

; Status bits from 1771

sNOTRDY	equ	80h		;1=not ready
sWRPROT	equ	40h		;1=write protected
sHDLOAD	equ	20h		;1=head is loaded
sSKERR	equ	10h		;1=seek error
sCRCERR	equ	08h		;1=CRC error
sTRACK0	equ	04h		;1=on track zero
sINDEX	equ	02h		;1=index detected
sBUSY	equ	01h		;1=1771 busy

sSECERR	equ	10h		;1=sector not found
sWRTFLT	equ	20h		;1=write fault
sDLOST	equ	04h		;1=lost data 

; Combined status flag masks for status of a seek, read, and write

smSEEK	equ	sSKERR+sCRCERR+sNOTRDY+sBUSY
smREAD	equ	sSECERR+sCRCERR+sDLOST+sNOTRDY+sBUSY
smWRITE	equ	smREAD+sWRPROT+sWRTFLT

;--------------------------------------------------------------
; dSelDrv - If a new selDrv has occured (fNewSel is true),
;    do a physical drive select and update the track register
;    in the 1771 by reading an address mark from the disk. If
;    this operation fails, a restore to track zero is requested.
; On Entry:
;    drvNum = drive to be selected
;    fNewSel = "new select" flag when not zero
; On Exit:
;    curDrv = drvNum
;    Z flag set = drive selected and ready
;    Z flag clear = error
; Clobbers H,L
;--------------------------------------------------------------
dSelDrv	lxi	h,fNewSel	;test the "new select" flag
	mov	a,m
	ora	a
	rz			;new select not requested, exit

	mvi	m,0		;clear the new select flag

; New drive select requested. Output the new drive select to the
;    Tarbell controller.

	inx	h		;HL->drvNum
	mov	a,m		;A=drvNum
	cma			;the selection decoder inverts
	add	a		;shift left 4 with zero fill
	add	a
	add	a
	add	a
	ori	2		;set the latch bit
	out	FDCSEL		;select the drive

; Reset the 1771 in case we've entered from a bad state. This is
;    done by calling the check write-protect routine which does
;    a reset as part of its function.

	call	dChkWp		;does a 1771 reset

; Read address mark to find out what track the new drive is on.
;    Update the track register in the 1771 with the proper track.

	call	dReadAm		;returns A=track from address mark
	jnz	dRestor		;failed, restore to track 0 and exit

	out	FDCTRK		;update track register in 1771
	ret			;return with zero true

;--------------------------------------------------------------
; dChkWp - Reset 1771 and return status of the write protect
;    status from the 1771.
; On Entry:
;    Drive selected and ready
; On Exit:
;    Z flag set = not write protected
;    Z flag clear = write protected.
; Clobbers none
;--------------------------------------------------------------
dChkWp	mvi	a,cRESET	;ensure 1771 is idle
	out	FDCCMD
	xthl			;waste some time before checking
	xthl

	in	FDCSTAT		;read 1771 status
	ani	sWRPROT		;return write protect bit alone
	ret

;--------------------------------------------------------------
; dSeek - Seek to the track specified in trkNum (set by a
;    previous call to the BIOS setTrk entry point) on the
;    currently selected drive. After the seek completes, an
;    address mark is read to verify the seek and to determine 
;    what sector is under the head. This action also ensures
;    the head is settled so subsequent reads and writes can
;    assume a loaded and settled head.
; On Entry:
;    trkNum = desired track number
; On Exit:
;    E = sector number from the address mark
;    Z flag set = seek worked and verified
;    Z flag clear = error
; Clobbers D,E,H,L
;--------------------------------------------------------------
dSeek	mvi	d,3		;D=retry counter

dsRetry	call	dSeekC		;issue seek command
	rnz			;hard error, probably not ready

	call	dReadAm		;returns A=track from address mark
	jnz	dsFail		;couldn't read address mark

	lxi	h,trkNum	;HL->desired track number
	cmp	m		;does actual match requested?
	rz			;yes, we're done

dsFail	dcr	d		;decrement retry counter
	rm			;went below zero, exit with <> 0

	call	dRestor		;restore to track zero
	jz	dsRetry		;try again
	
	ret			;return with error

;--------------------------------------------------------------
; dSeekC - Issue command to 1771 to seek to trkNum. After
;    the seek completes, the head settle time (HDSETL) is
;    waited before returning.
; On Entry:
;    trkNum = desired track number
; On Exit:
;    Z flag set = seek completed
;    Z flag clear = error
; Clobbers none
;--------------------------------------------------------------
dSeekC	lda	trkNum		;give track to seek to 1771
	out	FDCDATA

	mvi	a,cSEEK		;issue seek command
	out	FDCCMD
	in	FDCWAIT		;wait for seek to complete

	mvi	a,HDSETL	;wait for head settle time
	call	delay

	in	FDCSTAT		;test status of the seek
	ani	smSEEK		;any of these bits is an error
	ret

;--------------------------------------------------------------
; dRestor - Restore the currently selected drive to track 0.
;    Waits for the direction reversal delay (REVDIR) before
;    exiting in case a subsequent seek is immediately done.
; On Entry:
;    
; On Exit:
;    Z flag set = restore operation worked
;    Z flag clear = restore failed
; Clobbers none
;--------------------------------------------------------------
dRestor	mvi	a,cRESTOR	;issue restore to track zero
	out	FDCCMD
	in	FDCWAIT		;and wait for restore to complete

	mvi	a,REVDIR	;wait direction reversal delay
	call	delay

	in	FDCSTAT		;verify restore worked
	ani	smSEEK		;isolate seek bits
	ret			;return status

;--------------------------------------------------------------
; dRead - Read sector on current track. The sector is read
;    starting at HL+1. The byte at (HL) is a status byte
;    that is set to zero for a good read, non-zero for a
;    bad read.
; On Entry:
;    Drive is selected, on proper track, head loaded
;    HL->sector buffer
;    E = sector number to read
; On Exit:
;    Z flag set = success
;    Z flag clear = error
; Clobbers H,L
;--------------------------------------------------------------
dRead	mov	a,e		;give sector number to FDC
	out	FDCSEC
	mvi	a,cREAD		;output read command
dReadA	out	FDCCMD

	push	h		;save address of status byte
	inx	h		;point to 1st data byte

dRdLoop	in	FDCWAIT		;wait until byte available
	ora	a		;sector finished?
	jp	dRdDone		;yes

	in	FDCDATA		;read next byte and...
	mov	m,a		;store in buffer
	inx	h		;bump memory pointer
	jmp	dRdLoop

dRdDone	in	FDCSTAT		;get status of the read
	ani	smREAD		;any of these bits is an error
	pop	h		;hl->status byte
	mov	m,a		;save sector status in 1st byte
	ret

;--------------------------------------------------------------
; dReadAm - Read address mark from disk. Returns track
;    in A and sector in E from the first address mark found.
;    Since an address mark is read before any sector reads
;    or writes take place, this function is used as a single
;    point to initially disable interrupts prior to data I/O.
; On Entry:
;    Drive is selected
; On Exit:
;    Z flag set = success
;    Z flag clear = error
;    A = track number from disk
;    E = sector number from disk
; Clobbers H,L
;--------------------------------------------------------------
dReadAm	di			;disable interrupts
	lxi	h,amBuf		;HL->address mark buffer
	mvi	a,cRDADDR	;read address mark command
	call	dReadA		;read with command in A

	lxi	h,amSec		;HL->sector from address mark
	mov	e,m		;E=sector from address mark

	lda	amTrk		;A=track from address mark
	ret			;exit with data and status

;--------------------------------------------------------------
; dWrite - Write a sector on current track. Data is written
;    starting at HL+1 since the 1st byte is a sector status
;    byte for internal use.
; On Entry:
;    Drive is selected, on proper track, head loaded
;    HL->buffer address
;    E = sector number to write
; On Exit:
;    Z flag set = success
;    Z flag clear = error
; Clobbers H,L
;--------------------------------------------------------------
dWrite	mov	a,e		;give sector number to FDC
	out	FDCSEC
	mvi	a,cWrite	;output write command
	out	FDCCMD
	inx	h		;move to 1st data byte

dWrLoop	in	FDCWAIT		;wait until ready for byte
	ora	a		;sector finished?
	jp	dWrDone		;yes

	mov	a,m		;A=next byte to write
	out	FDCDATA
	inx	h		;bump memory pointer
	jmp	dWrLoop

dWrDone	in	FDCSTAT		;get status of the write
	ani	smWRITE		;any of these bits is an error
	ret

;--------------------------------------------------------------
; dNxtSec - Increment and wrap the sector number passed in E,
;    then fall into dSecAdr to return HL with a pointer to the
;    sector address within the track buffer.
; On Entry:
;    E = most recent sector number
; On Exit:
;    E = next sector number
;    HL->sector within trkBuf
; Clobbers none
;--------------------------------------------------------------
dNxtSec	inr	e		;bump to next sector
	jz	dSec1		;in case uninitialized rolled to 0

	mvi	a,NUMSEC	;A=number of sectors
	cmp	e		;compare NUMSEC - next sector
	jnc	dSecAdr		;next is good (1-26)

dSec1	mvi	e,1		;restart at sector 1
				;fall into dSecAdr

;--------------------------------------------------------------
; dSecAdr - Return HL with address of the sector buffer within
;    trkBuf for the sector number in E. This routine is hard
;    coded for a sector length of 128+1, the SECLEN equate is 
;    ignored. The extra byte accounts for the status byte at
;    the start of each sector.
; On Entry:
;    E = sector number (must be 1-26)
; On Exit:
;    E = sector number (not changed)
;    HL->sector within trkBuf
; Clobbers none
;--------------------------------------------------------------
dSecAdr	push	b		;save BC

	dcr	e		;zero index sector number
	mov	a,e
	ora	a		;clear carry

	rar			;form B=sector >> 1
	mov	b,a

	rar			;put sector lsbit into msbit of C	
	ani	80h
	mov	c,a		;BC=128*(sector-1)
	
	lxi	h,trkBuf	;compute HL=trkBuf+BC
	dad	b

	mov	c,e		;form BC=sector-1
	mvi	b,0
	dad	b		

	inr	e		;restore E
	pop	b		;restore BC
	ret

;----------------------------------------------------------------------------
; delay - Delay for A x 100us (works for a 2MHz 8080)
;
; On Entry
;    A = 100us increments to delay
;
; On Exit
;    A=0
;    Zero true
;    Clobbers A,B
;----------------------------------------------------------------------------
delay	mvi	b,(200/20)	;20 cycles in the loop below

delayLp	mov	b,b		;(5) delay 5 cycles
	dcr	b		;(5)
	jnz	delayLp		;(10)

	dcr	a		;decrement 100us counter
	jnz	delay
	ret

;******************************************************************************
;
;   BIOS Data Area
;
;******************************************************************************


;-----------------------------------------------------------------------------
; dpHead - disk parameter headers for each drive
;-----------------------------------------------------------------------------
dpHead	dw	tranTbl,0,0,0,dirBuf,dpb,csv0,alv0
	dw	tranTbl,0,0,0,dirBuf,dpb,csv1,alv1
	dw	tranTbl,0,0,0,dirBuf,dpb,csv2,alv2

;-----------------------------------------------------------------------------
; dpb - disk parameter block. This table gives a block size of 1024 bytes
;   and 64 directory entries. 
;-----------------------------------------------------------------------------
dpb	dw	NUMSEC		;sectors per track
	db	3		;allocation block shift factor (BSH)
	db	07h		;data location block mask (BLM)
	db	0		;extent mask (EXM)
	dw	DSM		;maximum block number (DSM 242)	
	dw	DRM		;maximum directory entry number (DRM 63)
	db	0C0h,0		;AL0, AL1 
	dw	CKS		;CKS=(DRM+1)/4
	dw	2		;reserved tracks for CPM and bootloader

; In order to save a few bytes, secTran implements the sector translation
;    of this table with an algorithm.

tranTbl	equ	$
;	db	01,07,13,19,25,05,11,17,23,03,09,15,21
;	db	02,08,14,20,26,06,12,18,24,04,10,16,22

; Data initialized at cold start

bfDirty	db	0		;non-zero if buffer has been written to
bfDrive	db	UNDEF		;drive currently in track buffer 

;----------------------------------------------------------------------------
;  Driver code size check. Must be less than 380h bytes in length here.
;
;  Note that ASM and MAC actually only check bit 0 of the expression
;  for zero, not the entire value, so the expression below moves the
;  sign bit to bit zero (sign bit set if BIOS is too big).
;----------------------------------------------------------------------------
SZDIFF	equ	380h-($-BIOSBAS)
SZFLAG	equ	(SZDIFF and 8000h) shr 15

 if SZFLAG

  BIOS IS TOO BIG ****

 endif
	
;------------------------------------------------------------------------------
; Reserved space for insertion of optional code to provide full IOBYTE
;    support. Enabled by the IOBENBL flag.
;------------------------------------------------------------------------------
 if IOBENBL
	org	($+7Fh) and 0FF80h	;force 80h boundary
IOBBASE	equ	$
	ds	IOBLEN
 endif

;----------------------------------------------------------------------------
; Non-initialized data
;----------------------------------------------------------------------------
bfTrack	ds	1		;currently buffered track (MUST FOLLOW bfDrive)

fNewSel	ds	1		;new drive selected flag
drvNum	ds	1		;drive number from CP/M call (MUST FOLLOW fNewSel)
trkNum	ds	1		;track number from CP/M call (MUST FOLLOW drvNum)
secNum	ds	1		;sector number from CP/M call
dmaAddr	ds	2		;dma address for disk operations from CP/M call

; Address mark buffer

amBuf	equ	$
	ds	1		;read status byte, ignored
amTrk	ds	1		;track
	ds	1
amSec	ds	1		;sector
amSecL	ds	1		;sector length code
amCrc	ds	2		;CRC 
	ds	4		;safety space

; Track buffer. One extra byte is allocated at the start of each sector
;    to flag a bad sector (read failure). This byte is zero if the sector
;    is good, non-zero if the sector is bad.

trkBuf	ds	NUMSEC*(SECLEN+1)

;-----------------------------------------------------------------------------
;  Disk scratchpad areas defined in the DPH table. ALV and CSV storage
;  MUST REMAIN in this order since address math is done on them in selDrv.
;-----------------------------------------------------------------------------
dirBuf	ds	128		;bdos directory scratchpad
alv0	ds	(DSM/8 + 1)	;allocation vector storage
csv0	ds	CKS		;change disk scratchpad
alv1	ds	(DSM/8 + 1)
csv1	ds	CKS
alv2	ds	(DSM/8 + 1)
csv2	ds	CKS

ACTLEN	equ	$-BIOSBAS	;actual length of BIOS 

	end


