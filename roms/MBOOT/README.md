# MBOOT

The boot ROM of the Vector Graphic Micropolis disk controller board. It is 256 bytes, and its
sum is BC, as the manual gives for F800-F8FF. It runs at any 1K base, because it copies itself to
RAM at 00A2 and finds its own address with RAM at 00A0-00A1.

The `vgmdc` board holds this ROM at its base, so a machine file does not mount it. Start it
with `GF800` from a Monitor, or with `RUN F800` at the monitor.

The image and the listing come from deramp.com. `reference/Vector Graphic Micropolis Controller.md`
describes the boot.
