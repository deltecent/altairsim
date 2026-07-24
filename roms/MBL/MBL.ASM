; MBL - Multi Boot Loader for the Altair 8800
;
; Disaasembled and commented by Geoff Harrison (ghov solivant com)
;
; This is a boot loader that can be loacated in ROM and which
; can read any of the punched tape formats that MITS designed.
; It runs out of ROM by creating a readbyte routine in RAM which
; is customized for whatever I/O board the user specifies at
; run time on the front panel switches. It then skips over the
; leadin bytes and the second stage loader which always preceed
; the payload on a MITS tape.  Since it doesn't use the
; second stage loader, it doesn't care which of several possible
; versions of tape it is loading.  Normally you have to be sure
; to use the correct 1st stage loader for the tape you are loading,
; with the MBL it doesn't matter.  After skipping the 2nd stage
; loader on the tape, it starts reading and storing payload
; bytes and checking that the checksum is correct for each packet
; of bytes read.  When the entire payload has been read into memory,
; it reads the entry point for the program from the tape and jumps
; to that location.
;
; The format of a MITS paper tape is:
;	- some number of identical bytes, each containing
;		the length (n) of the stage2 loader
;	- n bytes of stage 2 loader
;	- optionally some number of nulls
;	- one or more load records (see below)
;	- an EOF record (see below)
;
; A load record consists of:
;       074     - sync byte (hex 03cH)
;       NNN     - # bytes in record
;       lll     - low byte of load address
;       hhh     - high byte of load address
;   <NNN bytes> - NNN bytes of program data (the payload)
;       ccc     - checksum byte
;                (checksum includes lll & hhh)
; There will be 1 or more prog load records,
; each one with up to 256 bytes of payload
; bytes.
;
; The EOF record consists of:
;       0170    - sync byte (hex 078H)
;       lll     - low byte of start address
;       hhh     - high byte of start address
;
; Before executing this program, the front panel sense switches
; must be set to indicate what device to read the tape from and
; what device the terminal is attached to.  This program uses
; A11..A8 to determine the device attached to the tape reader.
; If the payload is not earlier than Basic 4.0, it uses A15..A12
; to determine where the terminal device is.
; Possible switch values are
;
; Device                Switch value
; 2SIO (2 stop bits)      0000b
; 2SIO (1 stop bit)       0001b
; SIO                     0010b
; ACR                     0011b
; 4PIO                    0100b
; PIO                     0101b
; HSR                     0110b
;
; If a value larger than 7 is entered this program will return
; an error.
;
; Prior to Basic 4.0, MITS used different sense switch settings
; to specify the terminal device.  You should still be able to
; load an older tape with this loader by setting the switches as
; above, stopping the program after it loads and starts, changing
; the sense switches (see the appropriate MITS manual for switch
; settings) and restarting the program at its entry point (e.g.
; 00000H for Basic 3.2).
; 
; This listing was generated from a hex dump of MBL provided by
; Grant Stockly.  The assembly code and comments were reverse
; engineered from that dump by Geoff Harrison.

; Default location is at 0fe00H.  May be assembled to run
; at any location.
;
; Assembler syntax is for the spasm assembler.  To assemble use:
;      spasm mbl /F
;      slink mbl=mbl.obj /c:0fe00
;
; For other assemblers, uncomment the following line and
; (possibly) change the syntax of org, db, etc.
;
		org 0fe00H

;----------------------------------------------------------
; We don't know what I/O cards are in the system, so
; try to initialize just about anything MITS had available
; at the time.
;
		di
		xra a
		out 020H
		out 021H
		out 024H
		out 025H
		out 026H
		out 022H
		cma
		out 023H
		out 027H
		mvi a,00cH
		out 024H
		mvi a,02cH
		out 020H
		out 022H
		out 026H
		mvi a,003H
		out 027H
		out 010H
		mvi a,011H
		out 010H

;----------------------------------------------------------
; Find top of RAM.  Assumes that there is not 64k of RAM
; available, a reasonable assumption given that this routine
; resides in ROM somewhere at the top of memory.
;
		lxi h, 0ffffH
scanram:	inx h		; point @ next byte
		mov a,m		; get it
		mov b,a		; save it
		cma		; invert it
		mov m,a		; try to write it back out
		cmp m		; see if it wrote out correctly
		mov m,b		; restore the byte to its orig value
		jz scanram 	; loop if the byte wrote correctly

		; Something's wrong if the failure to write to RAM
		; happened on other than a page boundary
		xra a		; a = 0
		cmp l
		jnz merror 	; if L not zero, go to 'M' error handler

		; HL now points to the last writable byte in RAM plus 1.
		; Subtract 14 to make room for a stack & initialize SP.
		lxi b,0fff2H
		dad b		; HL += BC
		sphl		; SP = HL

		dad b		; Set HL to point to what will be
				; the start of the readbyte routine.
		push h		; save the start address on the stack.

		in 0ffH		; read front panel switches
		ani 00fH	; mask lower 4 bits
		cpi 007H	; switches set to >= 7?
		jp ierror 	; y - go to 'I' error handler

		; point HL at the Nth entry in the mod table,
		; where N = the front panel switch setting
		lxi h, table1	; point HL to start of the mod table
		inr b		; B currently contains 0ffH, inc it to 0
		mov c,a		; \
		add a		;  \ C = A * 3
		add c		;  /
		mov c,a		; /
		dad b		; HL += BC

;----------------------------------------------------------
; Construct an input function on the stack.
; Since this is a push down stack, the routine is built in reverse.
; When finished, the routine will look like this. (XX values are
; calculated on the fly or read from the mod table.  The routine
; has to be built dynamically like this because different I/O cards
; require different code to read their data and status.  The user
; specified which card is in use on the front panel switches.)
; readbyte:
;	db xx		in xx		; check port status
;	e6 xx		ani xx
;	xx LO HI	jz/jnz readbyte	; loop 'till byte arrives
;	db xx		in xx		; get input byte
;	f5		push psw	; save it
;	80		add b		; update checksum in B
;	47		mov b,a
;	f1		pop psw		; retrieve input byte
;	c9		ret		; return A=byte, B=checksum
;
		pop d		; DE = the start adress of this routine

		; construct RET; POP PSW
		lxi b,0c9f1H
		push b

		; construct MOV B,A; ADD B
		lxi b,04780H
		push b

		; construct  PUSH PSW; {input port retrieved from mod table}
		mvi b,0f5H
		mov c,m
		mov a,c
		push b

		; construct IN (used with port constructed above); {high byte of jmp instr}
		mvi b,0dbH
		mov c,d
		push b

		; construct {low byte of jmp}; JZ/JNZ (from mod table)
		mov b,e
		inx h
		mov c,m
		push b

		; construct ANI {mask from mod table}
		inx h
		mov b,m
		mvi c,0e6H
		push b

		; construct IN {port status address (port number - 1)}
		dcr a
		mov b,a
		mvi c,0dbH
		push b
;----------------------------------------------------------
; Read a stream of input bytes.
;
		xchg		; HL = start of readbyte routine, DE = junk

		; ???????????????????????????????????????????????????
		mvi a,004H	; Why are we sending 04H to device 027H?
		out 027H	; In fact, what is device 027H? Anyone know? Is it the
				; 88-HSR?  Perhaps we're switching on the drive motor.
		; ???????????????????????????????????????????????????

		; Skip over leadin bytes.
		call link	; flush input buffer
		call link	; get a byte
		mov c,a		; remember it
leadinskip:	call link	; get another byte
		cmp c
		jz leadinskip	; loop until we receive a different byte value

		; At this point, C contains the first leadin byte
		; that was on the tape, which should represent the
		; length of the stage 2 loader.
		; Skip over stage 2 loader.
		dcr c
stage2skip:	call link	; get a byte
		dcr c
		jnz stage2skip	; loop while C > 0

		; Now we've skipped over all the unneeded stuff, start
		; looking for the first load record.
findtoken:	call link	; get a byte
		cpi 03cH	; is it a load record token?
		jz loadrecord	; Y - go and process a load record
		cpi 078H	; is it an EOF token?
		jnz findtoken	; N - keep looking

		; Got an EOF token.  Read next two bytes as program
		; start address and jump to that address.
eof:		call link	; get prog start low
		mov c,a
		call link	; get prog start high
		mov l,c
		mov h,a

		; This is mostly used to call the byte input routine
		; on the stack, but is also used to jump to the entry
		; point of the downloaded program.
link:		pchl		; jmp to (HL)

		; Process a load record
loadrecord:	call link	; get byte count in this record
		mov c,a		; save it
		mvi b,000H	; initialize checksum
		call link	; get load address low
		mov e,a
		call link	; get load address high
		mov d,a

loadrec2:	mov a,d
		cmp h		; Are the incoming bytes about to
				;    overwrite the readbyte routine?
		mvi a,04fH	; prepare to send 'O', if necessary
		jz errexit	; Y - error exit
		call link	; N - get another byte
		xchg		; HL = dest pointer, DE = start of readbyte
		mov m,a		; store received byte at destination
		cmp m		; did it store correctly?
merror:		mvi a,04dH	; prepare to send 'M', if necessary
		jnz errexit	; N - error exit
		inx h		; Y - increment destination pointer
		xchg		; HL = start of readbyte, DE = dest pointer
		dcr c		; decrement byte counter
		jnz loadrec2	; if not 0 then loop to get more bytes

		mov c,b		; save calculated checksum value before we update it again
		call link	; get expected checksum from input stream
		cmp c		; does it match our calculated sum?
		jz findtoken	; Y - look for more records on the tape
		mvi a,043H      ; N - prepare to send 'C' error
		db 001H		; lxi - hides next two bytes
ierror:		db 03eH		; mvi a, 049H	; prepare to send 'I'
		db 049H

; Store an error code and the address @ which it happened in
; the first 3 bytes of RAM, then loop forever sending the
; error code to all possible terminal devices.
;
errexit:	sta 00000H
		shld 00001H
		ei
forever:	out 001H
		out 011H
		out 005H
		out 023H
		jmp forever

;----------------------------------------------------------
; Mod table.
; This is a table of port addresses, commands for checking
; the port status (jz/jnz), and status masks for several
; I/O boards.  The values are stuffed into the readbyte
; routine at runtime to customize it for the hardware
; being used to load the tape.
;
table1:		db	011H, 0CAH, 001H	; 2SIO (2 stop bits)
		db	011H, 0CAH, 001H	; 2SIO (1 stop bit)
		db	001H, 0C2H, 001H	; SIO
		db	007H, 0C2H, 001H	; ACR
		db	021H, 0CAH, 080H	; 4PIO
		db	005H, 0CAH, 002H	; PIO
		db	025H, 0CAH, 040H	; HSR
