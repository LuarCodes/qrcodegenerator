@echo off
rem Build with MinGW-w64 g++ (e.g. MSYS2: pacman -S mingw-w64-ucrt-x86_64-gcc)
windres app.rc -O coff -o app.res.o
g++ -O2 -std=c++17 -municode -mwindows -static -s main.cpp qrcodegen.cpp app.res.o -ldwmapi -luxtheme -lcomctl32 -lcomdlg32 -lgdi32 -o QRClip.exe
echo Done: QRClip.exe
