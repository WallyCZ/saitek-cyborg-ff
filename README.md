# Saitek Cyborg Evo and 3D Force Feedback 64-bit fix

This is fix of the Siatek Cyborg Evo/3D Force Feedback driver for 64 bit Windows applcations.
Immersion (developer of the FF driver) made huge mistake when converting from 32 bit to 64 bit and forgot one of the internal effect structure field as 32bit pointer, what causes losing high 32 bits of the pointer and crash when using this pointer.

# Installation Saitek Cyborg Evo Force
As Administrator (f.e. start Ttal Commander as Administrator) delete (or rename if it can't be deleted) the old SaiQFFB5.dll which can be found in Windows\System32 folder. Then copy there SaiQFFB5.dll from this repo.

# Installation Saitek Cyborg 3D Force
Same as by Saitek Cyborg Evo Force, only DLL has different name saiQFF12.dll. So delete the old one, rename the from this repo to SaiQFF12.dll and copy it to System32 folder.

# Tested 
- Game Device control panel
- x360ce
- DCS A-10C (need to remove joystick from blacklist by renaming device - use saitek_evo_rename.reg)

