# Legacy: patched Immersion force feedback DLL

`SaiQFFB5.dll` is the original Saitek/Immersion force feedback DLL (Immersion "ImmPID") with a binary patch
for 64-bit programs: Immersion stored 64-bit pointers in 32-bit fields, which crashes 64-bit games. The patch
takes the lost upper 32 bits from another pointer at every place the broken one is used (see `patch.asm`).
This was this project's first fix, before the new driver.

**For the Saitek Cyborg Evo Force, use the new driver instead** (see the main README). This DLL still needs
Immersion's kernel driver, which Windows 11 Memory Integrity blocks, and the patch is a heuristic that can
fail when the two pointers end up in different 4 GB ranges of memory.

It is kept here for other Immersion based devices that the new driver does not support, because they need
Immersion's kernel driver to work at all.

## Community tested
- **Logitech WingMan Formula Force** (`VID_046D&PID_C291`) on Windows 11 24H2, together with timschumi's
  [iforce-binary-driver](https://github.com/timschumi/iforce-binary-driver): rename `SaiQFFB5.dll` to
  `saiQFF04.dll`, replace that file in the extracted driver package, and install the package with driver
  signature enforcement disabled (Memory Integrity must be off). Full instructions in
  [issue #6](https://github.com/WallyCZ/saitek-cyborg-ff/issues/6). Tested in Assetto Corsa, SnowRunner,
  RBR RSF and RaceRoom.

## Saitek Cyborg Evo Force / 3D Force Stick (old method)
Install the original Saitek driver, then replace `SaiQFFB5.dll` in `C:\Windows\System32` with this file
(tested with the Evo Force; the old install script also used it in place of `SaiQFF12.dll` for the 3D Force
Stick and 3D Rumble Force, which was not confirmed to work).
