set PATH=C:\msys64\mingw64\bin;%PATH%

set SRC=C:\Libraries\SFML-2.6.2\SFML-install
set GCC=g++
set CXX=-std=c++17

set RES=resources\resource.o

set LIB_FLAGS=-lsfml-graphics -lsfml-window -lsfml-system
set DLL_FLAGS=-ldwmapi
set FLAGS=%LIB_FLAGS% %DLL_FLAGS% -mwindows -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive

%GCC% %CXX% -I"%SRC%\include" -L"%SRC%\lib" "%CD%\my_particle_effect_SFML.cpp" %RES% -o "%CD%\dist\my_particle_effect_SFML.exe" %FLAGS%