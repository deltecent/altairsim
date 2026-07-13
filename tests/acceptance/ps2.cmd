; MITS Programming System II's acceptance test, as the ReadMe's own procedure does it.
;
; The machine (`ps2`) brings the cards: the 2SIO console at 0x10, the ACR at 006, 16K of
; RAM, and the sense switches at 0x8E = A15 + A11 + A10 + A9 = "load from the cassette,
; the terminal is on the 2SIO, and do NOT use serial input interrupts".
;
; NOTE THE LOADER. It is `tapes/MitsPS2/LDRPS2.HEX` -- the 4K BASIC v3.2 loader
; (lxi h,0FAEh) -- and NOT 8K BASIC's LDR8K32.HEX, even though both tapes have the same
; 0xAE leader byte and the ReadMe's first page says otherwise. Its second page says "the
; same loader used to load 4K BASIC version 3.2", and the tape agrees: the second stage
; on PS2-MON.TAP is assembled for page 0F. Boot it with the 8K loader and it lands 4K too
; high and the machine wanders off in silence. See tapes/MitsPS2/LDRPS2.ASM.
MOUNT acr0:tape "tapes/MitsPS2/PS2-MON.TAP"
LOAD "tapes/MitsPS2/LDRPS2.HEX"
RUN 0
