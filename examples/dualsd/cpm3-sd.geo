# The bootable CP/M 3 system card (drive A:). The image is truncated to the live
# filesystem; the rest of the declared 769920-sector (~394 MB) card reads back as erased
# 0xFF. The BIOS assigns the drive letter from the SOCKET (socket 1 -> A:), not from here.
sector_size 512
sectors     769920
