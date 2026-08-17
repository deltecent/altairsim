# A BLANK spare CompactFlash card on IDE/CF drive 1 (B:). The backing image is empty, so every
# one of the declared 7806960 sectors reads back as erased 0xFF -- an unwritten CF card. CP/M sees
# no directory, so DIR B: reports "No File" until you copy files onto it.
sector_size 512
sectors     7806960
