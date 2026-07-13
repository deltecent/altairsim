;****************************************************************************
;
;  CP/M cold boot loader for Altair CP/M
;
;    This code is loaded from sectors 0 and 2 into RAM by the disk boot
;    loader PROM (DBL). This loader, in turn, loads the CP/M image into
;    high RAM.
;
;    The loader is based on code from track buffered CP/M 2.2b v2.0.
;    It can be used for the Altair 8" floppy or Altair Mini Disk by
;    setting or clearing the MINIDSK equate as required.
;
;    Version History
;
;    Ver     Date	  Description
;    ---   ---------	---------------------
;    1.0   11/4/2017	Original, Mike Douglas     
;
;--------------------------------------------------------------------------
;
;    To patch changes made to this loader into a CP/M image saved from
;    MOVCPM8 or MOVCPM5 (e.g., CPMxx.COM), use the following commands:
;
;	A>DDT CPMxx.COM
;	-IBOOT.HEX
;	-R900
;       -M980,9FF,A00	(splits code onto sectors 0 and 2)
;	-G0	    	(Go zero, not "oh")
;	A>SYSGEN
;
;****************************************************************************

MINIDSK	equ	1		;choose 1=Mini Disk, 0=8" Floppy
MEMSIZE	equ	48		;set memory size in K bytes

;------------------------
;  8" floppy equates
;------------------------
   if NOT MINIDSK
BIOSLEN	equ	1900h		;length of BIOS for 8" floppy
NUMTRK	equ	77		;number of tracks on the disk
NUMSEC	equ	32		;sectors per track
DATATRK	equ	6		;1st data format track
SECMASK	equ	1Fh		;five bit sector numbers
STRTSEC	equ	3		;starting sector of CP/M image on track 0
SECCNT	equ	NUMSEC*2-STRTSEC   ;two CPM boot tracks less three sectors
   endif

;------------------------
;  Mini Disk equates
;------------------------
   if MINIDSK
BIOSLEN	equ	1000h		;length of BIOS for mini disk
NUMTRK	equ	35		;number of tracks on the disk
NUMSEC	equ	16		;sectors per track
DATATRK	equ	4		;1st data format track
SECMASK	equ	0Fh		;four bit sector numbers
STRTSEC	equ	3		;starting sector of CP/M image on track 0
SECCNT	equ	NUMSEC*4-STRTSEC   ;four CPM boot tracks less three sectors
   endif

;------------------------
; CP/M size & locations
;------------------------
CCPLEN	equ	0800h		;CPM 2.2 fixed length
BDOSLEN	equ	0E00h		;CPM 2.2 fixed length

CCPBASE	equ	MEMSIZE*1024 - BIOSLEN - BDOSLEN - CCPLEN
BIOSBAS	equ	(CCPBASE+CCPLEN+BDOSLEN) ;base address of this BIOS

;------------------------
; Common disk parameters
;------------------------
CSECLEN	equ	128		;CP/M sector length
SSECLEN	equ	133		;Altair system sector length

; System track sector format

SYSTRK	equ	0		;offset of track number
SYSDATA	equ	3		;offset of 128 byte data payload
SYSSTOP	equ	131		;offset of stop byte (FFh)
SYSCSUM	equ	132		;offset of checksum

;------------------------
; Altair disk controller 
;------------------------
DRVSLCT	equ	08h		;drive select register (out)
cDSLCT	equ	80h		;deselect drive

DRVSTAT	equ	08h		;drive status register (in)
sENWD	equ	01h		;enter new write data flag
sMOVEOK	equ	02h		;OK to move head
sHDSTAT	equ	04h		;head status flag
sDSKEN	equ	08h		;disk is selected and enabled
sINTEN	equ	20h		;processor interrupts enabled
sTRACK0	equ	40h		;on track zero flag
sNRDA	equ	80h		;new read data available

DRVCMD	equ	09h		;drive command register (out)
cSTEPI	equ	01h		;step in
cSTEPO	equ	02h		;step out
cHDLOAD	equ	04h		;head load
cRESTMR	equ	04h		;restart motor-off timer (MINIDSK)
cHDUNLD	equ	08h		;head unload
cINTEN	equ	10h		;interrupt enable
cINTDIS	equ	20h		;interrupt disable
cHCSON	equ	40h		;reduce head current switch
cWRTEN	equ	80h		;write enable

DRVSEC	equ	09h		;drive sector position (in)
sNEWSEC	equ	01h		;new sector flag (sector true)

DRVDATA	equ	0Ah		;drive read/write data (in/out)

	
;--------------------------------------------------------------------------
;  Entry point
;--------------------------------------------------------------------------
	org	0

start	di			;disable interrupts
	lxi	sp,CCPBASE	;stack grows down from lowest CPM address

; Deselect and reselect the drive in case this loader was loaded
;    from somewhere other than drive 0

dSelLp	mvi	a,cDSLCT	;deselect a possibly attached drive
	out	DRVSLCT	

	mvi	a,0		;select drive 0
	out	DRVSLCT
	
	in	DRVSTAT		;see if select worked
	ani	sDSKEN
	jnz	dSelLp		;not selected, try again

	mvi	a,cHDLOAD	;load head 8"/restart timer for mini
	out	DRVCMD
	
; Restore to track zero

seek0	in	DRVSTAT		;loop until OK to move the head 
	ani	sMOVEOK
	jnz	seek0

	in	DRVSTAT		;see if we are at track 0
	ani	sTRACK0		;at track zero?
	jz	load		;yes, start the load process

	mvi	a,cSTEPO	;issue step out
	out	DRVCMD
	jmp	seek0
	
; Load CP/M image

load	lxi	b,SECCNT*256+STRTSEC    ;B=# secs to read, C=start sec
	lxi	h,CCPBASE	;HL->CP/M load address
	xra	a		;start on track 0
	
trkLoop	sta	trkNum		;save track number to use

secLoop	shld	dmaAddr		;set read destination
	mov	a,c		;set sector number from C
	sta	secNum

	push	b		;save sector count and number
	call	read		;read next sector
	pop	b
	jnz	start		;failed read, start over
	
	dcr	b		;decrement total sector count
	jz	BIOSBAS		;done, jump to BIOS cold start entry point
	
	inr	c		;bump to next sector number
	mvi	a,NUMSEC	;A=number of sectors on a track
	sub	c		;all 32 sectors on the track done?
	jnz	secLoop		;no, read next sector
	
	mov	c,a		;start with C=sector 0 on text track
	lda	trkNum		;increment track number
	inr	a

	jmp	trkLoop		;continue

;----------------------------------------------------------------------------
; read - Read one sector on the selected drive using trkNum, secNum,
;    and dmaAddr previously specified.
;
; On Entry
;    trkNum = track to read from
;    secNum = sector number to read (0-31)
;    dmaAddr = address of buffer to read into
;
; On Exit
;    sector read into (dmaAddr)
;    HL = (dmaAddr) + CSECLEN
;    Clobbers all
;----------------------------------------------------------------------------
read	call	readTrk		;fill track buffer if needed

	lxi	h,secNum	;HL->sector number
	mov	d,m		;DE MSB=256*sector
	inr	d		;account for trkBuf starting at 0100h
	mvi	e,SYSDATA+1	;DE->data portion of secNum in trkBuf
	
	lhld	dmaAddr		;HL->destination for data
	mvi	b,CSECLEN	;B=number of bytes to move
	
mrLoop	ldax	d		;move sector from trkBuf to (dmaAddr) 
	mov	m,a
	inx	h		;increment pointers
	inx	d
	dcr	b		;loop count
	jnz	mrLoop 

	ei
	ret

;----------------------------------------------------------------------------
; readTrk- read full track into track buffer if the requested 
;    track (trkNum) does not match the buffered track (bfTrack).
;    Unlike the BIOS version of readTrk, this code requires all
;    sectors to read without error before exiting.
;
; On Entry
;    trkNum = track to read (assumed each change is next track)
;    bfTrack = track from which trkBuf was filled
;
; On Exit
;    trkNum read into trkBuf
;    Clobbers all
;----------------------------------------------------------------------------
readTrk	lda	trkNum		;A=requested track
	lxi	h,bfTrack	;HL->buffered track number
	cmp	m		;same track buffered?
	rz			;yes, already have this track
	
	mov	m,a		;update buffered track number
	jc	skpStep		;unknown track to track 0, skip step

	mvi	a,cSTEPI	;step in to next track (assume track
	out	DRVCMD		;   change is always by +1)
	
skpStep	lxi	h,256*NUMSEC	;HL->flag byte in last sector of trkBuf

initLp	mov	m,h		;set sector flag false (non-zero)
	dcr	h		;decrement to preceding sector
	jnz	initLp	

	mvi	e,NUMSEC	;E=sectors remaining to fill

rdLoop	call	dNxtSec		;wait for next sector, HL->sector buf
	mov	a,m		;A=sector flag byte
	ora	a		;this sector already read?
	jz	rdLoop		;yes, skip it

	push	h		;save sector address
	inx	h		;HL->1st read location in sector
	call	dRead		;read the sector
	pop	h		;restore sector address in HL
	jnz	rdLoop		;read error

	xra	a		;set sector flag to zero (good sector)
	mov	m,a

	dcr	e		;decrement sectors left to fill
	jnz	rdLoop		;loop until all sectors good

	ret

;---------------------------------------------------------------------------
; dNxtSec - Wait for next (any) sector. Returns pointer to the sector
;    buffer within trkBuf.
;
; On Entry
;    Drive is selected, head loaded
;
; On Exit
;    HL->sector buffer in trkBuf for sector found
;    Clobbers A,H,L
;---------------------------------------------------------------------------
dNxtSec	equ	$

   if MINIDSK
	mvi	a,cRESTMR	;restart motor timeout
	out	DRVCMD
   endif

dnLoop	in	DRVSEC		;read sector position register
	rar			;wait for sector true (0=true)
	jc	dnLoop
	
	ani	SECMASK		;A=sector number found
	mov	h,a		;HL MSB=256*sector
	inr	h		;account for trkBuf starting at 0100h
	mvi	l,0		;HL->sector in trkBuf
	
	ret

;---------------------------------------------------------------------------
; dRead - Read a sector, verify checksum, stop byte, and track number.
;    Verification is done after the sector is read in the time between
;    the last byte of the sector and before the start of the next sector.
;    With index alignment and rotation speed tolerance accounted for,
;    we safely have 360 cycles from the last byte of the sector until code
;    should be in the sector true hunt loop.
;
; On Entry
;    Drive is selected, sector true just detected
;    HL->sector buffer
;
; On Exit
;    Sector read to (HL)
;    C = track number from disk with sync bit set
;    Zero true if no error, Zero false for checksum error, missing 
;        FF stop byte, or track number error 
;    Clobbers A,B,C,H,L
;---------------------------------------------------------------------------
dRead	push	h		;(11) save pointer to start of sector
	lxi	b,256+SSECLEN	;(10) B=chksum (-FF), C=bytes to read

; The sector transfer loop is 116 cycles for two bytes read (has to be
;     less than 128) and computes checksum over all bytes read. For the
;     mini disk, only one byte is read per loop iteration.

drSecWt	in	DRVSTAT		;(10) get drive status byte
	ora	a		;(4) wait for NRDA flag true (zero)
	jm	drSecWt		;(10) 

   if NOT MINIDSK		;8" reads two bytes per loop
	in	DRVDATA		;(10) read first byte at 24-48 cycles
	mov	m,a		;(7) store in buffer
	add	b		;(4) update checksum
	mov	b,a		;(5) keep checksum in B
	inx	h		;(5) increment buffer pointer
	dcr	c		;(5) decrement byte count
	jz	drSecDn		;(10) exit if done
   endif

	in	DRVDATA		;(10) read at 70-94 cycles (data at 64 and 128)
	mov	m,a		;(7) store in buffer
	add	b		;(4) update checksum
	mov	b,a		;(5) keep checksum in B
	inx	h		;(5) increment buffer pointer
	dcr	c		;(5) decrement byte count
	jnz	drSecWt		;(10) repeat until done 

; drSecDn - Validate the sector

drSecDn	dcx	h		;(5) HL->checksum in sector
	sub	m		;(7) subtract from computed checksum
	sub	m		;(7) final sum will be zero

	dcx	h		;(5) HL->stop byte
	mov	b,m		;(7) B=stop byte
	pop	h		;(10) restore HL->start of buffer
	inr	b		;(4) was FF stop byte there?
	rnz			;(5/11) no, return error

	mov	c,m		;(7) C=track number from disk
	sub	c		;(4) take track out of checksum

	inx	h		;(5) HL->2nd byte of sector
	sub	m		;(7) take out of checksum
	
	inx	h		;(5) HL->3rd byte of sector
	sub	m		;(7) sum should be zero here
	rnz			;(5/11) exit with error
	
	lda	trkNum		;(10) A=track we should be on
	ori	80h		;(7) set sync bit
	cmp	c		;(4) same track as from sector?
	ret			;(11) exit with compare status

;-------------------------------------------------------------------------
; Data Area
;-------------------------------------------------------------------------
bfTrack	db	0FFh		;currently buffered track (none)
trkNum	ds	1		;track number from CP/M call
secNum	ds	1		;sector number from CP/M call
dmaAddr	ds	2		;dma address for disk operations from CP/M

; Track buffer - Located at 0100h and 256 bytes allocated per sector
;    to reduce code space and increase sector lookup speed

trkBuf	equ	0100h		;track buffer fixed at 0100h.

	end













