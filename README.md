YaSlicer (Yet Another Slicer) is image set generator for DLP/SLA 3D printers written in C++.

Features:
- Fast (GPU accelerated & multicore optimized)
- Can handle very large and complex STL models (tested with ~1Gb binary STL files)
- Antialiased rendering (off by default)
- Many options to adjust for specific machine
- Supports printer profiles (machine configs)
- Simulation mode for performance testing
- PNG output
- Low dependencies count: boost, angle, libpng, glm, glew32
- Job file output for Envisiontech machines

Limitations:
- Do not check input models for any inconsistencies like cracks, holes, etc. Should not crash, but result may be incorrect.
- Windows only, though may be ported to other environments (some attempts were made to run on RaspberryPi).
- Need D3D11 drivers (but can work on D3D9 hardware).

Prerequisites:
- Microsoft Visual Studio 2017 with 64-bits C++ compiler (free Community edition is fine).
- vcpkg package manager

Build:
1. install dependencies with vcpkg:
vcpkg install angle boost glew glm libpng --triplet x64-windows
2. Open Tools.sln in Visual Studio 2017 
3. Build

Usage:
run slicer.exe --help for options

or just try
slicer.exe -m model.stl -o output
then you should find png slices in "output" subdirectory.

Detailed options description: To Be Done.