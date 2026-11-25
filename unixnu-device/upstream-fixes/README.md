# Fixes carried against the loader checkout

These are corrections to UniXNU itself, not to this board. They live here
because they have to be reapplied to a fresh checkout, and because each one
is a real upstream defect worth reporting rather than a local preference.

## `zzz-boot-progress.patch`

The generic boot-progress patch logs either side of a call to a power
manager that only one other board has, and that board's own patch is what
introduces the call. A board that does not select that power manager, which
is every board except one, has no such code, so the hunk cannot apply and
the kernel build stops before it starts.

The hunk is only logging. Removing it changes nothing else.

This surfaced because we are the second board ever to use this profile and
the first that is not the one it was developed on. The same coupling will
stop anyone else who tries.

To apply to a fresh checkout:

```sh
cp unixnu-device/upstream-fixes/zzz-boot-progress.patch \
   ../UniXNU/patches/xnu-12377/zzz-boot-progress.patch
```
