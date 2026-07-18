;===============================================================
; Hard Disk Boot Loader (HDBL)
; By Martin Eberhard
;
; HDBL is a 256-byte PROM program that loads the boot file from
; an 88-HDSK hard disk, and executes the successfully loaded
; code. Progress and error messages are printed on a "standard"
; 6850-based Altair Terminal port at address 10H and 11h, such
; as port A of an 88-2SIO, the serial port on a Turnkey Module,
; or the serial port of an 88-UIO.
;
; The standard 88-HDSK system uses a Pertec D3422 disk drive,
; which contains 2 platters - one is in a removable cartridge,
; the other is a fixed platter. However, The 88-HDSK controller
; can actually support up to 4 platters, supporting the Pertec
; D3462 disk drive, which has one removable platter, and 3
; fixed platters.
;
; Altair software normally boots only from the removable
; cartridge of a D3422 disk drive. HDBL allows you to boot also
; from the fixed platter. You can select from which platter to
; boot using Sense Switch 3 (A11 on the front panel). 0 (down)
; selects the removable cartridge, 1 (up) selects the fixed
; platter. This switch was chosen to minimize collisions with
; the normal use of sense switches <A11:A8>. 0000 through 0110
; select the input device for MBL and for the various checksum
; loaders on tape (and values above 0110 will generate an
; error). So if you have the sense switches (which may be
; inaccessible on an 8800b Turnkey board) set forloading from
; e.g. cassette, then the machine will still boot from the
; removable cartridge when booting via HDBL.
;
; There are 24 256-byte sectors per track, and these are
; numbered 0 through 23 on each track. Each platter has 2
; sides, numbered 0 and 1. Data on each platter is organized as
; a sequence of Disk Pages, where each Page is one sector.
; Pages are numbered sequentially starting at 0 (on track 0,
; side 0), through the 24 sectors on track 0, side 0, and then
; on to track 0, side 1, where sector 0 is page 24.  Page 47 is
; the first sector on track 1, side 0, and page numbering
; continues this way through all the tracks.
;
; Page 0 (which is track 0, side 0, sector 0) is the Pack
; Descriptor Page, containing various information about the
; particular disk platter. Bytes 40-43 of this Page are the
; "Opsys Pointers." Bytes 40 & 41 are the Page number of the
; starting boot Page, Bytes 42 & 43 are the number of Pages to
; load during boot. HDBL assumes that the boot file is to be
; loaded into memory starting at address 0000, and executed
; there.
;
; During loading, the INTE (Interrupt Enabled) LED on the front
; panel will be off. Any error during loading will cause the
; INTE LED to light and a "LOAD ERR" message to be printed
; on the Terminal. The error code is stored in memory at
; address 0. HDBL will then hwng in a loop until Reset.
;
; Because HDBL may be running stand-alone, the Terminal port
; gets initialized during HDBL initialization. But if control
; came from UBMON (with an "L" command) or from TURMON (with a
; "J 176000" command) then the Terminal port's ACIA will still
; be transmitting the last two character-echos of the command
; when HDBL begins. HDBL will stall 6.5 uS (one character time
; above 1800 baud) before initializing the Terminal port, so
; that the ACIA will have time to finish transmitting this last
; characters before it gets reset.
;
; Newer Turnkey Modules (and older ones with the 88-SYS-CLG
; modification) will disable the PROMs when an IN instruction
; reads port FFh (the Sense Switches). Some of these boards
; will disable the PROM when *any* IN instruction is
; executed. For this reason, HDBL copies itself to RAM, and
; runs from there, before executing any IN instructions.
;
; This code is written to assemble with ASM by Digital Research
;
;===============================================================
; Revision History
;   1.00  15SEP2013  M.Eberhard
;     Created
;   1.01  25MAR2014  M.Eberhard
;     Fixed bug with loading from alternate platters, code
;     squeeze, improve comments
;   1.02  26MAR2014  M.Eberhard
;     Further compression. Print platter number in signon msg.
;     Jump to XENTER instead of XMON on error, so ACIA reset
;     doesn't hose the last chr of error code.
;   1.03  05JUN2013  M.Eberhard
;     Use sense switch All to select boot platter. Eliminate
;     selection of boot drive.
;   2.00  15AUG2014 M.Eberhard
;     Copy code to RAM before execution, for Turnkey Module
;     compatibility. Remove RAMCOD option. Eliminate return
;     to UBMON/TURMON on load error (Must reset on error.)
;
; (Remember to update the Version String below)
;===============================================================
FALSE	equ	0
TRUE	equ	not FALSE

; PROM address and Entry point for HDBL. UBMON assumes FC00h.

HDBL	equ	0FC00h	;Beginning of HDBL PROM

; RAM address for moved code. Exactly one of these
; should be used.

RAMPAG	equ	0BF00h	;beginning of RAM page (48K system)
;RAMPAG	equ	0F700h	;beginning of RAM page (62K system)
;RAMPAG	equ	0FB00h	;beginning of RAM page (63K system)
			;(e.g. Turnkey Module's RAM)

ROF	equ	HDBL-RAMPAG	;RAM relocation offset

; Sense Switch assignment for selecting the boot platter & unit

SSWTCH	equ	0FFh		;Sense Switch address
PSWTCH	equ	008h		;mask for platter Switches
				;Code assumes 008h (bit 3)

; Terminal port equates - same for 88-2SIO port 0, Turnkey
; Module, and 88-UIO (all based on the Motorola 6850 ACIA)
; Note: transmitting with 2 stop bits is also compatible with a
; receiver that is programmed for 1 stop bit.

ACCTRL	EQU	10h		;ACIA Control output port
ACSTAT	EQU	10h		;ACIA Status input port
ACTXD	EQU	11h		;ACIA TX Data register
ACRXD	EQU	11h		;ACIA RX Data register

ACRSET	EQU	00000011b	;Master reset
ACRDF	EQU	00000001b	;RX Data register full
ACTDE	EQU	00000010b	;TX Data register empty
ACINIT	EQU	00010001b	;/16, 8-bit, No Parity, 2Stops

; 88-HDSK ports (The interface board is actually an 88-4PIO.)

CREADY	equ	0A0h	;IN: Ctlr ready for command (bit7)
CSTAT	equ	0A1h	;IN: error flags, reset CREADY
ACSTA	equ	0A2h	;IN: Command Ack (bit 7)
ACMD	equ	0A3h	;IN: reset Command Ack
			;OUT: Command high byte/initiate
CDSTA	equ	0A4h	;IN: data/stat availablr at CDATA
CDATA	equ	0A5h	;IN: Disk data or status from Ctlr
ADSTA	equ	0A6h	;IN: ADATA Port Available (bit 7)
ADATA	equ	0A7h	;OUT: Command low byte

; 88-HDSK ACMD:ADATA Commands

CSEEK	equ	00h	;Bits 15:12 = 0000b
			;Bits 11:10 = Unit #
			;Bits  9:0  = Cylinder #

CRDSEC	equ	30h	;Bits 15:12 = 0011b
			;Bits 11:10 = Unit #
			;Bits  9:8  = Buffer #
			;Bit   7:6  = Platter #
			;Bits    5  = Side #
			;Bits  4:0  = Sector #

CSIDE	 equ	  020h	  ;Side select for CRDSEC
CFPLTR	 equ	  0C0h	  ;platter mask for CRDSEC
CUNIT	 equ	  00Ch	  ;Unit mask for CSEEK & CRDSEC

CRDBUF	equ	50h	;Bits 15:12 = 0101b
			;Bits 11:10 = not used
			;Bits  9:8  = buffer #
			;Bits  7:0  = # bytes to transfer
			;(00 means 256)

; 88-HDSK CSTAT error bits

ERDNR	equ	01h	;drive not ready
ERBADS	equ	02h	;illegal sector
ERSCRC	equ	04h	;CRC error during sector read
ERHCRC	equ	08h	;CRC error during header read
ERSWRG	equ	10h	;header has wrong sector
ERCWRG	equ	20h	;header has wrong cylinder
ERHWRG	equ	40h	;header has wrong head
WPROT	equ	80h	;Write Protect
ERMASK	equ	7Fh	;all the actual error bits

; 88-HDSK Constants

OSOFF	equ	40	;Page 0 offset to opsys pointers
HDSPT	equ	24	;Sectors per track
DBUFR	equ	0	;Default controller buffer: 0-3
			;Code gets longer if <>0

; ASCII characters

CR	equ	0Dh
LF	equ	0Ah

;==============================================================
; Start of HDBL PROM
	org	HDBL
;==============================================================
	di			;front panel INTE light off

;---------------------------------------------------------
; Copy code to RAM. This will provide 6.5 mS of delay.
; This will allow a UART that is running at 1700 baud or
; faster to complete transmission. (This also leaves 18
; bytes in RAM for the stack.)
;---------------------------------------------------------
	lxi	h,RAMPAG+100h		;last RAM address+1
	mvi	d,(HDBL/256)		;PROM code page

COPLUP:	dcx	h		;(5+1)
	mov	e,l		;(4+1)
	ldax	d		;(7+1)
	mov	m,a		;(7+1)
	mov	a,l		;(4+1)
	sui	RAMCOD and 0FFh	;(7+2)ends with a=0
	jnz	COPLUP		;(10+3)

;54 cycles per pass X (256-18) /2 = 6.426 mS

;------------------------------------------------------
; Set up system stack immediately below RAM code image
;------------------------------------------------------
	sphl

;-----------------------------
;go to loaded code (with a=0)
;-----------------------------
	pchl

;=============================================================
; All of the following code gets copied to RAM and run there.
; On Entry:
;   a = 0
;=============================================================
RAMCOD:	mov	h,a		;set load initial page
	mov	l,a		;hl=0
	
;----------------------------------------
; Initialize 88-HDSK interface board
; (Actually ports 0 and 1 of an 88-4PIO)
; On Entry:
;   a = 0
;  hl = 0
; On Exit:
;  hl = 0
;----------------------------------------
	out	0A0h		;Select port 0Ah DDR 
	out	0A2h		;Select port 0Bh DDR
	out	0A4h		;Select port 1Ah DDR
	out	0A6h		;Select port 1Bh DDR
	out	0A1h		;Port 0Ah is an input port
	out	0A5h		;Port 1Ah is an input port

	cma
	out	0A3h		;Port 0Bh is an output port
	out	0A7h		;Port 1Bh is an output port

	mvi	a,2Ch		;set up input port handshakes
	out	0A0h
	out	0A4h
	out	0a6h		;output port 1Bh handshakes

	mvi	a,24h		;set up port 0Bh handshakes
	out	0A2h

	in	CSTAT		;clear Controller Ready bit

;---------------------------------------------
; Reset and initialize the Terminal port ACIA
; On Entry & Exit:
;  hl = 0
;---------------------------------------------
	mvi	A,ACRSET
	out	ACCTRL
	mvi	A,ACINIT
	out	ACCTRL

;----------------------------
; Print HDBL version message
; On Entry & Exit:
;  hl = 0
;----------------------------
	call	PRINTF-ROF	;print the following string
	db	CR,LF,'HDBL 2.0','0'+80h

;-------------------------------------------------
; Read the Pack Descriptor Page (Disk Page 0)
; to get the Opsys Pointers:
;   Bytes 41:40 = Initial Disk Page number
;   Bytes 43:42 = Disk Page count (Byte 43=MSB=0)
; On Entry:
;  hl = 0
;-------------------------------------------------
	mvi	b,OSOFF+3	;byte count to end of pointers
	call	GETPAG-ROF	;Seek, read page hl into buffer
				;set up to read b buffer bytes

	push	h		;execution address on stack
	xchg			;load address into de

; Read from the controller buffer and discard everything until
; we get to the opsys pointers. Load the opsys pointers into
; C & HL. Note: no testing any handshake here - just assume
; the controller can keep up. (The controller can send a data
; byte every 2.5 uS.) This only reads the low byte of the
; page count, since the high byte must be 0 anyway.

PTRLUP:	in	CDATA		;read byte from controller

	mov	l,h		;shift everybody over...
	mov	h,c
	mov	c,a		;...and put it away

	dcr	b
	jnz	PTRLUP-ROF

; Announce 'LOADING FROM <platter>' on the Terminal

	call	LOADPF-ROF	;CR,LF,'LOAD', then string
	db	'ING FROM',' '+80h

	in	SSWTCH		;read platter switch
				;(disables PROMS in Turnkey bd)
	ani	PSWTCH		;mask off all others
	rrc
	rrc
	rrc
	adi	'0'		;make it ASCII
	call	PRINTA-ROF	;and print it

;--------------------------------------------------
; Read c Pages from disk, starting at Page hl, into
; memory starting at the address on the stack
; On Entry:
;   b = 0
;   c = page count
;  de = LDADDR
;  hl = initial Disk page number
;--------------------------------------------------
PAGELP:	call	GETPAG-ROF	;Seek, read page hl into buffer
				;set up to read b buffer bytes
				;b=0 here always.

; Load 256 bytes of buffer data into memory at de (b=0 here)
; Note: no testing any handshake here - just assume the
; controller can keep up. (The controller can send a data byte
; every 2.5 uS.)

BYTELP:	in	CDATA		;get a data byte
	stax	d		;write it to RAM
	inx	d		;next address
	dcr	b		;bump byte counter
	jnz	BYTELP-ROF	;until done (b=0)

; Next Disk Page

	inx	h		;Next Disk Page
	dcr	c		;bump Disk Page count
	jnz	PAGELP-ROF

;-----------------------------------------------------
; Go execute loaded code, at the address on the stack
;-----------------------------------------------------
	ret

;===Subroutine=================================================
; Seek and read disk Page hl into 88-HDSK buffer 0
; On Entry:
;   b=number of bytes to transfer (0 means 256)
; On Exit:
;   a,flags trashed, all others preserved
;   Controller has specified sector data in its buffer
;==============================================================
GETPAG:	push	h		;Save requested Page
	push	d		;Save regs
	push	b		;save byte count

;------------------------------------------------------------
; Compute cylinder and sectorX2  from Disk Page number in hl
;  hl := hl / (2*HDSPT) (Quotient=cylinder)
;   h := hl MOD (2*HDSPT) (Remainder=sectorx2)
; This is fast only if the cylinder number is low. MITS
; usually put the boot image starting at cylinder 0, side 1,
; so this will be faster and shorter than the 'fast'
; division of previous HDBL rev. This will become slower
; if the boot image is above cylinder 20 or so. But we will
; always miss the next sector anyway, so each sector will
; require a full disk rev (25 mS), plenty of time
;------------------------------------------------------------
	lxi	b,-2*HDSPT
	mov	d,b		;de=FFFF=-1
	mov	e,b		;since loop goes 1 extra

DIV1:	inx	d		;compute quotient=cylinder
	dad	b		;hl gets remainder
	jc	DIV1-ROF

	mov	a,l		;fix remainder, since
	sub	c		;..loop went 1 extra

	xchg			;cylinder number to hl

;----------------------------------------------------
; Compute Sector & Side
; If sectorX2 > sectors/track then set CSIDE
; bit, and reduce sector number by sectors/track
;   hl= Quotient (cylinder)
;   a = Remainder (sectorX2, either for head 0 or 1)
;----------------------------------------------------
	cpi	HDSPT		;past end of side 0?
	jc	SIDEOK-ROF	;N: sector number is good

	adi	CSIDE-HDSPT	;Compute sector mod HDSPT,
				;..and set side 1 bit

SIDEOK:	mov	b,a		;save sector # with side

;---------------------------------------------------
; Seek Cylinder
;    b = sector number, with side bit set correctly
;    hl = cylinder number<9:0>
;---------------------------------------------------
 if CSEEK+DBUFR			;these are actually 00
	mov	a,h		;h<1:0>=cylinder<9:8> 
	ori	CSEEK+DBUFR	;combine with SEEK cmd
	mov	h,a
 endif

				;hl=SEEK command with cyl #
	call	HDCMD-ROF	;HCMD gets unit # from switches

;---------------------------------------------------------
; Get platter from sense switch, and combine with
; side and sector already in b
;   b<7:6> = 0
;     b<5> = side
;   b<4:0> = sector number
;   Sense Switch <A11> = platter number
;---------------------------------------------------------
	mvi	h,CRDSEC+DBUFR	;read command, high byte

	in	SSWTCH		;read platter switches
	ani	PSWTCH		;mask off all others

	rlc			;Shift to CFPLTR position
	rlc			;..which are bits 7:6
	rlc

	ora	b		;combine w/ sect & side

;----------------------------------------------------------
; Read Sector from current track into controller's buffer 0
;   a<7:6> = platter
;     a<5> = side
;   a<4:0> = sector number
;----------------------------------------------------------
	call	HDCMDA-ROF	;low command byte is in a

;----------------------------------------------
; Issue CRDBUF command to kick off read of 256
; bytes from the controller's buffer
; Note: this assumes the controller is ready.
; (and it is, because HDCMD left it that way.)
;----------------------------------------------
	in	CDATA		;reset CDA in CDSTA
	in	ACMD		;clear CMDACK in ACSTA

	pop	b		;b=requested byte count
	mov	a,b
	out	ADATA		;..to controller

	mvi	a,CRDBUF+DBUFR	;issue Read Buffer command
	out	ACMD		;..to controller

	pop	d		;(10)
	pop	h		;(10) 10 uS total from 'out'

; The 8x300 is ready to transmit data in 8 uS. This code takes
; 30 cycles (including the 'ret'), or 15 uS min to get around
; to reading the data - so there is no need to wait on CDSTA

 if FALSE
DATAWT:	in	CDSTA		;Wait for data port to be ready
	rlc			;msb=CDA
	jnc	DATAWT-ROF
 endif

;-----------------------------------
; Controller is ready to transfer
; 256 bytes of data from its buffer
;-----------------------------------
	ret			;(10)done with GETPAG

;===Subroutine=================================================
; Issue a disk command, and then wait for the controller
; to complete it
;
; Note: this just assumes the controller is ready, which is OK
; since the last command was either a seek (where HDCMD waited
; for the controller to become ready) or it was a CRDBUF, which
; ended with all bytes transferred - and the controller becomes
; ready very soon (1.5 uS) after the last byte is transferred.
; On Entry at HDCMD:
;   hl = complete command
; On Entry at HDCMDA:
;   a=low byte of command
;   h=high byte of command
; On Exit:
;   a,flags trashed, all others preserved.
;   The command is completed and the controller is ready.
;   Any errors will terminate the load, and print an error
;   message on the Terminal 
;==============================================================
HDCMD:	mov	a,l		;low byte of command

HDCMDA:	out	ADATA		;..to data port

	in	CSTAT		;reset CRDY flag just in case
	in	ACMD		;clear CMDACK in ACSTA

	mov	a,h		;command high byte
	out	ACMD		;issue command

HDWAIT:	in	CREADY		;Is the controller done? 
	rlc			;look at msb=CRDY
	jnc	HDWAIT-ROF	;N: keep waiting

	in	CSTAT		;reset CRDY flag
	ani	ERMASK		;and get A=error code
	rz			;No errors: happy return

; 	Fall into error exit

;===Error Exit=================================================
; Report a load error and store error code in RAM at 0.
; Hang here forever, with the INTE light lit.
; On Entry:
;   a=error flag bits
;==============================================================
	sta	0		;save a=error flags
	call	LOADPF-ROF	;CR,LF,'LOAD', then string
	db	' ER','R'+80h

	ei			;INTE is error indicator light
FOREVR:	jmp	FOREVR-ROF	;N: die here, INTE light lit

;===Subroutine================================================
; Print inline string on the Terminal,
; preceeded by CR,LF,'LOAD'
; On Entry:
;    The string address is the "return address" on the stack.
;    The string is terminated by bit 7 set in its last chr.
;    The actual return address is the next address after the
;    last string character.
; On Exit:
;    Trashes a and flags, all other registers preserved.
;=============================================================
LOADPF:	call	PRINTF-ROF
	db	CR,LF,'LOA','D'+80h

;fall into PRINTF

;===Subroutine================================================
; Print inline string on the Terminal
; On Entry:
;    The string address is the "return address" on the stack.
;    The string is terminated by bit 7 set in its last chr.
;    The actual return address is the next address after the
;    last string character.
; On Exit:
;    Trashes a and flags, all other registers preserved.
;=============================================================
PRINTF:	xthl			;get string address, save hl

PRNTLP:	mov	a,m		;get string character
	ani	7Fh		;strip end-of-string mark
	call	PRINTA-ROF	;and print it

	cmp	m		;end of string?
	inx	h		;point to next chr
	jz	PRNTLP-ROF	;No difference: keep going

	xthl			;restore hl, put return address
	ret			;..onto stack, and go there

;===Subroutine==================
; Print a the Terminal
; On Entry:
;   a=chr to print
; On Exit:
;    all registers preserved.
;===============================
PRINTA:	push	psw		;save chr to print

PALOOP:	in	ACSTAT		;Wait for TX to be ready
	ani	ACTDE
	jz	PALOOP-ROF

	pop	psw
	out	ACTXD		;and send chr
	ret

	end

