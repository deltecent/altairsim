; The 88-ACR's acceptance test, as an operator would have done it in 1975.
;
; The machine (`basic4k`) brings the cards: the SIO console at 0, the ACR at 006, 4K of
; RAM, and the sense switches at 0x80 = A15 up = "load from the cassette".
;
; Everything below is what a HUMAN did, and nothing else: put the tape in, press PLAY
; (that is MOUNT), toggle in the bootstrap, and run it from 0.
MOUNT acr0:tape "tapes/4KBasic31/4K BASIC Ver 3-1.tap"
LOAD "tapes/4KBasic31/LDR4K31.HEX"
RUN 0
