# Cross-compilation MinGW. Cible : Windows XP 32 bits.
CC      = i686-w64-mingw32-gcc
WINDRES = i686-w64-mingw32-windres
TARGET  = gastinel-write-legacy.exe
SRC     = src/main.c src/core.c src/inflate.c src/zip.c src/odt.c

# C89 strict, i386 sans SSE : tourne sur tout processeur capable de faire tourner XP.
# WINVER 0x0501 = API Windows XP uniquement.
CFLAGS  = -std=c89 -Wall -Wextra -Os -march=i386 -mno-sse \
          -ffreestanding -fno-stack-protector -fno-asynchronous-unwind-tables \
          -ffunction-sections \
          -DWINVER=0x0501 -D_WIN32_WINNT=0x0501 -DWIN32_LEAN_AND_MEAN

# Pas de CRT : point d'entree maison, import de 4 DLL systeme seulement.
# Sous-systeme 5.01 : version minimale acceptee par le chargeur de XP.
LDFLAGS = -nostdlib -s -mwindows -e _WinMainCRTStartup \
          -Wl,--gc-sections -Wl,--subsystem,windows:5.01 \
          -Wl,--major-os-version,5 -Wl,--minor-os-version,1 \
          -Wl,--disable-dynamicbase -Wl,--disable-reloc-section
LIBS    = -lkernel32 -luser32 -lgdi32 -lcomdlg32 -lgcc

$(TARGET): $(SRC) src/*.h app.res.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC) app.res.o $(LIBS)

# Manifeste (styles visuels XP) + informations de version.
app.res.o: src/app.rc src/app.manifest
	$(WINDRES) -O coff -I src -i src/app.rc -o $@

# Tests des modules purs, compiles nativement avec AddressSanitizer.
test:
	python3 tests/run_tests.py

clean:
	rm -f $(TARGET) app.res.o

.PHONY: clean test
