; 8K BASIC's acceptance test, as an operator would have done it in 1976.
;
; The machine (`basic8k`) brings the cards: the 2SIO console at 0x10, the ACR at 006,
; 16K of RAM, and the sense switches at 0x8C = A15 + A11 + A10 = "load from the
; cassette, and the terminal is on the 2SIO".
;
; Everything below is what a HUMAN did, and nothing else: put the tape in, press PLAY
; (that is MOUNT), toggle in the bootstrap, and run it from 0.
MOUNT acr0:tape "tapes/8KBasic32/8K BASIC Ver 3-2.tap"
LOAD "tapes/8KBasic32/LDR8K32.HEX"
RUN 0
