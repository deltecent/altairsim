# PAiA SS-100 — Software Listings

Source: [PAiA SS-100 Speech Synthesizer.pdf](#) — PAiA Electronics *SS-100 Speech Synthesizer
Software* booklet (SS100-L), © 1982 PAiA Electronics, Inc. The provenance is in
`docs/sources.md`.

This file holds the three 8080 program listings of the booklet, copied line for line from the
page images. The board, the control word and the phoneme chart are in
[PAiA SS-100 Speech Synthesizer](PAiA%20SS-100%20Speech%20Synthesizer.md). The datasheet's
word list is in [PAiA SS-100 Phoneme Word List](PAiA%20SS-100%20Phoneme%20Word%20List.md).

| Program | Printed pages | PDF pages | Author |
|---|---|---|---|
| SC-01 Vocabulary Development System | 11–20 | 9–18 | Randy Carlstrom |
| Speech Development System (CP/M) | 21–26 | 19–24 | Randy Carlstrom, CP/M version by John Simonton |
| Simple Speech Output Driver | 27 | 25 | John Simonton |

Pages are cited as printed pages. PDF page N is printed page N+2.

**How the listings were copied.**

- Each printed page is one code block. The columns are kept: the address, the object bytes,
  the line number (the Vocabulary listing only), the label, the opcode, the operand and the
  comment. The spacing between the columns is regular here. On the page, it follows the tabs
  of the printer.
- The text is as printed, with the spelling of the comments and any error in the booklet.
- The printer's zero and letter O look the same. A digit is written `0` where the assembler
  needs a number (`0FFH`, `0A8H`).
- Every object byte was checked against its instruction with an 8080 encoder. The encoder
  checks that each address follows the last one, and that each label value is the address
  where the label is defined. A disagreement is listed in the notes after the program, with ⚠.
- A `DB` line prints only its first five bytes in the object column, as the assembler did.

---

## SC-01 Vocabulary Development System (printed pp. 11–20)

Randy Carlstrom's program, dated 15-JUN-81. It runs from `100H` and calls the monitor's
keyboard input at `0C01FH` and its output at `0C019H`. The program reads a line of phoneme
names and commands, separated by commas, and speaks it. The commands are in the summary on
p. 11. The interrupt jump is assembled at `RSTAD` (`38H`), the address for VI0 and PINT. The
service routine `INTSV` is at `026C`. The program writes the control word `0A8H` at the start,
and `68H` on `CTRL-C` before it jumps to the monitor at `0C004H`.

This listing has a line-number column. An `EQU` line prints the location counter in the address
column, not the value. An `ORG` line prints the location counter from before the `ORG`.
An `ASC` line puts the rest of its object bytes on the lines below, four on each line.

The four tables on pp. 16–19 list each phoneme name or command, then its code. `TBL1` holds
the names of one character, `TBL2` of two, `TBL3` of three and `TBL4` of four. Each table ends
with `0FFH`. A command code has a high digit of `4` (XI), `5` (XA), `6` (XF), `7` (XE) or `8`
(XS).

**Printed p. 11**

```
0000            0100 * * * * * * * * * * * * * * * * * * * * * * * * * * *
0000            0105 *                                                   *
0000            0110 *      SC-01 VOCABULARY DEVELOPMENT SYSTEM          *
0000            0115 *                                                   *
0000            0120 *                     BY                            *
0000            0125 *                RANDY CARLSTROM                    *
0000            0130 *                   15-JUN-81                       *
0000            0135 *                                                   *
0000            0140 * * * * * * * * * * * * * * * * * * * * * * * * * * *
0000            0145 *                - COMMAND SUMMARY -                *
0000            0150 *                                                   *
0000            0155 *    XA0-3          SET AMPLITUDE                   *
0000            0160 *    XF1-16         SET CLOCK FREQUENCY             *
0000            0165 *    XI1-4          SET INFLECTION                  *
0000            0170 *    XE             SET TO EFFECTS MODE             *
0000            0175 *    XS             SET TO SPEECH MODE              *
0000            0180 *    ESCAPE         REPEAT SEQUENCE                 *
0000            0185 *    CTRL-C         EXIT PROGRAM                    *
0000            0190 * * * * * * * * * * * * * * * * * * * * * * * * * * *
0000            0195 *
0000            0200 *
0000            0205 *
0000            0210 *                 - SYSTEM EQUATES -
0000            0215 *
0000            0220 RSTAD EQU 38H            RESTART ADDRESS FOR VI 0
0000            0225 START EQU 100H           PROGRAM STARTING ADDRESS
0000            0230 WIDTH EQU 64             TERMINAL WIDTH (CHARACTERS)
0000            0235 *
0000            0240 *
0000            0245 *            - I/O PORT ASSIGNMENTS -
0000            0250 *
0000            0255 SPORT EQU 30H            SYNTHESIZER DATA PORT
0000            0260 PPORT EQU 31H            SYNTHESIZER PARAMETER PORT
0000            0265 *
0000            0270 *
0000            0275 * * * * * * * *    MAIN  PROGRAM    * * * * * * * *
0000            0280 * *    (MAY EXIST IN EITHER ROM OR RAM)           * *
0000            0285 *
0000            0290 *
0000            0295 *
0000            0300       ORG RSTAD
0038            0305 *
0038 C3 6C 02   0310       JMP INTSV        THIS JUMP INSTRUCTION MUST BE STORED
003B            0315 *                      AT THE LOCATION CALLED DURING
003B            0320 *                      SYNTHESIZER INTERRUPTS (RST 0,
003B            0325 *                      RST 1, RST 2, ETC., AS DETERMINED
003B            0330 *                      BY AN ON-BOARD JUMPER). THE MEMORY
003B            0335 *                      LOCATION USED FOR THIS ASSEMBLY IS
003B            0340 *                      CORRECT FOR "VI 0" AND "PINT"
003B            0345 *                      CONNECTIONS TO THE S-100 BUS.
003B            0350 *
003B            0355 *
003B            0360       ORG START
0100            0365 *
0100 F3         0370       DI               DON'T WANT TO BE INTERRUPTED YET
0101 3E A8      0375       MVI A,0A8H       ENABLE HARDWARE AND INITIALIZE
```

**Printed p. 12**

```
0103 D3 31      0380       OUT PPORT        PARAMETERS TO XS, XF8, AND XA2
0105 32 D3 03   0385       STA PRMWD
0108 21 ED 03   0390       LXI H,BUFFR
010B F9         0395       SPHL
010C CD 14 02   0400       CALL CRLF
010F 3E 2A      0405       MVI A,'*'        DISPLAY PROMPT
0111 CD 32 02   0410       CALL COUT
0114 CD 23 02   0415 LOOP1 CALL CINP        GET A CHARACTER FROM KEYBOARD
0117 FE 7F      0420       CPI 7FH          DELETE LAST KEYBOARD ENTRY
0119 CA F8 01   0425       JZ BKSPC         ON "DELETE" OR "BACKSPACE"
011C FE 08      0430       CPI 08
011E CA F8 01   0435       JZ BKSPC
0121 FE 1B      0440       CPI 1BH          ESC KEY?
0123 CA 44 01   0445       JZ SPEAK         YES: REPEAT PHONEME SEQUENCE
0126 FE 03      0450       CPI 03           NO: CONTROL-C?
0128 C2 32 01   0455       JNZ STORE        NO: STORE ENTRY IN BUFFER
012B 3E 68      0460       MVI A,68H        YES: DISABLE SYNTHESIZER HARDWARE
012D D3 31      0465       OUT PPORT        AND RETURN TO MONITOR
012F C3 04 C0   0470       JMP 0C004H       THE ADDRESS OF YOUR MONITOR (OR
0132            0475 *                      OTHER PROGRAM) SHOULD BE PLACED
0132            0480 *                      IN THE LAST TWO BYTES OF THIS
0132            0485 *                      INSTRUCTION (LO ADDR, HI ADDR)
0132            0490 *
0132            0495 *
0132            0500 *
0132 4F         0505 STORE MOV C,A          STORE ENTRY FOR LATER USE
0133 77         0510       MOV M,A          THEN STORE IT IN BUFFER
0134 CD 32 02   0515       CALL COUT        ECHO THE ENTRY ON TERMINAL
0137 23         0520       INX H
0138 CD 3D 02   0525       CALL LINE        AUTOMATIC CR/LF, IF NECESSARY
013B 79         0530       MOV A,C          GET ENTRY BACK
013C FE 0D      0535       CPI 0DH          CONTINUE FILLING BUFFER
013E C2 14 01   0540       JNZ LOOP1        UNTIL A RET IS TYPED
0141 CD 14 02   0545       CALL CRLF
0144            0550 *
0144            0555 *
0144            0560 *
0144 21 ED 03   0565 SPEAK LXI H,BUFFR      INDEX BUFFER AND
0147 22 CD 03   0570 NEXT  SHLD BUFPT       UPDATE POINTER
014A CD 5B 02   0575       CALL LEN         CALCULATE NUMBER OF CHARACTERS
014D 79         0580       MOV A,C          MAKING UP NEXT STRING IN BUFFER
014E B7         0585       ORA A            DONE IF LENGTH = 0
014F CA 9F 01   0590       JZ DONE
0152 21 76 02   0595       LXI H,TBLNO-2    DETERMINE WHICH TABLE TO USE
0155 06 00      0600       MVI B,00
0157 09         0605       DAD B
0158 09         0610       DAD B
0159 5E         0615       MOV E,M
015A 23         0620       INX H
015B 56         0625       MOV D,M
015C EB         0630       XCHG
015D 22 CB 03   0635 LOOP3 SHLD TBLPT       STORE TABLE STARTING ADDRESS
0160 59         0640       MOV E,C
0161 2A CD 03   0645       LHLD BUFPT       INDEX CURRENT CHARACTER IN BUFFER
0164 22 CF 03   0650 LOOP4 SHLD BUFWK
0167 56         0655       MOV D,M          REG. D = CURRENT BUFFER CHARACTER
```

**Printed p. 13**

```
0168 2A CB 03   0660       LHLD TBLPT
016B 7E         0665       MOV A,M          GET NEXT CHARACTER FROM TABLE
016C FE FF      0670       CPI 0FFH         END OF TABLE?
016E CA E0 01   0675       JZ ERR           YES: INVALID BUFFER STRING
0171 BA         0680       CMP D            NO: FOUND A MATCH?
0172 C2 84 01   0685       JNZ NGOOD        NO: TRY NEXT STRING IN TABLE
0175 23         0690       INX H            YES: TRY TO MATCH REMAINING
0176 22 CB 03   0695       SHLD TBLPT       CHARACTERS IN BUFFER STRING
0179 1D         0700       DCR E            DOES STRING MATCH TABLE STRING?
017A CA 8B 01   0705       JZ TALK          YES: OUTPUT SC-01 CODE TO HARDWARE
017D 2A CF 03   0710       LHLD BUFWK       NO: CONTINUE TRYING TO FIND
0180 23         0715       INX H            A COMPLETE MATCH
0181 C3 64 01   0720       JMP LOOP4
0184            0725 *
0184 16 00      0730 NGOOD MVI D,00         COMPUTE NEXT TABLE ADDRESS TO TRY
0186 19         0735       DAD D
0187 23         0740       INX H
0188 C3 5D 01   0745       JMP LOOP3
018B            0750 *
018B            0755 *
018B            0760 *
018B 7E         0765 TALK  MOV A,M          GET DATA BYTE FROM TABLE
018C FE 40      0770       CPI 40H          CHECK TO SEE WHETHER IT IS A
018E D2 A6 01   0775       JNC CNTRL        PARAMETER CONTROL BYTE OR PHONEME
0191 47         0780       MOV B,A          TO BE SENT TO SC-01 AND PROCESS
0192 FB         0785       EI               IT ACCORDINGLY
0193 76         0790       HLT              WAIT FOR INTERRUPT FROM HARDWARE
0194            0795 *
0194 2A CF 03   0800 NTALK LHLD BUFWK       CHECK TO SEE IF THERE ARE ANY
0197 23         0805       INX H            MORE CHARACTER STRINGS IN
0198 7E         0810       MOV A,M          BUFFER TO PROCESS
0199 23         0815       INX H
019A FE 2C      0820       CPI ','          CONTINUE PROCESSING BUFFER
019C CA 47 01   0825       JZ NEXT          ENTRIES UNTIL BUFFER IS EMPTY
019F            0830 *
019F 06 3E      0835 DONE  MVI B,3EH        SEND PA1 CODE TO SYNTHESIZER IF
01A1 FB         0840       EI               THERE ARE NO MORE ENTRIES IN
01A2 76         0845       HLT              BUFFER
01A3 C3 00 01   0850       JMP START        AND THEN RE-START PROGRAM
01A6            0855 *
01A6            0860 *
01A6            0865 *
01A6 FE 50      0870 CNTRL CPI 50H          DETERMINE TYPE OF COMMAND
01A8 DA BC 01   0875       JC IFLCN         AND PROCESS IT
01AB FE 60      0880       CPI 60H
01AD DA C6 01   0885       JC AMPLT
01B0 FE 70      0890       CPI 70H
01B2 DA D0 01   0895       JC FRQCY
01B5            0900 *
01B5 E6 10      0905 MODE  ANI 10H          PROCESS XE, XS COMMANDS
01B7 1E EF      0910       MVI E,0EFH
01B9 C3 D4 01   0915       JMP RCORD
01BC            0920 *
01BC E6 03      0925 IFLCN ANI 03           PROCESS XI COMMAND
01BE 0F         0930       RRC
01BF 0F         0935       RRC
01C0 32 D2 03   0940       STA PITCH
```

**Printed p. 14**

```
01C3 C3 94 01   0945       JMP NTALK
01C6            0950 *
01C6 E6 03      0955 AMPLT ANI 03           PROCESS XA COMMAND
01C8 0F         0960       RRC
01C9 0F         0965       RRC
01CA 0F         0970       RRC
01CB 1E 9F      0975       MVI E,9FH
01CD C3 D4 01   0980       JMP RCORD
01D0            0985 *
01D0 E6 0F      0990 FRQCY ANI 0FH          PROCESS XF COMMAND
01D2 1E F0      0995       MVI E,0F0H
01D4            1000 *
01D4 47         1005 RCORD MOV B,A          UPDATE PARAMETER WORD FOR OUTPUT
01D5 3A D3 03   1010       LDA PRMWD
01D8 A3         1015       ANA E
01D9 B0         1020       ORA B
01DA 32 D3 03   1025       STA PRMWD
01DD C3 94 01   1030       JMP NTALK        SEE IF ANY MORE STRINGS TO PROCESS
01E0            1035 *
01E0            1040 *
01E0            1045 *
01E0 21 AB 03   1050 ERR   LXI H,MESG1      PRINT ERROR MESSAGE AND BAD CHARS.
01E3 CD 4D 02   1055       CALL PMESG
01E6 2A CD 03   1060       LHLD BUFPT
01E9 CD 4D 02   1065       CALL PMESG
01EC 21 B4 03   1070       LXI H,MESG2
01EF CD 4D 02   1075       CALL PMESG
01F2 CD 14 02   1080       CALL CRLF
01F5 C3 9F 01   1085       JMP DONE         ABORT PROCESSING BUFFER CONTENTS
01F8            1090 *
01F8            1095 *
01F8            1100 *
01F8 EB         1105 BKSPC XCHG             PROCESS DELETES AND BACKSPACES
01F9 21 ED 03   1110       LXI H,BUFFR
01FC 7D         1115       MOV A,L
01FD 93         1120       SUB E
01FE 7C         1125       MOV A,H
01FF 9A         1130       SBB D
0200 EB         1135       XCHG
0201 CA 14 01   1140       JZ LOOP1
0204 3E 5F      1145       MVI A,5FH        SEND UNDERLINE CHARACTER
0206 CD 32 02   1150       CALL COUT        TO TERMINAL
0209 2B         1155       DCX H
020A 3A D1 03   1160       LDA PSTN
020D 3D         1165       DCR A
020E 32 D1 03   1170       STA PSTN
0211 C3 14 01   1175       JMP LOOP1
0214            1180 *
0214            1185 *
0214            1190 * * * * * * * * *  SUBROUTINES  * * * * * * * * *
0214            1195 *
0214 3E 0A      1200 CRLF  MVI A,0AH
0216 CD 32 02   1205       CALL COUT        OUTPUT A LINE-FEED
0219 3E 0D      1210       MVI A,0DH
021B CD 32 02   1215       CALL COUT        OUTPUT A CARRIAGE-RETURN
021E AF         1220       XRA A
021F 32 D1 03   1225       STA PSTN         NEW LINE
0222 C9         1230       RET
```

**Printed p. 15**

```
0223            1235 *
0223            1240 *
0223            1245 *
0223 CD 1F C0   1250 CINP  CALL 0C01FH      USER'S KEYBOARD INPUT ROUTINE
0226 CA 23 02   1255       JZ CINP
0229 E6 7F      1260       ANI 7FH          STRIP PARITY BIT
022B C9         1265       RET              RETURN WITH CHAR. IN ACCUMULATOR
022C            1270       DS 6             EXTRA SPACE FOR USER'S ROUTINE
0232            1275 *
0232            1280 *
0232            1285 *
0232 E5         1290 COUT  PUSH H           USER'S OUTPUT ROUTINE
0233 47         1295       MOV B,A          CHARACTER TO BE OUTPUT IS IN
0234 CD 19 C0   1300       CALL 0C019H      THE ACCUMULATOR; RP H MUST BE
0237 E1         1305       POP H            RETURNED INTACT
0238 C9         1310       RET
0239            1315       DS 4             MORE USER SPACE
023D            1320 *
023D            1325 *
023D            1330 *
023D E5         1335 LINE  PUSH H           OUTPUT A CR/LF IF WE ARE
023E 21 D1 03   1340       LXI H,PSTN       GETTING CLOSE TO THE END
0241 34         1345       INR M            OF A LINE OF OUTPUT
0242 FE 2C      1350       CPI ','
0244 7E         1355       MOV A,M
0245 E1         1360       POP H
0246 C0         1365       RNZ
0247 FE 38      1370       CPI WIDTH-8      YOUR TERMINAL WIDTH MINUS 8
0249 D4 14 02   1375       CNC CRLF         SHOULD BE PLACED IN THE SECOND
024C C9         1380       RET              BYTE OF THIS INSTRUCTION
024D            1385 *
024D            1390 *
024D            1395 *
024D 7E         1400 PMESG MOV A,M          PRINT MESSAGE POINTED TO BY RP H
024E FE 2C      1405       CPI ','
0250 C8         1410       RZ               RETURN WHEN DELIMITING CHARACTER
0251 FE 0D      1415       CPI 0DH          IS FOUND
0253 C8         1420       RZ
0254 CD 32 02   1425       CALL COUT
0257 23         1430       INX H
0258 C3 4D 02   1435       JMP PMESG
025B            1440 *
025B            1445 *
025B            1450 *
025B 01 00 04   1455 LEN   LXI B,0400H      CLEAR LENGTH COUNTER AND
025E 7E         1460 LOOP2 MOV A,M          SET MAXIMUM LENGTH TO 4
025F FE 2C      1465       CPI ','
0261 C8         1470       RZ               COMPUTE LENGTH OF NEXT STRING
0262 FE 0D      1475       CPI 0DH          IN BUFFER TO BE PROCESSED
0264 C8         1480       RZ
0265 0C         1485       INR C
0266 05         1490       DCR B
0267 C8         1495       RZ
0268 23         1500       INX H
0269 C3 5E 02   1505       JMP LOOP2        LOOP UP TO FOUR TIMES
026C            1510 *
026C            1515 *
```

**Printed p. 16**

```
026C            1520 *
026C            1525 *           INTERRUPT-SERVICE ROUTINE
026C            1530 *             (VECTORED FROM RSTAD)
026C            1535 *
026C 3A D3 03   1540 INTSV LDA PRMWD        UPDATE PARAMETERS
026F D3 31      1545       OUT PPORT
0271 3A D2 03   1550       LDA PITCH        INCLUDE INFLECTION BITS
0274 B0         1555       ORA B            IN PHONEME DATA WORD
0275 D3 30      1560       OUT SPORT        SEND IT ALL TO THE SC-01
0277 C9         1565       RET
0278            1570 *
0278            1575 *
0278            1580 * * * * * * * * * *  TABLES  * * * * * * * * * *
0278            1585 *
0278 80 02      1590 TBLNO DW TBL1
027A AF 02      1595       DW TBL2
027C 01 03      1600       DW TBL3
027E 82 03      1605       DW TBL4
0280            1610 *
0280 4D         1615 TBL1  ASC 'M'
0281 0C         1620       DB 0CH
0282 4E         1625       ASC 'N'
0283 0D         1630       DB 0DH
0284 42         1635       ASC 'B'
0285 0E         1640       DB 0EH
0286 56         1645       ASC 'V'
0287 0F         1650       DB 0FH
0288 5A         1655       ASC 'Z'
0289 12         1660       DB 12H
028A 4C         1665       ASC 'L'
028B 18         1670       DB 18H
028C 4B         1675       ASC 'K'
028D 19         1680       DB 19H
028E 4A         1685       ASC 'J'
028F 1A         1690       DB 1AH
0290 48         1695       ASC 'H'
0291 1B         1700       DB 1BH
0292 47         1705       ASC 'G'
0293 1C         1710       DB 1CH
0294 46         1715       ASC 'F'
0295 1D         1720       DB 1DH
0296 44         1725       ASC 'D'
0297 1E         1730       DB 1EH
0298 53         1735       ASC 'S'
0299 1F         1740       DB 1FH
029A 41         1745       ASC 'A'
029B 20         1750       DB 20H
029C 50         1755       ASC 'P'
029D 25         1760       DB 25H
029E 4F         1765       ASC 'O'
029F 26         1770       DB 26H
02A0 49         1775       ASC 'I'
02A1 27         1780       DB 27H
02A2 55         1785       ASC 'U'
02A3 28         1790       DB 28H
02A4 59         1795       ASC 'Y'
02A5 29         1800       DB 29H
```

**Printed p. 17**

```
02A6 54         1805       ASC 'T'
02A7 2A         1810       DB 2AH
02A8 52         1815       ASC 'R'
02A9 2B         1820       DB 2BH
02AA 45         1825       ASC 'E'
02AB 2C         1830       DB 2CH
02AC 57         1835       ASC 'W'
02AD 2D         1840       DB 2DH
02AE FF         1845       DB 0FFH
02AF            1850 *
02AF 44 54      1855 TBL2  ASC 'DT'
02B1 04         1860       DB 04
02B2 41 32      1865       ASC 'A2'
02B4 05         1870       DB 05
02B5 41 31      1875       ASC 'A1'
02B7 06         1880       DB 06
02B8 5A 48      1885       ASC 'ZH'
02BA 07         1890       DB 07
02BB 49 33      1895       ASC 'I3'
02BD 09         1900       DB 09
02BE 49 32      1905       ASC 'I2'
02C0 0A         1910       DB 0AH
02C1 49 31      1915       ASC 'I1'
02C3 0B         1920       DB 0BH
02C4 43 48      1925       ASC 'CH'
02C6 10         1930       DB 10H
02C7 53 48      1935       ASC 'SH'
02C9 11         1940       DB 11H
02CA 4E 47      1945       ASC 'NG'
02CC 14         1950       DB 14H
02CD 4F 4F      1955       ASC 'OO'
02CF 17         1960       DB 17H
02D0 41 59      1965       ASC 'AY'
02D2 21         1970       DB 21H
02D3 59 31      1975       ASC 'Y1'
02D5 22         1980       DB 22H
02D6 41 48      1985       ASC 'AH'
02D8 24         1990       DB 24H
02D9 41 45      1995       ASC 'AE'
02DB 2E         2000       DB 2EH
02DC 55 48      2005       ASC 'UH'
02DE 33         2010       DB 33H
02DF 4F 32      2015       ASC 'O2'
02E1 34         2020       DB 34H
02E2 4F 31      2025       ASC 'O1'
02E4 35         2030       DB 35H
02E5 49 55      2035       ASC 'IU'
02E7 36         2040       DB 36H
02E8 55 31      2045       ASC 'U1'
02EA 37         2050       DB 37H
02EB 54 48      2055       ASC 'TH'
02ED 39         2060       DB 39H
02EE 45 52      2065       ASC 'ER'
02F0 3A         2070       DB 3AH
02F1 45 48      2075       ASC 'EH'
02F3 3B         2080       DB 3BH
```

**Printed p. 18**

```
02F4 45 31      2085       ASC 'E1'
02F6 3C         2090       DB 3CH
02F7 41 57      2095       ASC 'AW'
02F9 3D         2100       DB 3DH
02FA 58 45      2105       ASC 'XE'
02FC 70         2110       DB 70H
02FD 58 53      2115       ASC 'XS'
02FF 80         2120       DB 80H
0300 FF         2125       DB 0FFH
0301            2130 *
0301 45 48 33   2135 TBL3  ASC 'EH3'
0304 00         2140       DB 00
0305 45 48 32   2145       ASC 'EH2'
0308 01         2150       DB 01
0309 45 48 31   2155       ASC 'EH1'
030C 02         2160       DB 02
030D 50 41 30   2165       ASC 'PA0'
0310 03         2170       DB 03
0311 41 48 32   2175       ASC 'AH2'
0314 08         2180       DB 08
0315 41 57 31   2185       ASC 'AW1'
0318 13         2190       DB 13H
0319 41 48 31   2195       ASC 'AH1'
031C 15         2200       DB 15H
031D 4F 4F 31   2205       ASC 'OO1'
0320 16         2210       DB 16H
0321 55 48 33   2215       ASC 'UH3'
0324 23         2220       DB 23H
0325 41 45 31   2225       ASC 'AE1'
0328 2F         2230       DB 2FH
0329 41 57 32   2235       ASC 'AW2'
032C 30         2240       DB 30H
032D 55 48 32   2245       ASC 'UH2'
0330 31         2250       DB 31H
0331 55 48 31   2255       ASC 'UH1'
0334 32         2260       DB 32H
0335 54 48 56   2265       ASC 'THV'
0338 38         2270       DB 38H
0339 50 41 31   2275       ASC 'PA1'
033C 3E         2280       DB 3EH
033D 58 49 31   2285       ASC 'XI1'
0340 40         2290       DB 40H
0341 58 49 32   2295       ASC 'XI2'
0344 41         2300       DB 41H
0345 58 49 33   2305       ASC 'XI3'
0348 42         2310       DB 42H
0349 58 49 34   2315       ASC 'XI4'
034C 43         2320       DB 43H
034D 58 41 30   2325       ASC 'XA0'
0350 53         2330       DB 53H
0351 58 41 31   2335       ASC 'XA1'
0354 52         2340       DB 52H
0355 58 41 32   2345       ASC 'XA2'
0358 51         2350       DB 51H
0359 58 41 33   2355       ASC 'XA3'
035C 50         2360       DB 50H
```

**Printed p. 19**

```
035D 58 46 31   2365       ASC 'XF1'
0360 6F         2370       DB 6FH
0361 58 46 32   2375       ASC 'XF2'
0364 6E         2380       DB 6EH
0365 58 46 33   2385       ASC 'XF3'
0368 6D         2390       DB 6DH
0369 58 46 34   2395       ASC 'XF4'
036C 6C         2400       DB 6CH
036D 58 46 35   2405       ASC 'XF5'
0370 6B         2410       DB 6BH
0371 58 46 36   2415       ASC 'XF6'
0374 6A         2420       DB 6AH
0375 58 46 37   2425       ASC 'XF7'
0378 69         2430       DB 69H
0379 58 46 38   2435       ASC 'XF8'
037C 68         2440       DB 68H
037D 58 46 39   2445       ASC 'XF9'
0380 67         2450       DB 67H
0381 FF         2455       DB 0FFH
0382            2460 *
0382 53 54 4F 50 2465 TBL4  ASC 'STOP'
0386 3F         2470       DB 3FH
0387 58 46 31 30 2475       ASC 'XF10'
038B 66         2480       DB 66H
038C 58 46 31 31 2485       ASC 'XF11'
0390 65         2490       DB 65H
0391 58 46 31 32 2495       ASC 'XF12'
0395 64         2500       DB 64H
0396 58 46 31 33 2505       ASC 'XF13'
039A 63         2510       DB 63H
039B 58 46 31 34 2515       ASC 'XF14'
039F 62         2520       DB 62H
03A0 58 46 31 35 2525       ASC 'XF15'
03A4 61         2530       DB 61H
03A5 58 46 31 36 2535       ASC 'XF16'
03A9 60         2540       DB 60H
03AA FF         2545       DB 0FFH
03AB            2550 *
03AB            2555 *
03AB            2560 *
03AB            2565 * * * * * * * * * *  MESSAGES  * * * * * * * * * *
03AB            2570 *
03AB 54 59 50 49 2575 MESG1 ASC 'TYPING ",'
     4E 47 20 22
     2C
03B4 22 20 48 41 2580 MESG2 ASC '" HAS NO MEANING HERE.,'
     53 20 4E 4F
     20 4D 45 41
     4E 49 4E 47
     20 48 45 52
     45 2E 2C
03CB            2585 *
03CB            2590 *
03CB            2595 * * * * * * * *  SCRATCHPAD AREA  * * * * * * * *
03CB            2600 * *             (MUST EXIST IN RAM)             * *
03CB            2605 *
03CB            2610 TBLPT DS 2
```

**Printed p. 20**

```
03CD            2615 BUFPT DS 2
03CF            2620 BUFWK DS 2
03D1            2625 PSTN  DS 1             PRINT HEAD/CURSOR POSITION
03D2 00         2630 PITCH DB 00            XI1 AT START-UP
03D3            2635 PRMWD DS 1             PARAMETER WORD
03D4            2640       DS 25            STACK AREA
03ED 2C         2645 BUFFR DB ','           BUFFER AREA
03EE            2650       DS 100
0452            2655 *
0452            2660 *
0452            2665 * * * * * * * *   HAPPY TALKING !     * * * * * * *
```

**Transcription notes (pp. 11–20).**

- The page is tilted. A comment is put on the line that its first word is level with.
- The box at the top of p. 11 and the banner lines (`TABLES`, `MESSAGES`, `SCRATCHPAD AREA`,
  `HAPPY TALKING !`) are copied with regular spacing. The position of each `*` is close to the
  page, not exact.
- The quote marks of `'*'`, `','` and the `ASC` strings are printed as `’`. They are written `'`
  here.
- An object field of 4 bytes (`ASC 'STOP'`, `ASC 'XF10'` and the messages) fills its column.
  One space is put before the line number here.
- The bytes of `MESG1` and `MESG2` run on below the line, as printed.
- On p. 17, the object byte at `02B4` is smudged and looks like `J5`. The source is `DB 05`, so
  it is written `05`.
- The checker passes on the whole program, `0000` to `0452`, with every label defined in the
  listing.
- The four phoneme tables (`TBLNO` to the last `0FFH`, `0278`–`03AA`) are 307 bytes. That is
  the size of the space for the tables in the CP/M listing (`02B9`–`03EB`).
- The comment on p. 12 says that `0A8H` sets XS, XF8 and XA2. The table on p. 18 agrees:
  `XA2` is code `51H`, and `AMPLT` (p. 14) puts `51H AND 03` = `01` in bits 6–5. Bits 6–5 of
  `A8` are `01`. The XA field holds `3−n`, so XA2 is the medium setting.

---

## Speech Development System, CP/M (printed pp. 21–26)

The Vocabulary Development System, changed to run under CP/M on a North Star Horizon. The
program reads a line of phoneme names and commands, separated by commas, and speaks it. The
commands are in the summary on p. 21. `CTRL-U` is new in this version. It copies the last line
into the new line. The interrupt service routine `INTSV` is assembled at `02B1` and copied to
`30H` (VI6) when the program starts. The routine sends `PITCH OR B` to the data port. The
control word starts as `0A8H`. The program clears bit 7 (`ANI 7FH`) after each line and writes
`68H` on `CTRL-C`.

**Printed p. 21**

```
               ;THE FOLLOWING PROGRAM WAS DERIVED FROM THE PRECEDING LISTING
               ;TO ALLOW INSTALLATION OF THE PAIA SS-100 SPEECH SYNTHESIZER
               ;BOARD TO BE INSTALLED ON A NORTH-STAR HORIZON SYSTEM RUNNING
               ;UNDER THE CP/M OPERATINGS SYSTEM.
               ;WITH THE EXCEPTION OF THE ADDED CTRL-U COMMAND, THIS PROGRAM
               ;IS FUNCTIONALLY IDENTICAL TO THE PREVIOUS LISTING.
               ;THE CTRL-U COMMAND AIDS IN THE DEVELOPEMENT OF LONG PHRASES
               ;BY COPYING THE "OLD" COMMAND BUFFER CONTENTS TO THE "NEW" COMMAND
               ;BUFFER WITHOUT HAVING TO RE-TYPE THE LINE.
               ;
               ;*******************************************************************
               ;*                                                                 *
               ;*                PAIA SPEECH DEVELOPEMENT SYSTEM                  *
               ;*                                                                 *
               ;*                              BY                                 *
               ;*                       RANDY CARLSTROM                           *
               ;*                                                                 *
               ;*                CP/M INTERFACE BY JOHN SIMONTON                  *
               ;*                                                                 *
               ;*           COPYRIGHT 1982 BY PAIA ELECTRONICS, INC.              *
               ;*******************************************************************
               ;
               ;
               ;                         COMMAND SUMMARY
               ;
               ;
               ;       XA0-3                   SET AMPLITUDE
               ;       XF1-16                  SET CLOCK FREQUENCY
               ;       XI1-4                   SET INFLECTION
               ;       XE                      SET TO EFFECTS MODE
               ;       XS                      SET TO SPEECH MODE
               ;       ESCAPE                  REPEAT SEQUENCE
               ;       CTRL-C                  EXIT PROGRAM
               ;       CTRL-U                  COPY OLD INPUT BUFFER
               ;
               ;
0100 =         INIT    EQU 100H        ;PROGRAM STARTS AT CP/M TPA
0005 =         BDOS    EQU 5           ;CP/M BDOS ENTRY POINT
0001 =         CONIN   EQU 1           ;CONSOLE INPUT FUNCTION
0002 =         CONOUT  EQU 2           ;CONSOLE OUTPUT FUNCTION
000B =         CONST   EQU 0BH         ;CONSOLE STATUS FUNCTION
0030 =         INTAD   EQU     30H     ;INTERRUPT VECTOR ADDRESS FOR VI6
0050 =         WIDTH   EQU     80      ;TERMINAL WIDTH (CHARACTERS)
               ;
               ;
               ;       - I/O PORT ASSIGNMENTS -
               ;
0030 =         SPORT   EQU     30H     ;SYNTHESIZER DATA PORT
0031 =         PPORT   EQU     31H     ;PARAMETER DATA PORT
               ;
               ; ****************** MAIN PROGRAM ****************************
                         (MAY BE EITHER RAM OR ROM)
               ;
               ;
0100                   ORG INIT
               ;
0100 F3                DI              ;NOT READY FOR INTERRUPTS YET, DISABLE
0101 317404            LXI SP,BUFFR    ;INITIALIZE STACK POINTER
               ;
               ;THE FOLLOWING BLOCK MOVES THE INTERRUPT SERVICE ROUTINE FROM ITS
               ;ASSEMBLED LOCATION TO THE VI6 INTERRUPT VECTOR ADDRESS
               ;
0104 21B102            LXI H,INTSV     ;ADDRESS OF INTERRUPT SERVICE SUB BLOCK
0107 113000            LXI D,INTAD     ;ADDRESS OF INTERRUPT VECTOR
010A 7E        MLOOP   MOV A,M         ;GET BYTE FROM HIGH ADDRESS
010B FEFF              CPI 0FFH        ;MOVER TERMINATOR CHARACTER?
010D CA1801            JZ SETPAR       ;YES, QUIT MOVING
0110 EB                XCHG            ;NO, EXCHANGE POINTERS
0111 77                MOV M,A         ;PUT BYTE TO LOW ADDRESS
```

**Printed p. 22**

```
0112 EB                XCHG            ;MUSICAL POINTERS
0113 23                INX H           ;POINT TO NEXT SOURCE BYTE
0114 13                INX D           ;AND DESTINATION BYTE
0115 C30A01            JMP MLOOP       ;JUMP ALWAYS TO LOOP FOR MORE
               ;
0118 3EA8      SETPAR  MVI A,0A8H      ;BYTE TO SET SS-100 PARAMETERS
011A 325A04            STA PRMWD       ;SAVE BYTE FOR LATER USE
               ;
011D CD5802            CALL CRLF       ;SEND CR/LF
0120 CD5802            CALL CRLF       ;AND ANOTHER
0123 21EC03            LXI H,BANNER    ;POINT TO START OF BANNER
0126 CD9202            CALL PMESG      ;PRINT BANNER
0129 CD5802            CALL CRLF       ;SEND TWO CR/LF'S
012C CD5802            CALL CRLF       ;THE SECOND
012F C37701            JMP SPEAK       ;ANNOUNCE BANNER
               ;
               ;
0132 CD5802    START   CALL CRLF       ;SEND A CR/LF
0135 212C04            LXI H,PROMPT    ;POINT TO PROMPT CHARACTER
0138 7E                MOV A,M         ;GET PROMPT CHARACTER
0139 CD7902            CALL COUT       ;PROMPT USER
               ;
013C 217404            LXI H,BUFFR     ;LOAD HL INDEX WITH POINTER TO INPUT BUFFER
013F 317404            LXI SP,BUFFR    ;INITIALIZE STACK POINTER
0142 CD6702    LOOP1   CALL CIN        ;GET A CHARACTER FROM THE CONSOLE
0145 FE7F              CPI 7FH         ;DELETE LAST KEYBOARD ENTRY
0147 CA4102            JZ BKSPC        ;ON "DELETE" AND "BACKSPACE"
014A FE08              CPI 08
014C CA4102            JZ BKSPC
014F FE1B              CPI 1BH         ;ESC KEY?
0151 CA7701            JZ SPEAK        ;YES, REPEAT PHONEME SEQUENCE
0154 FE15              CPI 15H         ;CTRL-U ?
0156 C26101            JNZ NTEST       ;NO, BRANCH TO TEST NEXT
0159 7E                MOV A,M         ;GET THE OLD BUFFER CHARACTER
015A CD7902            CALL COUT       ;PRINT OLD CHARACTER
015D 7E                MOV A,M         ;GET THE CHARACTER AGAIN
015E C36B01            JMP STORE       ;AND GO TO PROCESS AS IF NEW
0161 FE03      NTEST   CPI 03          ;CONTROL - C?
0163 C26B01            JNZ STORE       ;NO, STORE ENTRY IN BUFFER
0166 3E68              MVI A,68H       ;YES, DISABLE SYNTHESIZER HARDWARE
0168 D331              OUT PPORT
016A C7                RST 0           ;AND RETURN TO CP/M
               ;
016B 77        STORE   MOV M,A         ;STORE CHARACTER IN BUFFER
016C 23                INX H           ;POINT TO NEXT BUFFER LOCATION
016D 57                MOV D,A         ;TEMPORARILY STORE CHARACTER IN D
016E CD8202            CALL LINE       ;AUTOMATIC CR/LF, IF NECESSARY
0171 7A                MOV A,D         ;RETRIEVE THE CONSOLE CHARACTER
0172 FE0D              CPI 0DH         ;CONTINUE FILLING BUFFER UNTIL
0174 C24201            JNZ LOOP1       ;A RET IS TYPED, THEN SPEAK LINE
               ;
               ;STARTING FROM BEGINNING OF INPUT BUFFER, PARSE INPUT BUFFER
               ;COMMAND INTO PHONEME SYMBOLS AND TRANSLATE TO CONROL AND
               ;PHONEME BYTES
               ;
0177 217404    SPEAK   LXI H,BUFFR     ;INDEX BUFFER AND
017A 225404    NEXT    SHLD BUFPT      ;UPDATE POINTER
017D CDA002            CALL LEN        ;COUNT CHARACTERS IN PHONEME SUBSTRING
0180 79                MOV A,C         ;ACC= NO. OF CHARACTERS
0181 B7                ORA A           ;TEST FOR 0 CHARACTER STRING
0182 CAD801            JZ DONE         ;DONE IF LENGTH = 0
               ;
               ;DETERMINE WHICH TABLE TO USE
               ;
0185 21B702            LXI H,TBLNO-2   ;GET POINTER TO TABLE ADDRESSES
0188 0600              MVI B,00        ;BC PAIR NOW 000LEN, LEN=(1-4)
018A 09                DAD B           ;TWO BYTES PER ADDRESS, SO ADD
018B 09                DAD B           ;TWICE INDEX TO HL REGISTER PAIR
018C 5E                MOV E,M         ;GET LOW BYTE OF ADDRESS
```

**Printed p. 23**

```
018D 23                INX H           ;POINT TO HIGH BYTE
018E 56                MOV D,M         ;GET HIGH BYTE OF ADDRESS
018F EB                XCHG            ;GET READY TO SAVE TABLE POINTER
               ;
               ;COMPARE TABLE ENTRY TO INPUT BUFFER SUB-STRING
               ;
0190 225204    LOOP3   SHLD TBLPT      ;STORE TABLE STARTING ADDRESS
0193 59                MOV E,C         ;PREPARE REG E FOR USE AS CHARACTER COUNTER
0194 2A5404            LHLD BUFPT      ;POINT TO FIRST CHARACTER IN SUB-STRING
0197 225604    LOOP4   SHLD BUFWK      ;SAVE SUB-STRING CHARACTER POINTER
019A 56                MOV D,M         ;REG D = CURRENT BUFFER CHARACTER
019B 2A5204            LHLD TBLPT      ;POINT TO PRESENT LOOK-UP TABLE CHARACTER
019E 7E                MOV A,M         ;GET NEXT CHARACTER FROM TABLE
019F FEFF              CPI 0FFH        ;END OF TABLE?
01A1 CA2602            JZ ERR          ;YES, INVALID BUFFER STRING
01A4 BA                CMP D           ;NO, FOUND A MATCH?
01A5 C2B701            JNZ NGOOD       ;NO, TRY NEXT STRING IN TABLE
01A8 23                INX H           ;POINT TO NEXT TABLE CHARACTER
01A9 225204            SHLD TBLPT      ;SAVE THE POINTER
01AC 1D                DCR E           ;ONE LESS CHAR. IF NONE LEFT, ALL MATCH AND
01AD CABE01            JZ TALK         ;HL POINTS TO PHONEME CODE, GO TALK
01B0 2A5604            LHLD BUFWK      ;MORE, GET POINTER TO SUB-STRING CHARACTER
01B3 23                INX H           ;POINT TO NEXT CHARACTER
01B4 C39701            JMP LOOP4       ;AND JUMP TO CONTINUE MATCHING
               ;
01B7 1600      NGOOD   MVI D,00        ;COMPUTE NEXT TABLE ADDRESS TO TRY
01B9 19                DAD D           ;ADD BYTES LEFT TO MATCH TO TABLE POINTER
01BA 23                INX H           ;SKIP PHONEME (OR CONTROL CODE) BYTE
01BB C39001            JMP LOOP3       ;JUMP TO CONTINUE TABLE SEARCH
               ;
               ;PREPARE FOR HARDWARE AND CPU INTERRUPT
               ;
01BE 7E        TALK    MOV A,M         ;GET DATA BYTE FROM TABLE
01BF FE40              CPI 40H         ;CHECK TO SEE WHETHER IT IS A
01C1 D2EC01            JNC CNTRL       ;PARAMETER CONTROL BYTE OR PHONEME
01C4 47                MOV B,A         ;TO BE SENT TO SC-01 AND PROCESS
01C5 F3                DI              ;DISABLE INTERRUPTS SET BY CP/M
01C6 3A5A04            LDA PRMWD       ;GET SS-100 PARAMETER CONTROL BYTE
01C9 D331              OUT PPORT       ;SEND TO CARD (INT HDWR ON)
01CB FB                EI              ;ENABLE CPU INTERRUPTS
01CC 76                HLT             ;WAIT FOR INTERRUPT FROM HARDWARE
               ;
01CD 2A5604    NTALK   LHLD BUFWK      ;CHECK TO SEE IF THERE ARE ANY
01D0 23                INX H           ;MORE CHARACTER STRINGS IN
01D1 7E                MOV A,M         ;BUFFER TO PROCESS
01D2 23                INX H
01D3 FE2C              CPI ','         ;CONTINUE PROCESSING BUFFER
01D5 CA7A01            JZ NEXT         ;ENTRIES UNTIL BUFFER IS EMPTY
               ;
01D8 F3        DONE    DI              ;DISABLE CPU INT. WHICH CP/M MAY HAVE SET
01D9 063E              MVI B,3EH       ;PREPARE PA1 PHONEME TO STOP SYNTHESIZER
01DB 3A5A04            LDA PRMWD       ;GET THE CURRENT PARAMETER
01DE D331              OUT PPORT       ;AND UPDATE SS-100 REGISTER (INT. HDWR ON)
01E0 FB                EI              ;NOW ENABLE CPU INTERRUPTS
01E1 76                HLT             ;AND WAIT FOR SS-100 INTERRUPT
01E2 3A5A04            LDA PRMWD       ;GET PARAMETER WORD
01E5 E67F              ANI 7FH         ;DISABLE SS-100 INTERRUPT HARDWARE
01E7 D331              OUT PPORT       ;PUT TO SS-100 PARAMETER REGISTER
01E9 C33201            JMP START       ;AND THEN RE-START PROGRAM
               ;
               ;
               ;
01EC FE50      CNTRL   CPI 50H         ;DETERMINE TYPE OF COMMAND
01EE DA0202            JC IFLCN        ;AND PROCESS IT
01F1 FE60              CPI 60H
01F3 DA0C02            JC AMPLT
01F6 FE70              CPI 70H
01F8 DA1602            JC FRQCY
               ;
```

**Printed p. 24**

```
01FB E610      MODE    ANI 10H         ;PROCESS XE, XS COMMANDS
01FD 1EEF              MVI E,0EFH
01FF C31A02            JMP RCORD
               ;
0202 E603      IFLCN   ANI 03          ;PROCESS XI COMMAND
0204 0F                RRC
0205 0F                RRC
0206 325904            STA PITCH
0209 C3CD01            JMP NTALK
               ;
020C E603      AMPLT   ANI 03          ;PROCESS XA COMMAND
020E 0F                RRC
020F 0F                RRC
0210 0F                RRC
0211 1E9F              MVI E,9FH
0213 C31A02            JMP RCORD
               ;
0216 E60F      FRQCY   ANI 0FH         ;PROCESS XF COMMAND
0218 1EF0              MVI E,0F0H
               ;
021A 47        RCORD   MOV B,A         ;UPDATE PARAMETER WORD FOR OUTPUT
021B 3A5A04            LDA PRMWD
021E A3                ANA E
021F B0                ORA B
0220 325A04            STA PRMWD
0223 C3CD01            JMP NTALK       ;SEE IF ANY MORE STRINGS TO PROCESS
               ;
               ;
0226 CD5802    ERR     CALL CRLF       ;CARRIAGE RETURN/LINE FEED
0229 210C04            LXI H,MESG1     ;PRINT ERROR MESSAGE AND BAD CHARACTER
022C CD9202            CALL PMESG      ;PRINT FIRST PART OF MESSAGE
022F 2A5404            LHLD BUFPT      ;POINTER TO OFFENDING CHARACTER STRING
0232 CD9202            CALL PMESG      ;PRINT STRING (UNTIL "," IN STRING)
0235 211504            LXI H,MESG2     ;POINT TO SECOND HALF OF MESSAGE
0238 CD9202            CALL PMESG      ;PRINT MESSAGE
023B CD5802            CALL CRLF       ;AND CR/LF
023E C3D801            JMP DONE        ;ABORT PROCESSING BUFFER CONTENTS
               ;
               ;
0241 EB        BKSPC   XCHG            ;MOVE PRESENT BUFFER POINTER TO DE PAIR
0242 217404            LXI H,BUFFR     ;GET POINTER TO START OF INPUT BUFFER
0245 7D                MOV A,L         ;COMPARE PRESENT BUFFER POINTER TO
0246 93                SUB E           ;THE POINTER TO THE START OF THE
0247 7C                MOV A,H         ;BUFFER AND IF THE SAME, BUFFER IS EMPTY
0248 9A                SBB D
0249 EB                XCHG            ;GET CURRENT BUFFER POINTER
024A CA4201            JZ LOOP1        ;ZERO, NO CHARACTERS TO DELETE, SO LEAVE
024D 2B                DCX H           ;BACK UP POINTER ONE CHARACTER
024E 3A5804            LDA PSTN        ;SAVE NEW PRINT HEAD POSITION
0251 3D                DCR A           ;MOVE "HEAD" BACK ONE SPACE
0252 325804            STA PSTN        ;SAVE NEW POSITION
0255 C34201            JMP LOOP1       ;LOOP FOR MORE CHARACTERS
               ;
               ;
               ;********************* SUBROUTINES ****************************
               ;
0258 3E0A      CRLF    MVI A,0AH
025A CD7902            CALL COUT       ;OUTPUT A LINE-FEED
025D 3E0D              MVI A,0DH
025F CD7902            CALL COUT       ;OUTPUT A CARRIAGE-RETURN
0262 AF                XRA A           ;ZERO ACC
0263 325804            STA PSTN        ;NEW LINE, HEAD POSITION =0
0266 C9                RET             ;RETURN
               ;
               ;GET A CHARACTER FROM THE CONSOLE
               ;
0267 E5        CIN     PUSH H          ;PROTECT CURRENT POINTER FROM CP/M
```

**Printed p. 25**

```
0268 0E0B      CINLP   MVI C,CONST     ;PREPARE CONSOLE STATUS FUNCTION
026A CD0500            CALL BDOS       ;GET CONSOLE STATUS
026D FE00              CPI 0           ;0 = NO CHARACTER?
026F CA6802            JZ CINLP        ;LOOP UNTIL CHARACTER READY
0272 0E01              MVI C,CONIN     ;PREPARE CONSOLE INPUT FUNCTION
0274 CD0500            CALL BDOS       ;GET CONSOLE CHARACTER
0277 E1                POP H           ;RETRIEVE POINTER
0278 C9                RET             ;AND LEAVE
               ;
               ;SEND A CHARACTER TO THE CONSOLE
               ;
0279 E5        COUT    PUSH H          ;SAVE CURRENT INPUT BUFFER POINTER
027A 5F                MOV E,A         ;OUTPUT CHARACTER FROM ACC. TO REG E
027B 0E02              MVI C,CONOUT    ;PREPARE FOR CONSOLE OUTPUT
027D CD0500            CALL BDOS       ;OUTPUT CHARACTER
0280 E1                POP H           ;RETRIEVE POINTER
0281 C9                RET             ;RETURN
               ;
               ;GENERATE AUTO CR/LF WHEN NEEDED
               ;
0282 E5        LINE    PUSH H          ;SAVE CURRENT INPUT BUFFER POINTER
0283 215804            LXI H,PSTN      ;POINT TO HEAD POSITION COUNTER
0286 34                INR M           ;ADVANCE HEAD POSITION ONE SPACE
0287 FE2C              CPI ','         ;IS CURRENT CHARACTER A SUBSTRING DELIMITER?
0289 7E                MOV A,M         ;ACC = HEAD POSITION POINTER
028A E1                POP H           ;INPUT BUFFER POINTER BACK TO HL REG
028B C0                RNZ             ;IF CHARACTER WAS NOT "," - RETURN
028C FE48              CPI WIDTH-8     ;OTHERWISE, WITHIN 8 CHARACTERS OF LINE WIDTH?
028E D45802            CNC CRLF        ;YES, SENT CR/LF TO CONSOLE
0291 C9                RET             ;THEN RETURN
               ;
               ;PRINT A MESSAGE
               ;
0292 7E        PMESG   MOV A,M         ;PRINT MESSAGE POINTED TO BY RP H
0293 FE2C              CPI ','
0295 C8                RZ              ;RETURN WHEN DELIMITING CHARACTER
0296 FE0D              CPI 0DH         ;IS FOUND
0298 C8                RZ
0299 CD7902            CALL COUT
029C 23                INX H
029D C39202            JMP PMESG
               ;
               ;
               ;COUNT THE NUMBER OF CHARACTERS IN THE NEXT PHONEME STRING
               ;
02A0 010004    LEN     LXI B,0400H     ;INIT. C=CHAR. 0; B= 4 CHAR. MAXIMUM
02A3 7E        LOOP2   MOV A,M         ;GET NEXT PHONEME SUBSTRING CHARACTER
02A4 FE2C              CPI ','         ;IS IT A DELIMITER?
02A6 C8                RZ              ;YES, RETURN WITH # OF SUBSTRING CHARS IN C REG
02A7 FE0D              CPI 0DH         ;IS IT A CARRIAGE RETURN?
02A9 C8                RZ              ;YES, RETURN WITH # OF SUBSTRING CHARS IN C REG
02AA 0C                INR C           ;NEITHER DELIMITER, ADD ONE CHAR TO COUNT
02AB 05                DCR B           ;ONE LESS CHARACTER POSSIBLE
02AC C8                RZ              ;NO POSSIBLE LEFT, RETURN
02AD 23                INX H           ;SOME LEFT, POINT TO NEXT
02AE C3A302            JMP LOOP2       ;LOOP UP TO FOUR TIMES
               ;
               ;
               ;*************** INTERRUPT SERVICE ROUTINE ***************************
               ;
               ;NOTE THAT THIS ROUTINE IS MOVED INTO PAGE ZERO ADDRESS SPACE BY THE
               ;MOVER CODE AT THE BEGINNING OF THE PROGRAM.  THIS ROUTINE DOES NOT
               ;RUN IN THIS ADDRESS SPACE.
               ;
02B1 3A5904    INTSV   LDA PITCH       ;INCLUDE INFLECTION BITS
02B4 B0                ORA B           ;IN PHONEME DATA WORD
02B5 D330              OUT SPORT       ;OUTPUT A PHONEME TO SC-01
02B7 C9                RET             ;THEN RETURN
02B8 FF                DB 0FFH         ;MOVER TERMINATOR CHARACTER
```

**Printed p. 26**

```
               ;
               ;
               ;************************* TABLES *******************************
               ;
;IN THE INTEREST OF CONSERVING SPACE AND PAPER, THE PHONEME
;TRANSLATION TABLES HAVE BEEN DELETED FROM THIS LISTING.
;THESE TABLES ARE IDENTICAL TO THOSE IN THE PRECEDING PROGRAM
;AND MAY BE COPIED DIRECTLY.
               ;
               ;                       MESSAGES
               ;
03EC 5041494120BANNER  DB 'PAIA SPEECH DEVELOPEMENT SYSTEM,'
               ;
040C 545950494EMESG1   DB 'TYPING ",'
0415 2220484153MESG2   DB '" HAS NO MEANING HERE.,'
               ;
042C 3A        PROMPT  DB ':'
               ;
042D 434F505952CPYRT   DB 'COPYRIGHT 1982, PAIA ELECTRONICS,INC.'
               ;
               ;                       SCRATCH PAD AREA
               ;                       (MUST EXIST IN RAM)
               ;
0452           TBLPT   DS 2            ;POINTER TO TRANSLATION TABLES
0454           BUFPT   DS 2            ;POINTER TO INPUT BUFFER
0456           BUFWK   DS 2            ;TEMPORARY BUFFER POINTER
0458           PSTN    DS 1            ;PRINT HEAD POSITION
0459 00        PITCH   DB 00           ;XI1 AT START-UP
045A           PRMWD   DS 1            ;PARAMETER WORD
045B                   DS 25           ;STACK AREA
0474 5041312C50BUFFR   DB 'PA1,P,AH1,Y1,Y,AH2,PA1,S,P,E,T,CH,PA0,D,D,EH3,V,EH1,L,'
04AA 5548312C50        DB 'UH1,P,M,EH3,N,T,PA0,S,I1,S,T,UH1,M,PA1,PA1,R,EH3,D,Y1'
04DF 0D0D              DB 0DH,0DH
04E1                   DS 100
```

**Transcription notes.**

- The phoneme translation tables are not printed (p. 26). They are at `02B9` to `03EB`, 307
  bytes. `TBLNO` is `02B9`, because `LXI H,TBLNO-2` assembles to `21B702`. The booklet says
  the tables are the same as in the Vocabulary Development System.
- The line `(MAY BE EITHER RAM OR ROM)` on p. 21 has no `;` on the page ⚠.
- On p. 21, a small mark follows `ORG INIT`. It is not copied.
- The quote marks of the strings are printed as `´`. They are written `'` here.
- The `CPYRT` string has no space between `ELECTRONICS,` and `INC.`. The next address, `0452`,
  shows that the string has 37 bytes. On the page, the period is followed by a gap before the
  quote. The gap is not a space.
- The length of each string agrees with the address of the next line. That checks the bytes
  that the object column does not show.
- `BUFFR` holds the first line to speak: the banner "PAIA SPEECH DEVELOPMENT SYSTEM" as
  phonemes, ended by two `0DH`. `SETPAR` jumps to `SPEAK` to say it. The buffer is also the
  top of the stack (`LXI SP,BUFFR`).
- The service routine does not do `EI`. The `EI` before each `HLT` enables the interrupt again.
- `LINE` compares the typed character with `','` after `INR M`. `INR M` does not change the
  carry flag, but it changes the zero flag. The `CPI ','` sets the flags again, so the test is
  correct.

---

## Simple Speech Output Driver (printed p. 27)

A subroutine that speaks one phrase. It writes `0A8H` to the parameter port (interrupt enabled,
volume `01`, speech mode, XF8). It then waits with `EI` and `HLT` for each interrupt. The
interrupt service routine is at `30H`, the restart address of VI6 (`RST 6`). The service routine
sends the byte at `HL` to the data port and returns to the `INX H` after the `HLT`. The byte
`0FFH` ends the phrase. The sample phrase is "COME SEE ME".

**Printed p. 27**

```
               ;THE FOLLOWING PROGRAM WAS DEVELOPED ON A NORTH-STAR HORIZON
               ;RUNNING UNDER CP/M.  IT IS USEFUL BOTH AS A SPEECH DRIVER SUBROUTINE
               ;AND AS A QUICK WAY TO VERIFY THE OPERATION OF THE PAIA SS-100
               ;SPEECH SYNTHESIZER BOARD.
               ;
               ;*******************************************************************
               ;*                                                                 *
               ;*                  SIMPLE SPEECH OUTPUT DRIVER                    *
               ;*                                                                 *
               ;*                              BY                                 *
               ;*                         JOHN SIMONTON                           *
               ;*                                                                 *
               ;*                (C) 1982 PAIA ELECTRONICS, INC.                  *
               ;*******************************************************************
               ;
               ;WHEN USED AS A DRIVER SUBROUTINE FOR MULTIPLE PHRASES, THE LXI AND
               ;MVI INSTRUCTIONS AT THE BEGINNING OF SPOUT MAY BE REMOVED AND
               ;THESE PARAMETERS PASSED BY THE CALLING ROUTINE.  IF THIS MOD
               ;IS MADE, THE ACCUMULATOR SHOULD CONTAIN THE PARAMETER BYTE AND THE
               ;HL REGISTER PAIR SHOULD CONTAIN THE POINTER TO THE PHRASE TO BE
               ;SPOKEN WHEN THE ROUTINE IS CALLED.
               ;
0030 =         SPORT   EQU     30H     ;SS-100 DATA PORT
0031 =         PPORT   EQU     31H     ;SS-100 PARAMETER PORT
               ;
0030           INTSVC  ORG 30H         ;ADDRESS OF INTERRUPT VECTOR VI6
               ;
0030 7E                MOV A,M         ;GET PHONEME BYTE FOR OUTPUT
0031 D330              OUT SPORT       ;SEND PHONEME TO SS-100
0033 C9                RET             ;THEN RETURN FOR MORE
               ;
2000           SPOUT   ORG 2000H       ;CHANGE ORIGIN TO SUIT APPLICATION
               ;
2000 F3                DI              ;DISABLE INTERRUPTS AT CPU
2001 211A20            LXI H,PHRASE1   ;POINTER TO APPROPRIATE PHRASE
2004 3EA8              MVI A,0A8H      ;GET SS-100 PARAMETER AND
2006 D331              OUT PPORT       ;SEND TO SS-100 (ENABLE HARDWARE INTERRUPT)
2008 7E        LOOP    MOV A,M         ;GET PHONEME BYTE TO TEST
2009 EEFF              XRI 0FFH        ;TEST FOR TERMINATOR BYTE
200B CA1420            JZ DONE         ;IF FINISHED, BRANCH TO EXIT
               ;
200E FB                EI              ;ENABLE SOFTWARE INTERRUPTS
200F 76                HLT             ;WAIT FOR SS-100 INTERRUPT
2010 23                INX H           ;POINT TO NEXT PHONE BYTE IN PHRASE TABLE
2011 C30820            JMP LOOP        ;THEN JUMP FOR MORE
               ;
2014 F3        DONE    DI              ;DISABLE SOFTWARE INTERRUPTS
2015 3E00              MVI A,00        ;PREPARE PARAMETER BYTE (INTERRUPT DISABLED)
2017 D331              OUT PPORT       ;SEND TO SS-100 PARAMETER REGISTER
2019 FF                RST 7           ;FINISH SUBROUTINE (CHANGE TO RET IF NECESSARY)
               ;
               ;                 COME             SEE              ME
               ;        PA1/ K /UH1/ M /PA1/ S /E1 / Y /PA1/ M /E1 / Y /PA1/TERM
201A 3E19320C3EPHRASE1 DB 3EH,19H,32H,0CH,3EH,1FH,3CH,29H,3EH,0CH,3CH,29H,3EH,0FFH
               ;
```

**Transcription notes.**

- The object column of `PHRASE1` runs into the label (`3E19320C3EPHRASE1`), as printed. The
  `DB` has 14 bytes. The phonemes agree with Table 1 (`PA1` 3E, `K` 19, `UH1` 32, `M` 0C, `S`
  1F, `E1` 3C, `Y` 29).
- `0A8H` is the control word `1 01 0 1000`. Bit 7 enables the interrupt. The volume field is
  `01`, which is medium. The comment says "ENABLE HARDWARE INTERRUPT".
- The service routine at `30H` does not do `EI`. The `EI` before each `HLT` in `LOOP` enables the
  interrupt again.
- The listing ends with `RST 7`, which returns to the monitor of a North Star Horizon. The
  comment says to change it to `RET` for use as a subroutine.
