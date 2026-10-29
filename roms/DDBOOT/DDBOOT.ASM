;******************************************************
;						      *
;	PROGRAM ID:     DOUBLE D BOOTSTRAP (DDBOOT)   *
;						      *
;******************************************************
;						      *
;	VERSION:	CP/M  2.2    RELEASE 2	      *
;						      *
;******************************************************
;						      *
;	DISTRIBUTOR:	JADE COMPUTER PRODUCTS	      *
;			4901 W. ROSECRANS BLVD.	      *
;			HAWTHORNE, CALIFORNIA	      *
;			90250,   U.S.A.		      *
;						      *
;************************************************SK****


;******************************************************
; THE DOUBLE D BOOTSTRAP PROGRAM  (DDBOOT) IS USED TO *
; INITIATE THE SYSTEM TRACKS LOAD SEQUENCE FROM DRIVE *
; A (OR 0) AND TO PROVIDE CONSOLE I/0 SUBROUTINES FOR *
; THE DISK OPERATING SYSTEM (CP/M).  THIS PROM SHOULD *
; BE LOCATED AT F000 HEX.  THE SOURCE CODE FOR DDBOOT *
; CAN BE  ASSEMBLED  WITH  DIGITAL RESEARCH ASSEMBLER *
; ASM.COM.  MACHINE CODE IS  8080/8085/Z80 COMPATABLE *
;******************************************************


;******************************************************
; DDBOOT INJECTION MODULE IS COMMAND  COMPATABLE WITH *
; THE FOLLOWING  WESTERN  DIGITAL  CONTROLLER  CHIPS. *
; DOUBLE D  USER SWITCH 0  (U0 OR R0)  MUST BE SET TO *
; INDICATE THE  CONTROLLER CHIP  DATA  BUS  POLARITY. *
;******************************************************
;	CONTROLLER IC    	USER SW0	      *
;	-------------    	--------	      *
;	FD1791-02 (01)    	 CLOSED		      *
;	FD1793-02 (01)    	 OPENED		      *
;	FD1795-02         	 CLOSED		      *
;	FD1797-02         	 OPENED		      *
;******************************************************
; THE FD1795-02 AND FD1797-02 PROVIDE ENHANCED SINGLE *
; DENSITY PERFORMANCE IN THAT  THESE  CHIPS ARE FULLY *
; COMPATABLE WITH FD1771-01 3740 FORMATS.	      *
;******************************************************


;******************************************************
; CBIOS SCRATCH   ******   SYSTEM MEMORY ALLOCATION   *
;******************************************************
;  ADDRESS	NAME	  FUNCTION		      *
;  -------	----	  --------		      *
;  0040-41	D$ADDR	  ADDRESS POINTER TO DOUBLE D *
;  0042		D$MASK	  STATUS PORT HALT BIT MASK   *
;  0043		D$TEMP	  TEMPORARY, INSERT DISK MSG  *
;******************************************************


;******************************************************
; MODIFICATION MAY BE NEEDED.  THIS PROM PROVIDES THE *
; CONSOLE STATUS, INPUT, AND OUTPUT SUBROUTINES. MANY *
; END USER SYSTEMS REQUIRE UART / USART AND BAUD RATE *
; GENERATOR INITIALIZATION. THESE  ROUTINES  MAY NEED *
; TO BE PATCHED TO PROVIDE FOR PROPER CONSOLE LINKAGE *
; PATCHING MAY ALSO BE DONE FOR SOME DISK DRIVES.     *
;******************************************************
;      PROM LOCATIONS THAT MAY NEED PATCHING	      *
;******************************************************
; SYSTEM INITIALIZATION - UART/USART, BAUD RATE, AND  *
; AND POWER ON JUMP CIRCUITRY MAY REQUIRE SOFTWARE    *
; INITIALIZATION. A PATCH AREA IS RESERVED AT "INIT"  *
;******************************************************
; CONSOLE STATUS CHECK - RETURNS KEYBOARD STATUS TO   *
; CP/M OPERATING SYSTEM.  THIS SUBROUTINE MUST USE    *
; THE CORRECT PORT ADDRESS AND TEST PROPER STATUS BIT.*
; ROUTINE IS LABELED "CNS$CK" AND CONTAINS PATCH AREA.*
;******************************************************
; CONSOLE INPUT - RETURNS KEYBOARD CHARACTER TO CP/M  *
; OPERATING SYSTEM.  THIS ROUTINE MUST ALSO USE THE   *
; CORRECT PORT ADDRESSING.   THIS ROUTINE IS LABELED  *
; "CNS$IN" AND CONTAINS A PATCH AREA.		      *
;******************************************************
; CONSOLE OUTPUT - DISPLAYS CHARACTER TO CONSOLE UNIT.*
; THIS ROUTINE MUST USE THE CORRECT PORT ADDRESS FOR  *
; BOTH THE OUTPUT STATUS AND OUTPUT DATA PORTS.  THIS *
; ROUTINE MUST TEST THE CONSOLE OUTPUT STATUS BIT.    *
; THIS ROUTINE IS LABELED  "CNS$OT"  AND CONTAINS A   *
; PATCH AREA.					      *
;******************************************************
; BOARD REVISION - NOTE SOURCE FILE HAS CONDITIONAL   *
; STATEMENTS FOR BOARD REVISION.  REVISION B BOARDS   *
; MAY BE MODIFIED TO ACT AS REV-C (CALL JADE) OR      *
; DDBOOT MAY BE PATCHED. PROM IS FOR REV-C.	      *
; --------------------------------------------------- *
; ADDRESS  NAME    FUNCTION       REV-C    REV-B      *
; -------  ----    --------       -----    -----      *
;  F040	   DS$ASW  ADDR SW MASK	   0E       0C	      *
;  F043    D$BASE  ADDR 8K RANGE   E0       E4 **     *
;  F04B    DS$HLT  DD HALT BIT     01       02        *
; --------------------------------------------------- *
;  ** SHOULD BE "E0" IF MA10 JUMPER IS INSTALLED.     *
;******************************************************
; DISK DRIVES - DDBOOT IS USING A 10 MILLISECOND STEP *
; WHICH WILL HOME THE R/W HEAD ON MOST DRIVES. IF THE *
; USER DRIVE IS MUCH FASTER (SHUGART SA850 OR SIEMENS *
; FD100-8D) THEN THE STEP RATE CONSTANT MAY BE PATCHED*
; NOTE: A DDBOOT PROM PATCHED FOR FAST DRIVES WILL    *
; NOT  FUNCTION  PROPERLY  IF LATER USED WITH  SLOWER *
; DRIVES.  SLOWER STEPS SHOULD ALWAYS WORK.  A DELAY  *
; BEFORE READING HAS BEEN PROVIDED FOR HEAD LOAD TIME *
; AND CAN BE USED FOR DRIVE-MOTOR START UP TIME WHEN  *
; THE DRIVE MOTORS ARE CONTROLLED BY THE DOUBLE-D.    *
; --------------------------------------------------- *
; ADDRESS     NAME        FUNCTION		      *
; -------     ----        --------		      *
; F1CE-F1CF   IM$TM$STP   STEP TIME		      *
; F1F7-F1F8   IM$TM$DBR   DELAY BEFORE READ	      *
;******************************************************


;******************************************************
; DOUBLE D BOOTSTRAP SYSTEM ADDRESS		      *
;******************************************************

PROM$ADDR	EQU	0F000H	;DDBOOT SYSTEM ADDRESS.

;******************************************************
; SET DOUBLE D SYSTEM PORT ADDRESS		      *
;******************************************************

D$PORT	EQU	043H	;DOUBLE D PORT ADDRESS.

;******************************************************
; SET USER DOUBLE D BOARD REVISION		      *
;******************************************************

TRUE	EQU	1	;SET TRUE TO LOGIC ONE.
FALSE	EQU	0	;SET FALSE TO LOGIC ZERO
REV$B	EQU	FALSE	;SET TRUE FOR REV B BOARDS.
REV$C	EQU	TRUE	;SET TRUE FOR REV C BOARDS.
MA10	EQU	FALSE	;TRUE IF MA10 JUMPED (REV-B).

;******************************************************
; DEFINE HALT MASK AND BASE ADDRESS OF DOUBLE D       *
;******************************************************

	IF	REV$B
DS$HLT	EQU	002H	;STATUS PORT HALT INDICATOR.
DS$ASW	EQU	00CH	;STATUS PORT ADDR SW MASK.
D$BASE	SET	0E400H	;SYSTEM WINDOW BASE ADDRESS
	ENDIF

	IF	MA10
D$BASE	SET	0E000H	;SYSTEM WINDOW BASE ADDRESS
	ENDIF

	IF	REV$C
DS$HLT	EQU	001H	;STATUS PORT HALT INDICATOR.
DS$ASW	EQU	00EH	;STATUS PORT ADDR SW MASK.
D$BASE	SET	0E000H	;SYSTEM WINDOW BASE ADDRESS
	ENDIF

;******************************************************
; BOOTSTRAP LINKAGE ADDRESS.			      *
;******************************************************

BSTACK	EQU	0080H	;BOOTSTRAP TOP OF STACK.
D$ADDR	EQU	0040H	;DOUBLE D ADDRESS POINTER.
D$MASK	EQU	0042H	;DOUBLE D HALT BIT ADDR.
D$TEMP	EQU	0043H	;DDBOOT TEMPORARY LOCATION.
BL$DCS	EQU	0377H	;DCM DISK CONTROLLER STATUS.
BL$ADR	EQU	0378H	;DCM LOAD AND JUMP ADDR PNTR.
BL$BSZ	EQU	037AH	;DCM BLOCK LOAD SIZE.
BL$DNR	EQU	0080H	;DRIVE NOT READY BIT.

;******************************************************
; DOUBLE D HARDWARE COMMANDS			      *
;******************************************************

DC$BGN	EQU	080H	;RESET Z80A AND EXECUTE.
DC$MRQ	EQU	001H	;REQUEST MEMORY WINDOW.
DC$MRT	EQU	000H	;RELEASE MEMORY WINDOW.
DC$MB0	EQU	001H	;SELECT MEMORY BANK 0.
DC$MB1	EQU	003H	;SELECT MEMORY BANK 1.
DC$EXC	EQU	002H	;ISSUE DOUBLE D INTERRUPT.

;******************************************************
; ASSEMBLER DIRECTIVES				      *
;******************************************************

	ORG	PROM$ADDR	;MODULE ADDRESS.

;******************************************************
; DDBOOT FUNCTIONS VECTOR TABLE			      *
;******************************************************

	JMP	INIT		;INITIALIZE AND BOOT.
	JMP	BOOT		;REBOOT DISK SYSTEM.
	JMP	CNS$CK		;CONSOLE STATUS.
	JMP	CNS$IN		;CONSOLE INPUT.
	JMP	CNS$OT		;CONSOLE OUTPUT.
	JMP	MSG$OT		;MESSAGE TO CONSOLE.

;******************************************************
; INITIALIZE SYSTEM HARDWARE - USER PATCH AREA	      *
;******************************************************

INIT:	NOP!NOP!NOP!NOP		;PATCH AREA.
	NOP!NOP!NOP!NOP		;PATCH AREA.
	NOP!NOP!NOP!NOP		;PATCH AREA.
	NOP!NOP!NOP!NOP		;PATCH AREA.
	NOP!NOP!NOP!NOP		;PATCH AREA.
	NOP!NOP!NOP!NOP		;PATCH AREA.
	NOP!NOP!NOP!NOP		;PATCH AREA.
	NOP!NOP!NOP!NOP		;PATCH AREA.
	NOP!NOP!NOP!NOP		;PATCH AREA.
	NOP!NOP!NOP!NOP		;PATCH AREA.

;******************************************************
; SET STACK AND DETERMINE CONTROLLER ADDRESS	      *
;******************************************************

BOOT:	LXI	SP,BSTACK	;SET STACK POINTER.
	IN	D$PORT		;INPUT STATUS PORT.
	ANI	DS$ASW		;MASK FOR ADDR SWS.
	RLC			;POSITION BITS.
	ORI	D$BASE SHR 8	;OR IN BASE ADDR.
	MOV	H,A		;HIGH BYTE VALUE.
	MVI	L,0		;LOW BYTE VALUE.
	SHLD	D$ADDR		;STORE THE ADDRESS
	MVI	A,DS$HLT	;LOAD HALT BIT MASK.
	STA	D$MASK		;STORE FOR BIOS USE.
	STA	D$TEMP		;SET REPEAT FLAG NZ.

;******************************************************
; INJECT BOOT MODULE INTO CONTROLLER		      *
;******************************************************

INJECT:	MVI	A,DC$MB0	;REQUEST DD MEM BANK 0.
	OUT	D$PORT		;ISSUE COMMAND.
	LXI	B,IM$END-IM$BGN	;INJECTION MODULE SIZE.
	XCHG			;D$ADDR HL TO DE.	
	LXI	H,IM$BGN	;INJECTION MODULE ADDR.
	CALL 	BLOCK		;BLOCK MOVE.

;******************************************************
; RESET AND START THE DISK PROCESSOR		      *
;******************************************************

	MVI	A,DC$BGN	;BEGIN DD PROCESSOR.
	OUT	D$PORT		;ISSUE  COMMAND.
	XTHL			;ALLOW DOUBLE D TIME
	XTHL			;TO START UP.

;******************************************************
; WAIT FOR TASK COMPLETION			      *
;******************************************************

	LDA	D$MASK		;HALT BIT MASK.
	MOV	B,A		;MOVE INTO B REG.
WAIT:	IN	D$PORT		;INPUT DD STATUS.
	ANA	B		;TEST HALT* STATUS.
	JNZ	WAIT		;WAIT TILL HALTED.

;******************************************************
; SWITCH CONTROLLER MEMORY INTO SYSTEM BUS	      *
;******************************************************

	MVI	A,DC$MRQ	;REQUEST MEM (BANK 0).
	OUT	D$PORT		;ISSUE COMMAND.

;******************************************************
; CHECK FOR BOOTSTRAP MALFUNCTION		      *
;******************************************************

	LHLD	D$ADDR		;CONTROLLER ADDRESS.
	LXI	D,BL$DCS	;ERROR CODE IM$BGN.
	DAD	D		;SET HL POINTER.
	MOV	A,M		;GET ERROR CODE.
	ANI	BL$DNR		;TEST DRIVE NOT READY.
	JNZ	INSERT		;IF DRIVE NOT READY.
	MOV	A,M		;GET ERROR CODE.
	ANA	A		;TEST REGISTER.
	JNZ	BAD$LD		;BAD LOAD.

;******************************************************
; PERFORM BLOCK TRANSFER FROM DISK MEMORY	      *
;******************************************************

	LHLD	D$ADDR		;CONTROLLER ADDRESS.
	LXI	D,BL$ADR	;LOAD ADDRESS PNTR.
	DAD	D		;SET HL POINTER.
	MOV	E,M		;LOW ORDER ADDR.
	INX	H		;INCREMENT HL.
	MOV	D,M		;HIGH ORDER ADDR.
	INX	H		;REQUIRES BL.BSZ NEXT.
	MOV	C,M		;LOW ORDER LENGTH.
	INX	H		;INCREMENT HL.
	MOV	B,M		;HIGH ORDER LENGTH.
	PUSH	D		;USE AS JUMP ADDR.
	MVI	A,DC$MB1	;SWITCH TO MEM BANK 1.
	OUT	D$PORT		;ISSUE COMMAND.
	LHLD	D$ADDR		;DOUBLE D MEM ADDRESS.
	CALL	BLOCK		;MOVE BIOS MODULE.

;******************************************************
; TRANSFER CONTROL TO OPERATING SYSTEM		      *
;******************************************************

	MVI	A,DC$MB0	;SWITCH TO BANK.0
	OUT	D$PORT		;ISSUE COMMAND.
	RET			;GOTO BIOS COLD ENTRY.

;******************************************************
; BLOCK MOVE SUBROUTINE (Z80 BLOCK MOVE REGISTERS)    *
;******************************************************

BLOCK:	MOV	A,M		;GET BYTE.
	INX	H		;INC POINTER
	XCHG			;GET DESTINATION.
	MOV	M,A		;PUT BYTE.
	INX	H		;INC POINTER
	XCHG			;GET SOURCE.
	DCX	B		;ONE LESS TO DO.
	MOV	A,B		;GET HI COUNT.
	ORA	C		;GET LO COUNT.
	JNZ	BLOCK		;FINISH LOADING.
	RET

;******************************************************
; DISK DRIVE IS NOT READY			      *
;******************************************************

INSERT:	LDA	D$TEMP		;LOAD INIT FLAG.
	ANA	A		;TEST FOR INITIAL.
	JZ	INJECT		;TRY BOOTING AGAIN.
	XRA	A		;ZERO A REGISTER.
	STA	D$TEMP		;CLEAR INITIAL FLAG.
	LXI	H,MSG$IN	;INSERT MESSAGE ADDR.
	CALL	MSG$OT		;OUTPUT MESSAGE.
	JMP	INJECT		;TRY BOOTING AGAIN.

;******************************************************
; DOUBLE D BOOTSTRAP MALFUNCTION		      *
;******************************************************

BAD$LD:	STA	D$TEMP		;STORE ERROR CODE.
	LXI	H,MSG$ER	;ERROR MESSAGE ADDEESS.
	CALL	MSG$OT		;DISPLAY MESSAGE.
	LDA	D$TEMP		;LOAD ERROR CODE.
	CALL	HXB$OT		;DISPLAY HEX BYTE.
	HLT!NOP!NOP		;HALT OR JUMP MONITOR.

;******************************************************
;  CONSOLE INPUT AND OUTPUT			      *
;******************************************************
;						      *
;  XXX$SP:  STATUS PORT ADDRESS			      *
;  XXX$SB:  STATUS READY BIT			      *
;  XXX$SI:  IF READY TRUE IS "1" USE "00" ELSE "FF"   *
;  XXX$DP:  DATA PORT ADDRESS			      *
;						      *
;******************************************************
  
CNI$SP	EQU	000H		;INPUT STATUS PORT.
CNI$SB	EQU	002H		;INPUT STATUS BIT.
CNI$SI	EQU	000H		;INPUT STATUS INVERT.
CNI$DP	EQU	001H		;INPUT DATA PORT.

CNO$SP	EQU	000H		;OUTPUT STATUS PORT.
CNO$SB	EQU	004H		;OUTPUT STATUS BIT.
CNO$SI	EQU	000H		;OUTPUT STATUS INVERT.
CNO$DP	EQU	001H		;OUTPUT DATA PORT.

;******************************************************
; CONSOLE INPUT STATUS CHECK			      *
;******************************************************

CNS$CK:	IN	CNI$SP		;INPUT STATUS PORT.
	XRI	CNI$SI		;ADJUST POLARITY.
	ANI	CNI$SB		;TEST READY BIT.
	RZ			;ZERO IS NOT READY.
	MVI	A,0FFH		;SET CONSOLE READY.
	RET			;ONES INDICATE READY.

	DB	0,0,0,0,0,0	;PATCHING AREA.
	DB	0,0,0,0,0,0	;PATCHING AREA.
	DB	0,0,0,0,0,0	;PATCHING AREA.

;******************************************************
; CONSOLE DATA INPUT				      *
;******************************************************

CNS$IN:	CALL	CNS$CK		;TEST INPUT READY.
	JZ	CNS$IN		;REPEAT TEST FOR RDY.
	IN	CNI$DP		;INPUT CONSOLE DATA.
	ANI	07FH		;SEVEN BITS OF ASCII.
	RET			;RETURN WITH DATA.

	DB	0,0,0,0,0,0	;PATCHING AREA.
	DB	0,0,0,0,0,0	;PATCHING AREA.
	DB	0,0,0,0,0,0	;PATCHING AREA.

;******************************************************
; CONSOLE DATA OUTPUT				      *
;******************************************************

CNS$OT:	IN	CNO$SP		;OUTPUT STATUS PORT.
	XRI	CNO$SI		;ADJUST POLARITY.
	ANI	CNO$SB		;TEST READY BIT.
	JZ	CNS$OT		;TEST AGAIN FOR RDY.
	MOV	A,C		;OUTPUT SETUP.
	OUT	CNO$DP		;OUTPUT CONSOLE DATA.
	RET			;RETURN COMPLETE.

	DB	0,0,0,0,0,0	;PATCHING AREA.
	DB	0,0,0,0,0,0	;PATCHING AREA.
	DB	0,0,0,0,0,0	;PATCHING AREA.


;******************************************************
; MESSAGE DISPLAY ROUTINE - HL REG POINTS TO STRING   *
;******************************************************

MSG$OT:	MOV	A,M		;LOAD CHARACTER/BYTE.
	CPI	'$'		;CHECK FOR TERMINATOR.
	RZ			;EXIT IF TERMINATOR.
	MOV	C,A		;PASS BYTE IN C REG.
	CALL	CNS$OT		;DISPLAY CHARACTER.
	INX	H		;POINT TO NEXT BYTE.
	JMP	MSG$OT		;REPEAT SEQUENCE.

;******************************************************
; DISPLAY A REGISTER IS HEXIDECIMAL		      *
;******************************************************

HXB$OT:	PUSH	PSW		;SAVE A REGISTER.
	RRC!RRC!RRC!RRC		;SHIFT 4 PLACES.
	CALL	HXN$OT		;DISPLAY HEX NIBBLE.
	POP	PSW		;RESTORE A REGISTER.
HXN$OT:	ANI	00FH		;MASK LOWER NIBBLE.
	CPI	00AH		;TEST IF LETTER HEX.
	JC	HXN$NM		;DISPLAY NUMBER.
	ADI	'A'-'9'-1	;ADD LETTER OFFSET.
HXN$NM:	ADI	'0'		;START WITH ASCII 0.
	MOV	C,A		;OUTPUT BYTE TO C REG.
	JMP	CNS$OT		;CONSOLE OUTPUT.

;******************************************************
; SYSTEM BOOTSTRAP MESSAGE AREA    		      *
;******************************************************

MSG$IN:	DB	CR,LF,LF,'INSERT SYSTEM DISKETTE ','$'
MSG$ER:	DB	CR,LF,LF,'DDBOOT LOAD ERROR - ','$'

CR	EQU	00DH		;CARRAIGE RETURN.
LF	EQU	00AH		;LINE FEED COMMAND.

;******************************************************
;******************************************************
; INJECTION MODULE *** THE FOLLOWING EXECECUTES IN DD *
;******************************************************
; THIS SECTION OF CODE IS HAS BEEN WRITTEN WITH	AN    *
; ADDRESS OFFSET SO AS TO ASSEMBLE WITH IM$BGN AT     *
; LOCATION ZERO.				      *
;******************************************************
;******************************************************


;******************************************************
; DOUBLE D INTERNAL PORT ASSIGNMENTS		      *
;******************************************************

IM$BL$STS	EQU	000H	;BOARD STATUS
IM$BL$CTL	EQU	000H	;BOARD CONTROLS
IM$WD$CMD	EQU	004H	;179X COMMAND REGISTER
IM$WD$STS	EQU	004H	;179X STATUS REGISTER
IM$WD$TRK	EQU	005H	;179X TRACK REGISTER
IM$WD$SEC	EQU	006H	;179X SECTOR REGISTOR
IM$WD$DTA	EQU	007H	;179X DATA REGISTER
IM$XP$STP	EQU	008H	;STEPPER PULSE
IM$XP$MTO	EQU	010H	;MOTOR TIME OUT
IM$XP$MTX	EQU	040H	;MOTOR TIME EXTEND
IM$XP$DSH	EQU	080H	;DATA SYNC HOLD

;******************************************************
; 179X-02 COMMAND CODES				      *
;******************************************************

IM$DC$HDL	EQU	00011000B  ;LOAD R/W HEAD.
IM$DC$RMS	EQU	10011000B  ;READ MULTI-SECTOR.
IM$DC$STS	EQU	11010000B  ;SET TYPE 1 STATUS.

;******************************************************
; Z80 INSTRUCTION HEX CODES - NOTE HI/LOW ORDER SWAP  *
;******************************************************

IM$LXIY		EQU	021FDH	;LOAD Y REG IMED.
IM$RETN		EQU	045EDH	;RETN (NMI RETURN).
IM$XTIY		EQU	0E3FDH	;EXCHANGE (SP) <> IY.

;******************************************************
; BOARD STATUS AND CONTROL PORTS		      *
;******************************************************

IM$BS$US0	EQU	001H	;179X-02 POLARITY TEST.
IM$BC$DR0	EQU	004H	;DRIVE 0 SELECT/ENABLE.

;******************************************************
; DISK STATUS MASKS				      *
;******************************************************

IM$DM$RER  EQU	10011100B 	;READ ERROR TEST MASK.
IM$DM$TK0  EQU	00000100B 	;TRACK 0 TEST.
IM$DM$DNR  EQU	10000000B	;DRIVE NOT READY.

;******************************************************
; DISK DRIVE PARAMETERS				      *
;******************************************************

IM$TM$STP	EQU	10	;STEPPER INTERVAL - MS.
IM$TM$DBR	EQU	40	;DELAY BEFORE READ- MS.
IM$NB$TRK	EQU	76	;MAXIMUM NMBR OF STEPS.

;******************************************************
; INTERNAL MEMORY ASSIGNMENTS			      *
;******************************************************

IM$BK0	   EQU	0000H		;LOWER BANK ADDRESS.
IM$BKL     EQU	0400H		;1K BANK LENGTH.
IM$BK1     EQU	IM$BK0+IM$BKL	;UPPER BANK ADDRESS.
IM$NMI     EQU	IM$BK0+0066H	;NON-MASKABLE INT ADDR.
IM$BL$ERC  EQU	IM$BK0+0376H	;ERROR CODE LOCATION.
IM$BL$DCS  EQU	IM$BK0+BL$DCS	;DISK CONTROLLER STAT.

;******************************************************
; BOOTSTRAP COMMUNICATION			      *
;******************************************************

BE$HOM	EQU	001H		;HOME ERROR.
BE$RDA	EQU	002H		;READ ERROR A.
BE$RDB	EQU	004H		;READ ERROR B.

;******************************************************
; DISK CONTROLLER MODULE (DCM) LINKAGE		      *
;******************************************************

DCM$SS	EQU	13		;FIRST DCM SECTOR = 13.
DCM$BG	EQU	IM$BK1+3	;DCM COLD START ENTRY.
DCM$LN	EQU	0400H		;DCM LENGTH

;******************************************************
; SET STACK, START DRIVE MOTOR, AND SET INVERT SW (C) *
;******************************************************

IM$BGN:	LXI	SP,IM$BK1	;SET UP STACK.
	IN	IM$XP$MTX	;TURN ON MOTOR.
	MVI	C,0		;ASSUME 1793.
	IN	IM$BL$STS	;INPUT STATUS.
	ANI	IM$BS$US0	;TEST USER SW 0.
	JNZ	IM$DRV-IM$BGN	;GOTO SELECT DRV.
	MVI	C,0FFH		;1791-01 INVERTS.

;******************************************************
; CLEAR 179X-01 INTERRUPT AND SELECT DRIVE 0          *
;******************************************************

IM$DRV:	CALL	IM$STS-IM$BGN	;179X-01 FORCED CLEAR. 
	MVI	A,IM$BC$DR0	;DRIVE 0, ENABLED.
	OUT	IM$BL$CTL	;OUTPUT CONTROLS.

;******************************************************
; CHECK FOR DRIVE READY SIGNAL			      *
;******************************************************

	CALL	IM$STS-IM$BGN	;GET DRIVE STATUS.
	STA	IM$BL$DCS	;STORE DRIVE STATUS.
	ANI	IM$DM$DNR	;CHECK DRIVE NOT RDY.
	JZ	IM$HDL-IM$BGN	;IF READY, BOOT SYSTEM.
	XRA	A		;ZERO A REGISTER.
	JMP	IM$HLT-IM$BGN	;DOUBLE D SHUTDOWN.

;******************************************************
; LOAD R/W HEAD ON SELECTED DRIVE		      *
;******************************************************

IM$HDL:	MOV	A,C		;GET TRACK 0 VALUE.
	OUT	IM$WD$TRK	;SET TRACK REGISTER.
	OUT	IM$WD$DTA	;SEEK SAME TRACK.
	DW	IM$LXIY		;Z80 LXIY HEX CODE.
	DW	IM$HME-IM$BGN	;SET NMI RETURN ADDR.
	MVI	A,IM$DC$HDL	;HEAD LOAD COMMAND.
	XRA	C		;INVERT (1791-01).
	OUT	IM$WD$CMD	;ISSUE COMMAND.
IM$WFI:	JMP	IM$WFI-IM$BGN	;WAIT FOR INTERRUPT.

;******************************************************
; POSITION R/W HEAD AT TRACK ZERO		      *
;******************************************************

IM$HME:	MVI	L,IM$NB$TRK	;SET MAX TRACKS.
IM$STP:	CALL	IM$STS-IM$BGN	;GET 179X STATUS.
	ANI	IM$DM$TK0	;TEST TRACK 0 BIT.
	JNZ	IM$RSU-IM$BGN	;TRACK 0 EXIT.
	DCR	L		;DEC ATTEMPTS.
	JZ	IM$EHM-IM$BGN	;CANT FIND TRK 0?
	IN	IM$XP$STP	;ISSUE STEP PULSE.
	LXI	D,IM$TM$STP	;STEP INTERVAL TIME.
	CALL	IM$TMR-IM$BGN	;PAUSE FOR PERIOD.
	JMP	IM$STP-IM$BGN	;TRY ANOTHER TIME.

;******************************************************
; GET UPDATED 179X-01 STATUS			      *
;******************************************************

IM$STS:	MVI	A,IM$DC$STS	;TYPE 4 - STATUS.
	XRA	C		;INVERT (1791-01).
	OUT	IM$WD$CMD	;ISSUE COMMAND.
	XTHL			;DELAY
	XTHL			;DELAY
	XTHL			;DELAY
	XTHL			;DELAY
	IN	IM$WD$STS	;GET STATUS
	XRA	C		;INVERT (1791-01).
	RET			;RETURN TO CALLER.

;******************************************************
; DISK INTERRUPT "NMI" ROUTINE			      *
;******************************************************

	ORG	IM$BGN+IM$NMI

	IN	IM$WD$STS	;GET 179X STATUS.
	XRA	C		;INVERT (1791-01).
	STA	IM$BL$DCS	;MAKE STATUS VISIBLE.
	DW	IM$XTIY		;EXCHANGE (SP) <> IY.
	DW	IM$RETN		;NMI RETURN (RETN).

;******************************************************
; SET-UP FOR DCM READ OPERATION			      *
;******************************************************

IM$RSU:	LXI	D,IM$TM$DBR	;DELAY BEFORE READ.
	CALL	IM$TMR-IM$BGN	;CALL MS. TIMER.
	LXI	D,IM$BKL	;SET BANK LENGTH
	LXI	H,IM$BK1	;DCM LOAD ADDRESS
	DW	IM$LXIY		;Z80 LXI Y HEX CODE.
	DW	IM$REA-IM$BGN	;READ ERROR TRAP.
	MVI	A,DCM$SS	;FIRST SEC OF DCM.
	XRA	C		;INVERT (1791-01)
	OUT	IM$WD$SEC	;SET 179X SEC REG.
	MVI	A,IM$DC$RMS	;READ MULTI-SECTOR.
	XRA	C		;INVERT (1791-01).
	OUT	IM$WD$CMD	;ISSUE 179X COMMAND.

;******************************************************
; ACCEPT EACH BYTE AND STORE IN MEMORY		      *
;******************************************************

IM$RBT:	IN	IM$XP$DSH	;WAIT FOR DATA.
	IN	IM$WD$DTA	;INPUT INV DATA.
	XRA	C		;INVERT (1791-01).
	MOV	M,A		;STORE DCM BYTE.
	INX	H		;INCREMENT POINTER.
	DCX	D		;DECREMENT LENGTH.
	MOV	A,D		;GET HIGH REG.
	ORA	E		;THEN OR-IN LOW REG.
	JNZ	IM$RBT-IM$BGN	;READ ANOTHER BYTE.

;******************************************************
; TEST READ STATUS, TERMINATE OPERATION, GO DCM	      *
;******************************************************

IM$TRS:	IN	IM$WD$STS	;INPUT READ STATUS.
	XRA	C		;INVERT (1791-01).
	ANI	IM$DM$RER	;TEST FOR ERRORS.
	JNZ	IM$REB-IM$BGN	;READ ERROR TRAP.
	CALL	IM$STS-IM$BGN	;TERMINATE READ.
	JMP	DCM$BG		;TRANSFER TO DCM.

;******************************************************
; READ ERROR HAS BEEN DETECTED			      *
;******************************************************

IM$REA:	MVI	A,BE$RDA	;LOAD READ ERROR CODE.
	JMP	IM$HLT-IM$BGN	;GO TO ERROR HALT.
IM$REB:	MVI	A,BE$RDB	;LOAD READ ERROR CODE.
	JMP	IM$HLT-IM$BGN	;GO TO ERROR HALT.
IM$EHM:	MVI	A,BE$HOM	;HOME ERROR CODE.
IM$HLT:	STA	IM$BL$ERC	;DISPLAY ERROR CODE.
	XRA	A		;ZERO A REG.
	OUT	IM$BL$STS	;DESELECT DRIVE.
	IN	IM$XP$MTO	;MOTOR OFF!
	HLT			;TERMINATE.

;******************************************************
; TIMER - WAIT FOR (BC * 1.0) MILLISECONDS	      *
;******************************************************

IM$TMR:	MVI	A,220		;LOAD INT MS VALUE.
IM$TMX:	DCR	A		;DEC FOR 1 MS.
	NOP			;EXTRA TIMING DELAY.
	JNZ	IM$TMX-IM$BGN	;REPEAT FOR 1 MS.
	DCX	D		;TEST FOR ANOTHER MS.
	MOV	A,D		;CHECK REG D.
	ORA	E		;AND REGISTER E.
	JNZ	IM$TMR-IM$BGN	;DO ANOTHER 1 MS.
	RET			;TIME PERIOD EXPIRED!

;******************************************************

IM$END:	END	BEGIN		;END OF ASSEMBLY.
 