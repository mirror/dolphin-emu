Dolphin-emu - The Gamecube / Wii Emulator
==========================================
Homesite: http://dolphin-emu.org/
Project Site: http://code.google.com/p/dolphin-emu

Dolphin-emu is a emulator for Gamecube, Wii, Triforce that lets
you run Wii/GCN/Tri games on your Windows/Linux/Mac PC system.

Open Source Release under GPL 2

Project Leaders: F|RES, ector

Team members: http://code.google.com/p/dolphin-emu/people/

Please read the FAQ before use:

http://dolphin-emu.org/docs/faq/

System Requirements:
* OS: Microsoft Windows (XP/Vista or higher) or Linux or Apple Mac OS X (10.6 or higher).
  Windows XP x64 is NOT supported.
* Processor: Fast CPU with SSE2 supported (recommended at least 2Ghz).
  Dual Core for speed boost.
* Graphics: Any reasonably modern graphics card (Direct3D9/OpenGL 2.1, shader model 3.0).

[Command line usage]
Usage: Dolphin [-hdlbBI] [--version] [-e <str>] [-V <str>] [-A <str>]
  --version                   Print version
  -A, --audio_emulation=<str> Low level (LLE) or high level (HLE) audio
  -b, --batch                 Exit Dolphin with emulator
  -B, --benchmark             Run benchmark from the specified movie
  -c, --cli                   Run Dolphin without graphical interface
  -d, --debugger              Opens the debugger
  -e, --exec=<str>            Loads the specified file (ciso, dff, dol, dtm, elf, gcm, gcz, iso, sav, s##, tmd, wad, wbfs)
  -h, --help                  Show this help message
  -l, --logger                Opens the logger
  -V, --video_backend=<str>   Specify a video plugin.

[Libraries]
Cg: Cg Shading API (http://developer.nvidia.com/object/cg_toolkit.html)
*.pdb = Program Debug Database (use these symbols with a program debugger)

[DSP Emulator Engines]
HLE: High Level DSP Emulation
LLE: Low Level DSP Emulation (requires DSP dumps)
     Recompiler is faster than interpreter but may be buggy.

[Video Backends]
Direct3D9: Render with Direct3D 9
Direct3D11: Render with Direct3D 11
OpenGL: Render with OpenGL + Cg Shader Language
Software Renderer: Render using the CPU only (for devs only)

[Sys Files]
totaldb.dsy: Database of symbols (for devs only)
font_ansi.bin/font_sjis.bin: font dumps
setting-usa/jpn/usa.txt: config files for Wii

[Support Folders]
Cache: used to cache the ISO list
Config: emulator configuration files
Dump: anything dumped from dolphin will go here
GameConfig: holds the INI game config files
GC: Gamecube memory cards
Load: custom textures
Logs: logs go here
Maps: symbol tables go here (dev only)
OpenCL: OpenCL code
ScreenShots: screenshots are saved here
Shaders: post-processing shaders
StateSaves: save states are stored here
Wii: Wii saves and config is stored here
