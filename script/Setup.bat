@echo off
rem Checks/installs build dependencies with winget, then configures CMake (see Setup.py --help).
pushd %~dp0..
python script\Setup.py %*
popd
pause
