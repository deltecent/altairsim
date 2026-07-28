; PMMITERM.ASM -- a bare-metal modem terminal for the PMMI MM-103.
;
; The console is an 88-2SIO (status/control at 10h, data at 11h); the modem is the
; PMMI MM-103 at its factory base 0C0h (pmmiterm.toml). The program is a plain
; full-duplex terminal: every key you type on the 2SIO is handed to the PMMI's UART
; transmitter, and every character the PMMI receives is written to the 2SIO screen.
; Four keys are stolen for the modem instead of being sent down the line:
;
;   ^S (13h)  toggle the PMMI 6860 Self Test loopback  (OUT 0C3h bit 4, active low)
;   ^D (04h)  toggle DTR                                 (OUT 0C3h bit 6)
;   ^I (09h)  display the modem-status byte in hex       (IN  0C2h)
;   ^C (03h)  print BYE and HLT
;
; Self Test only engages with DTR up (the 6860 loopback means nothing with the modem
; disabled -- see docs/boards/pmmi-mm103.md), so the way to see the terminal echo with
; no phone line attached is ^D then ^S: your keystrokes now leave the PMMI transmitter,
; the 6860 loops them straight back to its receiver, and they land on the 2SIO screen.
; With the line dead and Self Test off, a typed character goes out the modem and simply
; vanishes -- nothing echoes, which is the honest behavior of an unconnected modem.
;
; Assembles at 0100h. There is no assembler in the tree; PMMITERM.HEX beside this file
; is the assembled image, and this listing is here to be read.
;   altairsim pmmiterm.toml
;   altairsim> LOAD PMMITERM.HEX
;   altairsim> RUN 0100

        ORG  0100H

; ---- console: 88-2SIO channel A ----
SIOS    EQU  10H            ; status (R) / control (W)
SIOD    EQU  11H            ; data (R/W)
RDRF    EQU  01H            ; 2SIO status bit 0: receive data register full
TDRE    EQU  02H            ; 2SIO status bit 1: transmit data register empty

; ---- modem: PMMI MM-103 at base 0C0h ----
PMS     EQU  0C0H           ; UART status (R) / frame + SH,RI (W)
PMD     EQU  0C1H           ; UART data (R/W)
PMMS    EQU  0C2H           ; modem status (R) / rate divisor (W)
PMC     EQU  0C3H           ; 6860 modem control (W)
TBMT    EQU  01H            ; PMMI UART status bit 0: transmit buffer empty
DAV     EQU  02H            ; PMMI UART status bit 1: received char available
DTR     EQU  40H            ; PMMI modem control bit 6: data terminal ready
SELFT   EQU  10H            ; PMMI modem control bit 4: Self Test (ACTIVE LOW)

; ---- the four hijacked keys ----
CTRLC   EQU  03H
CTRLD   EQU  04H
CTRLI   EQU  09H
CTRLS   EQU  13H

; ---------------------------------------------------------------------------

START:  MVI  A, 03H         ; 6850 master reset
        OUT  SIOS
        MVI  A, 15H         ; 8 bits, no parity, 1 stop, /16, no interrupts
        OUT  SIOS

        MVI  A, 1CH         ; PMMI UART frame 8N1, SH and RI held low
        OUT  PMS
        MVI  A, 34H         ; PMMI rate divisor N=52 -> ~300 baud (250000/(16*N))
        OUT  PMMS
        MVI  A, SELFT       ; DTR off, Self Test bit high (loopback disengaged)
        STA  MCTL
        OUT  PMC

        LXI  H, SIGNON      ; greet the operator on the console
        CALL PUTS

; ---- the terminal loop: console <-> modem, both directions each pass ----

MAIN:   IN   SIOS           ; did the operator type something?
        ANI  RDRF
        JZ   RXCHK           ; no -- go drain the modem
        IN   SIOD           ; A = the key
        ANI  7FH            ; 7-bit ASCII

        CPI  CTRLC          ; ^C -> quit
        JZ   BYE
        CPI  CTRLD          ; ^D -> toggle DTR
        JZ   TOGDTR
        CPI  CTRLS          ; ^S -> toggle Self Test
        JZ   TOGST
        CPI  CTRLI          ; ^I -> show modem status
        JZ   SHOWMS

        MOV  B, A           ; an ordinary key -> down the line
TXWAIT: IN   PMS            ; wait for the PMMI transmitter
        ANI  TBMT
        JZ   TXWAIT
        MOV  A, B
        OUT  PMD

RXCHK:  IN   PMS            ; did the modem receive anything?
        ANI  DAV
        JZ   MAIN            ; no -- round again
        IN   PMD            ; A = character off the line
        MOV  C, A
        CALL PUTC           ; ...onto the console screen
        JMP  MAIN

; ---- the hijacked keys ----

TOGDTR: LDA  MCTL           ; flip DTR in the shadow, then write the port
        XRI  DTR
        STA  MCTL
        OUT  PMC
        JMP  MAIN

TOGST:  LDA  MCTL           ; flip the Self Test bit (loopback engages iff DTR up)
        XRI  SELFT
        STA  MCTL
        OUT  PMC
        JMP  MAIN

SHOWMS: IN   PMMS           ; read modem status
        MOV  B, A
        LXI  H, MSGMS       ; "\r\nMS="
        CALL PUTS
        MOV  A, B
        CALL PUTHEX         ; the byte, two hex digits
        LXI  H, CRLF
        CALL PUTS
        JMP  MAIN

BYE:    LXI  H, BYEMSG
        CALL PUTS
        HLT

; ---- console output helpers (88-2SIO) ----

; PUTC: send the character in C to the console, waiting for TDRE.
PUTC:   IN   SIOS
        ANI  TDRE
        JZ   PUTC
        MOV  A, C
        OUT  SIOD
        RET

; PUTS: send the 00-terminated string at HL to the console.
PUTS:   MOV  A, M
        ORA  A
        RZ
        MOV  C, A
        CALL PUTC
        INX  H
        JMP  PUTS

; PUTHEX: print the byte in A as two hex digits.
PUTHEX: MOV  B, A
        RRC
        RRC
        RRC
        RRC
        CALL PUTNIB         ; high nibble
        MOV  A, B
                            ; fall through for the low nibble
PUTNIB: ANI  0FH
        ADI  90H            ; the classic nibble-to-ASCII-hex fold
        DAA
        ACI  40H
        DAA
        MOV  C, A
        JMP  PUTC           ; tail call: PUTC returns for us

; ---- state and strings ----

MCTL:   DB   0              ; shadow of OUT 0C3h (the port is write-only)

SIGNON: DB   0DH, 0AH, 'PMMI MM-103 TERMINAL', 0DH, 0AH
        DB   '^D DTR  ^S SELF-TEST  ^I STATUS  ^C QUIT', 0DH, 0AH, 00H
MSGMS:  DB   0DH, 0AH, 'MS=', 00H
CRLF:   DB   0DH, 0AH, 00H
BYEMSG: DB   0DH, 0AH, 'BYE', 0DH, 0AH, 00H

        END
