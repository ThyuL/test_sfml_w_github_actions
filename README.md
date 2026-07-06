# sfml_my_particle_effect_test

- project tree

```cmd
C:.
│   arial.ttf
│   my_particle_effect_SFML.cpp
│   my_particle_effect_SFML.exe
│   run.bat
│
├───bin
│       libgcc_s_seh-1.dll
│       libstdc++-6.dll
│       libwinpthread-1.dll
│       openal32.dll
│       sfml-audio-2.dll
│       sfml-graphics-2.dll
│       sfml-network-2.dll
│       sfml-system-2.dll
│       sfml-window-2.dll
│
├───images
│       my_icon.ico
│
└───resources
        resource.o
        resource.rc
```



- Pre-requiste:

```cmd
set PATH=C:\msys64\mingw64\bin;%PATH%
```



`resource.rc`:

```c++
MAINICON ICON "..\\images\\my_icon.ico"
```

- Compile `resource.rc` into `resource.o`:

```cmd
windres resources\resource.rc -o resources\resource.o
```



- Keep `msys2 mingw32 gcc` in use:

- Compile `my_particle_effect_SFML.cpp`:

```cmd
g++ -std=c++17 -I"C:\Libraries\SFML-2.6.2\SFML-install\include" -L"C:\Libraries\SFML-2.6.2\SFML-install\lib" "C:\Users\Me\Downloads\Borderless_window_basics\SFML_tests\sfml_my_particle_effect_test\my_particle_effect_SFML.cpp" resources\resource.o -o "C:\Users\Me\Downloads\Borderless_window_basics\SFML_tests\sfml_my_particle_effect_test\my_particle_effect_SFML.exe" -lsfml-graphics -lsfml-window -lsfml-system -ldwmapi -mwindows -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive
```

- Run `my_particle_effect_SFML.cpp`:

```cmd
cd .
set PATH=bin;%PATH%
my_particle_effect_SFML.exe
```



# Refactored project tree

```cmd
C:.
│   arial.ttf
│   my_particle_effect_SFML.cpp
│   _compile.bat
│   _run.bat
│   __compile_res.bat
│
├───bin
│       libgcc_s_seh-1.dll
│       libstdc++-6.dll
│       libwinpthread-1.dll
│       openal32.dll
│       sfml-audio-2.dll
│       sfml-graphics-2.dll
│       sfml-network-2.dll
│       sfml-system-2.dll
│       sfml-window-2.dll
│
├───dist
│       my_particle_effect_SFML.exe
│
├───images
│       my_icon.ico
│
└───resources
        resource.o
        resource.rc
```



`__compile_res.bat`:

```batch
set PATH=C:\msys64\mingw64\bin;%PATH% && windres resources\resource.rc -o resources\resource.o
```



`_compile.bat`:

```batch
set PATH=C:\msys64\mingw64\bin;%PATH%

set SRC=C:\Libraries\SFML-2.6.2\SFML-install
set GCC=g++
set CXX=-std=c++17

set RES=resources\resource.o

set LIB_FLAGS=-lsfml-graphics -lsfml-window -lsfml-system
set DLL_FLAGS=-ldwmapi
set FLAGS=%LIB_FLAGS% %DLL_FLAGS% -mwindows -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive

%GCC% %CXX% -I"%SRC%\include" -L"%SRC%\lib" "%CD%\my_particle_effect_SFML.cpp" %RES% -o "%CD%\dist\my_particle_effect_SFML.exe" %FLAGS%
```



`_run.bat`:

```batch
cd .
set PATH=bin;%PATH% && start dist\my_particle_effect_SFML.exe
```



`clean.bat`:

```batch
del /f /s /q dist\*.exe && del /f /s /q resources\*.o
```
