# A BLANK spare card (drive B:). The backing image is empty, so every one of the declared
# 769920 sectors reads back as erased 0xFF -- an unwritten microSD card. CP/M sees no
# directory, so DIR B: reports "No File" until you copy files onto it or format it.
sector_size 512
sectors     769920
