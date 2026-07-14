;-----------------------------------------------------------------------
; HB.INC -- the Host Bridge, as the guest sees it.
;
; NOT ACTUALLY INCLUDED BY ANYTHING. Digital Research's ASM.COM has no
; INCLUDE directive (that is M80's MACLIB), so every one of R.ASM, W.ASM
; and HDIR.ASM carries its own copy of the equates below. This file is the
; MASTER: if a number here and a number there ever disagree, this one is
; right and the other is a typo.
;
; The card is src/boards/hostbridge.h. It is OUR OWN design -- not a period
; board -- and it exists because there is otherwise no way to move a file
; between the host and a running CP/M guest.
;-----------------------------------------------------------------------

;---- The two ports ----------------------------------------------------
;
; PORT B0, AND THAT NUMBER TOOK A CENSUS OF THE WHOLE S-100 CATALOG.
;
; 0x30 was the obvious pick and it is WRONG: the WD179X floppy controller
; defaults there and the Cromemco 64FDC sits on top of it. Also taken:
; 00-01 88-SIO, 04-05 88-PIO, 06-07 88-ACR, 08-0A 88-DCDD, 0E-0F Dazzler,
; 10-13 88-2SIO, 20-23 88-4PIO, 40/C0 bank select, A0-A7 88-HDSK,
; C0 PMMI, F8-FC Tarbell, FE 88-VI/RTC, FF front panel.
;
; B0-BF and D0-DF are the only 16-port holes left.
;
; IF YOU MOVE THE CARD, REASSEMBLE. These utilities do NOT scan the bus
; looking for the bridge, and that is deliberate: a blind IN across 256
; ports of an unknown machine would take a byte out of a UART's receive
; register and step a floppy controller's head on the way past. A program
; that guesses at hardware is a program that breaks other hardware.
;
HBBASE  EQU     0B0H            ; SET hb0 PORT=.. moves it; then change this
HBCMD   EQU     HBBASE          ; OUT = command      IN = status
HBDATA  EQU     HBBASE+1        ; OUT = data         IN = data

;---- Status bits (IN HBCMD) -------------------------------------------
;
; An IN from HBCMD is ALWAYS the status register. Never a result code,
; never "the second byte of the thing you asked for". That is the whole
; difference between this card and the SIMH pseudo-device it replaces.
;
; BITS 5-7 ALWAYS READ ZERO, so a status of 0FFH means THERE IS NO CARD
; THERE -- an IN from an undecoded port floats the bus high. Do not lean
; on that, though; use IDENT, which is the real probe.
;
RDY     EQU     01H             ; ready for a command (always set today)
DAV     EQU     02H             ; a byte is waiting at HBDATA
TBE     EQU     04H             ; HBDATA will accept a byte
HEOF    EQU     08H             ; the stream you are draining has run dry
HERR    EQU     10H             ; the last operation failed -- ask CERROR

;---- Commands (OUT HBCMD) ---------------------------------------------
;
; THE ONE RULE THAT MATTERS:
;
;     ANY OUT TO HBCMD ABANDONS THE STREAM IN FLIGHT.
;
; You can walk away from a half-finished transfer at any point, for any
; reason, and the next command simply works. There is no "the device is
; now in an undefined state", and therefore no need for the 128-resets-in-
; a-row incantation that AltairZ80's R.COM and W.COM open with.
;
; The one thing a command does NOT abandon is the directory enumerator.
; That is not an exception -- a listing is not a stream -- and it is what
; lets R walk a wildcard, doing a whole OPEN_READ between DIR_NEXTs.
;
CIDENT  EQU     0               ; -> "ALTAIRSIM HOSTBRIDGE 1",0 at HBDATA
CVOPENR EQU     1               ; then the name, NUL-terminated. Then read.
CVOPENW EQU     2               ; then the name. Then write. Then CCLOSE.
CCLOSE  EQU     3               ; REQUIRED -- an unclosed write is discarded
CDIRF   EQU     4               ; then a glob (may be empty). Then read names.
CDIRN   EQU     5               ; next name; HEOF when the list runs out
CDELET  EQU     6               ; then the name
CERROR  EQU     7               ; -> code byte, then a NUL-terminated message
CRESET  EQU     8               ; abort everything, clear the error latch

;---- Error codes (the first byte CERROR hands back) --------------------
;
; These are on the wire, so they are part of the card's published
; interface and will not be renumbered.
;
ENONE   EQU     0
ENOFILE EQU     1               ; no such file
EPERM   EQU     2               ; permission denied
EOUTSID EQU     3               ; OUTSIDE THE SANDBOX -- see below
EIO     EQU     4               ; host I/O error
ENOOPEN EQU     5               ; no file open
EBIG    EQU     6               ; larger than the card will carry (8 MB)
EBADNAM EQU     7               ; bad file name
EAMBIG  EQU     8               ; several host files differ only in case

;---- The sandbox ------------------------------------------------------
;
; Host names resolve against the card's `hostdir` and CANNOT LEAVE IT.
; A name may be a relative path -- SRC/FOO.ASM -- and BOTH separators work
; on every host, so one assembled .COM runs against a Mac, a Linux box and
; a Windows box without being told which. (AltairZ80's device has a "what
; is your path separator?" command for this. The guest should not have to
; care, and here it does not.)
;
; Refused with EOUTSID: an absolute path, a drive letter, a `..` component,
; and a symlink that resolves out of the root. There is no way to talk the
; card into any of them, which is the point: a rogue CP/M program must not
; be able to read your ssh key.
;
; The empty `hostdir` -- the default -- is the directory you started
; altairsim from.
;-----------------------------------------------------------------------
