	org	0
;
; Cassette Loader for the MITS Programming System II monitor (PS2-MON.TAP)
;
;   ** Set A15 on (cassette load) **
;   ** Set A11, A10 on (2SIO terminal) **
;   ** Set A9  on (monitor does NOT use serial input interrupts) **
;
; THIS IS THE 4K BASIC VERSION 3.2 LOADER, NOT THE 8K ONE -- lxi h,0FAEh.
;
; The ReadMe says both things in two different places: page 1 says "the same bootstrap
; loader as would be used for BASIC version 3.2", page 2 says "the same loader used to
; load 4K BASIC version 3.2". Page 2 is the correct one, and THE TAPE SETTLES IT: the
; second stage that comes off PS2-MON.TAP is assembled for page 0F --
;
;       0F00  DI
;       0F01  LXI SP,0FB2
;       0F04  IN FF            ;read the sense switches
;       ...   JC 0F28 / JC 0F3C
;
; Load it at 1FAE with the 8K loader and that code runs 4K too high: every one of its own
; jumps lands in the empty page below it and the machine wanders off, silently. The leader
; byte is 0xAE either way -- it is the LOW byte of the load address, so it cannot tell the
; two apart, and neither can you until you disassemble what landed.
;
; H = msb of load address
; L = lsb of load address = length of loader = leader byte
;
	lxi	h,0FAEh		;4K BASIC V3.2 / MITS PS II monitor

loop	lxi	sp,stack	;init SP so a RET jumps to loop
	in	06h		;get acr status
	rrc			;new byte available?
	rc			;no (jumps back to loop)

	in	07h		;get the byte
	cmp	l		;new byte = leader byte?
	rz			;yes (jumps back to loop)

	dcr	l		;not leader, decrement address
	mov	m,a		;store the byte (reverse order)
	rnz			;loop until L = 0

	pchl			;jump to code just downloaded

stack	dw	loop
	end
