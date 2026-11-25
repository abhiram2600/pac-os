#include <Library/GpioBankLib.h>

STATIC
EFI_GPIO_BANK
mGpioBanks[] = {
  // Controller 0
  { .CtrlNum = 0, .Id = BANK_ID_P, .Number = 0, .Offset = 0x0 },
  { .CtrlNum = 0, .Id = BANK_ID_P, .Number = 1, .Offset = 0x20 },
  { .CtrlNum = 0, .Id = BANK_ID_P, .Number = 2, .Offset = 0x40 },
  { .CtrlNum = 0, .Id = BANK_ID_P, .Number = 3, .Offset = 0x60 },
  { .CtrlNum = 0, .Id = BANK_ID_P, .Number = 4, .Offset = 0x80 },
  { .CtrlNum = 0, .Id = BANK_ID_P, .Number = 5, .Offset = 0xa0 },
  { .CtrlNum = 0, .Id = BANK_ID_P, .Number = 6, .Offset = 0xc0 },
  { .CtrlNum = 0, .Id = BANK_ID_P, .Number = 7, .Offset = 0xe0 },
  { .CtrlNum = 0, .Id = BANK_ID_P, .Number = 8, .Offset = 0x100 },
  { .CtrlNum = 0, .Id = BANK_ID_P, .Number = 9, .Offset = 0x120 },
  { .CtrlNum = 0, .Id = BANK_ID_P, .Number = 10, .Offset = 0x140 },
  { .CtrlNum = 0, .Id = BANK_ID_P, .Number = 11, .Offset = 0x160 },
  { .CtrlNum = 0, .Id = BANK_ID_P, .Number = 12, .Offset = 0x180 },
  { .CtrlNum = 0, .Id = BANK_ID_P, .Number = 13, .Offset = 0x1a0 },
  { .CtrlNum = 0, .Id = BANK_ID_P, .Number = 14, .Offset = 0x1c0 },
  { .CtrlNum = 0, .Id = BANK_ID_P, .Number = 15, .Offset = 0x1e0 },
  { .CtrlNum = 0, .Id = BANK_ID_P, .Number = 17, .Offset = 0x200 },
  { .CtrlNum = 0, .Id = BANK_ID_P, .Number = 16, .Offset = 0x220 },
  // Controller 1
  { .CtrlNum = 1, .Id = BANK_ID_P, .Number = 19, .Offset = 0x0 },
  { .CtrlNum = 1, .Id = BANK_ID_P, .Number = 20, .Offset = 0x20 },
  { .CtrlNum = 1, .Id = BANK_ID_P, .Number = 21, .Offset = 0x40 },
  { .CtrlNum = 1, .Id = BANK_ID_P, .Number = 24, .Offset = 0x60 },
  { .CtrlNum = 1, .Id = BANK_ID_P, .Number = 22, .Offset = 0x80 },
  { .CtrlNum = 1, .Id = BANK_ID_P, .Number = 23, .Offset = 0xa0 },
  // Controller 2
  { .CtrlNum = 2, .Id = BANK_ID_H, .Number = 0, .Offset = 0x0 },
  { .CtrlNum = 2, .Id = BANK_ID_H, .Number = 1, .Offset = 0x20 },
  { .CtrlNum = 2, .Id = BANK_ID_H, .Number = 2, .Offset = 0x40 },
  // Controller 3
  { .CtrlNum = 3, .Id = BANK_ID_H, .Number = 3, .Offset = 0x0 },
  { .CtrlNum = 3, .Id = BANK_ID_H, .Number = 4, .Offset = 0x20 },
  // Controller 4
  { .CtrlNum = 4, .Id = BANK_ID_H, .Number = 5, .Offset = 0x0 },
  // Controller 5
  { .CtrlNum = 5, .Id = BANK_ID_N, .Number = 0, .Offset = 0x0 },
  { .CtrlNum = 5, .Id = BANK_ID_N, .Number = 1, .Offset = 0x20 },
  { .CtrlNum = 5, .Id = BANK_ID_N, .Number = 2, .Offset = 0x40 },
  { .CtrlNum = 5, .Id = BANK_ID_N, .Number = 3, .Offset = 0x60 },
  { .CtrlNum = 5, .Id = BANK_ID_N, .Number = 4, .Offset = 0x80 },
  { .CtrlNum = 5, .Id = BANK_ID_N, .Number = 5, .Offset = 0xa0 },
  { .CtrlNum = 5, .Id = BANK_ID_N, .Number = 6, .Offset = 0xc0 },
  { .CtrlNum = 5, .Id = BANK_ID_N, .Number = 7, .Offset = 0xe0 },
  { .CtrlNum = 5, .Id = BANK_ID_N, .Number = 8, .Offset = 0x100 },
  { .CtrlNum = 5, .Id = BANK_ID_N, .Number = 9, .Offset = 0x120 },
  // Controller 6
  { .CtrlNum = 6, .Id = BANK_ID_A, .Number = 0, .Offset = 0x0 },
  { .CtrlNum = 6, .Id = BANK_ID_A, .Number = 1, .Offset = 0x20 },
  { .CtrlNum = 6, .Id = BANK_ID_A, .Number = 2, .Offset = 0x40 },
  { .CtrlNum = 6, .Id = BANK_ID_A, .Number = 3, .Offset = 0x60 },
  { .CtrlNum = 6, .Id = BANK_ID_A, .Number = 4, .Offset = 0x80 },
  { .CtrlNum = 6, .Id = BANK_ID_A, .Number = 6, .Offset = 0xa0 },
  { .CtrlNum = 6, .Id = BANK_ID_A, .Number = 7, .Offset = 0xc0 },
  { .CtrlNum = 6, .Id = BANK_ID_A, .Number = 8, .Offset = 0xe0 },
  { .CtrlNum = 6, .Id = BANK_ID_A, .Number = 9, .Offset = 0x100 },
  { .CtrlNum = 6, .Id = BANK_ID_A, .Number = 10, .Offset = 0x120 },
  // Controller 7
  { .CtrlNum = 7, .Id = BANK_ID_A, .Number = 5, .Offset = 0x0 },
  // Controller 8
  { .CtrlNum = 8, .Id = BANK_ID_S, .Number = 4, .Offset = 0x0 },
  // Controller 9
  { .CtrlNum = 9, .Id = BANK_ID_S, .Number = 0, .Offset = 0x0 },
  // Controller 10
  { .CtrlNum = 10, .Id = BANK_ID_S, .Number = 1, .Offset = 0x0 },
  // Controller 11
  { .CtrlNum = 11, .Id = BANK_ID_S, .Number = 2, .Offset = 0x0 },
  // Controller 12
  { .CtrlNum = 12, .Id = BANK_ID_S, .Number = 3, .Offset = 0x0 },
};

VOID
GetGpioBanks (
  OUT EFI_GPIO_BANK **Bank,
  OUT UINT8          *Count)
{
  *Bank  = mGpioBanks;
  *Count = ARRAY_SIZE (mGpioBanks);
}
