_Head:
  adr   x1, _Payload    /* Set _Entry address */
  b     _Start          /* Jump to the real code */

#if REQUIRES_KERNEL_HEADER == 1
  /*
   * arm64 Linux image header, per Documentation/arm64/booting.rst.
   *
   * text_offset MUST be 0 here. It is an offset from a 2 MB aligned base in
   * usable RAM, not an absolute address. Putting FD_BASE in it told the
   * Pixel 10a bootloader to load us at DRAM start + 0xF4800000, which on
   * this device lands in the hole between its RAM banks, so the image was
   * never placed and never ran. _Start relocates itself to FD_BASE anyway,
   * so the bootloader can put us wherever it likes.
   *
   * flags 0xA matches the stock Pixel kernel: bits 1-2 = 1 (4 KB pages),
   * bit 3 = 1 (the image may be loaded anywhere in physical RAM rather than
   * as near the start of DRAM as possible).
   *
   * image_size carries slack for the shim that sits in front of the FD.
   */
  .quad 0                     /* Text Offset */
  .quad FD_SIZE + 0x10000     /* Image Size  */
  .quad 0xA                   /* Flags       */
  .quad 0                     /* Reserved    */
  .quad 0                     /* Reserved    */
  .quad 0                     /* Reserved    */
  .ascii "ARM\x64"            /* ARM64 Magic */
  .long 0                     /* Reserved    */
#endif

_Start:
  mov   x4, x1
  ldr   x5, =FD_BASE
  cmp   x4, x5
  beq   _Entry
  ldr   x6,  =FD_SIZE

_CopyLoop:
  ldp   x2, x3, [x4], #16
  stp   x2, x3, [x5], #16
  subs  x6, x6, #16
  b.ne  _CopyLoop
  ldr   x5, =FD_BASE

_Entry:
  br    x5

_Dead:
  b     _Dead

.text
.align 4

_Payload:
