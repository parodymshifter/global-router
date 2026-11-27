#!/bin/bash
# The ISPD 2025 contest's OpenROAD fork (liangrj2014/OpenROAD_ISPD25), built like ~/Code/OpenROAD (see its logs/PATCHES.txt).
cd ~/Code/OpenROAD_ISPD25
python3 -m venv .venv && source .venv/bin/activate
export PATH="$(brew --prefix bison)/bin:$(brew --prefix flex)/bin:$(brew --prefix tcl-tk@8)/bin:${PATH}"
export LIBRARY_PATH=/opt/homebrew/lib
export CMAKE_PREFIX_PATH="$(brew --prefix or-tools);$(brew --prefix qt@5)"
echo "== configure (resume) $(date)"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RELEASE -DBUILD_GUI=OFF -DBUILD_PYTHON=OFF -DENABLE_TESTS=OFF -DTCL_READLINE_LIBRARY=TCL_READLINE_LIBRARY-NOTFOUND -DTCL_READLINE_H=TCL_READLINE_H-NOTFOUND \
  -DCUDD_DIR=$HOME/.local -DQt5_DIR=$(brew --prefix qt@5)/lib/cmake/Qt5 \
  -DTCL_LIBRARY=$(brew --prefix tcl-tk@8)/lib/libtcl8.6.dylib -DTCL_INCLUDE_PATH=$(brew --prefix tcl-tk@8)/include/tcl-tk \
  -DFLEX_INCLUDE_DIR=$(brew --prefix flex)/include -DCMAKE_CXX_FLAGS=-DBOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED || { echo "CONFIGURE FAILED"; exit 1; }
echo "== build $(date)"
nice -n 10 ninja -C build -j6 -k 0 openroad || { echo "BUILD FAILED"; exit 1; }
ls -la build/src/openroad build/bin/openroad 2>/dev/null
echo BUILD OK
