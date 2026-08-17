# The bootable CP/M 3 system card on drive A: (CompactFlash, IDE-CF half of the board). The
# image is truncated to the live filesystem (the CP/M 3 system, its directory, and its files);
# the rest of the declared 7806960-sector (~3.72 GB) CF card reads back as the erased fill byte
# (0xFF). The BIOS assigns the drive letter from the socket (IDE drive 0 -> A:), not from here.
sector_size 512
sectors     7806960
