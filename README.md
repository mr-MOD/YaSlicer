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

Build:
1. Get vcpkg and install packages:
vcpkg install angle boost glew glm libpng --triplet x64-windows
2. Open Tools.sln in Visual Studio 2017 (free Community edition is fine)
3. Build

Usage:
run slicer.exe --help for options