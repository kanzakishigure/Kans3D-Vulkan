#!/bin/sh
# Checks/installs build dependencies, then configures CMake (see Setup.py --help).
cd "$(dirname "$0")/.." && exec python3 script/Setup.py "$@"
