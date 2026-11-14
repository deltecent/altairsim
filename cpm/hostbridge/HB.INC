;-----------------------------------------------------------------------
; HB.INC: the Host Bridge, as the guest sees it.
;
; No program includes this file. ASM.COM has no INCLUDE, so R.ASM,
; W.ASM, HDIR.ASM and SIMCLK.ASM each have their own copy of the
; equates they use.
; This file is the master list. If a value here and a value in a
; program are different, this one is correct.
;
; The board is src/boards/hostbridge.h. It is an altairsim design, not
; a period board.
;-----------------------------------------------------------------------

;---- The two ports ----------------------------------------------------
;
; Other boards decode these ports: 00-01 88-SIO, 04-05 88-PIO,
; 06-07 88-ACR, 08-0A 88-DCDD, 0E-0F Dazzler, 10-13 88-2SIO,
; 20-23 88-4PIO, 30 WD179X, 40/C0 bank select, A0-A7 88-HDSK, C0 PMMI,
; F8-FC Tarbell, FE 88-VI/RTC, FF front panel. B0-BF and D0-DF are free.
;
; The utilities do not search the bus for the board. An IN can change
; the state of another board, for example a UART's receive register.
; If you move the board, change HBBASE and assemble again.
;
HBBASE	EQU	0B0H		; SET hb0 PORT=.. moves the board
HBCMD	EQU	HBBASE		; OUT = command      IN = status
HBDATA	EQU	HBBASE+1	; OUT = data         IN = data

;---- Status bits (IN HBCMD) -------------------------------------------
;
; An IN from HBCMD always reads the status register, at any time.
; Bits 5-7 always read 0. A port that no board decodes reads 0FFH, but
; IDENT is the correct test for the board.
;
RDY	EQU	01H		; ready for a command. Clear only while
				; the machine applies a CSETCK rate.
DAV	EQU	02H		; a byte is waiting at HBDATA
TBE	EQU	04H		; HBDATA will take a byte
HEOF	EQU	08H		; the stream has no more data
HERR	EQU	10H		; the last operation failed. Use CERROR.

;---- Commands (OUT HBCMD) ---------------------------------------------
;
; An OUT to HBCMD stops the stream in progress, and the next command
; works. A command does not stop a directory listing, so R can do an
; OPEN_READ between two DIR_NEXTs.
;
CIDENT	EQU	0		; -> "ALTAIRSIM HOSTBRIDGE 1",0 at HBDATA
CVOPENR EQU	1		; then the name and a NUL. Then read.
CVOPENW EQU	2		; then the name. Then write. Then CCLOSE.
CCLOSE	EQU	3		; required: the board discards an unclosed write
CDIRF	EQU	4		; then a pattern (can be empty). Then read names.
CDIRN	EQU	5		; the next name. HEOF at the end.
CDELET	EQU	6		; then the name
CERROR	EQU	7		; -> a code byte, then a message and a NUL
CRESET	EQU	8		; stop everything, clear the error
CDIRL	EQU	9		; as CDIRF, but each entry is three strings:
				; name<0> size<0> date<0>. The size is "<DIR>"
				; for a folder, else a decimal byte count. The
				; date is MM/DD/YY. Use CDIRN for the next one.
CSETCK	EQU	0AH		; then a rate in Hz as decimal digits and a
				; NUL. 0 = flat out. RDY clears until the
				; machine applies the rate. Then HERR is set
				; if the CPU board refused it. Send no command
				; before RDY: a command drops the request.
CGETCK	EQU	0BH		; -> the rate in Hz as decimal text and a NUL.
				; 0 = flat out.

;---- Error codes (the first byte from CERROR) -------------------------
;
; These numbers are part of the board's interface. They do not change.
;
ENONE	EQU	0
ENOFILE EQU	1		; no such file
EPERM	EQU	2		; permission denied
EOUTSID EQU	3		; outside the sandbox (see below)
EIO	EQU	4		; host I/O error
ENOOPEN EQU	5		; no file open
EBIG	EQU	6		; larger than the board holds (8 MB)
EBADNAM EQU	7		; bad file name
EAMBIG	EQU	8		; two or more host files differ only in case
EREFUSD EQU	9		; the machine refused the request, for
				; example a clock rate out of range

;---- The sandbox ------------------------------------------------------
;
; A host name is relative to the board's `hostdir`, and cannot go out of
; it. A name can be a path, such as SRC/FOO.ASM. Both `/` and `\` work on
; every host, so one .COM works on macOS, Linux and Windows.
;
; The board refuses these with EOUTSID: an absolute path, a drive letter,
; a `..` part, and a symlink that goes out of `hostdir`.
;
; An empty `hostdir` (the default) is the folder you started altairsim in.
;-----------------------------------------------------------------------
