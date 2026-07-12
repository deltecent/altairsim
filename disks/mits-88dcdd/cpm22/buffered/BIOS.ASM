****************************************************************************
;
; CP/M 2.2b BIOS for MITS Altair 8800 and either the Altair 8" Floppy
;    Drive or the Altair 5.25" Mini Disk Floppy Drive. Conditional
;    assembly allows this BIOS to work for either type of drive.
;
;    This BIOS for CP/M 2.2 implements full track buffering on both reads
;    and writes to improve performance. Compared to the original CP/M 1.4
;    and 2.2 versions for the 8" Altair floppy, loading programs and other
;    sequential reads improve by more than 50%. Performance of virtually
;    all programs with substantial disk I/O improves by 25%-75%. And note
;    these speed comparisons were made with write verify enabled in this
;    BIOS compared to write verify off in the older CP/M versions.
;
;    The same disk layout as used with the original versions of CP/M 1.4 
;    and 2.2 is maintained for full backward and forward compatibility.
;
;    This version adds full IOBYTE support to allow use of a wider variety
;    of standard Altair I/O boards. Search for "IOBYTE" in this source
;    file for details.
;
;    This version looks for a Teletype on the console port during cold boot
;    and if detected (based on baud rate), subsequently follows any CR to
;    the console device with a NULL to give the Teletype carriage time to
;    reach the left margin. The send null flag (sndNull) is located imme-
;    diately following the MODE and IOBYTE in memory so it can be over-
;    ridden if needed. Note: The MODE byte is kept in the same location as
;    with Burcon CP/M 2.2 for the Altair.
;
;    This version implements a disk select timeout feature (works only on
;    the 8" floppy drive) so if a non-present drive is selected, it will
;    eventually timeout. To still allow the operator time to insert a disk,
;    close the door, and wait for the several second drive enable one-shot
;    to expire, the default timeout is set to seven seconds. This value can
;    be patched in the SELTIME (select timeout) location as shown below.
;
;    As of version 2.3, a timeout feature for 5.25" drives has been
;    implemented in the sector hunt loop (dNxtSec) so that a drive with
;    no disk won't hang when selected.
;
;		Item	  Offset from start of BIOS
;		------	  ----------------------------------
;		MODE	  0559 (25D9 in memory after MOVCPM)
;		IOBYTE	  055A (25DA in memory after MOVCPM)
;		SNDNULL	  055B (25DB in memory after MOVCPM)
;		SELTIME	  055C (25DC in memory after MOVCPM)
;
;    Version History
;
;    Ver     Date	  Description
;    ---   ---------	---------------------
;    1.0    7/15/2014	Original, Mike Douglas
;
;    1.1    3/25/2015	Mike Douglas
;			Invalidate read buffer on head unload. This forces
;			an extra read at times, but ensures the disk is
;			re-read when a disk is swapped. Add verify and
;			retry to track writes.
;
;    1.2    11/6/2015	Mike Douglas
;			Disable interrupts when disk flush is called as a
;			result of the BIOS CONIN call. Previously, this
;			flush ran without interrupts blocked.
;
;    2.0    11/1/2017	Mike Douglas
;			Complete rewrite from scratch. Previous versions were
;			created using Burcon CP/M from 1980 as the starting 
;			point which contains a lot of convoluted code I was 
;			never happy with. The update provides an additional
;			5%-20% speed improvement over previous versions of
;			CP/M 2.2b. The update also adds some new features:
;			  1) Works with Altair 8" or Altair Mini-Disk with
;			     conditional assembly.
;			  2) Disk select timeout (8" floppy only).
;			  3) Individual sectors can be bad within a track
;			     and the remaining sectors still accessed.
;			  4) To improve speed, checksum and stop byte vali-
;			     dation are done on the fly as the sector is read.
;			     (only Altair software I know of that does this)
;			  5) Smarter and faster error detection and recovery:
;			     Up to 12 retries per sector for all 32 sectors
;			     on a track and a possible track restore and
;			     re-seek all complete in less than 3 seconds.
;			  6) Write verify can be enabled/disabled with the
;			     fWRTVFY bit in the MODE byte (same bit as used
;			     with Altair (Burcon) CP/M 2.2).
;
;    2.1    11/28/2017	Mike Douglas
;			Add Extended Function support. This is a hook for 
;			non-standard BIOS functions via the select disk
;			(selDsk) entry point. This BIOS supports "Flush
;			Track Buffer" (function 0). See selDsk for details.
;
;    2.2    11/10/2019	Mike Douglas
;			Clean up serial I/O code. No functional changes.
;
;    2.3    06/07/2020	Mike Douglas
;			Include 1.4s timeout in sector hunt (dNxtSec) so
;			that 5.25" drives (which don't support "ready" the
;			same as 8" drives) won't hang when a disk is not
;			inserted. Ensure 600us delay after writes to better
;			support drives with tunnel erase heads when write
;			verify is disabled.
;
;--------------------------------------------------------------------------
;
;    To patch changes made to this BIOS into a CP/M image saved from
;    MOVCPM8 or MOVCPM5 (e.g., CPMxx.COM), use following commands:
;
;	A>DDT CPMxx.COM
;	-IBIOS.HEX
;	-Rxxxx      (where xxxx = BIAS computed below)
;	-G0	    (Go zero, not "oh")
;	A>SYSGEN
;
;****************************************************************************

MINIDSK	equ	0		;choose 1=Altair Mini Disk, 0=Altair 8" Floppy
MEMSIZE	equ	56		;set memory size in K bytes

;-----------------------------
;  8" floppy equates
;-----------------------------
   if NOT MINIDSK
BIOSLEN	equ	1900h		;length of BIOS for 8" floppy

NUMTRK	equ	77		;number of tracks on the disk
NUMSEC	equ	32		;sectors per track
DATATRK	equ	6		;1st data format track
SECMASK	equ	1Fh		;five bit sector numbers

BSH	equ	4		;allocation block shift factor
BLM	equ	0Fh		;allocation block mask
EXM	equ	0		;extent mask
DSM	equ	149		;max block number (150 blocks of 2K bytes)
DRM	equ	63		;max directory entry number (64 entries) 
AL0	equ	0C0h		;directory allocation block bits byte 0
AL1	equ	0		;directory allocation block bits byte 1
RESTRK	equ	2		;reserved tracks for boot image
CKS	equ	(DRM+1)/4	;directory check space
   endif

;-----------------------------
;  Mini Disk equates
;-----------------------------
   if MINIDSK
BIOSLEN	equ	1000h		;length of BIOS for mini disk

NUMTRK	equ	35		;number of tracks on the disk
NUMSEC	equ	16		;sectors per track
DATATRK	equ	4		;1st data format track
SECMASK	equ	0Fh		;four bit sector numbers

BSH	equ	3		;allocation block shift factor
BLM	equ	07h		;allocation block mask
EXM	equ	0		;extent mask
DSM	equ	61		;max block number (62 blocks of 1K bytes)
DRM	equ	31		;max directory entry number (32 entries) 
AL0	equ	80h		;directory allocation block bits byte 0
AL1	equ	0		;directory allocation block bits byte 1
RESTRK	equ	4		;reserved tracks for boot image
CKS	equ	(DRM+1)/4	;directory check space
   endif

;-----------------------------
; CP/M size & locations
;-----------------------------
CCPLEN	equ	0800h		;CPM 2.2 fixed length
BDOSLEN	equ	0E00h		;CPM 2.2 fixed length

CCPBASE	equ	MEMSIZE*1024 - BIOSLEN - BDOSLEN - CCPLEN
BDOSENT	equ	(CCPBASE+CCPLEN+6)	 ;entry address of BDOS
BIOSBAS	equ	(CCPBASE+CCPLEN+BDOSLEN) ;base address of this BIOS
MODEADR	equ	BIOSBAS+0559h	;location of MODE byte (for compatibility)

BIAS	equ	0A80h-CCPBASE	;bias in DDT to patch in this BIOS

;-----------------------------
; CP/M page zero equates
;-----------------------------
WBOOTV	equ	00h		;warm boot vector location
BDOSV	equ	05h		;bdos entry vector location
CDISK	equ	04h		;CPM current disk
DEFDMA	equ	80h		;default dma address

IOBYTE	equ	03h		;CPM IOBYTE address
SHFTCON	equ	1		;left rotate count to align CON in bits 2,1
SHFTRDR	equ	7		;left rotate count to align RDR in bits 2,1
SHFTPUN	equ	5		;left rotate count to align PUN in bits 2,1
SHFTLST	equ	3		;left rotate count to align LST in bits 2,1

;-----------------------------
; Common disk parameters
;-----------------------------
NUMDISK	equ	4		;four drives supported
CSECLEN	equ	128		;CP/M sector length
SSECLEN	equ	133		;Altair system sector length
DSECLEN	equ	136		;Altair data sector length
TSECLEN	equ	DSECLEN+1	;length of sector in track table

WBSSEC	equ	3		;starting sector of CP/M image
WBSECS	equ	(CCPLEN+BDOSLEN)/CSECLEN   ;# warm boot sectors to read

; System tracks (0-5) sector format

SYSTRK	equ	0		;offset of track number
SYSDATA	equ	3		;offset of 128 byte data payload
SYSSTOP	equ	131		;offset of stop byte (FFh)
SYSCSUM	equ	132		;offset of checksum

; Data tracks (6-76) sector format

DATTRK	equ	0		;offset of track number
DATSEC	equ	1		;offset of sector number
DATCSUM	equ	4		;offset of checksum
DATDATA	equ	7		;offset of 128 byte data payload
DATSTOP	equ	135		;offset of stop byte (FFh)

;-----------------------------
; Altair disk controller 
;-----------------------------
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

;-----------------------------
; 2SIO Serial Board 
;-----------------------------
SIO1CTL	equ	10h		;1st port on 2SIO board - control register
SIO1DAT	equ	11h		;1st port on 2SIO board - data register
SIO2CTL	equ	12h		;2nd port on 2SIO board - control register
SIO2DAT	equ	13h		;2nd port on 2SIO board - data register
SIORDRF	equ	01h		;read data register full flag
SIOTDRE	equ	02h		;transmit data register empty flag

;-----------------------------
; SIO Serial Board 
;-----------------------------
SIOCTL	equ	00h		;SIO board at 0 - control register
SIODAT	equ	01h		;SIO board at 0 - data register
ACRCTL	equ	06h		;SIO board at 6 - control register (cassette)
ACRDAT	equ	07h		;SIO board at 6 - data register (cassette)
SIORCV	equ	01h		;data received bit (inverted)
SIOXMT	equ	80h		;read to transmit (inverted)

;-----------------------------
; LPC Line Printer Controller
;-----------------------------
LPCCTL	equ	02h		;line printer control port
LPCDAT	equ	03h		;line printer data port
LPCBSY	equ	02h		;mask to test printer busy flag

;-----------------------------
; Misc equates
;-----------------------------
RDTRIES	equ	6		;read tries (must be <= 8)
VFTRIES	equ	4		;verify tries per write (must be <= 8)
WRTRIES	equ	3		;track writes to try

UNDEF	equ	0FFh		;undefined value
CR	equ	13		;ascii for carriage return
LF	equ	10		;ascii for line feed

;**************************************************************************
; MODE, IOBYTE, sndNull, selTime
;    For backward compatibility, the MODE byte must be at offset 0559h
;    from the start of the BIOS. The default IOBYTE, sndNull flag, and
;    select timeout values follow the MODE byte. A FOUR BYTE HOLE AT
;    OFFSET 0559h MUST BE PRESENT TO MAKE ROOM FOR THESE BYTES.
;**************************************************************************
	org	MODEADR		;at 0559h from start of BIOS
	
mode	db	40h		;default mode byte, write verify on

fCLDCMD	equ	01h		;true = CCP process cmd on cold start
fWRMCMD	equ	02h		;true = CCP process cmd on warm start
fWRTVFY	equ	40h		;true = verify writes

;---------------------------------------------------------------------------
;  IOBYTE definition - default IOBYTE at offset 055Ah
;
;    CON device (bits 1,0):
;	00 - TTY on SIO at I/O address 0
;	01 - CRT on 2SIO port 1
;	10 - BAT indirect through RDR logical device
;	11 - UC1 on 2SIO port 2
;
;    RDR device (bits 3,2):
;	00 - TTY on SIO at I/O address 0
;	01 - PTR on 2SIO port 1
;	10 - UR1 on cassette port (SIO 6/7)
;	11 - UR2 on 2SIO port 2
;
;    PUN device (bits 5,4):
;	00 - TTY on SIO at I/O address 0
;	01 - PTP on 2SIO port 1
;	10 - UP1 on cassette port (SIO 6/7)
;	11 - UP2 on 2SIO port 2
; 
;    LST device bits (7,6):
;	00 - TTY on SIO at I/O address 0
;	01 - CRT on 2SIO port 1
;	10 - LPT on LPC board
;	11 - UL1 on 2SIO port 2
;---------------------------------------------------------------------------
dIoByte	db	055h		;CON = CRT (2SIO serial port #1)
				;RDR = PTR (2SIO serial port #1)
				;PUN = PTP (2SIO serial port #1)
				;LST = UL1 (2SIO serial port #1)
				
; sndNull flag - at offset 055Bh
;    Determines if a NULL should be transmitted following a CR when
;    writing to the console device. This is automatically set during
;    cold start based on the baud rate of the console device. A user
;    can change this flag if needed after each cold start.

sndNull	db	0		;true if null should be sent after CR

; selTime - drive select timeout in seconds, at offset 055Ch

selTime	db	7		;drive select timeout in seconds

	org	BIOSBAS
;-----------------------------------------------------------------------------
;  BIOS Entry Jump Table
;-----------------------------------------------------------------------------
	jmp	boot		;cold start
wbJump	jmp	wBoot		;warm start
	jmp	conSt		;console status
	jmp	conIn		;console character in (and disk flush)
	jmp	conOut		;console character out
	jmp	list		;list character out
	jmp	punch		;punch character out
	jmp	reader		;reader character in
	jmp	home		;move head to home position
	jmp	selDsk		;select disk
	jmp	setTrk		;set track number
	jmp	setSec		;set sector number
	jmp	setDma		;set dma address
	jmp	read		;read disk
	jmp	write		;write disk
	jmp	listSt		;return list status
	jmp	secTran		;sector translate

;----------------------------------------------------------------------------
; boot - Cold boot BIOS entry. CPM is already loaded in memory. Initialize
;    serial ports, display welcome banner and enter the CCP.
;----------------------------------------------------------------------------
boot	lxi	sp,0100h	;init stack pointer

	call	serInit		;initialize serial ports and IOBYTE

	call	dispMsg		;display welcome message
	db	CR,LF,LF
	db	MEMSIZE/10 + '0',MEMSIZE mod 10 + '0'
	db	'K CP/M 2.2b v2.3',CR,LF

   if MINIDSK
	db	'For Altair Mini Disk',CR,LF,0
   endif

   if NOT MINIDSK
	db	'For Altair 8" Floppy',CR,LF,0
   endif
				;dispMsg returns with A=0
	sta	CDISK		;current CPM disk is drive zero
	sta	coldSt		;no longer in cold start

	call	initCpm		;common cold/warm boot CPM initialization

	lda	mode		;see if cold start command line requested
	ani	fCLDCMD
	jz	CCPBASE+3	;enter CCP and don't run command line

	jmp	CCPBASE		;enter CCP with possible cmd line

;----------------------------------------------------------------------------
; wBoot - Warm boot BIOS entry. Reload CPM from disk up to, but
;    not including the BIOS. Re-enter CPM after loading.
;----------------------------------------------------------------------------
wBoot	lxi	sp,0100h	;init stack pointer

	xra	a		;boot from drive zero
	sta	drvNum

	lxi	b,WBSECS*256+WBSSEC    ;B=secs to read, C=starting sec
	lxi	h,CCPBASE	;HL->CP/M load address

wbTrk	sta	trkNum		;save track number to use

; wbLoop - read sector loop for warm boot

wbLoop	shld	dmaAddr		;set read destination
	mov	a,c		;set sector number from C
	sta	secNum

	push	b		;save sector count and number
	call	read		;read next sector
	pop	b
	jnz	wBoot		;failed read, start over
	
	dcr	b		;decrement total sector count
	jz	warmCpm		;warm boot is done, warm enter CP/M
	
	inr	c		;bump to next sector number
	mvi	a,NUMSEC	;A=number of sectors in a track
	sub	c		;all sectors in track done?
	jnz	wbLoop		;no, read next sector
	
	mov	c,a		;start with C=sector 0 on text track
	lda	trkNum		;increment track number
	inr	a

	jmp	wbTrk		;continue

warmCpm	call	initCpm		;common warm/cold boot CPM initialization

	lda	mode		;see if warm start command line requested
	ani	fWRMCMD
	jz	CCPBASE+3	;enter CCP and don't run command line

	jmp	CCPBASE		;enter CCP with possible cmd line

;----------------------------------------------------------------------------
; initCpm - Initialization for CPM that is common to both warm
;    boot and cold boot.
;----------------------------------------------------------------------------
initCpm	lxi	h,DEFDMA	;set the default dma address (80h)
	shld	dmaAddr

	mvi	a,JMP		;8080 "jump" opcode
	sta	WBOOTV		;store in 1st byte of warm boot vector
	sta	BDOSV		;and 1st byte of bdos entry vector

	lxi	h,wbJump	;get the warm boot jump address
	shld	WBOOTV+1	;and put it after the jump

	lxi	h,BDOSENT	;BDOS entry address
	shld	BDOSV+1		;put it after the jump opcode

	lda	CDISK		;get current disk number
	mov	c,a		;pass it to CPM in register C
	ret

;----------------------------------------------------------------------------
; selDsk - Select Disk BIOS entry. C contains the disk number to select.
;    Validate the disk number and return a pointer to the disk parameter
;    header in HL. Zero is returned in HL for invalid drive number. The
;    disk number is stored in drvNum. No drive activity takes place.
;
; On Entry
;    C = disk number to select (or FFh, see Extended Functions below)
;
; On Exit
;    drvNum = disk number to select
;    Success: HL->DPH for this drive. Error: HL=0
;    Clobbers A,D,E,H,L
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
	cpi	NUMDISK		;verify drive number less than number of disks
	rnc			;invalid drive number, return with HL=0=error

	sta	drvNum		;save new drive number

	rlc			;compute disk parameter header address
	rlc			;   as (16*drvNum) + dpHead
	rlc
	rlc			;A=16*drvNum

	lxi	h,dpHead  	;HL->start of disk parameter headers
	mov	e,a
	mvi	d,0
	dad	d		;HL->DPH for the passed drive number
	ret

;----------------------------------------------------------------------------
; home - Home BIOS entry. Set trkNum to zero. No drive activity takes place.
;
; On Entry
;
; On Exit
;    trkNum set to zero
;    Clobbers A
;----------------------------------------------------------------------------
home	xra	a		;set trkNum to zero
	sta	trkNum
	ret

;----------------------------------------------------------------------------
; setTrk - Set Track BIOS entry. C contains the desired track number.
;    The track number is saved in trkNum for later use.
;
; On Entry
;    C = track number to set
;
; On Exit
;    trkNum = track specified in C
;    Clobbers A
;----------------------------------------------------------------------------
setTrk	mov	a,c		;save track number passed in C
	sta	trkNum
	ret

;----------------------------------------------------------------------------
; setSec - Set Sector BIOS entry. C contains the 1-indexed CPM physical 
;   sector. Saved in secNum as a zero-indexed value.  
;
; On Entry
;    C = 1-indexed sector number to set
;
; On Exit
;    secNum = zero-indexed sector number
;    Clobbers A
;----------------------------------------------------------------------------
setSec	mov	a,c		;A=1-indexed sector number
	dcr	a		;convert to zero indexed
	sta	secNum
	ret

;----------------------------------------------------------------------------
; setDma - Set DMA BIOS entry. BC contains the address for reading or
;    writing sector data for subsequent I/O operations. The address is
;    stored in dmaAddr.
;
; On Entry
;    BC = I/O buffer address
;
; On Exit
;    dmaAddr = address specified in BC
;    Clobbers H,L
;----------------------------------------------------------------------------
setDma	mov	h,b		;save buffer address passed in BC
	mov	l,c
	shld	dmaAddr
	ret
	
;----------------------------------------------------------------------------
; secTran - Sector translation BIOS entry. Convert zero-indexed logical 
;    sector number in BC to a CPM one-indexed physical sector number in HL
;    based on the translate table passed in DE.
;
; On Entry
;    BC = logical sector number
;    DE->sector translate table for selected drive
;
; On Exit
;    HL = physical sector number
;    Clobbers B,D,E,H,L
;----------------------------------------------------------------------------
secTran	xchg			;HL->translate table
	mvi	b,0		;make sure msb of sector is zero
	dad	b		;HL->translated sector num
	mov	l,m		;L=lsb of sector
	mov	h,b		;H=0=msb of sector
	ret

;----------------------------------------------------------------------------
; read - Read sector BIOS entry. Read one sector using the drvNum, 
;    trkNum, secNum, and dmaAddr previously specified.
;
; On Entry
;    drvNum = drive to read from
;    trkNum = track to read from
;    secNum = sector number to read (0-31)
;    dmaAddr = address of buffer to read into
;
; On Exit
;    If read successful
;	sector read into (dmaAddr)
;	HL = (dmaAddr) + CSECLEN
;	A=0, Z flag set true
;    Else
;	A=1, Z flag set false
;    Interrupts enabled
;    Clobbers all
;----------------------------------------------------------------------------
read	call	ckFlush		;flush track buffer if needed
	ei			;re-enable interrupts

	call	readTrk		;fill track buffer if needed
	ei			;re-enable interrupts
	jnz	exitDio		;track read error, exit
	
	call	movRead		;move sector to (dmaAddr)

exitDio	mvi	a,0		;if zero is true, return zero
	rz

	inr	a		;else return A<>0, Z false
	ret
	
;----------------------------------------------------------------------------
; write - Write sector BIOS entry. Write one sector using the drvNum,
;    trkNum, secNum and dmaAddr specified. 
;
; On Entry
;    drvNum = drive to write
;    trkNum = track to write
;    secNum = sector number to write (0-31)
;    dmaAddr = address of buffer to write from
;
; On Exit
;    If successful
;	sector written to trkBuf from (dmaAddr)
;	A=0, Z flag set true
;    Else
;	A=1, Z flag set false
;    Interrupts enabled
;    Clobbers all
;----------------------------------------------------------------------------
write	call	ckFlush		;fluhsh track buffer if needed
	ei			;re-enable interrupts

	call	readTrk		;fill track buffer if needed
	ei			;re-enable interrupts
	jnz	exitDio		;track read error, exit

	jmp	movWrt		;move (dmaAddr) to sector and exit
	
;----------------------------------------------------------------------------
; movRead - Move sector data from track buffer to (dmaAddr) for a
;    CPM read request
;
; On Entry
;    trkNum = track to read
;    secNum = sector number to read (0-31)
;    dmaAddr = address of buffer to read into
;
; On Exit
;    If successful (good sector)
;	sector data moved to (dmaAddr), Z flag set true
;	HL = (dmaAddr) + CSECLEN
;    Else (sector flagged as bad)
;	Z flag set false
;    Clobbers all
;----------------------------------------------------------------------------
movRead	call	altSkew		;secNum to hard sector in A
	call	dSecAdr		;HL->sector in trkBuf
	
	mov	a,m		;A=sector valid flag
	ora	a
	rnz			;bad sector, return error
	
	lxi	d,DATDATA+1	;DE=offset to data portion of sector
	lda	trkNum		;on a data track?
	cpi	DATATRK
	jnc	mrMove		;yes, data track (DE already correct)
	
	lxi	d,SYSDATA+1	;DE=offset to data portion of sector

mrMove	dad	d		;HL->data portion of sector
	xchg			;DE->data portion of sector
	lhld	dmaAddr		;HL->destination for data
	mvi	b,CSECLEN	;B=number of bytes to move
	
mrLoop	ldax	d		;move sector from trkBuf to (dmaAddr) 
	mov	m,a
	inx	h		;increment pointers
	inx	d
	dcr	b		;loop count
	jnz	mrLoop 

	ret			;exit with zero	status
	
;----------------------------------------------------------------------------
; movWrt - Move sector data from (dmaAddr) to track buffer and create 
;    metadata for the sector for a CPM write request.
;
; On Entry
;    trkNum = track to write
;    secNum = sector number to write (0-31)
;    dmaAddr = address of buffer to write from
;
; On Exit
;    Sector data moved to trkBuf from (dmaAddr)
;    A=0, Z status true
;    Clobbers all
;----------------------------------------------------------------------------
movWrt	call	altSkew		;secNum to hard sector in A
	call	dSecAdr		;HL->sector in trkBuf
	
	mvi	m,0		;flag sector as good
	
	inx	h		;HL->track
	lda	trkNum		;A=track
	ori	80h		;set sync bit
	mov	m,a		;set track in sector
	
	inx	h		;HL->byte after track
	cpi	DATATRK+80h	;on a system or data track?
	jnc	wDatTrk		;data track 6-76 (mini disk 4-34)

; Create Altair sector for system tracks 0-5 (mini disk 0-3)

	xra	a		;put 0100h (16 bit) at offset 1,2
	mov	m,a
	inx	h		;HL->offset 2
	inr	a		;A=1
	mov	m,a
	
	inx	h		;HL->128 byte CPM sector in Altair sector
	call	mwMove		;move (dmaAddr) to sector in trkBuf
	
	mvi	m,0FFh		;offset 131 is stop byte (0FFh)
	
	inx	h		;offset 132 is checksum
	mov	m,b		;store checksum at offset 132
	
	jmp	mwExit		;exit

; wDatTrk- Create Altair sector for tracks 6-76 (mindisk 4-34)

wDatTrk	lda	secNum		;A=sector before Altair skew
	mov	m,a		;store Altair logical sector number
	
	inx	h		;HL->offset 2 in sector
	xra	a		;store zero at offsets 2-6
	mov	m,a		;offset 2
	
	inx	h
	mov	m,a		;zero at offset 3
	
	inx	h
	push	h		;save address of offset 4 = checksum
	
	inx	h
	mov	m,a		;zero at offset 5
	
	inx	h
	mov	m,a		;zero at offset 6
	
	inx	h		;HL->128 byte CPM sector in Altair sector
	call	mwMove		;move (dmaAddr) to sector in trkBuf

	mvi	m,0FFh		;offset 135 is stop byte (0FFh)
	
	pop	h		;HL->checksum byte in Altair sector
	mov	m,b		;store the checksum
	
; mwExit - set dirty flag true, return success status

mwExit	mvi	a,0FFh		;set dirty flag true
	sta	bfDirty
	
	xra	a		;return success status
	ret

;------------------------------------------------------------------------------
; mwMove - Move sector buffer (128 bytes) from (dmaAddr) to (HL) as part
;   of a CPM write command. Compute checksum on all bytes moved and return
;   the checksum in B.
;
; On Entry
;    HL->destination sector in trkBuf
;    dmaAddr = address of buffer to move from
;
; On Exit
;    128 bytes moved from (dmaAddr) to (HL)
;    HL = HL + 128
;    B = checksum of the 128 bytes moved
;    Clobbers all
;------------------------------------------------------------------------------
mwMove	xchg			;DE->destination CPM sector in trkBuf
	lhld	dmaAddr		;HL->source buffer
	lxi	b,CSECLEN	;B=checksum (0), C=128 byte count
	
mwLoop	mov	a,m		;move from (HL) to (DE)
	stax	d
	
	add	b		;add byte to checksum
	mov	b,a
	
	inx	d		;increment both pointers
	inx	h
	dcr	c		;decrement character count
	jnz	mwLoop		;loop until count = 0
	
	xchg			;return with buffer pointer in HL	
	ret
	
;----------------------------------------------------------------------------
; altSkew - Perform Altair skew on the sector number in secNum and return
;    the result in A. The skew is based on the track as:
;
;	Tracks 0-5, secOut = secIn
;	Tracks 6-76, secOut = (secIn * 17) MOD 32
;
;    The skew computation for tracks 6-76 is implemented as:
;	secOut = secIn if secIn is even
;	secOut = secIn XOR 10h if secIn is odd
;
; On Entry
;    trkNum = current track
;    secNum = sector number (0-31)
;
; On Exit
;    A = sector number after Altair skew
;    Clobbers A,B
;----------------------------------------------------------------------------	
altSkew	equ	$

   if MINIDSK
	lda	secNum		;no skewing done for mini disk
	ret
   endif

   if NOT MINIDSK
	lda	trkNum		;on a data track?
	cpi	DATATRK
	lda	secNum		;A=unmodified sector number
	rc			;system track, no change to sector
	
	mov	b,a		;save secNum in B
	rrc			;test for even/odd
	mov	a,b		;restore secNum in A
	rnc			;return with secNum if even
	
	xri	10h		;else translate as in comments above
	ret
   endif
		
;----------------------------------------------------------------------------
; readTrk- read full track into track buffer if the requested 
;    drive (drvNum) or track (trkNum) does not match the buffered
;    drive (bfDrive) or buffered track (bfTrack). The status byte
;    at the start of each sector is set to zero if the sector is
;    good, non-zero if the sector couldn't be read.
;
; On Entry
;    drvNum = drive to read
;    trkNum = track to read
;    bfDrive = drive from which trkBuf was filled
;    bfTrack = track from which trkBuf was filled
;
; On Exit
;    trkNum on drvNum read into trkBuf
;    Zero true if track read or already there, zero false otherwise
;    Clobbers all
;----------------------------------------------------------------------------
readTrk	lhld	bfDrive		;L=buffered drive, H=buffered track

	lda	drvNum		;A=requested drive
	cmp	l		;same drive buffered?
	jnz	rtNew		;drive doesn't match, need a new buffer

	lda	trkNum		;A=requested track
	cmp	h		;same track buffered?
	rz			;yes, already have this buffer

; rtNew - New track needs to be read

rtNew	mvi	a,UNDEF		;invalidate buffered data
	sta	bfDrive
	
	call	dSelDrv		;select drive, load head
	rnz			;drive select failed, exit with error

	mvi	a,2		;init restore/seek try counter
	sta	skRetry
	
rtRtry	call	dSeek		;seek to trkNum (disables interrupts)
	rnz			;a restore was required and failed
	
	call	initTrk		;init all sectors to "bad" (not read)
	lxi	d,RDTRIES*NUMSEC*256 + NUMSEC
				;D=max sector reads = RDTRIES revolutions
				;E=sectors remaining to fill

; Read sector loop - All sectors are originally marked "bad" (unread) and
;    a sector counter in E is started at NUMSEC (a full track of sectors).
;    Sectors are read until sector counter E reaches zero or the total
;    reads counter in D reaches zero (RDTRIES revolutions). The 1st time
;    the total reads counter reaches zero, a restore and re-seek is 
;    performed in case we're on the wrong track. The second time it reaches
;    zero, the bad sectors remain marked as bad in the track buffer.

; Within dRead, 152 cycles execute after the last byte is read until we're
;    executing the instruction here following the dRead call. An additional
;    78 cycles are executed here until we're in dNxtSec looking for sector
;    true. This totals 230 cycles (115us) from the last byte read until
;    we're hunting for sector true. Allowing for 200us of index alignment
;    error and 10% speed variance, we have at least 180us to get this done.

; For the Mini Disk, a full track is 16 sectors instead of 32 and the time
;    available after a sector is read is well over 1000us, so timing is not
;    an issue.

rtLoop	call	dNxtSec		;(17) wait for next sector, HL->sector buf
	jnz	rtRead		;sector pulse found (inverted logic)

	inr	a		;force non-zero status
	ret			;return the error

rtRead	mov	a,m		;A=sector flag byte
	ora	a		;this sector already read?
	jz	rtNext		;yes, skip it

	push	h		;save sector address
	inx	h		;HL->1st read location in sector
	call	dRead		;read the sector
	pop	h		;(10) restore sector address in HL
	jnz	rtNext		;(10) read error

	xra	a		;(4) set sector flag to zero (good sector)
	mov	m,a		;(7) 

	dcr	e		;(5) decrement sectors left to fill
	jz	rtExit		;(10) all sectors read without error

rtNext	dcr	d		;(5) decrement total sectors counter
	jnz	rtLoop		;(10) go read next sector

	lxi	h,skRetry	;HL->seek retry counter
	dcr	m		;decrement the counter
	jz	rtExit		;re-seek(s) done, we're finished

	call	dRestor		;restore to track zero
	jz	rtRtry		;start over after the re-seek

	ret			;else, exit with error

rtExit	lhld	drvNum		;L=drvNum, H=trkNum
	shld	bfDrive		;set the buffered drive and track values
	ret			;exit with Z flag true

;----------------------------------------------------------------------------
; initTrk - Set the flag byte in each sector in trkBuf to "bad" to
;    indicate none of the sectors have been read.
;
; On Entry
;
; On Exit
;    All sectors in trkBuf marked "bad" (unread)
;    Clobbers all
;----------------------------------------------------------------------------
initTrk	lxi	b,UNDEF*256 + NUMSEC;  B=UNDEF (bad), C=NUMSEC
	lxi	h,trkBuf	;HL->track buffer
	lxi	d,TSECLEN	;DE=length of each sector in track buffer

itLoop	mov	m,b		;mark sector as bad (not read)
	dad	d		;HL->next sector in trkBuf
	dcr	c		;repeat for all sectors
	jnz	itLoop
	
	ret

;----------------------------------------------------------------------------
; ckFlush - Check if track buffer should be flushed. This
;    function must be called before any drive selection or seek
;    operation. The buffer is flushed if the bfDirty flag is
;    set and the drive or track number are different than the
;    buffered track.
;
; On Entry
;    Drive still selected and on same track as in trkBuf
;    drvNum, trkNum updated for the new I/O call
;
; On Exit
;    Zero true for no error, zero false if write error occured
;    Clobbers all
;----------------------------------------------------------------------------
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

wrtTrk	equ	$

   if MINIDSK
	mvi	a,cRESTMR	;restart motor timeout
	out	DRVCMD
 
	lda	curDrv		;make sure current drive still enabled
	out	DRVSLCT
   endif

	mvi	e,cWRTEN	;E=write command, normal head current
	mvi	a,42		;tracks 0-42 are normal head current
	cmp	h		;compare 42 - current track
	jnc	wrtInit		;track is 0-42, E is correct
	
	mvi	e,cWRTEN+cHCSON	;E=write command with reduced current
	
wrtInit	xra	a		;clear buffer dirty flag
	sta	bfDirty

	mvi	a,WRTRIES	;initialize write retry count
	sta	wrRetry

	di			;disable interrupts

; rtryWrt - Write track retry entry point. Write a full track starting
;    with any sector. Don't write sectors that are flagged as bad.

rtryWrt	mvi	d,NUMSEC	;D=count of sectors to write
	xra	a		;zero the count of sectors actually written
	sta	secCnt

wrtLoop	call	dNxtSec		;(78) wait for next sector, HL->sector buf
	mov	a,m		;(7) check sector flag
	ora	a		;(4) 
	jnz	wrtNext		;(10) skip if bad or previously verified
	
	mov	a,e		;(5) issue write command to drive
	out	DRVCMD		;(10) at 114 cycles 57us
	
	inx 	h		;point to 1st byte of sector
	call	dWrite		;write the sector

	lxi	h,secCnt	;count number of sectors written	
	inr	m

wrtNext	dcr	d		;decrement write sector count
	jnz	wrtLoop		;loop until all sectors processed

	mvi	a,6		;delay 600us for tunnel trim erase
	call	delay

; If the verify flag is set in the mode byte, verify the track just written.
;    At this point, secCnt has the number of sectors actually written.

	lda	mode		;verifying writes?
	ani	fWRTVFY
	rz			;no, exit

	mvi	d,NUMSEC*VFTRIES   ;D=maximum number of sector reads
	
vfyLoop	call	dNxtSec		;wait for next sector, HL->sector buf
	mov	a,m		;test sector flag, only good and
	ora	a		;   not previously verified sectors
	jnz	vfyNext		;   are verified
		
	push	h		;save pointer to sector flag
	inx	h		;point to 1st byte of sector
	call	dVerify		;verify the sector
	pop	h		;HL->sector flag for current sector
	jnz	vfyNext		;didn't match
	
	mvi	m,UNDEF		;set flag to indicate sector is verified

	lxi	h,secCnt	;HL->sectors left to verify
	dcr	m		;decrement the count
	rz			;track verified, exit

vfyNext	dcr	d		;decrement sector count
	jnz	vfyLoop		;loop until all verify tries expired

; Read re-tries expired, decrement the write retry count and write again

	lxi	h,wrRetry	;decrement the write retry counter
	dcr	m
	jnz	rtryWrt		;retry starting with the write

	call	dispMsg
	db	CR,LF,'Delayed Write Error', CR, LF, 0

	inr	a		;dispMsg returns zero
	ret			;exit with non-zero

;-------------------------------------------------------------------
; invFlush - Invalidate and flush the track buffer.
;    Console input is used as an idle indicator and as a good time
;    to flush a dirty track buffer, so the conIn function calls this
;    subroutine. Even if not flushed, the track buffer is invalidated
;    to force a track read on the next disk I/O request. This helps
;    catch a disk swap.
;
; On Entry:
;    Same drive is on the same track as buffered in trkBuf
;
; On Exit:
;    Track buffer written if required and invalidated.
;    Head unloaded
;    Z set for no error, Z cleared if write error occured.
; Clobbers all except HL
;-------------------------------------------------------------------
invFlsh	mvi	a,UNDEF		;invalidate track buffer
	sta	bfDrive

	push	h		;preserve HL in case conIn caller expects it
	call	ckFlush		;flush the track if needed
	pop	h

	ei			;restore interrupts

   if NOT MINIDSK
	mvi	a,cHDUNLD	;unload head
	out	DRVCMD
   endif

	ret
	
;------------------------------------------------------------------------------
; dispMsg - Display the null-terminated string following the dispMsg call
;
; On Entry SP->message
;
; On Exit
;    Clobbers C,H,L
;------------------------------------------------------------------------------
dispMsg	pop	h		;HL->string

dmLoop	mov	a,m		;A=next character
	inx	h		;bump string pointer
	ora	a		;null terminator?
	jz	dmExit		;yes, exit

	mov	c,a		;conOut needs character in C
	call	conOut		;send the character
	jmp	dmLoop

dmExit	pchl			;return past message string

;---------------------------------------------------------------------------
;  CON device I/O
;---------------------------------------------------------------------------

; conSt - Console status BIOS entry point. Return FFh if character ready,
;    return zero otherwise. 

conSt	mvi	b,SHFTCON	;B=console IOBYTE alignment shift count
	call	doIo		;call the proper I/O routine below

	dw	sioSt		;TTY use SIO at I/O address 0
	dw	sio1St		;CRT use 2SIO port 1
	dw	batchSt		;BAT indirect through RDR logical device
	dw	sio2St		;UC1 use 2SIO port 2

; conIn - Console input BIOS entry point. Wait for console input and
;    return character in A. Console input is used as an idle indicator
;    and as a good time to flush a dirty track buffer, unload the head, 
;    and invalidate the track buffer.

conIn	call	invFlsh		;invalidate and flush track buffer
	
	mvi	b,SHFTCON	;B=console IOBYTE alignment shift count
	mvi	c,07Fh		;C=AND mask for input byte
	call	doIo		;call the proper I/O routine below

	dw	sioIn		;TTY use SIO at I/O address 0
	dw	sio1In		;CRT use 2SIO port 1
	dw	reader		;BAT indirect through RDR logical device
	dw	sio2In		;UC1 use 2SIO port 2

; conOut - Console output BIOS entry point. Output the character in C.
;   If the sndNull flags is true (i.e., it was determined during cold start
;   that the console is a Teletype), then follow CR with a null.

conOut	call	doConO		;send the character passed in C

	lda	sndNull		;sending null for CR?
	ora	a
	rz			;no, exit
	
	mov	a,c		;was character a CR?
	sui	CR
	rnz			;no, exit
	
	mov	c,a		;put a zero in C and send it

doConO	mvi	b,SHFTCON	;B=console IOBYTE alignment shift count
	call	doIo		;call the proper I/O routine below

	dw	sioOut		;TTY use SIO at I/O address 0
	dw	sio1Out		;CRT use 2SIO port 1
	dw	list		;BAT indirect through LST logical device
	dw	sio2Out		;UC1 use 2SIO port 2

;---------------------------------------------------------------------------
; RDR device - Reader input character BIOS entry point. Return in A
;---------------------------------------------------------------------------
reader	mvi	b,SHFTRDR	;B=reader IOBYTE alignment shift count
	mvi	c,0FFh		;C=AND mask for input byte
	call	doIo		;call the proper I/O routine below

	dw	sioIn		;TTY SIO at I/O address 0
	dw	sio1In		;PTR use 2SIO port 1
	dw	acrIn		;UR1 use cassette port (SIO 6/7)
	dw	sio2In		;UR2 use 2SIO port 2 

;---------------------------------------------------------------------------
; PUN device - Punch output character BIOS entry point. Byte to send in C.
;---------------------------------------------------------------------------
punch	mvi	b,SHFTPUN	;B=punch IOBYTE alignment shift count
	call	doIo		;call the proper I/O routine below

	dw	sioOut		;TTY SIO at I/O address 0
	dw	sio1Out		;PTP use 2SIO port 1
	dw	acrOut		;UP1 use cassette port (SIO 6/7)
	dw	sio2Out		;UP2 use 2SIO port 2

;---------------------------------------------------------------------------
;  LST device I/O
;---------------------------------------------------------------------------

; listSt - List output test BIOS entry point. Return 0FFh if list device
;    is ready, 0 if not ready

listSt	mvi	b,SHFTLST	;B=list IOBYTE alignment shift count
	call	doIo		;call the proper I/O routine below

	dw	sioOSt		;TTY use SIO at I/O address 0
	dw	sio1OSt		;CRT use 2SIO port 1
	dw	lpcOSt		;LPT use LPC board
	dw	sio2OSt		;UL1 use 2SIO port 2

; list - List output character BIOS entry point. Send the character
;    passed in C out the list port

list	mvi	b,SHFTLST	;B=list IOBYTE alignment shift count
	call	doIo		;call the proper I/O routine below

	dw	sioOut		;TTY use SIO at I/O address 0
	dw	sio1Out		;CRT use 2SIO port 1
	dw	lpcOut		;LPT use LPC board
	dw	sio2Out		;UL1 use 2SIO port 2

;---------------------------------------------------------------------------
;  BAT device I/O - The BAT device is assigned as a physical device
;     for CON. However, BAT is actually a logical device that uses
;     whatever RDR is assigned to for input and whatever LST is assigned
;     to for output. The only jump table that does not already exist
;     is the input test routine for the reader devices
;---------------------------------------------------------------------------

; batchSt - BAT device input status

batchSt	mvi	b,SHFTRDR	;BAT in uses RDR device
	call	doIo		;call the proper I/O routine below

	dw	siost		;TTY test SIO at I/O address 0	
	dw	sio1St		;RDR test 2SIO port 1
	dw	acrSt		;UR1 test cassette port (SIO 6/7)
	dw	sio2St		;UR2 test 2SIO port 2

;---------------------------------------------------------------------------
; doIo - Perform physical I/O device based on the IOBYTE. The value passed
;    in B contains the left-shift count to align the calling device's IOBYTE
;    field into bits 2 and 1 (not 1 and 0). Preserves H,L
;--------------------------------------------------------------------------- 
doIo	lda	IOBYTE		;A=CP/M IO BYTE

ioAlign	rlc			;rotate into bits 2 and 1
	dcr	b
	jnz	ioAlign

	ani	6		;get the 2 bit field alone

; 2 bit field aligned in bits 2 and 1. Index into jump table of caller.

	xthl			;hl=table address from stack, save hl

	mov	e,a		;form DE=table offset
	mvi	d,0
	dad	d		;HL->address in table based on IOBYTE

	mov	e,m		;E=lsb of where to jump
	inx	h
	mov	d,m		;D=msb of where to jump
	xchg			;HL->where to jump

	xthl			;jump address on stack, restore HL
	ret			;jump to address specified in table

;---------------------------------------------------------------------------
; Input status routines
;---------------------------------------------------------------------------

; sio1St - Console status routine for 2SIO port 1. Return FFh 
;    if character ready, return zero otherwise. 

sio1St	in	SIO1CTL		;read 2SIO #1 status/control register
	ani	SIORDRF		;data present?
	rz			;no, return zero

	mvi	a,0FFh		;else, return FF
	ret

; sio2St - Console status routine for 2SIO port 2. Return 0ff 
;    if character ready, return zero otherwise. 

sio2St	in	SIO2CTL		;read 2SIO #2 status/control register
	ani	SIORDRF		;data present?
	rz			;no, return zero

	mvi	a,0FFh		;else, return FF
	ret

; sioSt - Console status routine for SIO at I/O address 0. Return FFh 
;    if character ready, return zero otherwise. 

sioSt	in	SIOCTL		;read SIO status/control register
	xri	0FFh		;convert to positive logic
	ani	SIORCV		;data present?
	rz			;no, return zero

	mvi	a,0FFh		;else, return FF
	ret

; acrSt - Console status routine for ACR status (could get called through BAT)

acrSt	in	ACRCTL		;read ACR (SIO) control register
	xri	0FFh		;convert to positive logic
	ani	SIORCV		;data present?
	rz			;no, return zero

	mvi	a,0FFh		;else, return FF
	ret

;---------------------------------------------------------------------------
; Character input routines
;---------------------------------------------------------------------------

; sio1In - input character from 1st port on a 2SIO board and return in A.
;    C contains AND mask.

sio1In	call	sio1St		;wait for character
	jz	sio1In		;no, loop until data available

	in	SIO1DAT		;read the character
	ana	c		;possibly clear msbit
	ret

; sio2In - input character from 2nd port on a 2SIO board and return in A
;    C contains AND mask.

sio2In	call	sio2St		;wait for character
	jz	sio2In		;no, loop until data available
	
	in	SIO2DAT		;read the character
	ana	c		;possibly clear msbit
	ret

; sioIn - input character from SIO board at address 0/1
;    C contains AND mask.

sioIn	call	sioSt		;wait for character
	jz	sioIn		;no, loop until data available
	
	in	SIODAT		;read the character
	ana	c		;possibly clear msbit
	ret

; acrIn - input character from ACR SIO board at address 6/7

acrIn	call	acrSt		;wait for character
	jz	acrIn		;no, loop until data available
	
	in	ACRDAT		;read the character
	ret

;----------------------------------------------------------------------------
; Output status routines
;----------------------------------------------------------------------------

; sio1OSt - Return FFh if 2SIO Port #1 is ready, 0 if not ready

sio1OSt	in	SIO1CTL		;read 2SIO #1 status/control register
	ani	SIOTDRE		;0=busy
	rz			;not ready, return 0

	mvi	a,0FFh		;else, return FF
	ret

; sio2OSt - Return FFh if 2SIO Port #1 is ready, 0 if not ready

sio2OSt	in	SIO2CTL		;read 2SIO #2 status/control register
	ani	SIOTDRE		;0=busy
	rz			;not ready, return 0

	mvi	a,0FFh		;else, return FF
	ret

; lpcOst - Return FFh if 88-LPC is ready, 0 if not ready

lpcOSt	in	LPCCTL		;88-LPC board at port 2,3
	ani	LPCBSY		;0=busy
	rz			;not ready, return 0

	mvi	a,0FFh		;else, return FF
	ret

; sioOSt - Return FFh if SIO at 0/1 is ready, 0 if not ready
 
sioOSt	in	SIOCTL		;read SIO status/control register
	xri	0FFh		;convert to positive logic
	ani	SIOXMT		;0=busy
	rz			;not ready, return 0

	mvi	a,0FFh		;else, return FF
	ret
	
;---------------------------------------------------------------------------
; Character output routines
;---------------------------------------------------------------------------

; sio1Out - Transmit byte through 2SIO port 1. Byte to send in C. Detect
;   110 baud teletype during cold start and set sndNull flag true.

sio1Out	lxi	d,0		;init inter-character timer

sio1Wt	in	SIO1CTL		;(10) read 2SIO #1 status/control register
	ani	SIOTDRE		;(7) transmit data register empty?
	jnz	sio1Rdy		;(10) yes, ready for next character

	inx	d		;(5) count 21us tics
	jmp	sio1Wt		;(10) keep waiting for ready

; If still in cold start, then see if the inter-character timer indicates
;    we're connected to a 110 baud Teletype. If so, set the sndNull
;    flag to true

sio1Rdy	lda	coldSt		;A=FF during cold start, 0 otherwise
	ana	d		;A=D during cold start, 0 otherwise
	cpi	11		;if D (MSB of counter) < 11, not 110 baud
	jc	sio1Xmt		;not 110 baud

	sta	sndNull		;otherwise, set send null flag true

; sio1Xmt - send the character in C. 

sio1Xmt	mov	a,c		;get character to xmit
	out	SIO1DAT		;send it
	ret

;---------------------------------------------------------------------------
; sio2Out - Transmit byte through 2SIO port 2. Byte to send in C. Detect
;   110 baud teletype during cold start and set sndNull flag true.

sio2Out	lxi	d,0		;init inter-character timer

sio2Wt	in	SIO2CTL		;read 2SIO #2 status/control register
	ani	SIOTDRE		;transmit data register empty?
	jnz	sio2Rdy		;yes, ready for next character

	inx	d		;count 21us tics
	jmp	sio2Wt		;keep waiting for ready

; if still in cold start, then see if the inter-character timer indicates
;    we're connected to a 110 baud Teletype. If so, set the sndNull
;    flag to true

sio2Rdy	lda	coldSt		;A=FF during cold start, 0 otherwise
	ana	d		;A=D during cold start, 0 otherwise
	cpi	11		;if D (MSB of counter) < 11, not 110 baud
	jc	sio2Xmt		;not 110 baud

	sta	sndNull		;otherwise, set send null flag true

; sio2Xmt - send the character in C. 

sio2Xmt	mov	a,c		;get character to xmit
	out	SIO2DAT		;send it
	ret

;---------------------------------------------------------------------------
; sioOut - Transmit byte through SIO at address 0/1. Byte to send in C. 
;   Detect 110 baud teletype during cold start.

sioOut	lxi	d,0		;init inter-character timer

sioWt	in	SIOCTL		;read SIO control register
	ani	SIOXMT		;transmit data register empty?
	jz	sioRdy		;yes, ready for next character

	inx	d		;count 21us tics
	jmp	sioWt		;keep waiting for ready

; if still in cold start, then see if the inter-character timer indicates
;    we're connected to a 110 baud Teletype. If so, set the sndNull
;    flag to true

sioRdy	lda	coldSt		;A=FF during cold start, 0 otherwise
	ana	d		;A=D during cold start, 0 otherwise
	cpi	11		;if D (MSB of counter) < 11, not 110 baud
	jc	sioSnd		;not 110 baud

	sta	sndNull		;otherwise, set send null flag true

; sioSnd - send the character in C. 

sioSnd	mov	a,c		;get character to xmit
	out	SIODAT		;send it
	ret

;---------------------------------------------------------------------------
; acrOut - Transmit byte through ACR SIO at address 6/7. Byte to send in C.

acrOut	in	ACRCTL		;read ACR (SIO) control register
	ani	SIOXMT		;transmit data register empty?
	jnz	acrOut		;not yet, keep waiting

	mov	a,c		;get character to xmit
	out	ACRDAT		;send it
	ret

;---------------------------------------------------------------------------
; lpcOut - Transmit byte through 88-LPC board.

lpcOut	call	lpcOSt		;wait for printer		
	jz	lpcOut

	mov	a,c
	ani	07Fh		;force bit 7 to zero
	out	LPCDAT		;output the character
	ret

;-----------------------------------------------------------------------------
; serInit - Initialize serial I/O ports and IOBYTE
;-----------------------------------------------------------------------------
serInit	mvi	a,03h		;reset 2SIO ports
	out	SIO1CTL
	out	SIO2CTL

	mvi	a,11h		;select 8N2
	out	SIO1CTL
	out	SIO2CTL

	lda	dIoByte		;initialize IOBYTE with default
	sta	IOBYTE

	ret

;****************************************************************************
;
; Altair disk I/O routines
;
;****************************************************************************

;---------------------------------------------------------------------------
; dSelDrv - Select the drive specified in drvNum and load the head.
;    Save the track (curTrk) the current drive is on into the track table,
;    load curTrk for the new drive from the track table.
;
; On Entry
;    drvNum = drive to be selected
;    curDrv = drive currently selected
;    selTime = timeout in seconds to wait for drive select
;    curTrk = track the current drive is on
;
; On Exit
;    Drive selected and head loaded
;    curDrv = drvNum
;    curTrk = track the newly selected drive is on
;    Zero true for success, zero false for error
;    Clobbers all
;---------------------------------------------------------------------------
dSelDrv	equ	$

   if MINIDSK
	mvi	a,cRESTMR	;restart motor timeout
	out	DRVCMD
   endif

	lxi	h,curDrv	;HL->currently selected drive
	mov	e,m		;E=currently selected drive

	lda	drvNum		;A=desired drive
	mov	c,a		;save new drive in C
	cmp	e		;same or different drive?
	jnz	dNewDrv		;different, go select a new drive

	in	DRVSTAT		;drive still selected and enabled?
	ani	sDSKEN
	jz	dSelExt		;yes, exit

	jmp	dReSel		;otherwise, re-select the drive

; dNewDrv - Save current track (curTrk) for the current drive to the track
;    table, load curTrk for the new drive from the track table

dNewDrv	mov	m,c		;save new drive in curDrv
	
	mvi	d,0		;DE=currently selected drive
	lxi	h,trkTbl	;HL->track table
	dad	d		;HL->trkTbl entry for current drive

	lda	curTrk		;A=track current drive is on
	mov	m,a		;save it in the track table

	lxi	h,trkTbl	;HL->track table
	mov	e,c		;DE=offset in trkTbl for new drive
	dad	d		;HL->trkTbl for new drive

	mov	a,m		;A=track new drive is on
	sta	curTrk		;update current track

; Select the new drive with the timeout specified in selTime

dReSel	lxi	h,selTime	;move select timeout in seconds into B
	mov	b,m

dSelLp1	lxi	h,25641		;HL=count of 78 cycle loops for 1 second

dSelLp	mvi	a,cDSLCT	;(7) deselect a possibly attached drive
	out	DRVSLCT		;(10)

	mov	a,c		;(5) A=drive to select
	out	DRVSLCT		;(10) select it

	in	DRVSTAT		;(10) did drive select work?
	ani	sDSKEN		;(7)
	jz	dSelExt		;(5) yes, exit

	dcx	h		;(5) decr 1 sec counter
	mov	a,h		;(5) test HL counter for zero
	ora	l		;(4)
	jnz	dSelLp		;(10) loop for one second

	dcr	b		;decrement seconds expired
	jnz	dSelLp1		;repeat for a new 1 second

	inr	a		;timeout, return non-zero
	ret

dSelExt	mvi	a,cHDLOAD	;issue head load command
	out	DRVCMD		;restarts timeout for mini disk
  
	ret			;return with zero status

;---------------------------------------------------------------------------
; dSeek - Seek to track in trkNum
;
; On Entry
;    Drive selected and ready
;    trkNum = desired track
;    curTrk = current track, UNDEF means we don't know
;
; On Exit
;    curTrk = trkNum
;    Interrupts disabled
;    Zero true for success, zero false for failure
;	(failure is a failed restore to track zero)
;    Clobbers A,B,C,H,L
;---------------------------------------------------------------------------
dSeek	di			;disable interrupts
	lda	curTrk		;A=current track
	cpi	UNDEF		;valid track number?
	jnz	dTrkVld		;yes, track is valid

	call	dRestor		;otherwise, restore to track zero
	rnz			;exit if restore failed

; dTrkVld - Current track is valid, compute direction and number
;     of steps

dTrkVld	lxi	h,trkNum	;HL->trkNum
	sub	m		;A=current-requested
	rz			;return if already on correct track

	mvi	b,cSTEPO	;B=step out (assume requested<current)
	jnc	dDoSeek		;it is
	
	mvi	b,cSTEPI	;B=step in (requested>current)
	cma			;compute A=-A
	inr	a

; Save target track in curTrk, step there, then verify track

dDoSeek	mov	c,a		;C=number of steps

	mov	a,m		;save new track in curTrk
	sta	curTrk
	
	call	dStep		;do the seek
	xra	a		;return zero
	ret
	
;---------------------------------------------------------------------------
; dRestor - Restore to track 0 and then delay to ensure any subsequent
;    seek meets the minimum direction change period.
;
; On Entry
;    Drive selected and ready
;
; On Exit
;    If successful
;       curTrk = 0
;	A=0
;	Zero true
;    else
;       curTrk = UNDEF
;       Zero false
;    Clobbers A,B,C
;---------------------------------------------------------------------------
dRestor	xra	a
	sta	curTrk		;set current drive track to zero

	in	DRVSTAT		;test for track 0
	ani	sTRACK0
	rz			;at track 0, exit

; Step in three tracks, then step out to track 0

	mvi	c,3		;C=step in 3 tracks
	mvi	b,cSTEPI	;B=step in command
	call	dStep		;do 3 steps in

	mvi	a,200		;20ms delay puts us past the 1ms step window
	call	delay		;  and ensures proper dir change delay

; Seek back out until track 0 detected

	mvi	c,NUMTRK+10	;C=maximum number of step outs to try
	mvi	b,cSTEPO	;B=step out command
	call	dStep		;step until track 0 detected
	jnz	drFail		;restore failed

	mvi	a,200		;20ms delay puts us past the 1ms step window
	call	delay		;  and ensures proper dir change delay	
	ret			;return with zero

drFail	mvi	a,UNDEF		;restore failed, track is still undefined
	sta	curTrk
	ret			;return with non-zero

;---------------------------------------------------------------------------
; dStep - Step head number of steps in C, direction command in B.
;   If track zero is detected during a step-out operation, curTrk
;   is forced to zero and zero is returned in A.
;
; On Entry
;    C = number of steps > 0
;    B = step command (cSTEPO or cSTEPI)
;
; On Exit
;    If stepping out and track zero hit
;	curTrk = 0
;	A = 0
;	Zero true
;    else
;	Zero false
;    Clobbers A,B,C
;---------------------------------------------------------------------------
dStep	in	DRVSTAT		;loop until OK to move the head 
	ani	sMOVEOK
	jnz	dStep

	in	DRVSTAT		;see if we are at track 0
	ani	sTRACK0		;at track zero?
	jnz	dStep1		;no, go on

	mov	a,b		;stepping out?
	sbi	cSTEPO
	jz	dStep0		;yes, we've hit track 0

dStep1	mov	a,b		;A=step command and direction
	out	DRVCMD		;issue the step	

	dcr	c		;decrement step counter
	jnz	dStep		;loop until count reaches zero

	inr	c		;force non-zero
	ret

dStep0	sta	curTrk		;curTrk=0
	ret			;return with zero true and A=0

;---------------------------------------------------------------------------
; dNxtSec - Wait for next (any) sector. Returns pointer to the sector
;    buffer within trkBuf. Control is back to caller at 78 cycles (39us).
;
; On Entry
;    Drive is selected, head loaded
;
; On Exit
;    HL->sector buffer in trkBuf for sector found
;    Clobbers A,B,C,H,L
;---------------------------------------------------------------------------
dNxtSec	equ	$

   if MINIDSK
	mvi	a,cRESTMR	;restart motor timeout
	out	DRVCMD
   endif

	lxi	b,0		;1.4s timeout

dnLoop	dcx	b		;decrement the 1.4s timer
	mov	a,b		;test for zero
	ora	c
	rz			;exit on timeout (zero=error)

	in	DRVSEC		;read sector position register
	rar			;wait for sector true (0=true)
	jc	dnLoop
	
	ani	SECMASK		;(7) get sector number alone
				;fall into dSecAdr

;---------------------------------------------------------------------------
; dSecAdr - Convert hard sector in A to address within trkBuf for
;    the specified sector
;
; On Entry
;    A = Hard sector number
;
; On Exit
;    HL->sector buffer in trkBuf
;    Clobbers A,B,C,H,L
;---------------------------------------------------------------------------
dSecAdr	lxi	h,secAddr  	;(10) HL->sector address table
	mvi	b,0		;(7) form BC=sector*2
	rlc			;(4) A=sector*2 (2 bytes per table entry)
	mov	c,a		;(5) BC=sector*2
	dad	b		;(10) HL->address table entry for passed sector
	
	mov	a,m		;(7) A=lsb of sector buffer address
	inx	h		;(5)
	mov	h,m		;(7) H=msb of sector buffer address
	mov	l,a		;(5) HL->sector buffer

	ora	h		;(4) force non-zero return status (good)
	ret			;(11)

;----------------------------------------------------------------------------
; delay - Delay for A x 100us
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

	org	MODEADR+4	;make hole for MODE, IOBYTE, etc. at 05D9h
;---------------------------------------------------------------------------
; dRead - Read a sector, verify checksum, stop byte, and track number.
;    Verification is done after the sector is read in the time between
;    the last byte of the sector and before the start of the next sector.
;    With index alignment and rotation speed tolerance accounted for,
;    we safely have 360 cycles from the last byte of the sector until code
;    should be in the sector true hunt loop. The RET is complete from this
;    routine 152 cycles worst case after the last byte is read.
;
; On Entry
;    Drive is selected, sector true just detected
;    HL->sector buffer
;
; On Exit
;    Sector read to (HL)
;    C = track number from disk with sync bit set
;    Zero true if no error, Zero false for checksum error, missing 
;	FF stop byte, or track number error
;    Clobbers A,B,C,H,L
;---------------------------------------------------------------------------
dRead	push	h		;(11) save pointer to start of sector

	lxi	b,256+DSECLEN	;(10) B=chksum (-FF), C=bytes to read
	lda	curTrk		;(13) A=track we're on
	cpi	DATATRK		;(7) data track or system track?
	jnc	drSecWt		;(10) data track, length in C is correct

	mvi	c,SSECLEN	;(7) C=length of a system sector

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

; drSecDn - Sector read is complete, now validate the sector based on whether
;   it is from a system track or data track.

drSecDn	lda	curTrk		;(10) A=track we're supposed to be on
	cpi	DATATRK		;(7) system or data track?
	jnc	drDatTk		;(10) data track

; Validate a system track sector. A system track sector is three bytes
;    bytes shorter, so this codes starts 3*64 cycles sooner than for a
;    data sector. This code completes before a data sector would even
;    finish reading. Therefore, a data sector is the worst timing path.

	mov	a,b		;(5) A=calculated checksum over all bytes
	dcx	h		;(5) HL->checksum in sector
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
	jmp	drExit		;(10) do common compares and exit

; Validate a data track sector (125 + 27 cycles for sector type jump)

drDatTk	dcx	h		;(5) HL->stop byte
	mov	a,m		;(7) A=stop byte
	pop	h		;(10) restore HL->start of buffer
	inr	a		;(4) was FF stop byte there?
	rnz			;(5/11) no, return error

	mov	a,b		;(5) A=calculated checksum over all bytes
	mov	c,m		;(7) C=track number from disk
	sub	c		;(4) take track out of checksum

	inx	h		;(5) HL->sector number
	sub	m		;(7) take out of checksum
	
	inx	h		;(5) move to checksum byte
	inx	h		;(5)
	inx	h		;(5) HL->checksum
	
	sub	m		;(7) subtract from checksum
drExit	sub	m		;(7) checksums match?
	rnz			;(5/11) no, exit with error
	
	lda	curTrk		;(10) A=track we should be on
	ori	80h		;(7) set sync bit
	cmp	c		;(4) same track as from sector?
	ret			;(11) exit with compare status

;---------------------------------------------------------------------------
; dVerify - Verify a sector
;    The read loop is 116 cycles for two bytes. This must complete in 128
;    cycles, so there is plenty of headroom. The 2nd byte is read is at
;    70 cycles which gives six cycles of headroom (70-64). 
;
; On Entry
;    Drive is selected, sector true just detected
;    HL->sector buffer to compare against
;
; On Exit
;    Zero true if match, zero false for error
;    Clobbers A,B,C,H,L
;---------------------------------------------------------------------------
dVerify lxi	b,DSECLEN	;(10) B=0, C=data sector length
	lda	curTrk		;(13) A=track we're on
	cpi	DATATRK		;(7) data track or system track?
	jnc	dVfLoop		;(10) data track, length in C is correct

	mvi	c,SSECLEN	;(7) set C=length of a system sector

dVfLoop	in	DRVSTAT		;(10) get drive status byte
	ora	a		;(4) wait for NRDA flag true (zero)
	jm	dVfLoop		;(10)

   if NOT MINIDSK		;8" reads two bytes per loop
	in	DRVDATA		;(10) read first byte at 24-48 cycles
	xra	m		;(7) verify data matches buffer
	ora	b		;(4) accumulate errors in b
	mov	b,a		;(5)

	inx	h		;(5) increment buffer pointer
	dcr	c		;(5) decrement characters remaining counter
	jz	dVfDone		;(10) done verifying bytes
   endif

	in	DRVDATA		;(10) read 2nd byte at 70-94 cycles
	xra	m		;(7) verify data matches buffer
	ora	b		;(4) accumlate errors in b
	mov	b,a		;(5) 

	inx	h		;(5) increment buffer pointer
	dcr	c		;(5) decrement byte counter
	jnz	dVfLoop		;(10) loop for all bytes

dVfDone	ora	a		;A=zero if all bytes matched
	ret			;return status

;---------------------------------------------------------------------------
; dWrite - Write a sector
;    The write loop is 61 cycles which gives just 3 cycles (64-61) of 
;    headroom. However, since the write timing required by the FDC board 
;    is derived from the 2 MHz CPU/bus clock, this loop is always exactly
;    in sync with the FDC write timing. 3 cycles of headroom is plenty.
;
; On Entry
;    Drive is selected, sector true just detected, and
;	the write command has already been issued
;    HL->sector buffer to write
;
; On Exit
;   Clobbers A,C,H,L
;---------------------------------------------------------------------------
dWrite	mvi	c,DSECLEN	;C=number of bytes to write
	lda	curTrk		;(13) A=track we're on
	cpi	DATATRK		;(7) data track or system track?
	jnc	dWrLoop		;(10) data track, length in C is correct

	mvi	c,SSECLEN	;(7) set C=length of a system sector

dWrLoop	in	drvStat		;(10) read drive status register
	rrc			;(4) wait for ENWD flag (zero)
	jc	dWrLoop		;(10)

	mov	a,m		;(7) A=next byte to write
	out	drvData		;(10) write 2nd byte at 68-92 cycles
	inx	h		;(5) increment buffer pointer
	dcr	c		;(5) decrement bytes remaining
	jnz	dWrLoop		;(10) loop until all bytes written

dWrDone	in	drvStat		;wait for another write flag
	rrc
	jc	dWrDone
	
	xra	a		;write a trailing zero
	out	drvData
	ret

;******************************************************************************
;
;   BIOS Data Area
;
;******************************************************************************

;---------------------------------------------------------------------------
; secAddr - Sector address table. Returns sector buffer address within
;    the track buffer when indexed by hard sector number. Each sector
;    requires TSECLEN bytes.
;---------------------------------------------------------------------------
secAddr	dw	trkBuf+00*TSECLEN,trkBuf+01*TSECLEN,trkBuf+02*TSECLEN
	dw	trkBuf+03*TSECLEN,trkBuf+04*TSECLEN,trkBuf+05*TSECLEN
	dw	trkBuf+06*TSECLEN,trkBuf+07*TSECLEN,trkBuf+08*TSECLEN
	dw	trkBuf+09*TSECLEN,trkBuf+10*TSECLEN,trkBuf+11*TSECLEN
	dw	trkBuf+12*TSECLEN,trkBuf+13*TSECLEN,trkBuf+14*TSECLEN
	dw	trkBuf+15*TSECLEN

   if NOT MINIDSK
	dw	trkBuf+16*TSECLEN,trkBuf+17*TSECLEN,trkBuf+18*TSECLEN
	dw	trkBuf+19*TSECLEN,trkBuf+20*TSECLEN,trkBuf+21*TSECLEN
	dw	trkBuf+22*TSECLEN,trkBuf+23*TSECLEN,trkBuf+24*TSECLEN
	dw	trkBuf+25*TSECLEN,trkBuf+26*TSECLEN,trkBuf+27*TSECLEN
	dw	trkBuf+28*TSECLEN,trkBuf+29*TSECLEN,trkBuf+30*TSECLEN
	dw	trkBuf+31*TSECLEN
   endif
	
;-----------------------------------------------------------------------------
; dpHead - disk parameter header for each drive
;-----------------------------------------------------------------------------
dpHead	dw	tranTbl,0,0,0,dirBuf,dpb,csv0,alv0
	dw	tranTbl,0,0,0,dirBuf,dpb,csv1,alv1
	dw	tranTbl,0,0,0,dirBuf,dpb,csv2,alv2
	dw	tranTbl,0,0,0,dirBuf,dpb,csv3,alv3

;-----------------------------------------------------------------------------
; dpb - disk parameter block. This table gives a block size of 1024 bytes
;   and 64 directory entries. 
;-----------------------------------------------------------------------------
dpb	dw	NUMSEC		;sectors per track
	db	BSH		;allocation block shift factor (BSH)
	db	BLM		;data location block mask (BLM)
	db	EXM		;extent mask (EXM)
	dw	DSM		;maximum block number (DSM 242)	
	dw	DRM		;maximum directory entry number (DRM 63)
	db	AL0,AL1		;AL0, AL1 
	dw	CKS		;CKS=(DRM+1)/4
	dw	RESTRK		;reserved tracks for CPM and bootloader

   if MINIDSK
tranTbl	db	1,3,5,7,9,11,13,15,2,4,6,8,10,12,14,16
   endif

   if NOT MINIDSK
tranTbl	db	01,09,17,25,03,11,19,27,05,13,21,29,07,15,23,31
	db	02,10,18,26,04,12,20,28,06,14,22,30,08,16,24,32
   endif

;----------------------------------------------------------------------------
; Initialized data
;----------------------------------------------------------------------------
coldSt	db	0FFh		;FF during cold start, 0 otherwise

curDrv	db	0		;drive number currently selected
curTrk	db	RESTRK-1	;track the selected drive is on
trkTbl	db	UNDEF,UNDEF,UNDEF,UNDEF		;track each drive is on

bfDirty	db	0		;non-zero if buffer has been written to
bfDrive	db	UNDEF		;drive currently in track buffer
bfTrack	db	UNDEF		;currently buffered track (MUST follow bfDrive)

;----------------------------------------------------------------------------
; Non-initialized data
;----------------------------------------------------------------------------
drvNum	ds	1		;drive number from CP/M call
trkNum	ds	1		;track number from CP/M call (MUST follow drvNum)
secNum	ds	1		;sector number from CP/M call
dmaAddr	ds	2		;dma address for disk operations from CP/M call

secCnt	ds	1		;count of bad sectors remaining in trkBuf
wrRetry	ds	1		;write retry counter
skRetry	ds	1		;restore and seek retry counter

;-----------------------------------------------------------------------------
;  Disk scratchpad areas defined in the DPH table
;-----------------------------------------------------------------------------
dirBuf	ds	128		;bdos directory scratchpad
alv0	ds	(DSM/8 + 1)	;allocation vector storage
csv0	ds	CKS		;change disk scratchpad
alv1	ds	(DSM/8 + 1)
csv1	ds	CKS
alv2	ds	(DSM/8 + 1)
csv2	ds	CKS
alv3	ds	(DSM/8 + 1)
csv3	ds	CKS

;-----------------------------------------------------------------------------
; Track buffer. One extra byte is allocated at the start of each sector
;    to flag a bad sector (read failure). This byte is zero if the sector
;    is good, non-zero if the sector is bad.
;-----------------------------------------------------------------------------
trkBuf	ds	NUMSEC*TSECLEN

ACTLEN	equ	$-BIOSBAS	;actual length of the BIOS

	end











