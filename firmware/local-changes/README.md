# Changes that live outside the submodules' own history

The firmware depends on six upstream projects, pulled in as submodules. Git
records only which commit of each to use, not any edits made on top. Two sets
of edits here are not in any upstream repository, so without this folder they
exist only on one machine and a fresh clone silently builds something
different.

## `mu_basecore.patch`

Ten files, against the commit the submodule is pinned to. Timer libraries,
the boot manager menu, a USB header, an assembler macro header, and the
authenticated variable library.

Apply with:

```
cd firmware/Mu_Basecore
git apply ../local-changes/mu_basecore.patch
```

## `silicium-acpi/`

The ACPI tables for this phone. Never committed upstream because the board
does not exist there. Copy into place with:

```
cp -a firmware/local-changes/silicium-acpi/Platforms/Samsung/stallion \
      firmware/Silicium-ACPI/Platforms/Samsung/
```

## Checking

A fresh clone that skips either of these will still compile. It will just not
be the firmware that was tested on the phone, which is the worst kind of
difference to debug. Check both are applied before trusting a build.
