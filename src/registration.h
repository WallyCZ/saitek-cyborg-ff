#pragma once
#include <windows.h>
#include <string>
#include <vector>

extern HMODULE g_module;

// COM classes (effect driver and property sheet) in HKLM or HKCU Software\Classes. Machine-wide registration
// from the 64-bit DLL also registers the 32-bit DLL next to it (..\x86\SaitekFF.dll) for 32-bit programs.
HRESULT RegisterClasses(HKEY root);
void UnregisterClasses(HKEY root);

// DirectInput OEM data of the supported sticks: our effect driver, its effects and the property sheet.
// DirectInput reads it from HKLM and keeps a per-user copy in HKCU, which Windows fills with its generic PID
// driver when the stick is first plugged in, so both need to be written.
HRESULT RegisterDevices(HKEY root);
void UnregisterDevices(HKEY root);

// Gives the sticks a different name for the current user (OEMName in HKCU), for games like DCS that block
// the original name because the original driver crashed them
void RenameDevices();

// The original Saitek/Immersion driver packages (oemNN.inf in the Windows INF folder). Their kernel driver
// takes over the stick and talks a protocol this driver does not use.
std::vector<std::wstring> FindSaitekDriver();
// Removes them; the sticks then get the standard Windows HID driver again
void RemoveSaitekDriver();
