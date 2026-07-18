;==============================================================
; AMON
;
; ROM-based monitor for an 8080 based system, supporting the
; 88-2SIOJP and the Altair 88-2SIO, this version modified to
; support the CompuPro Interfacer 1 Channel B as transfer port
; option 7.
;
; Formatted to assemble with digital Research's ASM.
;
;==============================================================
; Entry Points:
; F800h: Cold-start AMON, enter command loop
; FC00h: Boot from MITS 88-HDSK Altair Hard Disk
;        (equivalent to my HDBL)
; FE00h: Boot from Altair paper or cassette tape
;        (equivalent to MITS's MBL)
; FF00h: Boot from MITS 88-DCDD 8" floppy or 88-MDS minidisk
;        (equivalent to my CDBL, and MITS's DBL and MDBL)
;==============================================================
;
; AMON assumes the console is on port 0 of the 88-2SIO/JP,
; and that the console terminal may optionally be a printing
; terminal (e.g. a Teletype) that has no backspace capability.
;
; AMON defines a "transfer port" for uploads, downloads, and
; terminal mode. This can be set to any of the standard Altair
; ports. You can also set up a custom port prior to assembly,
; which will be port 7 in the TP command. (If your custom port
; requires initialization, then you must add code for this.)
; This version of AMON has the custom port set up for the
; CompuPro Interfacer 1, Channel B.
;
; Commands: (All values are in hex). The "?" and "MT" commands
; are only available with a 2732 EPROM (EPROM32 = TRUE)
;
; ?   Print help screen {EPROM32 only}
;
; AD <ADR> <BCNT> [<GO>]
;     Write <BCNT> bytes of memory starting at <ADR> in Altair
;     Absolute Binary format, to the current Transfer Port.
;     Optional GO record appended if <GO> provided.
;
; AL  [<0/1/2>]
;    Load and execute an Altair Absolute Binary file from the
;    current Transfer Port. (This is MBL.) If the optional
;    parameter is 0 then the GO record in the file will be
;    ignored, and control returns to the monitor, after
;    printing the GO address on the console. If the parameter
;    is 2 then an IN from port FF is executed (to disable this
;    PROM) before loading AND the GO record is executed.
;    Parameter defaultsto 1 (meaning a Go record is executed).
;    The "disable PROM" option is only available if EPROM32.
;
; BO  Boot from Altair floppy disk. (This is CDBL.)
;
; CO <SRC> <DST> <BCNT> [<RPT>]
;    Copy <BCNT> bytes of memory from address <SRC> to address
;    <DST>. optionally repeat <RPT> times (For programming
;    EPROMS with e.g. a Cromemco Bytesaver).
;
; DU [<ADR> [<BCNT>]]
;    Dump <BCNT> (which defaults to 1) bytes of memory starting
;    at address <ADR> (which defaults to 0).
;
; EN [<ADR>]
;    Enter hex data into memory at <ADR>, which defaults to 0.
;    values are separated with spaces or CR'S. Quit EN command
;    with a blank line.
;
; EX [<ADR> [<OPT>]]
;    Execute at <ADR>, which defaults to 0. Programs can ret
;    to AMON's MAIN loop. If <OPT>=1 then an IN from port
;    FF is executed first, to disable this PROM.
;
; FI [<VAL> [<ADR> [<BCNT>]]]
;    Fill <BCNT> bytes of memory starting at <ADR> with <VAL>
;    <VAL> and <ADR> default to 0. <BCNT> defaults to all of
;    memory, stopping (after wrap-around if necessary) when
;    the fill reaches AMON's RAM page.
;
; HB [<PLTR>] Boot from hard disk platter <PLTR> (0 or 1)
;    <PLTR> argument only supported if EPROM32
;
; HD <ADR> <BCNT> [<OFST>]
;    Intel hex dump <BCNT> bytes of memory starting at <ADR>,
;    to the Transfer Port. Add <OFST> to each address.
;
; HL [<OFST>]
;    Load Intel hex file to memory from the Transfer Port. Add
;    optional address offset <OFST> to each record address.
;    Prints a pacifier dot on the console for each record.
;
; IN <PORT>
;    Read from <PORT> and print the result on the console
;
; MT <ADR> <BCNT> {EPROM32 only}
;    Test <BCNT> bytes of memory, starting at <ADR>. This will
;    skip over the portion of RAM used by AMON.
;
; OT <PORT> <VAL>
;    Write the specified value to the specified output port
;
; SE <ADR> <BYTE1> [<BYTE2> [<BYTE3> [..<BYTEn>]]]
;     or
; SE <ADR> 'text string'
;    Search for string of bytes in memory, starting at <ADR>
;    can also mix forms, e.g.
; SE 100 'hello world' 0D 0A 'second line'
;
; TE [<EXCHR>]
;    Terminal Mode: console keyboard data goes to the Transfer
;    port, and Transfer Port data goes to the console.
;    ^C to exit, unless you specified a different exit chr.
;
; TP [<port>]
;    Set the Transfer Port:
;        port   device
;         0    88-2SIO port 0, 2 stop bits
;         1    88-2SIO port 0, 2 stop bits
;         2    88-SIO
;         3    88-ACR
;         4    88-4PIO port 0
;         5    88-PIO
;         6    88-2SIO port 1, 2 stop bits
;         7    CompuPro Interfacer 1 Channel B
;
; TT [0/1]
;    TT 1 specifies a Teletype (or other non-backspacing
;    device) as the console. TT or TT 0 specifies a device
;    (such as a terminal) that can backspace. This controls how
;    a backspace is displayed.
;
; VE <SRC> <DST> <BCNT>
;    Verify (compare) <BCNT> bytes of memory, starting at <SRC>
;    and <DST>
;
;==============================================================
; RAM USAGE
;
; Amon finds and uses the highest contiguous 256-byte page of
; RAM for its stack, buffers, and code that gets modified (such
; as the serial I/O routines).
;
; When MBL is executed directly (not via a call from AMON), it
; reads the switch register to determine the boot port. Note
; that the 88-2SIOJP may be configured to disable the PROM once
; an "IN 0FFh" (input from the front panel switch register) is
; executed. 
;
; The sector buffer is positioned within the RAM page such that
; its last byte is the last byte of the RAM page. This makes
; the timing work in the critical byte-read loop, when booting
; from an 8" floppy diskette.
;
; The available space for the stack depends on the command. For
; most commands, the stack grows down from the console input
; buffer at xxB0, with room for 18 pushes. However, for the BO
; and AL commands, as well as for entry at MBL or CDBL, the
; stack grows down from the sector buffer at xx7B, with room 
; for only 8 pushes.
;
; RAM Page Organization:
;
; xx00: Transfer Port I/O routines
;       RSETP: set the Transfer Port according to register a
;              (see TP command below.)
;       RTPIS: get Transfer Port input status. Z clear if
;              data is available.
;       RTPIN: wait for and get one chr from the Transfer Port
;       RTIIF: read immediately from the Transfer Port (flush)
;       RTPOUT: write a to the Transfer Port
; xx49-xx68: PTABLE, which may be wiped out by the stack
;
; xx69-xx7A: DSTACK (room for 8 pushes)
; xx7B-xxFF: Sector buffer (for BO command) (DSKBUF)
; xx7B-xxFF: MBL RAM code for AL command, especially for direct
;            execution from FE00
;
; xx69-xxAF: STACK (room for 18 pushes)
; xxB0-xxFF: Console input buffer (LINBUF)
;==============================================================
;--------------------------
;This code'S version number
;--------------------------
VERSION	equ	31h	;high nibble = major revision number
			;low nibble minor version
;--------------------------------------------------------------
; REVISION HISTORY
; Vers. 3.1 M. Eberhard  10 February 2024
;   Fix bug in GETTPD (that was introduced in version 2.8),
;   which had caused AL to fail when the console was also the
;   transfer port. Tidy up few comments. Define console port
;   better.
; Vers. 3.0 D. Hansel 12 February 2023
;   Fix MBL direct entry at FE00
;   Add option to disable PROM before load in AL commmand
; Vers. 2.9 D. Hansel 29 January 2023
;   Fix hard disk boot loader
;   Fix hex dump command end condition and user interrupt
; Vers. 2.8 M. Eberhard 27 March 2022
;   say 'OK' if verify succeeds or memory test passes.
;   More thorough transfer port flush on AL command if EPROM32.
; Vers. 2.7 M. Eberhard 24 March 2022
;   Fix MT bug when tested memory overlaps the RAM page. Fix
;   stack bug: larger stack for commands that use a lot of
;   stack. Print 'Memory OK' if MT passes. Slightly improve
;   help screens. Allow ESC to abort commands too.
; Vers. 2.6 M. Eberhard 26 November 2020
;   No pacifiers during HD and HL if Transfer Port=Console
;   Supports CompuPro Interfacer 1 Channel B
; vers. 2.5 M. Eberhard 11 June 2020
;   Use 2732 EPROM to add MT and ? commands
; Vers. 2.4 M. Eberhard 13 October 2016
;   Unify error messages, add GO record to AD command, add
;   option to ignore GO record on AL command, verify memory
;   write on HL command, improve labels
; Vers. 2.3 M. Eberhard  10 October 2016
;   Squeeze code, improve error reporting, improve comments
; Vers. 2.2  M. Eberhard 4 October 2016
;   Squeeze code a bit, add IN and OT commands
; Vers. 2.1  M. Eberhard  27 August 2016
;   Fix bug when executing at F800
; Vers. 2.0  M. Eberhard  26 July 2016
;   First released version
; Vers. 1.00-1.06
;   Development
;==============================================================
FALSE	equ	0
TRUE	equ	not FALSE

;======================================
; EPROM size option
; Set EPROM32 to TRUE for 2732 EPROM
; (Otherwise, code will fit in a 2716.)
;======================================

EPROM32 equ	TRUE

;*****
;ASCII
;*****
CTRLC	equ	03H		;control-C
BS	equ	08H		;backspace
CR	equ	0DH
LF	equ	0AH
ESC	equ	1Bh		;Escape
QUOTE	equ	27h		;single-quote
DEL	equ	7Fh		;delete

;---------------
;program Equates
;---------------
PROMPT	equ	'>'	;Prompt character
CABKEY	equ	CTRLC	;command abort character
CBKEY2	equ	ESC	;2nd cmd abort chr (EPROM32 only)
DTEXIT	equ	CTRLC	;default Terminal Mode exit CHR
PAUKEY	equ	' '	;pauses dumping
PCFIER	equ	'.'	;console pacifier character

LBSIZE	equ	80	;input line buffer size
HRLEN	equ	16	;Intel hex record length for HD

DTPORT	equ	6	;default transfer port

;-------------------------------
;Single-Character Error Messages
;-------------------------------
CERMSG	equ	'C'	;checksum/marker byte error
HERMSG	equ	'H'	;Illegal hex digit
MERMSG	equ	'M'	;memory write verify error
OERMSG	equ	'O'	;memory overlay error

;-----------------------------------
;Altair Absolute Binary file Equates
;-----------------------------------
ALTPLR	equ	3CH	;Program load record
ALTGOR	equ	78H	;EOF/GO address record
ALTBNR	equ	55H	;begin/program name (not supported)
ALTBND	equ	0DH	;end-of-name mark (not supported)
ALTLDR	equ	60	;Leader/trailer length

;--------------------
;Sense Switch Equates
;--------------------
SSWTCH	equ	0FFh	;front panel switch register
LDMASK	equ	007H	;load device mask

;--------------
;88-SIO Equates
;--------------
;88-SIO registers	

SIOCTL	equ	00		;control port
SIOSTA	equ	00		;status
SIOTXD	equ	01		;transmit data
SIORXD	equ	01		;receive data

;Status register bits

SIOIDR	equ	00000001B	;input dev rdy (RX BUF full)
SIOPE	equ	00000100B	;parity error
SIOFE	equ	00001000B	;framing error
SIODOV	equ	00010000B	;data overflow
SIOODR	equ	10000000B	;output dev rdy (TX BUF empty)

;--------------------------------------------------------
;88-ACR (Audio Cassette recorder) Equates
;NOTE: the Altair 88-ACR is built around an Altair 88-SIO
;--------------------------------------------------------
;88-ACR registers	

ACRCTL	equ	06		;control port
ACRSTA	equ	06		;status
ACRTXD	equ	07		;transmit data
ACRRXD	equ	07		;receive data

;Status register bits

ACRIDR	equ	00000001B	;input dev rdy (RX BUF full)
ACRPE	equ	00000100B	;parity error
ACRFE	equ	00001000B	;framing error
ACRDOV	equ	00010000B	;data overflow
ACRODR	equ	10000000B	;output dev rdy (TX BUF empty)

;---------------
;88-2SIO Equates
;---------------
; 88-2SIO registers

SIOBAS	equ	10h
S2CTLA	EQU	SIOBAS		;ACIA A control output port
S2STAA	EQU	SIOBAS		;ACIA A status input port
S2TXDA	EQU	SIOBAS+1	;ACIA A Tx data register
S2RXDA	EQU	SIOBAS+1	;ACIA A Rx data register
S2CTLB	EQU	SIOBAS+2	;ACIA B control output port
S2STAB	EQU	SIOBAS+2	;ACIA B status input port
S2TXDB	EQU	SIOBAS+3	;ACIA B Tx data register
S2RXDB	EQU	SIOBAS+3	;ACIA B Rx data register

;MOTOROLA 6850 ACIA ctrl/stat values

S2RDF	EQU	00000001B	;Rx data register full
S2TBE	equ	00000010B	;Tx data register empty

S2RST	equ	00000011B	;Master reset
S22STP	equ	00010001B	;2 stop bits, /16
S21STP	equ	00010101B	;1 stop bit, /16

;--------------
;88-PIO Equates
;--------------
;88-PIO registers	

PIOCTL	equ	04		;control port
PIOSTA	equ	04		;status
PIOTXD	equ	05		;transmit data
PIORXD	equ	05		;receive data

;Status register bits

PIORDF	equ	00000010B	;RX data register full
PIOTDE	equ	00000001B	;TX data register empty

;-------------------------------------------
;88-4PIO Equates
;NOTE: the 88-HSR uses port 1 of the 88-4PIO
;-------------------------------------------
;88-4PIO registers	

P4CA0	equ	20h		;port 0 section A ctrl/stat
P4DA0	equ	21H		;port 0 section A data
P4CB0	equ	22H		;port 0 section B ctrl/stat
P4DB0	equ	23H		;port 0 section B data
P4CA1	equ	24H		;port 1 section A ctrl/stat
P4DA1	equ	25H		;port 1 section A data
P4CB1	equ	26H		;port 1 section B ctrl/stat
P4DB1	equ	27H		;port 1 section B data

;Status register bits

P4RDF	equ	10000000B	;RX data register full
P4TDE	equ	10000000B	;TX data register empty
HSRRDF	equ	01000000B	;RX data register full for HSR

;Control register bits

P4C1C0	equ	00000001B	;C1 control bit 0
P4C1C1	equ	00000010B	;C1 control bit 1
P4DDR	equ	00000100B	;data direction register
P4C2C3	equ	00001000B	;C2 control bit 3
P4C2C4	equ	00010000B	;C2 control bit 4
P4C2C5	equ	00100000B	;C2 control bit 5
P4IC2	equ	01000000B	;C2 interrupt control bit
P4IC1	equ	10000000B	;C1 interrupt control bit

;4PIO Initialization

P4INIT	equ	P4C2C5+P4C2C3+P4DDR	;2Ch
			;bits 0,1: C1 input active low, int off
			;bit 2: access data reg
			;bits 3-5: C2 output handshake

;-----------------------------------------------
;CompuPro Interfacer 1 Channel B Equates
;(Change these values for a custom transfer port
;The custom port's data port address must be
;immediately after its ctrl/stat port.) 
;-----------------------------------------------
CPRCTL	equ	02h		;Rx ctrl/stat port
CPRDAT	equ	CPRCTL+1	;Rx data must be CPRCTL+1
CPRRDY	equ	02h		;Receiver ready (Data Available)

CPTCTL	equ	02h		;Tx ctrl/stat port
CPTDAT	equ	CPTCTL+1	;Tx data must be CPTCTL+1
CPTRDY	equ	01h		;transmitter ready (Buffer Empty)

CPSPOL	equ	0		;0 for active-high flags
				;1 for active-low flags

CICTS	equ	08h		;CompuPro Interfacer CTS bit

;-----------------------------------------------------------
;Altair 8800 Floppy Disk Controller Equates (These are the
;same for the 88-DCDD controller and the 88-MDS controller.)
;-----------------------------------------------------------
DENABL	equ	08H		;Drive enable output
DDISBL	  equ	  80h		  ;disable disk controller

DSTAT	equ	08H		;status input (active low)
ENWDAT	  equ	  01h		  ;-enter write data
MVHEAD	  equ	  02h		  ;-Move Head OK
HDSTAT	  equ	  04h		  ;-Head status
DRVRDY	  equ	  08h		  ;-Drive Ready
INTSTA	  equ	  20h		  ;-interrupts enabled
TRACK0	  equ	  40h		  ;-Track 0 detected
NRDA	  equ	  80h		  ;-new Read data Available

DCTRL	equ	09h		  ;Drive control output
STEPIN	  equ	  01H		  ;Step-In
STEPOT	  equ	  02H		  ;Step-Out
HEDLOD	  equ	  04H		  ;8" disk: load head
				  ;Minidisk: restart 6.4 S timer
HDUNLD	  equ	  08h		  ;unload head (8" only)
IENABL	  equ	  10h		  ;enable sector interrupt
IDSABL	  equ	  20h		  ;Disable interrupts
WENABL	  equ	  80h		  ;enable drive write circuits

DSECTR	equ	09h		;Sector position input
SVALID	  equ	  01h		  ;Sector valid (1st 30 uS
				  ;..of sector pulse)
SECMSK	  equ	  3Eh		  ;Sector mask for MDSEC

DDATA	equ	0Ah		;Disk data (input/output)

;Floppy Disk Parameters

BPS	equ	128		;data bytes/sector
MDSPT	equ	16		;Minidisk sectors/track
				;this code assumes SPT for 8"
				;disks = MDSPT * 2.

HDRSIZ	equ	3		;header bytes before data
TLRSIZ	equ	2		;trailer bytes read after data

SECSIZ	equ	BPS+HDRSIZ+TLRSIZ ;total bytes/sector

RETRYS	equ	16		;max retries per sector

;------------------------------------
;88-HDSK Datakeeper Hard Disk Equates
;------------------------------------
;88-HDSK ports (The interface board is actually an 88-4PIO.)

CREADY	equ	0A0h	;IN: Ctlr ready for command (bit7)
CSTAT	equ	0A1h	;IN: error flags, reset CREADY
ACSTA	equ	0A2h	;IN: Command Ack (bit 7)
ACMD	equ	0A3h	;IN: reset Command Ack
			;OUT: Command high byte/initiate
CDSTA	equ	0A4h	;IN: data/stat available at CDATA
CDATA	equ	0A5h	;IN: Disk data or status from Ctlr
ADSTA	equ	0A6h	;IN: ADATA Port Available (bit 7)
ADATA	equ	0A7h	;OUT: Command low byte

;88-HDSK ACMD:ADATA Commands

BINIT	equ	24h	;bits 0,1: C1 input active low, int off
			;bit 2: access data reg
			;bits 3-5: C2 input handshake


CINIT	equ	2Ch	;bits 0,1: C1 input active low, int off
			;bit 2: access data reg
			;bits 3-5: C2 output handshake

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

;88-HDSK CSTAT error bits

ERDNR	equ	01h	;drive not ready
ERBADS	equ	02h	;illegal sector
ERSCRC	equ	04h	;CRC error during sector read
ERHCRC	equ	08h	;CRC error during header read
ERSWRG	equ	10h	;header has wrong sector
ERCWRG	equ	20h	;header has wrong cylinder
ERHWRG	equ	40h	;header has wrong head
WPROT	equ	80h	;Write Protect
ERMASK	equ	7Fh	;all the actual error bits

;88-HDSK Constants

OSOFF	equ	40	;Page 0 offset to opsys pointers
HDSPT	equ	24	;Sectors per track
DBUFR	equ	0	;Default controller buffer: 0-3
			;Code gets longer if <>0

;**************************
;Define the Console Port
;(Normal is 88-2SIO port A)
;**************************
CONSTA	equ	S2STAA
CONTXD	equ	S2TXDA
CONRXD	equ	S2RXDA
CONRDF	equ	S2RDF
CONTBE	equ	S2TBE
CONTPO	equ	NOP		;active-high Tx status bit
CONRPO	equ	NOP		;active-high Rx status bit

;***********************
;EPROM Memory Allocation
;***********************
DMAADR	equ	00000h		;Disk load/execution address
				;(Code assumes DMAADR=0)
MONADR	equ	0F800h		;Address of monitor
HDBADR	equ	0FC00h		;Beginning of HDBL PROM
MBLADR	equ	0FE00h		;MBL Subsystem address
DBLADR	equ	0FF00h		;CDBL Subsystem address

 if EPROM32
MON32A	equ	0F000h		;base of 2732 monitor extension
 endif	;EPROM32
 if not EPROM32
MON32A	equ	0
 endif ;not EPROM32

;************************************************
;Address Offsets of components in AMON's RAM page
;************************************************
RAMCOD	equ	0		;Relocated code at bottom
DSKBUF	equ	100h-SECSIZ	;Exactly room for 1 complete sector
DSTACK	equ	DSKBUF		;Disk cmd Stack down from here

LINBUF	equ	100h-LBSIZE	;Input line buffer
STACK	equ	LINBUF		;Normal cmd stack down from here
MINSTK	equ	10h		;minimum stack size

;Floppy disk sector buffer component offsets

SFSIZE	equ	DSKBUF+1	;file size
SDATA	equ	DSKBUF+HDRSIZ	;sector data
SMARKR	equ	SDATA+BPS	;marker byte
SCKSUM	equ	SMARKR+1	;checksum byte

;=====================================================
;Help the EPROM programmer auto-detect the memory page
;=====================================================
 if EPROM32
	org MON32A		;base of 2732 monitor extension
	db 0
 endif	;EPROM32

;=============================
;= Cold-start Initialization =
;=============================
	org MONADR		;Monitor ROM start

	lxi	b,INIT2		;return address

;Fall into INIT

;***Special Subroutine**********************
;Initialization
;   find RAM for the stack and sector buffer
;   Install RAM code
;   Initialize I/O ports
;On Entry:
;  bc = return address
;On Exit:
;  e = 0
;  sp = address of new stack
;  All standard Altair I/O ports initialized
;  interrupts disabled
;Trashes psw,d,hl
;*******************************************
INIT:	di			;no interrupts please

;----------------------------------------------
;Hunt for the highest RAM page
;This assumes at least one 256-byte page of RAM
;and that if one byte within each page is RAM
;then the other 255 bytes are RAM too.
;----------------------------------------------
	lxi	h,0FF00h

CSLOOP:	inr	h		;next RAM page

	mov	a,m		;Original RAM data
	cma
	mov	m,a		;write inverted
	cmp	m		;Correct?
	cma
	mov	m,a		;put original data back
	jz	CSLOOP		;keep looking if RAM write OK

	dcr	h		;point to last good RAM page

;-------------------------------------------
;Relocate  and Install RAM code
;This loop moves more bytes than necessary
;to install the actual RAM code. The extra
;bytes land harmlessly in the (uninitialized)
;stack space and buffer space.
;On Entry:
;   bc = return address
;   h = destination address high byte
; On Exit:
;   e = 0
;-------------------------------------------
	lxi	d,RIOCOD	;RAM code source

RCLOOP:	ldax	d
	cmp	d		;need to relocate an address?
	jnz	RCL1

	dcx	h		;back up to fix low address byte
	mov	a,m
	sui	(RIOCOD-RAMCOD) and 0FFh ;low byte of offset
	mov	m,a
	inx	h

	mov	a,h		;relocate high byte

RCL1:	mov	m,a

	inr	l
	inr	e		;end with e=0 for INIT exit

	jnz	RCLOOP	

;------------------------------------------------
;Create the stack in the RAM page now, so we can
;use CALLs. Push the given return address onto it
;
;Entry from MBL or CDBL requires the stack  to be
;at DSTACK instead of STACK, to make room for the
;disk/RAM code buffer. We always set the stack to
;DSTACK here. MAIN will move it to STACK, if
;that's where we return.
;On Entry:
;   bc = return address
;    h = RAM page high byte
;------------------------------------------------
	mvi	l,DSTACK	;put stack in RAM page
	sphl
	push	b		;push our return address

;-------------------------------------
;Reset all standard Altair I/O devices
;the way that MBL does
;-------------------------------------
;make 4PIO 'A' channels inputs and 'B' channels outputs

	xra	a
	out	P4CA0		;access 4PIO port 0A DDR
	out	P4DA0		;set 4PIO port 0A as input

	out	P4CB0		;access 4PIO port 0B DDR
	cma			;0FFh
	out	P4DB0		;set 4PIO port 0B as output

;Set up the other 3 4PIO ports all the same
	mvi	a,P4INIT
	out	P4CA0		;4PIO port 0A control
	out	P4CB0		;4PIO port 0B control

;Send reset command to both 2SIO ports
	mvi	a,S2RST		;2SIO reset
	out	S2CTLA		;2SIO port 0
	out	S2CTLB		;2SIO port 1

;Set up both 2SIO ports: 8 data bits, 2 stop bits, no parity, 
;clock divide by 16 
	mvi	a,S22STP	;8N2, /16
	out	S2CTLA		;2SIO port 0 control
	out	S2CTLB		;2SIO port 1 control

;-------------------------------------
;Reset the COmpuPro Interfacer 1
;(Really, just set CTS output active.)
;-------------------------------------
	mvi	a,CICTS		;turn on CTS
	out	CPTCTL

;----------------------------------------------------
;Fall into TPCMD to set the default transfer port and
;"Return" to the address provided in bc on entry.
;---------------------------------------------------- 
	mvi	l,DTPORT	;default transfer port

;***Command Routine*******************
;TP [<port>] Set Transfer Port
; Port   Device
;  0    88-2SIO port 0, 2 stop bits
;  1    88-2SIO port 0, 2 stop bits
;  2    88-SIO
;  3    88-ACR
;  4    88-4PIO port 0
;  5    88-PIO
;  6    88-2SIO port 1, 2 stop bits
;  7    CompuPro Interfacer 1 Port B
;
;On Entry:
;  l=port number (upper digit ignored)
;Trashes psw,bc,hl
;*************************************
TPCMD:	mov	a,l			;get port
	ani	7			;make it a legal value
	call	 RAMPAG
	mvi	l,RSETP-RIOCOD+RAMCOD
	pchl				;run RSETP (a=value)

;==============================================================
; AMON RAM I/O Code
; This code must be in RAM either because it gets modified or
; because it may get called after an IN from port FF (which may
; disable the PROM). All of RIOCOD must be in the same page.
;
; The ROM versions of some of these routines also double as the
; console I/O routines, when called in ROM.
;==============================================================
RIOCOD:

;---RAM Subroutine-------------------------------------
;Patch the Transfer Port routines with the correct
;parameters for the load port that is specified in a.
;On Entry:
;  a = transfer port value (values compatible with MITS
;      loaders from rev 3.0 onward.). A < 8
;Trashes psw,bc,hl
;------------------------------------------------------
RSETP:	lxi	b,PTABLE	;lookup table

	add	a		;4 bytes/entry
	add	a
	add	c		;look up in PTABLE (clr carry)
	mov	c,a		;bc=PTABLE(port value)

;Set up the input port routine
	ldax	b		;input data port & CMA flag
	rar			;move CMA flag into Carry
	sta	TPIDP+1		;install data port address

;hl gets the status port (in l) and either NOP or CMA (in h)
	mvi	h,NOP		;NOP instruction
	jnc	RSETP1
	mvi	h,CMA		;CMA instruction
RSETP1:

	dcr	a		;status port = data port-1
	mov	l,a		;install status port address

;Set the status port and either NOP or CMA instruction
	shld	TPISP+1		;status port and NOP/CMA

	inr	c		;next table entry is
	ldax	b		;..the data available mask
	sta	TPIMSK+1	;install mask

;Set up the output port routine
	inr	c		;next table entry is
	ldax	b		;..the data output port address
	sta	TPODP+1		;install data port address

	dcr	a		;status port = data port-1
	mov	l,a		;install stat port address
	shld	TPOSP+1		;status port and NOP/CMA

	inr	c		;next table entry is
	ldax	b		;..the transmitter ready mask
	sta	TPOMSK+1	;install ready mask

;Fall into RTPIS to return, saving one byte

;===Subroutine==============
;Get Console keyboard Status
;On Exit:
;  Z clear if data available
;===========================
KSTAT:

;Fall into the ROM version of Transfer Port Input Status,
;which is the get consolekeyboard status routine

;---RAM Subroutine---------------
;Get Transfer Port input status
;This code gets modified by RSETP
;On Exit:
;  Z clear if data available
;  a=0 and Z set if not
;--------------------------------
RTPIS:	
TPISP:	in	CONSTA		;(status port) read status
TPINOP:	db	CONRPO		;(either NOP or CMA)
TPIMSK:	ani	CONRDF		;(port bit mask)
	ret

;---RAM Subroutine----------------------------
;Get a byte from the Transfer Port immediately
;This code gets modified by RSETP
;(The ROM version of this subroutine is the
;get console keyboard data routine.)
;On Entry:
;  Transfer port Rx data is ready
;On Exit:
;  a = input character
;  Z cleared
;---------------------------------------------
RTPIN:				;call here to flush port
TPIDP:	in	CONRXD		;(data port) get data byte

	ret			;result in a

;===Subroutine============
;Send byte to Console
;On Entry:
;  a = byte to send
;On Exit:
;  All registers preserved
;=========================
PRINTA:

;Fall into the ROM version of Transfer Port Tx Data,
;which is the write to console routine

;---RAM Subroutine---------------
;Send a byte to the Transfer Port
;This code gets modified by RSETP
;On Entry:
;  a = byte to send
;All registers preserved
;--------------------------------
RTPOUT:	push	psw

WAITPO:
TPOSP:	in	CONSTA		;(status port) read status
TPONOP:	db	CONTPO		;(NOP or CMA)
TPOMSK:	ani	CONTBE		;(Tx port mask)
	jz	WAITPO

	pop	psw
TPODP:	out	CONTXD		;(data port)
	ret	

;==============================================================
; RAM Variable (the only one)
;==============================================================
TTYPE:	db	0	;0 (even) means terminal (backspacing)
			;1 (odd) means Teletype (no backspace)

;---RAM Table-------------------------------------------
;Port parameters: One 4-byte entry for each port:
; byte 1 = Rx data port address * 2 + cma flag
; byte 2 = ready mask for data input
; byte 3 = Tx data port address
; byte 4 = ready mask for data output
;Assumptions:
; the control ports for TX or Rx immediately precede the
;   data ports.
; the polarity of the Tx ready status bit is the same as
;   the rx empty status bit.
; Rx port addresses are all < 80h
;
;The first 6 entries are standard for MITS software.
;The 7th entry replaces the MITS high-speed paper tape
;  reader with the 88-2SIO's 2nd serial port.
;The 8th entry is a custom port, currently the CompuPro
;  Interfacer 1 channel B, defined way above.
;-------------------------------------------------------
PTABLE:	db	S2RXDA*2,S2RDF,S2TXDA,S2TBE	;0:2SIO A
	db	S2RXDA*2,S2RDF,S2TXDA,S2TBE	;1:2SIO A
	db	SIORXD*2+1,SIOIDR,SIOTXD,SIOODR	;2:SIO
	db	ACRRXD*2+1,ACRIDR,ACRTXD,ACRODR	;3:ACR
	db	P4DA0*2,P4RDF,P4DB0,P4TDE	;4:4PIO port 0
	db	PIORXD*2,PIORDF,PIOTXD,PIOTDE	;5:PIO
	db	S2RXDB*2,S2RDF,S2TXDB,S2TBE	;6:2SIO B

	db CPRDAT*2+CPSPOL,CPRRDY,CPTDAT,CPTRDY ;7: Interfacer

;===Assembly Check=====================================
; All of RIOCOD must be in the same 256-byte EPROM page
;======================================================
RCEND	equ	$

 if (RCEND-1)/256-(RIOCOD/256)
	ERROR: RAM I/O code is not all in one page
 endif

;===Assembly Check==========================
; All of RIOCOD must fit in one RAM page,
; together with the stack and the RAM buffer
;===========================================
 if (((RCEND-1)-RIOCOD)+MINSTK+SECSIZ)/256
	ERROR: RAM I/O code is too large
 endif

;========================================
;= Cool-Start Initialization            =
;= Print banner, go to MAIN             =
;= On Entry:                            =
;=   sp points to a valid stack address =
;========================================
INIT2:	call	CILPRT		;print banner
				;returns a=0
	db	'AMON '
	db 	((VERSION AND 0F0h)/16)+'0','.'
	db	(VERSION AND 0Fh) +'0'
	db	' by M. Eberhard',CR,LF
	db	'RAM:',' '+80h

;Announce address of the first byte of RAM page
;a=0 here
	call	RAMPAG
	call	PHLCHX		;Print hl on console

;Fall into main

;***********************************
; Command Processor Main entry point
; (Re)create the stack, print the
; prompt, get and process commands
;***********************************
MAIN:

;Repair/Move the stack to where there is plenty of space
;leaving room for just the line buffer above the stack
	mvi	a,STACK		;point to bottom of stack
	call	RAMPAG		;find stack
	sphl			;fix stack

;Print the prompt, and get a line of keyboard input
;This assumes PTABLE and MAIN are in the same page
	lxi	d,MAIN		;create command-return
	push	d		;..address on the stack

	call	CILPRT		;print CR,LF, prompt
	db	PROMPT+80h

	call	GETLIN		;get user input line
				;de=beginning of line
				;Z set if no character found
				;0 at end of line

	di			;INTE light off (cancel error)
	rz			;No command? just ignore.

;Check command list, and execute the command if found
	xchg			;command address to hl
	lxi	d,COMTAB-2	;point to command table

	mov	c,m		;1st command chr in c
	inx	h		;2nd command chr in m

;Search through table at de for a 2-character match of c,m
;allowing uppercase or lowercase letters.
NXTCOM:	inx	d		;skip over address
	inx	d
	ldax	d
	ora	a		;test for table end
	jz	CMDERR		;not in table

	xra	c		;test first character
	mov	b,a		;temp save result
	inx	d		;2nd table character
	ldax	d
	xra	m		;test 2nd character

	inx	d		;point to address offset

	ora	b		;both characters match?
	ani  ('a'-'A') XOR 0FFh	;lowercase is ok
	jnz	NXTCOM		;NO match: keep looking

	inx	h		;skip past 2-letter command

;Got a match. Get command routine address, put it on the stack
	xchg			;(hl)=address of cmd routine
				;de=input pointer

	mov	c,m		;address low byte
	inx	h

	mov	a,m		;address high byte
	ori	80h		;clear non-hex flag bit
	mov	b,a		;..to make legit address

	push	b		;command routine address

;If the msb of the routine address was zero (this bit used as
;a flag), then any parameters are not hex - so go directly to
;the command execution routine.
	xra	m		;Non-hex? (clears carry too)
	rnz			;y: go directly to routine

;Get the following hex parameter (if any) and put it in hl.
;Set the Carry flag if no parameter present.
;Leave de pointing to the 1st chr after the 1st parameter.
;'return' to the Command Routine on the stack.

;skip into FNDHEX

	db	21h		;'lxi h' opcode skips 2

;***Subroutine*************************************
;Scan past blanks and get a hex value
;On Entry:
;  de=address of next item in the input line buffer
;On Exit:
;  hl=value
;  de advanced past character
;  top-of-stack = prior hl value
;  Z set, Carry clear if value
;  Carry set and a=hl=0 if no value found
;**************************************************
PHFHEX:	xthl			;push hl
	push	h		;..beneath return address

;Fall into FNDHEX

;***Subroutine*************************************
;Scan past blanks and get a hex value
;On Entry:
;  de=address of next item in the input line buffer
;On Exit:
;  hl = hex value
;  de advanced past character
;  Z set, Carry clear if value
;  Carry set and a=hl=0 if no value found
;**************************************************
FNDHEX:	lxi	h,0		;default value
	call	SSPACE		;skip spaces to find 1st digit
	stc			;Carry set if no digits
	rz

FHEXLP:	ldax	d		;get digit
	ora	a		;end of line?
	rz			;y: ret with carry clear

	cpi	' '		;value separator?
	rz			;y: ret with carry clear

	cpi	'A'		;convert letters to uppercase
	jc	FHNUM
	ani	('a'-'A') XOR 0FFh	
FHNUM:

	dad	h		;make room for the new digit
	dad	h
	dad	h
	dad	h

	call	HEXCON		;Do the conversion
	jnc	CMDERR		;not valid hexidecimal value?

	add	l
	mov	l,a		;move new digit in
	inx	d		;bump the pointer
	jmp	FHEXLP

;***Command Routine***********************
;AD <SRC> <BCNT> [<GO>]
; (Dump memory in Altair binary format)
;On Entry:
;  hl=<SRC>
;  Carry set if none entered
;  de points to <BCNT>
;  TP command has set up the Transfer Port
;*****************************************
ADCMD:	call	GETHEX		;save <SRC>, get <BCNT>
	call	PHFHEX		;save <BCNT>, get <GO>

	pop	d		;get de=<BCNT>
	xthl			;save <GO>, get <SRC>
	push	psw		;Carry set if no <GO> provided

	xchg			;de= <SRC>, hl=<BCNT>

;de = source address
;hl = byte count

;Punch a pre-leader so that MITS's MBL can load this file
	mvi	a,20h		;punch 20h as the pre-leader
	call	LEADER

;Punch null leader
	call	LEADR0		;returns with b=0

;Loop to punch all the requested data
;(b=0 here, both on initial entry and upon looping)

;Compute b=data byte count of the next block, max=255
NXTBLK:	dcr	b		;b=FFh=255

	mov	a,h		;>256 bytes left?
	ora	a
	jnz	BLKSIZ
	mov	b,l		;N: do what's left
BLKSIZ:

;Punch the the block header info:
; sync chr, byte count, & 2-byte load address
;  b = block size
; de = starting memory address for block data
; hl = remaining bytes to punch
	push	d		;save load address

	mvi	e,ALTPLR	;Punch load-block sync chr
	mov	d,b		;and block byte count	
	call	TPOED

	pop	d		;restore load address
	call	TPOED		;Punch de=load address
				;ends with a=d
	add	e		;a=checksum of the address

;Punch b bytes of block data, computing checksum as we go
;  a = checksum so far
;  b = block size
; de = starting memory address for block data
; hl = remaining bytes to punch
BDATLP:	mov	c,a		;temp save checksum
	ldax	d		;get memory data
	call	TPOUT		;...and punch it
	dcx	h		;one fewer to punch

	add	c		;update checksum

	inx	d		;Next address
	dcr	b		;Loop 'til done with block data
	jnz	BDATLP		;ends with b=0

;a = block checksum
;b = 0
	call	TPOUT		;Punch the block checksum

;Continue until all the data has been punched
;  b = 0
; de = next address to punch
; hl = remaining bytes to punch
; Test for hl=0, meaning there are more bytes to punch
	mov	a,l
	ora	h
	jnz	NXTBLK		;Y: Do another block

;Punch a GO record, if the user asked for one
	pop	psw		;carry set if no <GO> provided
	pop	d		;Go address
	jc	LEADR0		;no go record?

	mvi	a,ALTGOR	;Go record sync chr
	call	TPOUT		;punch it

	call	TPOED		;Punch de=go address

;Fall into LEADR0 to punch a null trailer and return to MAIN

;---Local Subroutine------------
;Punch a null leader
;On Exit:
;  a=0
;  b=0
;  all other registers preserved
;-------------------------------
LEADR0:	xra	a		;leader chr

;Fall into LEADER (with a=0) to punch the leader

;---Local Subroutine------------
;Punch a leader
;On Entry:
;  a = leader character
;On Exit:
;  b=0
;  all other registers preserved
;-------------------------------
LEADER:	mvi	b,ALTLDR	;leader length

LEADLP:	call	TPOUT
	dcr	b
	jnz	LEADLP		;ends with b=0

	ret

;***Command Routine*********************************
;CO <SRC> <DST> <BCNT> [<RPT>] (Copy Memory)
;
; Copy <BCNT> bytes of memory from <SRC> to <DST>.
; Repeat <RPT> times (for EPROM programming). Verify
; result when done.
;On Entry:
;  hl=<SRC>
;  de points to <DST>, <BCNT>, <RPT> follow
;***************************************************
COCMD:	call	GETHEX		;save source, get destination
	call	GETHEX		;save dest, get byte count

	call	CILPRT
	db	'Copyin','g'+80h

	call	PHFHEX		;save <BCNT>, get <RPT>
	mov	a,l		;default to 1
	aci	0		;Carry if no value given

;Repeat copy the specified number of times (in a)
MCRLP:	pop	b		;bc=count
	pop	d		;de=destination
	pop	h		;hl=source

	push	h		;save source
	push	d		;save Dest
	push	b		;save count

	push	psw		;save a=repeat count

;Loop to copy bc bytes from (hl) to (de)
MCLOOP:	mov	a,m
	stax	d
	inx	h
	inx	d
	dcx	b
	mov	a,b
	ora	c
	jnz	MCLOOP

;Repeat the copy as requested by the user
	pop	psw		;recover repeat count
	dcr	a		;repeat as requested

;Print a pacifier dot for all but the last pass
;(This eliminates the dot for a single-pass copy)
	mov	b,a		;temp save repeat count
	mvi	a,PCFIER
	cnz	PRINTA		;preserves all regs
	mov	a,b		;repeat count

	jnz	MCRLP

	jmp	VERIFY		;good copy?

;***Command Routine*********************************
;VE <SRC> <DST> <BCNT> (Verify Memory)
;
; Compare <BCNT> bytes of memory from <SRC> to <DST>
; and report pass/fail
;On Entry:
;  hl=<SRC>
;  Carry set if none entered
;  de points to <DST>, <BCNT> follows
;***************************************************
VECMD:	call	GETHEX		;save <SRC>, get <DST>
	call	GETHEX		;save <DST>, get <BCNT>
	push	h		;save <BCNT>

;Fall into VERIFY to actually verify

;***Subroutine***************************
;Verify memory. Report errors to console.
;On Entry:
;  Top of stack=byte count
;  next on stack = destination address
;  next on stack - source address
;  next on stack=return address (TO MAIN)
;****************************************
VERIFY:	pop	b		;byte count
	pop	h		;hl=destination
	pop	d		;de=source

 if EPROM32
	call	CILPRT
	db	'Verifyin','g'+80h
				;returns carry clear
	push	psw		;c gets set if error found

	jmp	DOVRFY
 endif ;EPROM32

 if not EPROM32
	call	CILPRT
	db	'Checkin','g'+80h
				;returns carry clear
;Loop to compare memory, reporting mismatches
VLOOP:	ldax	d		;get expected data
	cmp	m		;match?
	cnz	MERROR		;N: error

	inx	h
	inx	d
	dcx	b
	mov	a,b
	ora	c
	jnz	VLOOP

	ret
 endif ;not EPROM32

;***Command Routine*********************************
;SE <ADR> <BYTE1> [<BYTE2> [<BYTEn>]]
;    Search for string of bytes, starting at <ADR>
;    <BYTEn> can be either hex byte or 'text string'
;On Entry:
;  hl=<ADR>
;  Carry set if none entered
;  de points to <BYTEs>
;***************************************************
SECMD:

;Get search string from input buffer, convert each byte
;to binary, and save result in the RAM buffer
	call	FNDBUF		;push hl, find RAM buffer

	push	h		;binary string address
	lxi	b,QUOTE		;b=byte count, c=QUOTE
	
;--------------------------------------
;loop to get either a 2-digit hex value
;or a text string (in quotes) each pass
;--------------------------------------
SCHLUP:	call	SSPACE		;returns a=found chr, 0 if none

	cmp	c		;is 1st chr a quote?
	cz	SSTRNG		;y:search for a string
	cnz	SCHHEX		;n: search for hex
				;returns carry set if end
	jnc	SCHLUP		;loop to get all input

;----------------------------------------	
;Search RAM for the requested string
; b = string length
; top-of-stack = binary string address
; next-on-stack = starting search address
;----------------------------------------
	pop	d		;binary string address
	pop	h		;search start address

	mov	a,b		;anything to search for?
	ora	a
	jz	CMDERR		;error if not

SLOOP1:	push	h		;search start address
	push	d		;binary string address

	mov	c,b		;string byte count

;Loop through all bytes of the requested string
;until either all bytes match or 1st non-matching byte
SLOOP2:	mov	a,d
	cmp	h		;don't search our own RAM page
	jz	NOMTCH

	ldax	d		;search string
	cmp	m		;current RAM
	jnz	NOMTCH

	inx	h		;test next byte
	inx	d
	dcr	c		;tested all bytes yet?
	jnz	SLOOP2

;String match found. Print address, ask to continue search
	pop	d		;binary string address
	pop	h		;search start address

	call	CILPRT
	db	'Found at',' '+80h

	push	b
	call	PHLCHX		;print match address, trash bc
	pop	b

	call	CILPRT
	db	'More (Y/N)?',' '+80h

	call	GETKBD		;user response
	call	PRINTA		;echo

	ori	('y'-'Y')	;make it lowercase
	cpi	'y'
	rnz			;anything but y ends

	push	h		;search start address
	push	d		;binary string address

;Search again, starting at the next byte after hl.
;Quit if we've reached the end of memory, FFFFh
NOMTCH:	pop	d		;binary string address
	pop	h		;search start address

	inx	h		;next RAM
	mov	a,h
	ora	l		;End of memory?
	jnz	SLOOP1

	call	CILPRT
	db	'Not foun','d'+80h

	ret

;---Local Subroutine-----------------------
;Get a text string from user input at (de),
;store string at (hl), bump count in b
;On Entry:
;  b = byte count
;  c=QUOTE
;  de points to initial quote
;On Exit:
;  Z flag set
;------------------------------------------
SSTRNG:	inx	d		;skip over quote

STLOOP:	ldax	d
	ora	a		;end quote is not required
	rz	

	inx	d		;point past this input chr

	cmp	c		;end of string?
	rz	

	mov	m,a		;store a string byte
	inx	h
	inr	b

	jmp	STLOOP		;get more of this string

;---Local Subroutine-------------------------------
;Get one hex value from user input at (de), convert
;it to binary, store it at (hl), bump count in b
;On Exit:
;  Carry set if no hex digit found
;--------------------------------------------------
SCHHEX:	call	PHFHEX		;save next string addr byte,
				;get a value.
				;hl=0 & carry set if none

	inr	h		;no high byte allowed
	dcr	h		;does not change carry
	jnz	CMDERR

	mov	a,l		;binary value
	pop	h		;next string address byte

	rc			;carry set means end of input

	mov	m,a		;store the hex digit
	inx	h
	inr	b

	ret

;***Command Routine****************************
;TT [0/1] Set Terminal Type
;  0 (even) means backspacing works
;  1 (odd) means no backspacing (e.g. Teletype)
;On Entry:
;  l = 0 or 1 (odd or even really)
;**********************************************
TTCMD:	mov	c,l		;user value
	mvi	a,TTYPE-RIOCOD+RAMCOD
	call	RAMPAG

	mov	m,c		;remember terminal type
	ret

;***Command Routine********************
;EX [<ADR> [<OPT>]] (execute)
;
;JUMP to <ADR>. If <OPT>=1 the execute
;an "IN FF" first, to disable this PROM
;On Entry:
;  hl = address, default to 0
;  de points to <OPT>
;  Carry set if none entered
;  TOP-of-stack has MAIN address
;**************************************
EXCMD:	call	PHFHEX		;save <ADR>, get l=<OPT>
	dcr	l		;anything but 1
	rnz			;..just executes at <ADR>

;Fall into EXECDP

;***Exit******************************
;Execute "IN FF" and then jump to code
;(This disables PROM)
;On Entry:
;  execution address is on stack
;*************************************
EXECDP:	mvi	e,RET		;RET opcode, <don't care>
	push	d		;..onto stack
	
	lxi	d,0FFDBh	;IN FF opcode
	push	d		;..onto stack

	lxi	h,0
	dad	sp		;point hl to our code

	pop	d		;point sp to <ADR>
	pop	d
	pchl			;execute: IN  FF
				;         RET

;***Command Routine**********************************
;DU [<ADR>] [<BCNT>] (dump memory to console)
;
;Print <BCNT> bytes of memory contents from <ADR> on
;the console in hex. If no count is specified, then
;then print the contents of all memory, 10000h bytes.
;Pause with the space bar, abort with control-C.
;On Entry:
;  hl=<ADR>
;  de points to <BCNT>, if any
;****************************************************
DUCMD:	call	PHFHEX		;save <ADR>, get hl=<BCNT>
	mov	a,l		;low byte
	aci	0		;default to 1
	mov	l,a

	xchg			;de has byte count
	pop	h		;recover start address

;Print the address at the beginning of each line
DLINE:	call	PHLADR		;print hl as an address
				;Sets b=0, trashes c

;Print 16 bytes of hex data separated by spaces
	push	h		;save for ASCII dump
	push	d

DLOOP:	mov	a,m		;get the character
	call	PAHEX		;TO console in hex (b=0)

	call	ILPRNT		;print a space
	db	' '+80h

	inx	h		;next address

	dcx	d		;all done?
	mov	a,d
	ora	e
	jz	DLDONE		;Y: done with command

	mvi	a,0Fh		;new line every XXX0 hex
	ana	l

	jnz	DLOOP		;not zero if more for this line

DLDONE:	pop	d		;recover count and address
	pop	h

;Print up to 16 ASCII characters, or '.' if unprintable
	call	ILPRNT		;pretty space
	db	' '+80h

ADLOOP:	mov	a,m		;get the character
	inr	a		;del (7F) is also nonprinting
	ani	7Fh		;clear parity
	cpi	' '+1		;everything below space
	jnc	PRNTBL		;..is nonprinting

	mvi	a,'.'+1		;dot for non-printing

PRNTBL:	dcr	a		;undo inc

	call	PRINTA		;Print ASCII or dot

	dcx	d		;all done?
	mov	a,d
	ora	e
	rz			;done with command

	inx	h		;next line?
	mvi	a,0Fh		;new line every XXX0 hex
	ana	l

	jnz	ADLOOP		;not zero if more for this line

;Give user a chance to pause or quit at the end of each line
	call	CKPAUS		;Pause or abort?
	jmp	DLINE		;next line

;***Command Routine**********************************
;FI [<VAL> [<ADR> [<BCNT>]]] (fill memory)
;
;Fill <BCNT> bytes of memory with <VAL> from <ADR>.
;if <VAL> is not provided, then fill the specified
;range with 00. <ADR> defaults to 0. If <BCNT> is not
;provided, fill until we reach AMON's RAM page.
;On Entry:
;  hl=<ADR>
;  Carry set if none entered
;  de points to <BCNT>, <VAL> follows, if any
;****************************************************
FICMD:	mov	c,l		;<VAL> c

	call	FNDHEX		;get <ADR>, default 0
	call	PHFHEX		;save <ADR>, get hl=<BCNT>
	xchg			;de has byte count

	call	RAMPAG		;find our RAM
	mov	b,h		;b remembers RAM page

	pop	h		;hl has start address

;Loop to fill memory, quitting if RAM page
FMLOOP:	mov	a,h
	cmp	b		;Filling RAM page?
	rz			;y: done	

	mov	m,c
	inx	h

	dcx	d		;done yet?
	mov	a,d
	ora	e
	jnz	FMLOOP

	ret

;***Command Routine**********************************
;TE [<EXCHR>] (simple Terminal Mode)
;
;Send all console keyboard data to Transfer Port,
;and send send all Transfer Port data to the console.
;If the Transfer Port is the console, then just echo
;the keyboard to the console. Nulls from the keyboard
;are ignored.
; <EXCHR> on the keyboard to exit
; (defaults to DTEXIT)
;****************************************************
TECMD:	call	ILPRNT		;announce exit character
	db	'Exit: ','^'+80h

	call	SSPACE		;get optional exit character
	jnz	TMNL1		;Got an exit value in a
	mvi	a,DTEXIT	;default abort

;Convert exit character to uppercase, non-control, and
;print exit character message
TMNL1:	ani	1Fh		;make it a control chr
	mov	l,a		;remember exit character

	ori	'C'-CTRLC	;make it printable
	call	PRINTA

	call	CILPRT		;CR,LF,LF to be pretty
	db	LF+80h

;Be a terminal until we get an exit character=l.
;Just echo if Transfer Port = console
TLOOP:	call	KSTAT		;anything typed?
	cnz	KDATA		;Y:get the keyboard data

	cmp	l		;exit character?
	rz			;Y: done

	ora	a		;anything typed? (ignore nulls)
	cnz	TPOUT		;KBD data to Transfer Port

	call	TESTTP		;Transfer Port = console?
				;Z set if so

	cnz	TPISTA		;Any Transfer Port data?
				;NZ if so

	cnz	TPIN		;get Transfer Port data
				;always returns W/ nz
	cnz	PRINTA		;and send it to console
	jmp	TLOOP

;***Command Routine*********************************
;OT <PORT> <DATA> (Output to port)
;
;On Entry:
;  l=PORT
;  de points to DATA
;
;Creates this routine on the stack, then executes it
;	NOP
;	MVI  a,<DATA>       
;       OUT  <PORT>
;       RET
;***************************************************
OTCMD:	mvi	h,RET		;opcode
	call	PHFHEX		;push <PORT>, RET opcode
				;Get l=<DATA>

	mvi	h,OUT		;opcode
	push	h		;data, OUT opcode

	lxi	h,3E00h		;NOP, MVI A, opcodes
	push	h

	mov	h,l		;hl=0
	dad	sp		;hl points to routine

	pop	d		;fix stack
	pop	d
	pop	d
	pchl			;execute RAM routine

;***Command Routine****************************************
;HD <ADR> <BCNT> [<OFST>] (Intel hex dump to transfer port)
;
;Dump the specified memory range to the Transfer
;Port as an Intel hex file
;On Entry:
;  hl=ADR
;  de points to subsequent parameters
;**********************************************************
HDCMD:	call	GETHEX		;save <ADR>, get hl=<BCNT>	
	call	PHFHEX		;save <BCNT>, get hl=<OFST>

	xthl			;hl=byte count
	pop	b		;bc=offset
	pop	d		;de= start address
	push	b		;address offset onto stack

;Loop to send requested data in HRLEN-byte records
HDLINE:

 if EPROM32

;Print a pacifier unless the transfer
;port is the same as the console

	call	PACIFY
 endif ;EPROM32

 if not EPROM32

;Give the user a chance to break in at the end of each line
	call	CHKKBD		;abort if user says so

 endif ;not EPROM32

;send record-start
	push	d		;print CRLF
	call	TPCRLF
	pop	d

	mvi	a,':'
	call	TPOUT

;Compute this record byte count
	mvi	b,HRLEN		;default bytes/line

	mov	a,l		;short last line?
	sub	b		;normal bytes/line
	mov	a,h
	sbi	0
	jnc	HDLIN1		;N: full line

	mov	b,l		;Y:short line
HDLIN1:

;If byte count is 0 then go finish EOF record
	mov	a,b
	ora	a
	jz	HDEOF

;Send record byte count=a to Transfer Port (b<>0)
	call	PAHEXC		;send byte count

	mov	c,b		;initiate checksum		

;Compute the address by adding the RAM address to the
;address offset. Send the address at the beginning of
;each address, computing checksum in c (b<>0)
	xthl			;hl=address offset
				;remaining byte count on stack
	push	h		;save address offset

	dad	d		;compute address with offset
	call	PHLHEX		;send address with offset

	pop	h		;recover address offset
	xthl			;offset on stack,
				;remaining byte count to hl

;Send the record type (00)
	xra	a
	call	PAHEXC

;Send b bytes of hex data on each line, computing
;the checksum in c. b>0 here.
HDLOOP:	ldax	d		;get the character
	call	PAHEXC		;send to Transfer Port
				;(b<>0)
	dcx	h
	inx	d
	dcr	b		;next
	jnz	HDLOOP

;Send the checksum (with b<>0)
	xra	a
	sub	c
	inr	b		;b<>0 means Transfer Port
	call	PAHEXC

;Next record
	jmp	HDLINE		;next record

;--------------------------------------------
;Finish end-of-file Intel hex record
;On Entry:
;  The CR LF and colon have already been sent
;  The address offset is still on the stack
;--------------------------------------------
HDEOF:	pop	b		;chuck address offset
	mvi	b,5		;5 bytes for EOF

HDELP:	xra	a
	call	PAHEX		;b<>0 for Transfer Port
	dcr	b
	jnz	HDELP

;Fall into TPCRLF

;===============
;= subroutines =
;===============

;***Subroutine*****************
;Send CRLF to the transfer port
;Trashes de
;******************************
TPCRLF:	lxi	d,LF*256+CR

;Fall into TPOED

;***Subroutine*********************
;Send e then d to the transfer port
;On Exit:
;  a=d
;**********************************
TPOED:	mov	a,e
	call	TPOUT
	mov	a,d

;Fall into TPOUT

;***Subroutine*********************
;Send a to the Transfer Port
;On Entry:
;  a = data to send
;  SP points into the RAM page
;  Transfer Port is already set up
;All registers preserved
;**********************************
TPOUT:	call	HRMPAG			;don't mess up a
	mvi	l,RTPOUT-RIOCOD+RAMCOD	;hl points to RTPOUT

	xthl				;restore hl, put
					;..address on stack
	ret				;go to RTPOUT with a

;***Subroutine***********************
;Print memory error details, and give
;user a chance to pause or abort
;On Entry:
;  a=Expected (Source) data
;  hl=Destination Address
;  (hl)=Found data
;Trashes psw
;************************************
MERROR:	push	b
	push	psw		;save source data

	call	CILPRT
	db	'?',':'+80H
	call	PHLCHX		;Print address in hl on console,
				;..trash c, set b=0

	CALL	ILPRNT
	db	' Expected',' '+80H

	pop	psw		;recover source data
	call	PAHEX

	call	ILPRNT
	db	', read',' '+80H
	mov	a,m		;Get destination data
	call	PAHEX

	pop	b

;Fall into CKPAUS

;***Subroutine*******************************
;Get a keyboard character, abort if control-C
;pause (until anything else typed) if space
;On Exit:
;  a=keyboard character, Z cleared
;********************************************
CKPAUS:	call	CHKKBD		;Abort or pause?

	cpi	PAUKEY		;Pause?
	rnz

;Fall into GETKBD and wait for any key to end pause

;***Subroutine*******************************
;Get a keyboard character, abort if control-C
;On Exit:
;   a=keyboard character, Z cleared
;********************************************
GETKBD:	call	CHKKBD		;get KBD character, test for ^C
	jz	GETKBD		;wait for character

	ret

;***Subroutine*********************************************
;Read a command line from the keyboard, echoing and saving
;it in the input line buffer
;
;CR input ends the sequence. the CR is not saved in the
;input line buffer. instead, the line is terminated with 0.
;
;On Exit:
;  complete command line is in the input line buffer
;  de=address of the first non-blank character on the line
;  a = first non-blank value found
;  Z set if nothing but spaces found
;**********************************************************
GETLIN:	mvi	a,LINBUF
	call	HRMPAG		;find line buffer, push h

	push	h		;save input line buffer'S
				;..start address

;Get & echo characters, stashing them in the input line buffer
;at hl, until a CR is encountered
GLLOOP:	call	LBCHR		;get kbd chr into line buffer
				;with echo

	sui	CR		;end of line from user?
	jnz	GLLOOP		;n: get another chr

	dcx	h		;back up to CR
	mov	m,a		; overwrite CR with null

	call	ILPRNT		;linefeed to follow CR
	db	LF+80h

	pop	d		;input line buffer address
	pop	h		;Restore original hl

;Fall into SSPACE to skip initial spaces

;***Subroutine****************************
;Scan past spaces, looking for the first
;non-space character
;
;On Entry:
;  de=address within the input line buffer
;On Exit:
;  a=0 and Z set if none found
;  a=character value and Z clear if found
;*****************************************
SSPACE:	ldax	d		;get next character
	ora	a		;terminating null?
	rz

	cpi	' '		;another space?
	rnz			;we're past them

	inx	d		;next scan address
	jmp	SSPACE		;keep skipping

;===Assembly Check================================
; The above code must not overrun the next section
;=================================================
H0END	equ	$

 if (HDBADR - H0END)/256
	ERROR: HDBL is overwriting prior code
 endif

;==============================================================
; Hard Disk Boot Loader Subsystem (HDBL)
;
; The standard 88-HDSK system uses a Pertec D3422 disk drive,
; which contains 2 platters - one is in a removable cartridge,
; the other is a fixed platter. (However, The 88-HDSK
; controller can actually support up to 4 platters, supporting
; the Pertec D3462 disk drive, which has one removable
; platter, and 3 fixed platters.)
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
;==============================================================
	org HDBADR

;==============================================================
; Entry here to execute HDBL directly, to boot from a hard
; disk. This is the same address where my HDBL PROM starts.
;==============================================================
HDBL:	lxi	b,HDBRET	;return address
	jmp	INIT		;go find a real stack
				;and initialize ACIAs
				;returns with e=0

HDBRET:	mov	l,e		;boot from platter 0

;Fall into HBCMD

;***Command Routine*****
;HB  Boot from hard disk
;On Entry:
;  l<0> = platter
;***********************
HBCMD:	
 if EPROM32
        mov	a,l
	ani	1		;just the lsb
	rrc			;Platter goes in bits <7:6>
	rrc			;..which is CFPLTR
        mov     d,a             ;d<7:6>=platter bits
 endif
;-------------------------------------- 
;Initialize 88-HDSK interface board
;(Actually ports 0 and 1 of an 88-4PIO)
;--------------------------------------

        xra	a
        mov     h,a
        mov     l,a
        
	out	0A0h		;Select port 0Ah DDR 
	out	0A2h		;Select port 0Bh DDR
	out	0A4h		;Select port 1Ah DDR
	out	0A6h		;Select port 1Bh DDR
	out	0A1h		;Port 0Ah is an input port
	out	0A5h		;Port 1Ah is an input port

	cma
	out	0A3h		;Port 0Bh is an output port
	out	0A7h		;Port 1Bh is an output port

	mvi	a,CINIT		;set up input port handshakes
	out	0A0h
	out	0A4h
	out	0a6h		;output port 1Bh handshakes

	mvi	a,BINIT		;set up port 0Bh handshakes
	out	0A2h

	in	CSTAT		;clear Controller Ready bit

;-----------------------------------------------
;Read the Pack Descriptor Page (Disk Page 0)
;to get the Opsys Pointers:
;  Bytes 41:40 = Initial Disk Page number
;  Bytes 43:42 = Disk Page count (Byte 43=MSB=0)
;On Entry:
;  d = platter in bits <7:6>, bits <5:0>=0
;  hl = 0
;-----------------------------------------------

        mvi     a,OSOFF+3       ;byte count to end of pointers
        mov     e,a             ;...into e
 if EPROM32
        ora     d               ;add in platter bits
 endif
        mov     b,a             ;...into b
	call	GETPAG		;Seek, read page hl platter b<7:6> 
                                ;into buffer, set up to read b<5:0> 
                                ;buffer bytes

;Read from the controller buffer and discard everything until
;we get to the opsys pointers. Load the opsys pointers into
;c & hl. Note: no testing any handshake here - just assume
;the controller can keep up. (The controller can send a data
;byte every 2.5 uS.) This only reads the low byte of the
;page count, since the high byte must be 0 anyway.
PTRLUP:	in	CDATA		;read byte from controller

	mov	l,h		;shift everybody over...
	mov	h,c
	mov	c,a		;...and put it away

	dcr	e
	jnz	PTRLUP

        
;-------------------------------------------------
;Read c Pages from disk, starting at Page hl, into
;memory starting at the address on the stack
;On Entry:
;  c = page count
;  d = platter in bits <7:6>, bits <5:0>=0
;  e = 0
;  hl = initial Disk page number
;-------------------------------------------------

 if EPROM32
        mov     b,d             ;b=platter bits
 endif
 if not EPROM32
        mov     b,e             ;b=0
 endif        
	mov	d,e		;set load/exec address to 0
	push	d		;execution address on stack
        
PAGELP:	call	GETPAG		;Seek, read page hl platter b<7:6> 
                                ;into buffer, set up to read b<5:0> 
                                ;buffer bytes

;Load 256 bytes of buffer data into memory at de
;Note: no testing any handshake here - just assume the
;controller can keep up. (The controller can send a data byte
;every 2.5 uS.)
BYTELP:	in	CDATA		;get a data byte
	stax	d		;write it to RAM
	inr	e		;write entire page
	jnz	BYTELP		;until done

; Next Disk Page
	inr	d		;next RAM page
	inx	h		;Next Disk Page
	dcr	c		;bump Disk Page count
	jnz	PAGELP

;---------------------------------------------------
;Go execute loaded code, at the address on the stack
;On Entry:
;  c = 0
;---------------------------------------------------
	jmp	EXECDP		;disable PROM,
				;go execute loaded code

;***Subroutine***************************************
;Seek and read disk Page hl into 88-HDSK buffer 0
;On Entry:
;  b<7:6>=platter
;  b<5:0>=number of bytes to transfer (0 means 256)
;On Exit:
;  a,flags trashed, all others preserved
;  Controller has specified sector data in its buffer
;****************************************************
GETPAG:	push	h		;Save requested Page
	push	d		;Save regs
	push	b		;save byte count

;----------------------------------------------------------
;Compute cylinder and sectorX2  from Disk Page number in hl
; hl := hl / (2*HDSPT) (Quotient=cylinder)
;  a := hl MOD (2*HDSPT) (Remainder=sectorx2)
;This is fast only if the cylinder number is low. MITS
;usually put the boot image starting at cylinder 0, side 1.
;But we will always miss the next sector anyway, so each
;sector will require a full disk rev (25 mS), lots of time.
;----------------------------------------------------------
	lxi	b,-2*HDSPT
	mov	d,b		;de=FFFF=-1
	mov	e,b		;since loop goes 1 extra

DIV1:	inx	d		;compute quotient=cylinder
	dad	b		;hl gets remainder
	jc	DIV1

	mov	a,l		;fix remainder, since
	sub	c		;..loop went 1 extra

	xchg			;cylinder number to hl

;--------------------------------------------------
;Compute Sector & Side
;If sectorX2 > sectors/track then set CSIDE
;bit, and reduce sector number by sectors/track
;  hl= Quotient (cylinder)
;  a = Remainder (sectorX2, either for head 0 or 1)
;--------------------------------------------------
	cpi	HDSPT		;past end of side 0?
	jc	SIDEOK		;N: sector number is good

	adi	CSIDE-HDSPT	;Compute sector mod HDSPT,
				;..and set side 1 bit

SIDEOK: mov     d,a             ;save sector # with side
        
;------------------------------------------
;Seek Cylinder
;  hl = cylinder number<9:0>
;------------------------------------------
 if CSEEK+DBUFR			;these are actually 00
	mov	a,h		;h<1:0>=cylinder<9:8> 
	ori	CSEEK+DBUFR	;combine with SEEK operation
	mov	h,a
 endif

	call	HDSKC		;hl=SEEK command with cyl #

;---------------------------------------------------------
;Read Sector from current track into controller's buffer 0
;  d<5> = side
;  d<4:0> = sector number
;---------------------------------------------------------
	pop	b		;b=platter number and byte count
 if EPROM32
        mov     a,b             ;get into a
        ani     0C0h            ;isolate platter bits <7:6>
        ora     d               ;add in sector # and side
 endif        
 if not EPROM32
        mov     a,d             ;get sector # and side
 endif
        
        mvi     h,CRDSEC+DBUFR  ;issue Read Sector command
	call	HDSKCA		;low command byte is in a

;--------------------------------------------
;Issue CRDBUF command to kick off read of
;256 bytes from the controller's buffer
;Note: this assumes the controller is ready.
;(and it is, because HDSKC left it that way.)
;--------------------------------------------
	in	CDATA		;reset CDA in CDSTA
	in	ACMD		;clear CMDACK in ACSTA

	mov	a,b             ;a=platter number and byte count
 if EPROM32
        ani     3Fh             ;isolate byte count (bits <5:0>)
 endif        
	out	ADATA		;..to controller

	mvi	a,CRDBUF+DBUFR	;issue Read Buffer command
	out	ACMD		;..to controller

	pop	d		;(10)
	pop	h		;(10) 15 uS total from 'out'

;The 8x300 is ready to transmit data in 8 uS. This code takes
;40 cycles (including the 'ret'), or 20 uS min to get around
;to reading the data - so there is no need to wait on CDSTA
 if FALSE
DATAWT:	in	CDSTA		;Wait for data port to be ready
	rlc			;msb=CDA
	jnc	DATAWT
 endif

;---------------------------------
;Controller is ready to transfer
;256 bytes of data from its buffer
;---------------------------------
	ret			;(10)done with GETPAG

;***Subroutine************************************************
;Issue a disk command, and then wait for the controller
;to complete it
;
;Note: this just assumes the controller is ready, which is OK
;since the last command was either a seek (where HDSKC waited
;for the controller to become ready) or it was a CRDBUF, which
;ended with all bytes transferred - and the controller becomes
;ready very soon (1.5 uS) after the last byte is transferred.
;On Entry at HDSKC:
;  hl = complete command
;On Entry at HDSKCA:
;  a=low byte of command
;  h=high byte of command
;On Exit:
;  a,flags trashed, all others preserved.
;  The command is completed and the controller is ready.
;  Any errors will terminate the load, and print an error
;  message on the Terminal 
;*************************************************************
HDSKC:	mov	a,l		;low byte of command

HDSKCA:	out	ADATA		;..to data port

	in	CSTAT		;reset CRDY flag just in case
	in	ACMD		;clear CMDACK in ACSTA

	mov	a,h		;command high byte
	out	ACMD		;issue command

HDWAIT:	in	CREADY		;Is the controller done? 
	rlc			;look at msb=CRDY
	jnc	HDWAIT		;N: keep waiting

	in	CSTAT		;reset CRDY flag
	ani	ERMASK		;and get A=error code
	rz			;No errors: happy return

; Report a load error and go to AMON's main loop
; On Entry:
;   a = error flag bits
;  hl = disk command
	call	PCAHEX		;print error code in hex
	jmp	HDERR		;finish the error message

;***Command Routine***************************************
;HL [<OFST>] (Intel hex load from transfer port)
;
;Load an Intel hex file from the Transfer Port into memory
;at the addresses specified in the hex file, with optional
;address offset <OFST>. done when any record with 0 data
;bytes is encountered, or if control-C is typed.
;
;print a pacifier dot for each record unless the transfer
;port is the same as the console
;
;On Entry:
;  hl= address offset from user (defaults to 0)
;
;register usage during hex load:
;  b: Scratch
;  c: record byte counter
;  d: record checksum
;  e: record byte count for EOF test
;  hl: memory address
;  Top of stack: address offset
;  Next on stack: record count
;*********************************************************
HLCMD:	push	h		;address offset onto stack

	lxi	h,0		;initialize record count
	push	h		;onto stack too

;Eat all characters until we get record-start colon
GETCOL:	call	GETTPD
	ani	7Fh		;strip parity
	sui	':'
	jnz	GETCOL

	mov	d,a		;d=0: Init checksum

 if EPROM32
	call	PACIFY
 endif ;EPROM32

 if not EPROM32
	call	ILPRNT		;print a pacifier per record
	db	PCFIER+80h
 endif ;not EPROM32

;Restart checksum, then get 4-byte record header: (a=0 here)
; c gets 1st byte = data byte count
; h gets 2nd byte = address high byte
; l gets 3rd byte = address low byte
; b gets 4th byte = record type (ignored)

	mvi	e,4		;get 4 header bytes

;Shift in the four header bytes: c <- h <- l <- b
HEDRLP:	mov	c,h		;c=byte 1: byte count
	mov	h,l		;h=byte 2: address MSB
	mov	l,b		;l=byte 3: address LSB
	call	GETTPH		;get header byte, do checksum
	dcr	e
	jnz	HEDRLP

;Offset the address by the value on top of the stack
;and bump the record count. a=checksum so far here
	pop	d		;get offset
	dad	d		;offset the address in hl

	xthl			;bump record count
	inx	h
	xthl			;..leaving it on the stack

	push	d		;save offset

	mov	d,a		;d=checksum so far
	mov	e,c		;remember count for EOF test

;c = e = record byte count
;hl = RAM address for this record=record address+offset
	mov	a,c		;c=record byte count
	ora	a		;0-byte record?
	jz	GETCSM

;Loop to get data into memory at hl.

DATALP:	call	GETTPH		;data byte in b, cksm in d

;See if this byte will overwrite our RAM area. This blocks
;out a 256-byte region of memory wherever this program found
;RAM for its stack.
	call	HRMPAG		;(stuffs hl on stack)
	mov	a,h		;a=RAM page address
	pop	h		;restore RAM address
	cmp	h		;same as AMON's RAM page?
	jz	OVRERR		;y:abort with overwrite error

;Write to memory, and verify the write
	mov	m,b		;store data in RAM
	mov	a,m
	cmp	b
	jnz	MEMERR		;successful write?
	inx	h

	dcr	c
	jnz	DATALP

GETCSM:	call	GETTPH		;get checksum in a & Z flag
	jnz	CSMERR		;should be zero

;All done with this record. Check for EOF (byte count=0)
	ora	e		;zero-byte record?
	jnz	GETCOL		;N: go get another record

;-------------------------------------------
;Done. Print record count and return to MAIN
;-------------------------------------------
HLDONE:	pop	h		;remove offset from stack
	pop	h		;record count

	call	CILPRT

 if EPROM32
	db	'Records:',' '+80h
 endif ;EPROM32
 if NOT EPROM32
	db	'Recs:',' '+80h
 endif ;NOT EPROM32

;Fall into PHLCHX

;***Subroutine**************************
;Print hl as 4 hex digits on the console
;On Entry:
;  c=checksum so far
;  hl=2 bytes to print
;On Exit:
;  b=0
;  c=updated checksum
;Trashes a
;Stack depth: 6
;****************************************
PHLCHX:	mvi	b,0	;print on console
	jmp	PHLHEX

;***Command Routine**************************************
;EN [<ADR>] (enter data into memory)
;
;Get hex values from the keyboard and enter them
;sequentially into memory, starting at <ADR>. a blank
;line ends the routine and returns control to the
;command Mode. values may be separated by spaces or CR'S.
;Print the current address at the beginning of each line.
;On Entry:
;  hl = <ADR>, defaulting to 0
;  Carry set if none entered
;********************************************************
ENCMD:	call	PHLADR		;print hl as an address

	call	GETLIN		;get a line of user input
	rz			;z=blank line terminates

;Get hex data from the user input line and write it to memory
ENLOOP:	call	PHFHEX		;save memory address,
				;Get/convert value

	mov	a,l		;get low byte as converted
	pop	h		;recover memory address

	mov	m,a		;put in the value
	inx	h		;next address

	call	SSPACE		;Scan to next input value
	jnz	ENLOOP		;not end of line: continue

	jmp	ENCMD		;end of line: start new line

;===============
;= subroutines =
;===============

;***Subroutine*********************************************
;Get, echo, and store a console character in the input
;line buffer. Handle deletes and backspaces.
;
;On Entry:
;  hl = next free spot in the input line buffer
;  LBSIZE is max characters allowed in the input line buffer
;On Exit (not full, no deletes):
;  a=character
;  hl = hl+1
;  (hl-1) = character
;**********************************************************
LBCHR:	call	GETKBD		;get a character
	mov	m,a		;store character in buffer

	cpi	DEL		;DEL character?
	jz	GCDEL
	cpi	BS		;BS is same as DEL
GCDEL:	mov	a,l		;buffer address low byte
	jz	GDELET

	xri	LINBUF+LBSIZE-2	;input buffer full?
	mov	a,m		;recover chr for echo
	rz			;full: ignore it

	inx	h		;bump line buffer pointer

	jmp	PRINTA		;echo & ret

;-----------------------------------------------------------
;Backspace or delete found. Delete if there is anything to
;delete, and echo to the user the right way, based on TTYPE.
;-----------------------------------------------------------
GDELET:	sui	LINBUF		;anything on the line?
	rz			;done if not.

;backspace either by erasing onscreen or Teletype-style
	dcr	l		;back up in buffer

	mvi	a,TTYPE-RIOCOD+RAMCOD
	call	HRMPAG		;pushes hl too
	mov	a,m		;get TTYPE variable
	pop	h
	rar			;0 (even): backspacing terminal
	jnc	GCBKUP

;Teletype-style delete
	call	PSLASH		;print deleted character

	mov	a,m		;..between slashes
	call	PRINTA

;Fall into PSLASH

;---Local Subroutine---
;Print a slash
;----------------------
PSLASH:	call	ILPRNT
	db	'/'+80h
	ret

;Terminal-style delete
GCBKUP:	call	ILPRNT		;back up on screen
	db	BS,' ',BS+80h	;Erase old character & back up

	ret

;***Subroutine*****************************
;Print CR LF then inline string at (sp)
;Calls to CILPRT are followed by the string
;the last string byte has its MSB set
;On Exit:
;  a = 0
;  Z & Carry cleared
;  all other registers preserved
;******************************************
CILPRT:	call	ILPRNT
	db	CR,LF+80h

;Fall into ILPRNT

;***Subroutine*****************************
;Print inline string at (SP)
;calls to ILPRNT are followed by the string
;the last string byte has its MSB set
;On Exit:
;  a = 0
;  Z & Carry cleared
;  all other registers preserved
;******************************************
ILPRNT:	xthl			;save hl, get string address

IPLOOP:	mov	a,m		;loop through message
	ani	7Fh		;strip end-marker
	call	PRINTA
	xra	m		;end? (clears Carry too)
	inx	h
	jz	IPLOOP

	xra	a		;for return
	xthl			;restore hl
				;..get ret address
	ret

;***Subroutine****************************
;Get console keyboard data
;On Entry:
;  A keyboard character is already waiting
;On Exit:
;  a=keyboard character, parity stripped
;  Z clear (unless null typed)
;*****************************************
KDATA:	in	CONRXD		;get keyboard character
	ani	7Fh		;strip parity
	ret

;***Subroutine**************************************
;Get keyboard status unless the transfer port is the
;console. Abort if CABKEY (control-C) or CBKEY2
;(ESCAPE).
;On Exit:
;  if a character is waiting, then character is in a
;  if no character waiting, Z set, a=0
;***************************************************	
CKABRT:	call	TESTTP		;Transfer Port = console?
	rz

;Fall into CHKKBD

;***Subroutine**************************************
;Get keyboard status. If a character is waiting,
;then return it in a with parity stripped. Abort
;if CABKEY (control-C) or CBKEY2 (ESCAPE).
;On Exit:
;  if a character is waiting, then character is in a
;  if no character waiting, Z set, a=0
;***************************************************	
CHKKBD:	call	KSTAT		;anything typed?
	rz			;N: return w/ Z set

	call	KDATA		;Y: get the data
	cpi	CABKEY		;abort character typed?

 if EPROM32
	jz	MAIN
	cpi	CBKEY2		;Other abort character
 endif ;EPROM32

	rnz

	jmp	MAIN

;***Subroutine******************************************
;Check for abort from the console (unless the transfer
;port is the console), and then get a printable ASCII
;byte from the Transfer Port.
;On Entry:
;  SP points into the RAM page
;  RAM code is in place
;On Exit:
;  character in a
;Stack depth: 6
;*******************************************************
GETTPD:	call	TESTTP		;Transfer Port = Console?
	cnz	CHKKBD		;No. User abort?

	call	TPISTA		;Transfer Port character?
	jz	GETTPD		;n: keep waiting

;Fall into TPIN

;***Subroutine**********************
;Get Transfer Port data immediately
;On Exit:
;  a=byte from Transfer Port
;  Z cleared
;***********************************
TPIN:	mvi	a,RTPIN-RIOCOD+RAMCOD

;Fall into GORAM

;***Subroutine****************
;Execute RAM code
;On Entry:
;  a=RAM code address low byte
;*****************************
GORAM:	call	HRMPAG		;pushes h

	xthl			;fix hl, put address on stack
	ret			;'call' code ppointe to by a

;***Subroutine**************
;Get Transfer Port Rx status
;On Exit:
;  a=0 & Z set if no data
;***************************
TPISTA:	mvi	a,RTPIS-RIOCOD+RAMCOD
	jmp	GORAM		;recycle some code

;***Subroutine*************************
;Test to see if Transfer Port = console
;On Exit:
;  Z set if console = Transfer Port
;Trashes a
;Stack depth: 4
;**************************************
TESTTP:	mvi	a,TPISP+1-RIOCOD+RAMCOD	;status register addr
	call	HRMPAG			;pushes h

	mov	a,m
	pop	h

	cpi	CONSTA			;Console's status port?
	ret

;***Subroutine****************************************
;Get 2 hex digits from the Transfer Port, combine them
;into 1 byte, and add the result to the checksum in d
;On Entry:
;  d = checksum so far
;On Exit:
;  b=byte of data
;  a=d=new checksum value
;  Z flag set if checksum is now 0
;  all other registers preserved, unless error abort
;*****************************************************
GETTPH:	call	GETTPN		;get high nibble
	add	a		;Shift high nibble in place
	add	a
	add	a
	add	a
	mov	b,a
	call	GETTPN		;get low nibble

	ora	b		;combine nibbleS
	mov	b,a		;save result for return
	add	d		;compute checksum
	mov	d,a		;ret with checksum in a & d
	ret

;---Local subroutine--------------------
;Get a hex digit from the Transfer Port,
;validate it, and return it in A<3:0>
;---------------------------------------
GETTPN:	call	GETTPD
	ani	7Fh		;strip parity
	call	HEXCON
	rc			;Carry means OK

;Abort: ASCII character error - not a valid hex digit
	mvi	a,HERMSG
	jmp	RPTERR

;==================================================
; Command Table
; Each entry:
;   Byte 0 = 1st command character
;   Byte 1 = 2nd command character
;   Byte 2 = command execution address low byte
;   Byte 3<6:0> = command execution address<14:8>
;                 (address<15> is assumed to be 1)
;   Byte 3<7> = 0 if the command's parameters are
;               not hexidecimal values
;
; The table is terminated by a null in Byte 0
;==================================================

;Table is in lower half of 2732 EPROM
 if EPROM32
H1END	equ	$
	org MON32A
 endif ;EPROM32

COMTAB:	db	'AD'		;Dump in Altair format
	dw	ADCMD
	db	'AL'		;Load Altair format
	dw	ALCMD

	db	'BO'		;Boot from FLOPPY
	dw	BOCMD
	db	'CO'		;Copy memory
	dw	COCMD
	db	'DU'		;Dump to console
	dw	DUCMD
	db	'EN'		;Enter
	dw	ENCMD
	db	'EX'		;Execute
	dw	EXCMD
	db	'FI'		;Fill memory
	dw	FICMD

	db	'HB'		;Boot from hard disk
	dw	HBCMD

	db	'HD'		;Intel hex dump
	dw	HDCMD
	db	'HL'		;Intel hex load
	dw	HLCMD

	db	'IN'		;Input from port
	dw	INCMD
	db	'OT'		;Output to port
	dw	OTCMD

	db	'SE'		;Search
	dw	SECMD

	db	'TE'		;Terminal Mode
	dw	TECMD and 7FFFh ;non-hex parameter

	db	'TP'		;Set Transfer Port
	dw	TPCMD
	dw	'TT'		;Terminal Type
	dw	TTCMD

	db	'VE'		;Verify
	dw	VECMD

 if EPROM32
	db	'MT'		;Memory test
	dw	MTCMD

	db	'?',0		;Help command
	dw	HLPCMD and 7FFFh ;non-hex parameter
 endif ;EPROM32

	db	0		;end of table mark

;=======================================================
;The following code is in the lower half of a 2732 EPROM
;=======================================================

 if EPROM32

;***2732 Command Routine**********************************
;MT [<ADR> [<CNT>]] (Test Memory)
;
;On Entry:
;  Carry set if no parameters provided
;  hl=<ADR>
;  de points to <CNT>
; On Entry:
;  hl = <ADR>, defaulting to 0
;  Carry set if none entered
;*********************************************************
MTCMD:	call	PHFHEX		;push <ADR>, get hl=<CNT>
				;hl=0 if none entered
	call	CILPRT
	db	'Testin','g'+80h

	xchg			;de=byte count
	call	RAMPAG		;get our RAM page
				;..and clear carry
	mov	a,h		;a=AMON's RAM page

	pop	h		;hl=start address

	lxi	b,MTPAT		;Test pattern sequence

	push	psw		;a=RAM page, carry is clear
				;..indicating no errors (yet)

;-----------------------------------------------------------
;Loop until all memory locations have seen each pattern byte
;
;Throughout this loop, the carry flag in the psw word that's
;on the stack is set if any error is ever found.
;-----------------------------------------------------------
MTLOOP:	pop	psw		;RAM page, error state
	push	h		;Start address
	push	d		;Byte count
	Push	b		;Pattern position
	push	psw		;a=RAM page, carry=error state
	
;------------------------------------------------
;Fill memory with pattern, avoiding the stack. Do
;a read/invert/write twice to stress the memory.
;------------------------------------------------
FIL0:	pop	psw		;a=RAM page address
	push	psw		;(and carry=error state)

	call	SKPSTK		;skip over the stack
	jnc	FIL2		;..if needed

	ldax	b		;Get a pattern byte

	ora	a		;Pattern end?
	jnz	FIL1
	lxi	b,MTPAT		;y: restart pattern

FIL1:	inx	b

;High-frequency memory byte test while
;we fill memory with the pattern
	mov	m,a		;Write pattern to memory

	mov	a,m		;Invert & write
	cma
	mov	m,a

	mov	a,m		;twice
	cma
	mov	m,a

FIL2:	inx	h		;next address

	dcx	d		;byte count
	mov	a,d		;end?
	ora	e
	jnz	FIL0		;n: keep filling

	pop	psw		;RAM page address & error state
	pop	b		;Pattern position
	pop	d		;Byte count
	pop	h		;Start address

;-------------------------------------------------
;Compare memory to the pattern, avoiding the stack
;-------------------------------------------------
	push	h		;Start address
	Push	d		;Byte count
	push	b		;Pattern position
	push	psw		;RAM page address & error state

CMLOOP:	pop	psw		;a=RAM page
	push	psw		;(and carry=error state)

	call	SKPSTK		;skip over the stack
	jnc	CML2		;..if needed

	ldax	b		;Get pattern byte

	ora	a		;Pattern end?
	jnz	CML1
	lxi	b,MTPAT		;y: restart pattern

CML1:	inx	b		;next pattern byte

	cmp	m		;compare pattern to memory
	jz	CML2		;OK?

;-------------------------
;Report and remember error
;-------------------------
	push	b		;PHLADR trashes bc
	push	psw		;a=pattern byte

	call	PHLADR		;address:
				;Stack depth: 10

	call	ILPRNT
	db	'Wrote',' '+80h

	pop	psw		;expected data
	call	PAHEX

	call	ILPRNT
	db	', read',' '+80h
	mov	a,m
	call	PAHEX		;memory data

	pop	b
	call	CKPAUS		;Abort or pause from user?

	pop	psw		;remember error
	stc			;..by setting carry in
	push	psw		;..the psw on the stack

;-------------------------------------------
;Next RAM location until done with this pass
;-------------------------------------------
CML2:	inx	h

	dcx	d		;next byte count
	mov	a,d		;end?
	ora	e
	jnz	CMLOOP

;-----------------------------------------------
;Done with one pass. Print pacifier, test for
;abort, and do another pass, unless we are done.
;-----------------------------------------------
	call	ILPRNT		;print pacifier
	db	PCFIER+80h

	call	CHKKBD		;Chance to abort

	pop	psw		;RAM page & error state
	pop	b		;Pattern position
	pop	d		;Byte count
	pop	h		;Start address

	push	psw		;RAM page & error state

	inx	b		;rotate pattern once
	ldax	b		;end of pattern?
	ora	a
	jnz	MTLOOP

;Fall into OKEXIT

;***Subroutine End**********************
;All done. Print 'OK' if we never
;saw an error, and then return (to Main)
;On Entry:
;  Top-of-stack has psw, with carry set
;  if any errors found 
;***************************************
OKEXIT:	pop	psw		;carry set if ever an error
	rc			;to MAIN, without OK

	call	CILPRT		;no errors: print OK
	db	'O','K'+80h
				;returns with Z cleared
	ret

;---Local Subroutine----------------------------
;SKip over the stack and I/O routines in RAMPAG
;On Entry:
;  a=RAM page high address byte
;  hl = next RAM address to test
;On Exit:
;  carry clear if hl points to RAM used by AMON
;Trashes a
;-----------------------------------------------
SKPSTK:	cmp	h		;on the RAM page?
	stc			;in case we return
	rnz			;n: done, z clear

	mvi	a,STACK-1	;top of stack
	cmp	l		;above the stack?
	ret			;carry set if yes

;--------------------------------------
;Memory Test Pattern Sequence
;Deliberately a prime number of bytes.
;The first byte may be 0. The last byte
;must be 0. No other bytes may be 0.
;--------------------------------------
MTPAT:	db	000h,0FFh,055h,0AAh,033h,0CCH,0F0h,00Fh
	db	0C3h,03CH,066h,099h,078h,001h,0FEh,002h
	db	0FDH,004h,0FBh,008h,0F7h,010h,0EFh,020h
	db	0FDH,040h,0BFh,080h,07Fh
	db	00h	;End of table mark

;***2732 Subroutine**********************
;Print a pacifier dot unless the transfer
;port is the same as the console port
;trashes a
;****************************************
PACIFY:	push    h
        lxi	h,0			;find transfer port
	dad	sp			;..output port address
	mvi	l,TPODP+1-RIOCOD+RAMCOD	;..located in RAM

	lda	TPODP+1			;get console output port
	cmp	m			;same?
        pop     h
	jz      CHKKBD			;y: done	

	call	ILPRNT			;print a pacifier
	db	PCFIER+80h

;Give the user a chance to break in at the end of each line

	call	CHKKBD		;abort if user says so

	ret

;***Subroutine End*********************
;Verify command end
;On Entry
;  psw with carry clear is on the stack
;**************************************
DOVRFY:
VLOOP:	ldax	d		;get expected data
	cmp	m		;match?
	jz	VLUPOK

	call	MERROR		;N: error
	pop	psw
	stc			;remember error
	push	psw

VLUPOK:	inx	h
	inx	d
	dcx	b
	mov	a,b
	ora	c
	jnz	VLOOP
				;carry on stack set if error
	jmp	OKEXIT		;say 'OK' of no errors

;***Subroutine End****************************************
;FLush the transfer port, and then jump to the already-
;loaded AL command's RAM code address
;On Entry:
;  hl=AL RAM code start address
;  e = 1 if Go record should be ignored
;  e = 2 if Go record should be executed
;  e = 3 if Go record should be executed and PROM disabled
;On Exit:
;  a=c=leader character
;Stack depth: 8
;*********************************************************
TFLUSH:

;Flush external data latches for e.g. the OP-80
;or flush garbage from UARTs
	call	TPIN		;(without checking status)

;Find the leader: wait for two identical bytes in a row
;while allowing the user to abort (unless the transfer
;port is also the console)
	xra	a

FNDLDR:	mov	c,a
	call	GETTPD		;Get chr, chk for abort
	cmp	c
	jnz	FNDLDR

;check if e=3, set Z if so
        mvi     a,3
        cmp     e
        mov     a,c

;Go to RAM code at SKPSWF (instead of MRCODE)
        mvi	l,(SKPSWF-MRCODE)+DSKBUF
        jnz     TFGO            ; jump if PROM should stay enabled
        
;Place "IN FF; MOV A,C" in RAM before SKPSWF and make sure we execute it
        dcx     h
        mvi     a,79h           ; 'MOV A,C' opcode
        mov     m,a
        dcx     h
        mvi     a,SSWTCH        ; sense switch address
        mov     m,a
        dcx     h
        mvi     a,0DBh          ; 'IN' opcode
        mov     m,a
        xra     a
        mov     e,a             ; e=0 means AMON PROM may be disabled
                
TFGO:   pchl

;***2732 Command Routine**********************************
;? (help)
;*********************************************************
HLPCMD:	call	CILPRT
;    123456789012345678901234567890123456789012345678901234567890123
 db 'AD <A> <C> [<G>]     Absolute binary dump, optional GO address'
 db CR,LF
 db 'AL [<0/1/2>]         Absolute binary load,0=noexec,2=EPROM off'
 db CR,LF
 db 'BO                   Boot from Altair Floppy'
 db CR,LF
 db 'CO <S> <D> <C> [<R>] Copy memory, optional repeat count <R>'
 db CR,LF
 db 'DU <A> [<C>]         Dump memory'
 db CR,LF
 db 'EN <A>               Enter data into memory'
 db CR,LF
 db 'EX <A> [<1>]         Execute memory, optional EPROM disable'
 db CR,LF
 db 'FI [<V> [<A> [<C>]]] Fill memory with hex value <V>'
 db CR,LF
 db 'HB [<P>]             Altair Hard disk boot, opt. platter <P>'
 db CR,LF
 db 'HD <A> <C> [<O>]     Hex dump, optional address offset <O>'
 db CR,LF
 db 'HL [<O>]             Hex load, optional address offset <O>'
 db CR,LF
 db 'MT <A> <C>           Memory test (can take several minutes)'
 db CR,LF,LF
 db '  <A>, <S>, <D> are addresses. <C> is a byte count. All in hex.'
 db CR,LF
 db '  ---More--','-'+80h

	call	GETKBD		;wait for anything to be typed

	call	CILPRT
;    123456789012345678901234567890123456789012345678901234567890123
 db LF
 db 'IN <P>               Read and report from input port <P>'
 db CR,LF
 db 'OT <P> <V>           Write hex value <V> to output port <P>'
 db CR,LF
 db 'SE <A> <V1> ... <Vn> Search for hex string'
 db CR,LF
 db 'SE <A> ',QUOTE,'text',QUOTE,'        Search for text string'
 db CR,LF
 db 'TE [<E>]             Terminal mode, ^C [or ^<E>] to exit'
 db CR,LF
 db 'TP <0-7>             Set transfer port:'
 db CR,LF
 db '                      0: 88-2SIO port 0  4: 88-4PIO port 0'
 db CR,LF
 db '                      1: 88-2SIO port 0  5: 88-PIO'
 db CR,LF
 db '                      2: 88-SIO          6: 88-2SIO port 1'
 db CR,LF
 db '                      3: 88-ACR          7: Interfacer 1 port B'
 db CR,LF
 db 'TT <0/1>             Video Terminal/Teletype-style deleting'
 db CR,LF
 db 'VE <S> <D> <C>       Verify (compare) memory'
 db CR,LF,LF
 db '  <A>, <S>, <D> are addresses. <C> is a byte count. All in hex','.'+80h

	ret		;done

H32END	equ	$
 endif ;EPROM32

 if not EPROM32
H32END	equ	0
 endif ;not EPROM32

;===Assembly Check================
;Check for overflow for the low 2K
;=================================
 if EPROM32 and (MON32A+800h-H32END)/800h
	ERROR: Lower 2K code is too large
 endif ;EPROM32 and (MON32A+800h-H32END)/256

;===Assembly Check================================
; All of Monitor must not overrun the next section
;=================================================
 if not EPROM32
H1END	equ	$
 endif ;not EPROM32

 if (MBLADR - H1END)/256
	ERROR: MBL is overwriting prior code
 endif ;(MBLADR - H1END)/256

;==============================================================
;              Multi Boot Loader Subsystem (MBLe)
;
; Loads and runs an Altair 'Absolute Binary file' from input
; Transfer Port specified by the Sense switch settings.
;
; This code may be entered either by a call from the AMON main
; loop or directly from reset (either via the front panel or
; via Jump-Start hardware). If entered from AMON, then AMON
; will pass the selected load port, as requested by the user. 
; If executed directly, then this code will look at the front
; panel switch register to determine the load port.
;
;** Differences between MITS MBL and this code **
;
; 1) The code starts off by relocating itself to the highest
;    page of RAM that is found, so that it will still work
;    if the PROM is Phantomed by an IN instruction from port
;    FF (the switch register).
; 2) All HSR support is eliminated, including 88-4PIO port 1
;    initialization and code for starting the HSR transport.
; 3) The second 88-2SIOJP port (port 1) is initialized.
; 4) The switch setting that was assigned to the HSR has been
;    reassigned to the 88-2SIOJP's second port.
; 5) PTABLE has an 8th entry, which is the same as the 1st
;    (2SIO port 0). Testing for illegal sense switch setting
;    is thereby eliminated.
; 6) An initial read is performed for both the 88-PIO and the
;    88-4PIO port 0, to clear data handshake latches in
;    external devices such as the OP-80 paper tape reader
; 7) If the tape leader character is 0, then no checksum
;    loader will be skipped. 
; 9) Sense switch A11 is ignored when getting the load device,
;    rather than generating an I error.
;
; Since the 88-2SIOJP may optionally disable PROMS when an IN
; instruction accesses port FFh (like some versions of the MITS
; 8800b Turnkey Module), this code cannot execute from
; PROM - at least not from the point where the Sense switches
; are read onwards.
;
;==============================================================
; An Altair 'Absolute Binary file' has 4 sections, which may be
; separated by any number of nulls. these sections are:
;
; 1) the Leader, which comprises 2 or more identical bytes, the
;    value of which is the length of the checksum loader.
;
; 2) the checksum loader, which is a program that is normally
;    used to load the subsequent sections
;
; 3) zero or more load records, each structured as follows:
;       byte 0: Sync byte = 3Ch (identifies a load record)
;       byte 1: NN = number of data bytes in record
;       byte 2: LL = load address low byte
;       byte 3: HH = load address high byte
; bytes 4-NN+3: NN data bytes to store at HHLl, NN>0
;    byte NN+4: CC = checksum of bytes 2 through NN+3
;
; 4) the Go record, structured as follows
;       byte 0: Sync byte = 78H (identifies the Go record)
;       byte 1: LL = low byte of go address
;       byte 2: HH = high byte of go address
;
; Altair file Leaders and checksum loaders are specific to
; both the version of the particular software and the memory
; size. for example, the checksum loader for 4K Basic 3.2 is
; different than the checksum loader for 8K Basic 3.2. and
; both the Leader and checksum loader for 8K Basic 3.2 are
; different than those for 8K Basic 4.0.
;
; The MBL code is able to read any such Altair file by simply
; skipping over the Leader and checksum loader, and loading
; the load and Go records directly.
;
; When executed at the MBL address, MBL chooses its input
; Port based on the front panel Sense switches <2:0>, using
; the conventions set up in Basic 4.X, more or less.
;
;  device                   bits 2:0
;  88-2SIO port 0 (2 stops)   000b
;  88-2SIO port 0 (2 stops)   001b
;  88-SIO                     010b
;  88-ACR                     011b
;  88-4PIO                    100b
;  88-PIO                     101b
;  88-2SIO Port 1 (2 stops)   110b (was high-speed reader)
;  ComnpuPro Interfacer 1     111b (custom)
;
; Prior to Basic 4.0, MITS used different Sense switch settings
; to specify the console device. You can load an older tape
; by setting the switches according to the above table and
; starting the load. after the checksum loader on the tape
; has been skipped, and load records are loading (but before
; the load completes) change the Sense switch settings as
; required by the earlier version of Basic (or other program)
; that you are loading.
;
; The stack gets move to the DSTACK position, to make room for
; the sector buffer above it. (The sector buffer is used here
; for RAM code.) This restricts the stack to only 16 bytes (8
; pushes) deep. Since we changed the stack, we must JMP to MAIN
;when done - we can't RET.
;==============================================================
	org MBLADR

;--------
;find RAM
;--------
MBL:	lxi	b,GOMBL		;return address to here
	jmp	INIT		;go install self-modifying I/O
				;routines, and initialize all
				;known ports. returns with e=0

;***Command Routine**********************************
;AL <0/1> (Boot from paper or cassette tape)
;   Go record ignored if parameter=0. Default to 1.
;Note: parameter is not bounds-checked, but
;nothing bad will happen with bogus values
;
;On Entry:
;  TP command has set up the Transfer Port
;  l=0 and carry set if no parameter typed
;  l=0 and carry clear if GO record should be ignored
;  l=1 if GO record should be obeyed
;****************************************************
ALCMD:	mvi	a,1
	adc	l		;catch carry bit
	mov	e,a		;e = 1 or 2

;---------------------------------------------------
;Entry at GOMBL from cold-start at MBL:
;   e=0
;   nothing on stack
;Entry here from monitor call to ALCMD (AL command):
;  e = 1 if Go record should be ignored
;  e = 2 if Go record should be executed
;  bottom of stack = address of MAIN
;---------------------------------------------------

;Move the stack out of the way for the disk sector buffer
;which gets used here for the MBL RAM code
        pop	b		;return address to MAIN
GOMBL:  call	FNDBUF		;hl=address of sector buffer
	sphl			;stack below buffer
	push	b		;return address to MAIN
                                ;(not used if e=0)

;---------------------------------------------------------
;Relocate PROM image to the sector buffer in RAM.
;Run-time relocation of addresses is done by replacing any
;byte that matches the MSB of the org address with the MSB
;of the destination RAM address. this requires the value
;of the org MSB never to appear in the assembled code other
;than as the MSB of an address. (FE00 works for this.)
;On Entry:
;  hl = RAMBUF address (where to move code and execute it)
;  e = 0 if PROM may be disabled (cold-start at MBL)
;  e = 1 if Go record should be ignored
;  e = 2 if Go record should be executed
;  e = 3 if Go record should be executed and PROM disabled
;On 'ret' to the RAM code:
;  d = RAM execution page
;  e = unchanged
;  Z set if sense switches determine transfer port
;---------------------------------------------------------
	push	h		;RAM code execution address

	lxi	b,MRCODE	;source address

RELOOP:	ldax	b
	cmp	b		;relocatable address byte?
	jnz	NOTADR
	mov	a,h		;Y: relocate this address
NOTADR:	mov	m,a
	inx	b
	inr	l		;don't let h change at the end
	jnz	RELOOP		;run to the end of the page

;Set d=RAM execution page for overwrite detection during load
	mov	d,h

;We will test the sense switches to get the transfer port if
;cold-start. Otherwise use transfer port as set up by Amon.

	inr	e		;e=1 if entry from amon
	dcr	e		;use switches? Z set if so
	rz			;execute the loaded code

	pop	h		;RAM code beginning address

 if not EPROM32
;Go execute AL's RAM code, skipping the sense-switch read,
;using the transfer port as it is already set up.
;(Not enough code space for a thorough transfer port flush)
	mvi	l,(SKPSW-MRCODE)+DSKBUF
	pchl			;skip sense switch test
 endif ;not EPROM32

 if EPROM32
;Go do a thorough transfer port flush, then go execute
;AL's RAM code starting at SKPSWF
	jmp	TFLUSH		;(uses 8 stack bytes)
 endif ;EPROM32

;==============================================================
; AMON Routines, occupying a hole in the PROM space
;==============================================================

;***Command Routine************************************
;IN <PORT> (Input from port)
;On Entry:
;  l=PORT
;Creates this routine on the stack, then executes it,
;then returns through PAHEX to print the value
;
;     NOP
;     IN   <PORT>
;     RET
;******************************************************
INCMD:	lxi	d,PCAHEX	;create return address
	push	d		;ret through PCAHEX

	mvi	h,RET		;Opcode
	push	h		;L=<PORT>
	lxi	h,IN*256	;NOP,IN opcode
	push	h
	mov	h,l		;hl=0
	dad	sp		;hl points to routine

	pop	d		;fix stack
	pop	d
	pchl			;execute RAM routine

;***Subroutine***************
;Print hl as 4 hex digits
;On Entry:
;  b=0 for the console
;  b<>0 for the Transfer Port
;  c=checksum so far
;  hl=2 bytes to print
;On Exit:
;  c=updated checksum
;Trashes a
;Stack depth: 6
;****************************
PHLHEX:	mov	a,h		;h first
	call	PAHEXC		;returns with Carry clear
	mov	a,l		;then l

	db	CPI		;CPI opcode skips PCAHEX
				;executing a NOP, and then
				;..falling into PAHEX

;***Subroutine*********************
;Print a on console as 2 hex digits
;On Entry:
;  a=byte to print
;On Exit:
;  b=0
;Trashes a,c
;Stack depth: 4
;**********************************
PCAHEX:	mvi	b,0		;print to console

;Fall into PAHEX

;***Subroutine******************************
;Print a as 2 hex digits and update checksum
;On Entry:
;  a=byte to print
;  b=0 for the console
;  b<>0 for the Transfer Port
;  c=checksum so far
;On Exit:
;  c=updated checksum
;Trashes a
;Stack depth: 4
;*******************************************
PAHEXC:	push	psw
	add	c		;compute checksum
	mov	c,a
	pop	psw		;recover character

;Fall into PAHEX

;***Subroutine***************
;Print a as 2 hex digits
;On Entry:
;  a=byte to print
;  b=0 for the console
;  b<>0 for the Transfer Port
;Trashes a
;Stack depth: 4
;****************************
PAHEX:	push	psw		;save for low digit
	
	rrc			;move the high four down
	rrc
	rrc
	rrc
	call	PNIBLE		;put them out
	pop	psw		;this time the low four

;Fall into PNIBLE

;---Local subroutine---------
;Print low nibble of a in hex
;On Entry:
;  b=0 for the console
;  b<>0 for the Transfer Port
;Trashes a
;Stack depth: 0
;----------------------------
PNIBLE:	ani	0Fh		;four on the floor
	adi	'0'		;We work with ASCII here
	cpi	'9'+1		;0-9?
	jc	PNIB1		;YUP: print & return

	adi	'A'-'9'-1	;make it a letter

PNIB1:	inr	b		;which port?
	dcr	b
	jnz	TPOUT		;print on Transfer Port

	jmp	PRINTA		;exit from there

;***Subroutine*********************************
;Print hl in hex on the console,
;preceeded by CR,LF,space, and followed by ': '
;On Exit:
;   b=0
;Trashes a,c
;Stack depth: 8
;**********************************************
PHLADR:	call	CILPRT		;CR LF space begins line
	db	' '+80h		;returns a=0

	mov	b,a		;a=0: output to console
	call	PHLCHX		;hl=address, b=0, trash c

	call	ILPRNT		;print colon space
	db	':',' '+80h
	ret

;***Subroutine******************************
;Convert ASCII hex digit to binary
;On Entry:
;  a=character to convert
;On Exit:
;  a=binary result
;  Carry set if OK, clear if bogus character
;*******************************************
HEXCON:	sui	'0'		;Remove ASCII bias
	cpi	10
	rc			;If 0-9 then we're done

	sui	9+('A'-'9')	;Should be 0-5 now
	cpi	6		;Gap chr or too high?
	rnc			;Error if so

	sui	0F6h		;Add 0AH, Set carry
	ret			;Ret with carry set


;===Assembly Check================================
; The above code must not overrun the next section
;=================================================
H2END	equ	$

 if (MRCODE - H2END)/256
	ERROR: Code in Hole 2 is too big
 endif

;=============================================================
; MBL RAM Execution Code
; All of the following code gets copied into the RAM Buffer
; (which is in the highest page of RAM that was discovered
; during initialization). this is in RAM because an IN from
; port FF (the front panel sense switches) optionally disables
; the PROM.
; On Entry:
;    d = RAM Execution page
; Entry at MRCODE:
;    e = 0 (PROM will be disabled by the upcoming IN FF)
; Entry at SKPSW:
;   Transfer Port already set up
;   PROM is still enabled
;   e = 1 if Go record should be ignored
;   e > 1 if Go record should be executed
; Entry at SKPSWF:
;   Transfer Port already set up
;   PROM is still enabled
;   c = leader character = loader length
;   e = 1 if Go record should be ignored
;   e > 1 if Go record should be executed
;=============================================================
	org MBLADR+DSKBUF	;force low address byte
				;..to be the same

;----------------------------------------------------------
;This entry is only used when running directly from the MBL
;entry address, emulating the Altair MBL code. The transfer
;port is set up according to the sense switches, and no
;further execution from ROM is allowed, since the IN FF
;might disable the ROMs.
;----------------------------------------------------------
MRCODE:	in	SSWTCH		;N: read sense switches
				;***This may disable ROM***

	ani	LDMASK		;bits specify load device

;	call	RSETP		;set up Transfer Port
	db	CALL		;call opcode
	db	RSETP-RIOCOD+RAMCOD ;low address byte
	db	MRCODE/256	;high byte (gets relocated)
SKPSW:

;----------------------------------------------
;Now that the transfer port code is installed,
;flush external data latches for e.g. the OP-80
;or flush garbage from UARTs
;On Entry & exit:
;  d = RAM execution page
;  e = 0 if PROM might be disabled
;  e = 1 if Go record should be ignored
;  e > 1 if Go record should be executed
;-----------------------------------------------
;	call	RTPIN
	db	CALL		;call opcode
	db	RTPIN-RIOCOD+RAMCOD ;low address byte
	db	MRCODE/256	;high byte (gets relocated)

;-----------------------------------------
;The first byte read must be a leader byte
;-----------------------------------------
	call 	GETBYT		;get 1st byte
	mov	c,a		;number of bytes in leader

;-----------------------------------------------------------
;Skip over leader - a sequence of identical bytes, the value
;of which is the length of the checksum loader. If the value
;is  0, then there is no loader to skip.
;On Entry:
;  a=c=leader chr = loader length
;  c=0 means there is no loader
;  d = RAM Execution page
;  The ROM may already be disabled
;On Exit:
;  c = checksum loader length
;  d = RAM execution page
;  e = 0 if PROM may be disabled
;  e = 1 if Go record should be ignored
;  e > 1 if Go record should be executed
;  The 1st byte of the checksum loader has already been read
;-----------------------------------------------------------
SKPSWF:	ora	a		;null leader?
	jz	RCHUNT		;Y: bypass skipping the loader

LDSKIP:	call	GETBYT		;get another byte
	cmp	c
	jz	LDSKIP		;loop until different

;-----------------------------------------------------------
;Skip over checksum loader
;
;On Entry:
;  the 1st byte of the checksum loader has already been read
;  c=checksum loader length
;  d = RAM execution page
;  e = 0 if PROM may be disabled
;  e = 1 if Go record should be ignored
;  e > 1 if Go record should be executed
;-----------------------------------------------------------
	dcr	c		;since we got a byte already

CLSKIP:	call	GETBYT		;get a loader byte
	dcr	c
	jnz	CLSKIP

;----------------------------------------------------------
;Main record-loading loop
;
;Hunt for a sync character - either for another load record
;or for the Go record. ignore all else.
;On Entry:
;  d = RAM execution page
;  e = 0 if PROM may be disabled
;  e = 1 if Go record should be ignored
;  e > 1 if Go record should be executed
;----------------------------------------------------------
RCHUNT:	call	GETBYT		;hunt for sync character

;Note: can't use cpi opcode here because it is FEh

	xri	ALTPLR		;load record sync byte?
	jnz	CHEKGO		;n: go see if it's a GO

;--------------------------------------------------------
;Load Record: Read and store data from a load record
;
;On Entry:
;  the load record sync byte has already been read
;  d = RAM execution page
;  e = 0 if PROM may be disabled
;  e = 1 if Go record should be ignored
;  e > 1 if Go record should be executed
;  RCHUNT's address is on the stack
;--------------------------------------------------------
	call	GETBYT		;get record byte count
	mov	c,a		;c counts data bytes

	call	GETWRD		;get load address into a,l
	mov	h,a		;hl = record load address

	add	l		;initialize checksum
	mov	b,a
	
;Loop to read c data bytes into memory at hl.
;Make sure data won't overwrite RAM Execution page.
LRLOOP:	mov	a,d		;d=RAM Execution page
	cmp	h		;same page as load address?
	mvi	a,OERMSG	;overwrite error message
	jz	ERDONE		;error exit if overwrite

	call	GETBYT		;get a data byte

	mov	m,a		;store data byte
	cmp	m		;did it store correctly?
	jnz	MERDON		;error exit if mismatch

	add	b		;compute checksum
	mov	b,a

	inx	h		;bump dest pointer
	dcr	c		;bump byte count
	jnz	LRLOOP		;loop through all bytes

;Validate checksum, fail if it doesn't match
	call	GETBYT		;test record's checksum
	cmp	b
	jz	RCHUNT		;match: get another record

	mvi	a,CERMSG	;checksum error message
	db	JZ		;skips 2 bytes

;Skip into ERDONE

MERDON:	mvi	a,MERMSG	;memory error message

;Fall into ERDONE

;------------------------------------------------------------
;Load Error:
; Turn the INTE light on as an error indicator. If the PROM
; has not been disabled (by a read from port FF), then report
; the error and return to the AMON monitor. If port FF has
; been read (to determine the load port), then save the error
; code and address at beginning of memory, and hang writing
; the error code forever to the console.
; On Entry:
;    a = error code
;    e = 0 if PROM may be disabled
;   hl = offending address
;------------------------------------------------------------
ERDONE:	dcr	e		;PROM disabled?
	jp	RPTERR		;N: report, return to monitor
				;this routine not relocated

;PROM is possibly disabled. Report error the old way.
	sta	00000h		;PROM disabled: store err code
	shld	00001H		;Store offending address

	ei			;INTE light as error indicator

ERHANG:	out 	CONTXD		;Console output
	jmp 	ERHANG

;------------------
;Test for GO record
;------------------
CHEKGO:	xri	ALTGOR XOR ALTPLR ;EOF record sync byte?
	jnz	RCHUNT		;N: ignore

;Fall into GO record execution

;-------------------------------------------
;Go Record: get the GO address and go there
;
;On Entry:
;  e = 0 if PROM may be disabled
;  e = 1 if Go record should be ignored
;  e > 1 if Go record should be executed
;  GO-record sync byte has already been read
;-------------------------------------------
	call	GETWRD		;get a,l=address
	mov	h,a		;high byte

	dcr	e		;execute go record?
	jz	PHLCHX		;n:print go address and quit	

	pchl			;go to go address

;---Local Subroutine---------------
;Get 2-byte word from Transfer Port
;On Entry:
;  b=checksum so far
;On Exit:
;  l = next byte
;  a = subsequent byte
;  b := b+a+l
;----------------------------------
GETWRD:	call	GETBYT
	mov	l,a

;Fall into GETBYT to get the high byte
	
;---Local Subroutine----------------------
;Get a byte of data from the Transfer Port
;with user-abort opportunity
;On Entry:
;  e = 0 if AMON PROM may be disabled
;On Exit:
;  a = received character
;-----------------------------------------
GETBYT:
;	call	RTPIS		;get transfer port status
	db	CALL		;call opcode
	db	RTPIS-RIOCOD+RAMCOD ;low address byte
	db	MRCODE/256	;high byte (gets relocated)

;	jnz	RTPIN		;go get transfer port data byte
	db	JNZ		;call opcode
	db	RTPIN-RIOCOD+RAMCOD ;low address byte
	db	MRCODE/256	;high byte (gets relocated)

	mov	a,e
	ora	a		;PROM certainlty enabled?
	cnz	CKABRT		;Y: user abort?

	jmp	GETBYT		;Wait for character

;===========================================
; End of MBL code copied into the RAM buffer
;===========================================
MRCEND:

;===Assembly Check===============================
; MBL code must not overwrite the CDBL code below
;================================================
SUBEND	equ	$

 if (DBLADR - SUBEND)/256
	ERROR: CDBL is overwriting prior code

 endif

;==============================================================
;=         Combo Disk boot loader Subsystem (CDBL)            =
;=        for the Altair 88-DCDD 8" disk system and           =
;=            the Altair 88-MDS Minidisk system               =
;=                                                            =
;= CDBL loads software (e.g. Altair Disk BASIC) from an       =
;= Altair 88-DCDD 8" disk or an 88-MDS 5-1/4" minidisk,       =
;= automatically detecting which kind of drive is attached.   =
;==============================================================
;=                         NOTES                              =
;=                                                            =
;= Minidisks have 16 sectors/track, numbered 0 through 15.    =
;= 8" disks have 32 sectors/track, numbered 0 through 31.     =
;= CDBL figures out which kind of disk drive is attached,     =
;= based on the existance of sector number 16.                =
;=                                                            =
;=       Altair Disk Sector Format (FOR boot sectors)         =
;=                                                            =
;=   byte(s)     FUNCTION                buffer address       =
;=     0       Track number+80h (sync)     RAMADR+7Bh         =
;=     1       file size low byte          RAMADR+7Ch         =
;=     2       file size high byte         RAMADR+7Dh         =
;=   3-130     Sector data          RAMADR+7Eh to RAMADR+FDh  =
;=    131      marker byte (0FFh)          RAMADR+FEh         =
;=    132      checksum                    RAMADR+FFh         =
;=    133-136  Spare                        not read          =
;=                                                            =
;= Each sector header contains a 16-bit file-size value:      =
;= this many bytes (rounded up to an exact sector) are read   =
;= from the disk and written to RAM, starting at address 0.   =
;= when done (assuming no errors), CDBL then jumps to         =
;= address 0 (DMAADR) to execute the loaded code.             =
;=                                                            =
;= Sectors are interleaved 2:1. CDBL reads the even sectors   =
;= on each track first (starting with track 0, sector 0)      =
;= followed by the odd sectors (starting with sector 1),      =
;= continuing through the interleaved sectors of each track   =
;= until the specified number of bytes have been read.        =
;=                                                            =
;= CDBL first reads each sector (including the actual data    =
;= payload, as well as the 3 header and the first 2 trailer   =
;= bytes) from disk into the RAM buffer (DSKBUF). next, CDBL  =
;= checks to see if this sector would overwrite the RAM       =
;= portion of Cdbl, and aborts with an 'O' error if so. it    =
;= then copies the data payload portion from the buffer to    =
;= its final RAM location, calculating the checksum along the =
;= way. During the copy, each byte is read back, to verify    =
;= correct writes. any write-verify failure will immediately  =
;= abort the load with an 'M' error.                          =
;=                                                            =
;= Any disk read error (a checksum error or an incorrect      =
;= marker byte) will cause a retry of that sector read. after =
;= 16 retries on the same sector, CDBL will abort the load    =
;= with a 'C' error.                                          =
;=                                                            =
;= If the load aborts with any error, then the CDBL subsystem =
;= print an error message with the offending address, and     =
;= jump to the AMON main loop.                                =
;=                                                            =
;= The stack gets move to the DSTACK position, to make room   =
;= for the sector buffer above it. This restricts the stack   =
;= to only 16 bytes (8 pushes) deep.                          =
;==============================================================

	org DBLADR

;==============================================================
; Entry here to execute CDBL directly, to boot from a floppy.
; This is the same address where MITS's DBL and MDBL start.
;==============================================================
CDBL:	lxi	b,GODBL		;return address
	jmp	INIT		;go find a real stack
				;..with room for a sector
				;and initialize ACIAs
;***Command Routine********
;BO (Boot from floppy disk)
;**************************
BOCMD:

;Move the stack out of the way for the disk buffer
;We don't need to fix the return address to MAIN because
;a load error willcause a JMP to INIT2, not a RET.
	call	FNDBUF
	sphl

GODBL:
;-----------------------------------------------------------
;Wait for user to insert a diskette into the drive 0, and
;then load that drive's head. Do this first so that the disk
;has plenty of time to settle. Note that a minidisk will
;always report that it is ready. Minidisks will hang (later
;on) waiting for sector 0F, until a few seconds after the
;user inserts a disk.
;-----------------------------------------------------------
WAITEN:	xra	a		;boot from disk 0
	out	DENABL		;..so enable disk 0

	call	CHKKBD		;abort from user?

	in	DSTAT		;Read drive status
	ani	DRVRDY		;Diskette in drive?
	jnz	WAITEN		;no: wait for drive ready

	mvi	a,HEDLOD	;load 8" disk head, or enable
	out	DCTRL		;..minidisk for 6.4 Sec

;-----------------------------------------------------------
;Step in once, then step out until track 0 is detected.
;The first time through, delay at least 25 ms to force a
;minimum 43 ms step wait instead of 10ms. This meets the 8"
; pec for changing seek direction. (Minidisk step time is
;always 50ms, enforced by the mninidsk conrtoller hardware.)
;See the 88-DCDD documentation for details. This loop ends
;with hl=0.
;-----------------------------------------------------------
	lxi	h,25000/12	;25 mS delay 1st time thru
	mvi	a,STEPIN	;step in once first

SEEKT0:	out	DCTRL		;issue step command

	inr	l		;After 1st time, the following
				;..loop goes 1 time.

T0DELY:	dcx	h		;(5)
	mov	a,h		;(5)
	ora	l		;(4)
	jnz	T0DELY		;(10)12 uS/pass

WSTEP:	in	DSTAT		;wait for step to complete
	rrc			;put MVHEAD bit in Carry
	rrc			;is the servo stable?
	jc	WSTEP		;no: wait for servo to settle

	ani	TRACK0/4	;Are we at track 00?
	mvi	a,STEPOT	;Step-out command
	jnz	SEEKT0		;no: step out another track

;------------------------------------------------------
;Determine if this is an 8" disk or a minidisk, and set
;c to the correct sectors/track for the detected disk.
;an 8" disk has 20h sectors, numbered 0-1Fh. a minidisk
;has 10h sectors, numbered 0-0Fh.
;------------------------------------------------------

;wait for the highest minidisk sector, sector number 0Fh
CKDSK1:	in	DSECTR		;Read the sector position

	ani	SECMSK+SVALID	;mask sector bits, and hunt
	cpi	(MDSPT-1)*2	;..for minidisk last sector
	jnz	CKDSK1		;..only while SVALID is 0

;wait for this sector to pass
CKDSK2:	in	DSECTR		;Read the sector position
	rrc			;wait for invalid sector
	jnc	CKDSK2	

;wait for and get the next sector number
CKDSK3:	in	DSECTR		;Read the sector position
	rrc			;put SVALID in Carry
	jc	CKDSK3		;wait for sector to be valid

;The next sector after sector 0Fh will be 0 for a minidisk,
;and 10h for an 8" disk. Adding MDSPT (10h) to that value
;will compute c=10h (for minidisks) or c=20h (for 8" disks).
	ani	SECMSK/2	;mask sector bits
	adi	MDSPT		;compute SPT
	mov	c,a		;..and save SPT in c

;------------------------------------------
;Set up to load
;On Entry:
;  hl = 0 (DMA address & execution address)
;  c = SPT (for either minidisk or 8" disk)
;------------------------------------------
	push	h		;exec address = 0 onto stack

	call	FNDBUF		;find hl=buffer address,
				;push DMA address = 0
	xthl			;push buffer address, recover
				;DMA address = 0

	mov	b,l		;initial sector number = 0

;------------------------------------------------------
;Read current sector over and over, until either the
;checksum is right, or there have been too many retries
;  b = current sector number
;  c = sectors/track for this kind of disk
;  hl = current DMA address
;  top-of-stack = buffer address
;  next on stack = execution address
;------------------------------------------------------
NXTSEC:	mvi	a,RETRYS	;(7)Initialize sector retries

;-----------------------------------------
;Begin Sector Read
;  a = Remaining retries for this sector
;  b = Current sector number
;  c = Sectors/track for this kind of disk
;  hl = current DMA address
;  top-of-stack = DSKBUF address
;  next on stack = execution address = 0
;-----------------------------------------
RDSECT:	pop	d		;(10)get DSKBUF address
	push	d		;(11)keep it on the stack
	push	psw		;(11)Remaining retry count

;---------------------------------------------------
;Sector Read Step 1: hunt for sector specified in b.
;Data will become avaiable 250 uS after -SVALID goes
;low. -SVALID is low for 30 uS (nominal).
;---------------------------------------------------
FNDSEC:	in	DSECTR		;(10)Read the sector position

	ani	SECMSK+SVALID	;(7)yes: mask sector bits
				;..along with -SVALID bit
	rrc			;(4)sector bits to bits <4:0>
	cmp	b		;(4)found the desired sector
				;..with -SVALID low?
	jnz	FNDSEC		;(10)no: wait for it

;-----------------------------------------------------------
;Test for DMA address that would overwrite the sector buffer
;or the stack. Do this here, while we have some time.
;-----------------------------------------------------------
	mov	a,h		;(5)high byte of DMA address
	cmp	d		;(4)high byte of RAM code addr

;Entry point for reporting an overrun error from HL command
;(Z flag is set on entry from HL.)
OVRERR:	mvi	a,OERMSG	;(7)overlay error message
	jz	RPTERR		;(10)report overlay error

;--------------------------------------
;Set up for the upcoming data move
;Do this here, while we have some time.
;--------------------------------------
	push	h		;(11)DMA address for retry
	push	b		;(11)Current sector & SPT
	lxi	b,BPS		;(10)b= init checksum,
				;c= byte count for movLUP

;-------------------------------------------------------
;Sector Read Step 2: Read sector data into DSKBUF at de.
;DSKBUF is positioned in memory such that e overflows
;exactly at the end of the buffer. Read data becomes
;available 250 uS after -SVALID becomes true (0).
;
;This loop must be << 32 uS per pass. 
;-------------------------------------------------------
DATLUP:	in	DSTAT		;(10)Read the drive status
	rlc			;(4)new Read data Available?
	jc	DATLUP		;(10)no: wait for data

	in	DDATA		;(10)Read data byte
	stax	d		;(7)store it in sector buffer
	inr	e		;(5)Move to next buffer address
				;..and test for end
	jnz	DATLUP		;(10)loop if more data

;------------------------------------------------
;Sector Read Step 3: Move sector data from DSKBUF
;into memory at hl. compute checksum as we go.
;
;8327 cycles for this section
;------------------------------------------------
	mvi	e,SDATA		 ;(7)de= address of sector data
				 ;..within the sector buffer

MOVLUP:	ldax	d		;(7)get sector buffer byte
	mov	m,a		;(7)store it at the destination
        cmp	m		;(7)Did it store correctly?
	jnz	MEMERR		;(10)no: abort w/ memory error

	add	b		;(4)update checksum
	mov	b,a		;(5)save the updated checksum

	inx	d		;(5)bump sector buffer pointer
	inx	h		;(5)bump DMA pointer
	dcr	c		;(5)more data bytes to copy?
	jnz	MOVLUP		;(10)yes: loop

;----------------------------------------------------
;Sector Read Step 4: check marker byte and compare
;computed checksum against sector's checksum. Retry/
;abort if wrong marker byte or checksum mismatch.
;On Entry and Exit:
;  a=computed checksum
;134 cycles for for this section
;----------------------------------------------------
	xchg			;(4)hl=1st trailer byte address
				;de=DMA address
	mov	c,m		;(7)get marker, should be FFh
	inr	c		;(5)c should be 0 now

	inx	h		;(5)(hl)=checksum byte
	xra	m		;(7)compare to computed cksum
	ora	c		;(4)..and test marker=ff

	pop	b		;(10)Current sector & SPT
	jnz	BADSEC		;(10)NZ: checksum error

;Compare next DMA address to the file byte count that came
;from the sector header. done of DMA address is greater.
	mvi	l,SFSIZE	;(7)hl=address of file size
	mov	a,m		;(7)low byte
	inx	h		;(5)point to high byte
	mov	h,m		;(7)high byte
	mov	l,a		;(5)hl=SFSIZE

	xchg			;(4)put DMA address back in hl
				;..and file size into de

	mov	a,l		;(4)16-bit subtraction
	sub	e		;(4)
	mov	a,h		;(5)..throw away the result
	sbb	d		;(4)..but keep Carry (borrow)

	pop	d		;(10)chuck old DMA address
	pop	d		;(10)chuck old retry count

	jnc	FDEXEC		;(10)done loading if hl >= de

;------------------------------------------------------
;Next Sector: the sectors are interleaved by two.
;Read all the even sectors first, then the odd sectors.
;
;44 cycles for the next even or next odd sector
;------------------------------------------------------
	lxi	d,NXTSEC	;(10)for compact jumps
	push	d		;(10)

	inr	b		;(5)sector = sector + 2
	inr	b		;(5)

	mov	a,b		;(5)even or odd sectors done?
	cmp	c		;(4)c=SPT
	rc			;(5/11)no: go read next sector
				;..at NXTSEC

;Total sector-to-sector = 28+8327+134+44=8533 cycles=4266.5 uS
;one 8" sector time = 5208 uS, so with 2:1 interleave, we will
;make the next sector, no problem.
	mvi	b,01H		;1st odd sector number
	rz			;Z: must read odd sectors now
				;..at NXTSEC

;------------------------------------------------------------
;Next Track: Step in, and read again.
;Don't wait for the head to be ready (-MVHEAD), since we just
;read the entire previous track. Don't need to wait for this
;step-in to complete either, because we will definitely blow
;a revolution going from the track's last sector to sector 0.
;(One revolution takes 167 mS, and one step takes a maximum
;of 40 uS.) Note that NXTRAC will repair the stack.
;------------------------------------------------------------
	mov	a,b		;STEPIN happens to be 01h
	out	DCTRL

	dcr	b		;start with b=0 for sector 0
	ret			;go to NXTSEC

;-------------------------------------------------
;Execute successfully loaded code, after disabling
;the floppy drive and disabling the PROM
;On Entry:
;  Top of stack = DSKBUF address
;  Next on stack = execution address
;-------------------------------------------------
FDEXEC:	mvi	a,DDISBL	;Disable floppy controller
	out	DENABL

	pop	d		;chuck DSKBUF address
				;..to expose exec address

	jmp	EXECDP		;disable PROM and execute code

;***Error Routine********************************************
;Checksum error: attempt retry if not too many retries
;already. Otherwise, abort, reporting the error 
;On Entry:
;  Top of stack = adress for first byte of the failing sector
;  next on stack = retry count
;************************************************************
BADSEC:	mvi	a,HEDLOD	;Restart Minidisk 6.4 uS timer
	out	DCTRL

	pop	h		;Restore DMA address
	pop	psw		;get retry count
	dcr	a		;Any more retries left?
	jnz	RDSECT		;yes: try reading it again

;----------------------------------------------------
;Irrecoverable error in one sector: too many retries.
;these errors may be either incorrect marker bytes,
;wrong checksums, or a combination of both.
;On Entry:
;  hl=RAM adress for first byte of the failing sector
;  sp = valid address in RAM page
;----------------------------------------------------
CSMERR:	mvi	a,CERMSG	;checksum error message
	db	11h		;'lxi d' opcode to skip
				;..MEMERR and go to RPTERR

;Skip into RPTERR

;***Error Routine********************
;Memory error: memory readback failed
;On Entry:
;  hl = offending RAM address
;  sp = valid address in RAM page
;************************************
MEMERR:	mvi	a,MERMSG	;memory error message

;Fall into RPTERR

;***CDBL (and MBL) Termination*************************
;Report an error: turn the disk controller off, report
;the error on the console, Turn on the INTE light, jump
;to the console loop.
;On Entry:
;  a = ASCII error code
;  hl = offending RAM address
;  sp = valid address in RAM page
;******************************************************
RPTERR:	call	PRINTA		;print the ASCII error code

	mvi	a,DDISBL	;Disable floppy controller
	out	DENABL

;Fall into HDERR

;***HDBL Termination***********************************
;Report an error: report the error on the console, Turn
;on the INTE light, jump to the console loop.
;On Entry:
;  a = error code
;  hl = offending RAM address or HDSK command
;  sp = valid address in RAM page
;******************************************************
HDERR:	call	ILPRNT
	db	' error:',' '+80h
	call	PHLCHX		;print hl in hex on console

;Cool-start AMON code
	ei			;INTE light on (indicate error)
	jmp	INIT2		;go to monitor

;***Subroutine*******************
;Find the DSKBUF address
;On Entry:
;  sp = valid address in RAM page
;On Exit:
;  hl = RAM page item address
;  prior hl value is on the stack
;  Carry is clear
;Trashes a
;********************************
FNDBUF:	mvi	a,DSKBUF

;Fall into HRMPAG

;***Subroutine**********************
;Set hl to location within RAM page
;On Entry:
;  a = address offset into RAM page
;  sp = valid address in RAM page
;On Exit:
;  hl = RAM page item address
;  prior hl value is on the stack
;  Carry is clear
;  other flags unafffected
;Stack depth: 2
;***********************************
HRMPAG:	xthl			;save hl, get return address
	push	h		;restore return address

;Fall into RAMPAG

;***Subroutine**********************
;Set hl to location within RAM page
;On Entry:
;  a = address offset into RAM page
;  sp = valid address in RAM page
;On Exit:
;  hl = RAM page item address
;  Carry is clear
;  other flags unafffected
;***********************************
RAMPAG:	lxi	h,0
	dad	sp		;get RAM page, clear carry
	mov	l,a		;requested RAM address
	ret

;***Subroutine*************************************
;Get a hex value from the line buffer
;Abort to CMDERR if none provided
;On Entry:
;  de=address of next item in the input line buffer
;On Exit:
;  hl=value
;  de advanced past character
;  top-of-stack = prior hl value
;  abort to CMDERR if no value found
;**************************************************
GETHEX:	xthl			;save hl, put ret address in hl
	call	PHFHEX		;save hl, get hl=hex value
	rnc

;Fall into CMDERR if no value

;*********************
;Commnd error handler
;**********************
CMDERR:	call	CILPRT		;returns Z flag cleared
	db	'?'+80h

	jmp	MAIN		;Repairs stack

;===Assembly Check================
; All of CDBL and the subsequent
; subroutines must fit in one page
;=================================
DBLEND	equ	$

 if (DBLEND - DBLADR)/256
	ERROR: CDBL does not fit in a single page

 endif
	end

