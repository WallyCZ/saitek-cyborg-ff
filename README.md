# Saitek Cyborg Evo and 3D Force Feedback 64-bit fix

This is fix of the Siatek Cyborg Evo/3D Force Feedback driver for 64 bit Windows applcations.
Immersion (developer of the FF driver) made huge mistake when converting from 32 bit to 64 bit and forgot one of the internal effect structure field as 32bit pointer, what causes losing high 32 bits of the pointer and crash when using this pointer.

# Installation Saitek Cyborg Evo/3D Force
Download this repository (for example via [this](https://github.com/WallyCZ/saitek-cyborg-ff/archive/refs/heads/main.zip) zip file and then extract it on your Windows machine). Then run Install.ps1 and follow the instructions.

# Tested 
- Game Device control panel
- x360ce
- DCS A-10C (need to remove joystick from blacklist by renaming device - answer yes when Install.ps1 asks for renaming device)

