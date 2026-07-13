;==============================================================
;= Altair Minidisk Boot Loader                                =
;= (Disassembled from Altair EPROMs)                          =
;=                                                            =
;= Loads software (e.g. Minidisk BASIC) from the Altair       =
;= Minidisk subsystem. Execution is at FF00h, and the sense   =
;= switches must be set of the correct Terminal port, as      =
;= described in the MDBL manual (which is the same as Altair  =
;= BASIC 4.X Terminal port switch settings).                  =
;=                                                            =
;= REVISION HISTORY                                           =
;=                                                            =
;=  VERS.    DATE     AUTHOR          NOTES                   =
;=  4.1?  05May2014  M. Eberhard   Disassembled from MITS     =
;=                                                            =
;= Because of the slow 1702A EPROM access time, this program  =
;= first copies itself into RAM at address 4C00 (RUNLOC),     =
;= and then executes there.                                   =
;=                                                            =
;= Once in RAM, this program reads from disk, starting at     =
;= track 00, sector 00. Sector data (which includes the       =
;= actual data payload, as well as header and trailer bytes)  =
;= is first loaded into a RAM buffer (SECBUF) that is located =
;= just after the RAM version of this program. The data       =
;= payload then gets moved to its final RAM location,         =
;= starting at 0000h (DMAADR), checking the checksum along    =
;= the way.                                                   =
;=                                                            =
;= Each sector has a 16-bit value in its header, which is the =
;= byte count for the file being loaded: this many bytes are  =
;= read from the disk and written to RAM. When done (assuming =
;= no errors), this program then jumps to 0000h (DMAADR) to   =
;= execute the loaded code.                                   =
;=                                                            =
;= Sectors are interleaved 2:1 on the disk. The even sectors  =
;= are read first, followed by the odd sectors.               =
;=                                                            =
;= When data is moved from the RAM buffer to its final RAM    =
;= locatation, it is also read back, to verify correct writes.=
;= Any write-verify failure will terminate the load with an   =
;= 'M' error.                                                 =
;=                                                            =
;= Any read errors (either a checksum error or an incorrect   =
;= marker byte) will cause a retry of that sector read. After =
;= 10h retries, the load will terminate with a 'C' error.     =
;=                                                            =
;= If the load terminates with an error, then it will also    =
;= turn on the INTE LED on the front panel, as an indicator.  =
;=                                                            =
;= DISK SECTOR FORMAT                                         =
;=                                                            =
;=   BYTE(s)     FUNCTION              BUFFER ADDRESS         =
;=     0       Track number+80h          RAMCOD+EBh           =
;=     1       File size low byte        RAMCOD+ECh           =
;=     2       File size High Byte       RAMCOD+EDh           =
;=   3-130     Sector data               RAMCOD+EEh           =
;=    131      Checksum Marker (0FFh)    RAMCOD+16Eh          =
;=    132      Checksum                  RAMCOD+16Fh          =
;=    133      Spare read at end (?)     RAMCOD+170h          =
;==============================================================
;---------------------
; Minidisk Parameters
;---------------------
SPT	equ	16		;sectors/track
SECSIZ	equ	128		;data bytes/sector
RETRYS	equ	16		;max retries 

;-------------------
; Memory Parameters
;-------------------
DMAADR	equ	0		;Disk load address
PROMAD	equ	0FF00H		;Address of MDBL EPROM
RUNLOC	equ	4C00H		;RAM Execution address of MDBL
SSIZE	equ	8		;stack size

;-----------------------------------------------------------
; Sense Switches - either a front panel or a Turnkey Module
;-----------------------------------------------------------
SWTCHS	equ	0FFH		;Sense Switches

;---------------
;88-SIO Equates	
;---------------
SIOCTL	EQU	00		;Control
SIOSTA	EQU	00		;Status
SIODAT	EQU	01		;RX/TX Data

;-----------------------------------------------
; 88-2SIO port 0, Turnkey Module, and 88-UIO
; Equates (all based on the Motorola 6850 ACIA)
;-----------------------------------------------

ACCTRL	equ	10h		;ACIA Control output port
ACSTAT	equ	10h		;ACIA Status input port
ACDATA	equ	11h		;ACIA TX/RX Data register

ACRST	equ	00000011b	;Master reset
ACRDF	equ	00000001b	;RX Data register full
ACTDE	equ	00000010b	;TX Data register empty
ACINIT	equ	00010001b	;/16, 8bit, No Parity, 2Stops

;---------------
;88-PIO Equates
;---------------	

PIOCTL	EQU	04		;Control
PIOSTA	EQU	04		;Status
PIODAT	EQU	05		;TX/RX Data

;----------------
;88-4PIO equates	
;----------------
P4CA0	equ	20H		;Port 0 Section A Ctrl/Status
P4DA0	equ	21H		;Port 0 Section A Data
P4CB0	equ	22H		;Port 0 Section B Ctrl/Status
P4DB0	equ	23H		;Port 0 Section B Data

;-----------------------------------------
; Altair 8800 Minidisk Controller Equates
;-----------------------------------------
MDENBL	equ	08H		;Drive Enable output
DDISBL	  equ	  80h		  ;Disable all disks

MDSTAT	equ	08H		;Status input (active low)
ENWDAT	  equ	  01H		  ;-Enter Write Data
MVHEAD	  equ	  02H		  ;-Move Head
HDSTAT	  equ	  04H		  ;-Head Status
DRVRDY	  equ	  08H		  ;-Drive Ready
INTSTA	  equ	  20H		  ;-Interrupts Enabled
TRACK0	  equ	  40H		  ;-Track 0 detected
NRDA	  equ	  80H		  ;-New Read Data Available

MDCTRL	equ	09H		  ;Drive Control output
STEPIN	  equ	  01H		  ;Step-In
STPOUT	  equ	  02H		  ;Step-Out
TMRSET	  equ	  04H		  ;Reset 6.4 sec disable timer
IENABL	  equ	  10H		  ;Enable sector interrupt
IDSABL	  equ	  20H		  ;Disable interrupts
WENABL	  equ	  80H		  ;Enable drive write circuits

MDSECT	equ	09H		;Sector Position input
SECVAL	  equ	  01h		  ;Sector Valid (1st 30 uS
				  ;..of sector pulse)
SECMSK	  equ	  3Eh		  ;Sector mask for MDSEC

MDDATA	equ	0AH		;Disk Data (input/output)

;--------------------------------------------------------------
;SINGLE-BYTE ERROR MESSAGES
;--------------------------------------------------------------
CERMSG	equ	'C'	;checksum error
MERMSG	equ	'M'	;memory error

;==============================================================
	ORG	PROMAD
;==============================================================

;--------------------------------------------------------------
; Copy MDBL code image into RAM and run it from there, because
; 1702A EPROMs are too slow to keep up with the disk data.
;--------------------------------------------------------------
	lxi	h,RCODE		;HL= ROM address of MDBL image
	lxi	d,RUNLOC	;DE= RAM execution address
	mvi	c,MOVSIZ	;number of bytes to move

MLOOP:	mov	a,m		;Get next EPROM byte
	stax	d		;Store it in RAM
	inx	h		;Bump EPROM address
	inx	d		;Bump RAM address
	dcr	c		;More program data to copy?
	jnz	MLOOP		;yes: loop

	jmp	RCODE-OFFSET	;no: go run the loaded code

;==============================================================
; RAM Code Image
; All of the following code gets copied into RAM and run there.
;==============================================================
OFFSET	equ	$-RUNLOC	;address offset for moved code

RCODE:	di			;Turn off INTE LED
				;(i.e. no errors yet)

;---------------------------------------------
; Initialize all known I/O ports (or at least
; the ones that need initialization).
;---------------------------------------------

	xra	a		;Initialize 4PIO
	out	P4CB0		  ;Port 0 section B is output
	cma			  ;All output bits high
	out	P4DB0 
	mvi	a,2CH		  ;set up handshake bits
	out	P4CB0

	mvi	a,ACRST		;Initialize 2SIO/Turnkey/UIO
	out	ACCTRL		  ;reset first

	in	SWTCHS		  ;Read the sense switches
	ani	ACINIT		  ;..to get stop bits
	rrc			  ;put bit in place for ACIA
	rrc
	adi	10H		  ;isolate bit
	out	ACCTRL		  ;set stop bits accordingly

;------------------------------------
; Wait for user to insert a diskette
;------------------------------------
	lxi	sp,STACK-OFFSET	;Initialize the stack

WAITEN:	xra	a		;Enable drive zero
	out	MDENBL
	in	MDSTAT		;Read drive status
	ani	DRVRDY		;Diskette in drive?
	jnz	WAITEN-OFFSET	;no: wait for drive ready

;--------------
; Seek track 0
;--------------
	mvi	a,TMRSET	;Reset the 6.4 second timer
	out	MDCTRL
	jmp	CHK00-OFFSET	;test for track 0 1st

SEEK00:	in	MDSTAT		;Allowed to move disk head?
	ani	MVHEAD
	jnz	SEEK00-OFFSET	;no: wait for head to settle

	mvi	a,STPOUT	;Issue STEP-OUT command
	out	MDCTRL

CHK00:	in	MDSTAT		;Read drive status
	ani	TRACK0		;Are we at track 00?
	jnz	SEEK00-OFFSET	;no: step-out again

;--------------------------------------------------------
; Load disk data into memory, starting at address DMAADR
; Each sector has the file size in its header. Done when
; the next address is higher than the file size.
;--------------------------------------------------------
	lxi	d,DMAADR	;DE = load address

;------------------------------------------
; Read in all sectors on the current track
;------------------------------------------
NXTRAC:	mvi	b,0		;B = Sector to load

;---------------------------------------------------------
; Read the current sector over and over, until either the
; checksum is right, or there have been too many retries
;---------------------------------------------------------
NXTSEC:	mvi	a,RETRYS	;Initialize sector retries

;----------------------------------------------------------
; Begin Sector Read: Read three header bytes, plus 128 data
; bytes (with checksum), plus three trailer bytes from the
; current sector, into the sector buffer.
;----------------------------------------------------------
RDSECT:	push	psw		;Retry count remaining
	push	d		;Load address
	push	b		;Current sector

	push	d		;Load address
	lxi	d,SECSIZ*256+86h ;E= bytes/sector
				 ; + header & trailer bytes
				 ;(unnecessary load of d)

	lxi	h,SECBUF-OFFSET	;hl= Sector buffer address

;---------------------------------------
; Sector Read: first, Hunt for sector b
;---------------------------------------
FNDSEC:	in	MDSECT		;Read the sector position
	rar			;Put SECVAL bit into carry
	jc	FNDSEC-OFFSET	;Sector value valid?

	ani	SECMSK/2	;yes: Mask sector bits
	cmp	b		;Found the desired sector?
	jnz	FNDSEC-OFFSET	;no: wait for it

;---------------------------------------------------------
; Sector Read: Second, read sector data into SECBUF at hl
;---------------------------------------------------------
DWAIT:	in	MDSTAT		;Read the drive status
	ora	a		;New Read Data Available?
	jm	DWAIT-OFFSET	;no: wait for data

	in	MDDATA		;Read data byte
	mov	m,a		;Store it in the sector buffer
	inx	h		;Move to next buffer address
	dcr	e		;bump byte count
	jnz	DWAIT-OFFSET	;Loop if more data

;------------------------------------------------------------
; Sector Read: Third, move the sector data from SECBUF, into
;              memory. The next memory address is on the
;              stack. Compute the sector checksum as we go.
;------------------------------------------------------------
	pop	h		;Get current load address
	lxi	d,SECBUF+3-OFFSET ;de= address of sector data
				  ;..within the sector buffer
	lxi	b,SECSIZ	 ;b= Checksum, c= byte count

MOVLUP:	ldax	d		;Get sector buffer byte
	mov	m,a		;Store it at the destination
        cmp	m		;Did it store correctly?
	jnz	MEMERR-OFFSET	;no: abort with memory error

	add	b		;update checksum
	mov	b,a		;Save the updated checksum
	inx	d		;Bump sector buffer pointer
	inx	h		;Bump destination pointer
	dcr	c		;More data bytes to copy?
	jnz	MOVLUP-OFFSET	;yes: loop

;------------------------------------------------------------
; Sector Read: fourth, compare computed checksum against
;              sector's checksum and retry/abort if mismatch
;------------------------------------------------------------
	ldax	d		;Marker byte follows sec data
	cpi	0FFH		;Is it a checksum marker?
	jnz	RDDONE-OFFSET	;no: handle bad checksum

	inx	d		;checksum follows marker byte
	ldax	d		;Get the sector's checksum
	cmp	b		;Compare to computed checksum

RDDONE:	pop	b		;Sector to load
	xchg			;de gets current load address
	jnz	BADSEC-OFFSET	;NZ: checksum error

	pop	psw		;Clean up stack
	pop	psw

        lhld	SECBUF+1-OFFSET	;hl=File size

	call	CMP16-OFFSET	;compare to next memory address
	jnc	LDDONE-OFFSET	;done loading if de >= hl

;------------------------------------------------------------
; Next sector: The sectors are interleaved by two. Read all
;              the even sectors first, then the odd sectors.
;------------------------------------------------------------
	inr	b		;Our interleave is 2
	inr	b		;..so bump sector by 2
	mov	a,b		;Have we read 1/2 of the track?
	cpi	SPT
	jc	NXTSEC-OFFSET	;no: go read next sector

	mvi	b,01H		;1st odd sector number
	jz	NXTSEC-OFFSET	;Z: must read odd sectors now

;-------------------------------------
; Next Track: Step in, and read again
;-------------------------------------
WAITHD:	in	MDSTAT		;Can we move the head now?
	ani	MVHEAD
	jnz	WAITHD-OFFSET	;no: wait for head to settle

	mvi	a,STEPIN	;step to the next track
	out	MDCTRL
	jmp	NXTRAC-OFFSET	;And go read it in

;----------------------------------------------
; Done loading: Disable the disk controller,
;               and go execute the loaded code
;----------------------------------------------
LDDONE:	mvi	a,DDISBL	;Disable the Minidisk system
	out	MDENBL
	jmp	DMAADR		;Execute the loaded program

;--------------------------------------------------------
; Checksum error: attempt retry if not too many already.
;                 Otherwise, abort, reporting the error 
;--------------------------------------------------------
BADSEC:	pop	d		;Current sector load address
	pop	psw		;Current retry count
	dcr	a		;Any more retries left?
	jnz	RDSECT-OFFSET	;yes: try reading it again

;----------------------------------------------------------
; Irrecoverable checksum error in sector: too many retries
;----------------------------------------------------------
	mvi	a,CERMSG	;no: Signal Checksum error
	db	01H		;Hack to ignore next opcode
				;to go to RPTERR
;--------------------------------------
; Memory error: memory readback failed
;--------------------------------------
MEMERR:	mvi	a,MERMSG	;Signal Memory Error

; Fall into RPTERR

;---------------------------------------------------------
; Report an error: turn the INTE light on, turn the disk
; controller off, and then loop forever writing the error
; code (in register a) to all known Terminal ports
;---------------------------------------------------------
RPTERR:	ei			;Signal error on the INTE LED
	sta	0		;Store the error code
	shld	1		;Store the bad address
	mov	b,a		;Save the error code
	mvi	a,DDISBL	;Disable the Minidisk system
	out	MDENBL
	mov	a,b		;recover the error code

ERHANG:	out	SIODAT		;SIO
	out	ACDATA		;2SIO/Turnkey/UIO
	out	PIODAT		;PIO
	out	P4DB0		;4PIO
	jmp	ERHANG-OFFSET	;Keep printing error code

;***Subroutine*******************
; Compare de to hl
;
; On Exit:
; Z flag set if DE = HL
; C flag set if DE < HL
;********************************
CMP16:	mov	a,d		;Get the MSB of DE
	cmp	h		;Is it equal to the MSB of HL?
	rnz			;no: return

	mov	a,e		;Get the LSB of DE
	cmp	l		;Is it equal to the LSB of HL?
	ret			;Return with flags set

MOVSIZ	equ	$-RCODE	;# of bytes to move into RAM

;==============================================================
; Sector buffer, holds sector data and metadata
;==============================================================
SECBUF:	db	0,0,0,0,0	;fill to end of EPROM
	db	0,0,0,0,0
	ds	SECSIZ+6-10	;account for 10 NOPs above too

;==============================================================
; Stack
;==============================================================
	ds	SSIZE		;space for stack
STACK:
           
            END


