# Cross-compilation MinGW (Linux ou Windows). Cible : Windows XP 32 bits.
CC      = i686-w64-mingw32-gcc
WINDRES = i686-w64-mingw32-windres
TARGET  = gastinel-write-legacy.exe

# C89 strict, i386 sans SSE : tourne sur tout processeur capable de faire tourner XP.
# WINVER 0x0501 = API Windows XP uniquement.
CFLAGS  = -std=c89 -Wall -Wextra -Os -march=i386 -mno-sse \
          -ffreestanding -fno-stack-protector -fno-asynchronous-unwind-tables \
          -ffunction-sections -fdata-sections \
          -DWINVER=0x0501 -D_WIN32_WINNT=0x0501 -DWIN32_LEAN_AND_MEAN

# Pas de CRT : point d'entree maison, import de 4 DLL systeme seulement.
# Sous-systeme 5.01 : version minimale acceptee par le chargeur de XP.
LDFLAGS = -nostdlib -s -mwindows -e _WinMainCRTStartup \
          -Wl,--gc-sections -Wl,--subsystem,windows:5.01 \
          -Wl,--major-os-version,5 -Wl,--minor-os-version,1
LIBS    = -lkernel32 -luser32 -lgdi32 -lcomdlg32 -lgcc

$(TARGET): main.c app.res.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ main.c app.res.o $(LIBS)

# Manifeste (styles visuels XP) + informations de version.
app.res.o: app.rc app.manifest
	$(WINDRES) -O coff -i app.rc -o $@

clean:
	rm -f $(TARGET) app.res.o

.PHONY: clean
