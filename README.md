# Saitek Cyborg Evo Force force feedback driver for Windows 10 and 11

A new force feedback driver for the Saitek Cyborg Evo Force joystick (and probably the Cyborg 3D Force Stick
and Cyborg 3D Rumble Force) that works on 64-bit Windows 10 and Windows 11, for both 64-bit and 32-bit games.

It replaces the original Saitek/Immersion driver, which no longer works:
- its kernel driver is blocked by Windows 11 Memory Integrity (Core Isolation),
- its force feedback DLL crashes in 64-bit applications (Immersion stored 64-bit pointers in 32-bit fields).

This driver needs **no kernel driver** and no Saitek software: the stick keeps the standard Windows HID driver,
so Memory Integrity can stay on.

## Installation
Download `SaitekFF-<version>.msi` from [Releases](https://github.com/WallyCZ/saitek-cyborg-ff/releases) and run it.
The installer asks two things:
- if the original Saitek driver is installed, whether to remove it. Leave it checked: this driver cannot work
  while the original one is installed.
- whether to rename the stick, for games like DCS that block the original device name.

Test it in the Game Controllers panel (`joy.cpl`) or in a game. Uninstall it from Settings > Apps.

Alternatively, without the MSI: download `SaitekFF-<version>.zip`, extract it, right click `Install.ps1` and
choose "Run with PowerShell" (uninstall with `Install.ps1 -Uninstall`).

Both install the driver to `C:\Program Files\SaitekFF` and register it for the stick. Without administrator
rights the driver can also be registered just for the current user:
`regsvr32 /n /i:user <path>\x64\SaitekFF.dll` (and the x86 one with `C:\Windows\SysWOW64\regsvr32.exe`).

## Settings
Open the Game Controllers panel (`joy.cpl`), select the stick and click Properties. Next to Windows' own Test
and Settings tabs there is a Force Feedback tab with:
- effect strength for all effects, springs and dampers,
- the stick's centering spring when no game uses force feedback (games with force feedback control it themselves),
- test effects played by the stick buttons (trigger: machine gun, 2: explosion, 3: engine rumble, 4: bumpy road).

## Supported effects
Constant force, ramp (emulated with a sawtooth wave, as the original driver did), square, sine, triangle,
sawtooth up/down, spring, damper, and inertia/friction (played as damper, as the original driver did).
Envelopes, trigger buttons and start delay are supported. Custom forces are not.

## How it works
The stick's HID descriptor describes a "host managed" PID force feedback device: the firmware understands
the standard PID reports, but the driver itself has to decide which of the 10 effect slots each effect uses
and where its parameters live in the stick's 300 bytes of effect memory. Windows' generic PID driver expects
the device to manage that itself, which is why it crashes on this stick. The original Saitek package solved it
with Immersion's kernel driver, which emulated a device managed stick; this driver does the same job in user
mode, inside the DirectInput effect driver:

- `src/hiddevice.*` - finds the force feedback reports in the descriptor, builds them with `HidP_*`, and reads
  effect status from the stick
- `src/ffdevice.*` - effect slots, effect memory allocation, conversion of DirectInput effects to device
  reports, and swapping idle effects out when a game creates more than the stick can hold
- `src/effectdriver.*`, `src/dllmain.cpp` - the `IDirectInputEffectDriver` COM object and its registration

## Building
Run `build.cmd` (needs Visual Studio with the C++ workload). It produces `bin\x64\SaitekFF.dll` and
`bin\x86\SaitekFF.dll`; `Install.ps1` in the repository root finds them there. `package.ps1 -Version x.y.z`
creates the MSI (WiX 5, installed by `dotnet tool restore`) and the zip in `dist\`.

GitHub Actions builds both on every push. Pushing a tag `vX.Y.Z` publishes a release with them; running the
Build workflow manually creates a draft release for testing.

Tools for testing: `tools\fftest` (DirectInput force feedback tests, e.g. `fftest suite`, `fftest directions`)
and `tools\hidraw` (raw HID report access).

## Troubleshooting
If the stick does not center itself after replacing the original Saitek driver, unplug it and plug it back in.
When the original driver shuts down it switches the stick's centering spring off in a way that only
reconnecting the stick undoes.

Set `HKCU\Software\SaitekFF` `Debug` (DWORD) to `1` and the driver logs to `%TEMP%\SaitekFF.log` and to the
debugger output (DebugView). Please attach the log when reporting a problem.

## Tested
- Saitek Cyborg Evo Force on Windows 11 (build 26200) with Memory Integrity on: all effects, 64-bit and 32-bit
