# A BLANK spare microSD card on Dual SD socket 1 (D:). The backing image is empty, so every one
# of the declared 769920 sectors reads back as erased 0xFF -- an unwritten microSD card. CP/M sees
# no directory, so DIR D: reports "No File" until you copy files onto it.
sector_size 512
sectors     769920
