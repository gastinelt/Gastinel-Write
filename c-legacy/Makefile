# Cross-compilation MinGW (Linux ou Windows). Toolchain definitive encore a
# confirmer avec Theo (Open Watcom envisage).
CC      = i686-w64-mingw32-gcc
TARGET  = gastinel-write-legacy.exe

# C89 strict, i386 sans SSE : tourne sur tout processeur x86 32 bits.
# WINVER 0x0400 = API Windows 95 / NT 4 uniquement.
CFLAGS  = -std=c89 -Wall -Wextra -Os -march=i386 -mno-sse \
          -ffreestanding -fno-stack-protector -fno-asynchronous-unwind-tables \
          -ffunction-sections -fdata-sections \
          -DWINVER=0x0400 -D_WIN32_WINNT=0x0400 -DWIN32_LEAN_AND_MEAN

# Pas de CRT : point d'entree maison, import de 4 DLL systeme seulement.
# Sous-systeme 4.0 pour que les chargeurs de Windows 95/NT4 acceptent l'exe.
LDFLAGS = -nostdlib -s -mwindows -e _WinMainCRTStartup \
          -Wl,--gc-sections -Wl,--subsystem,windows:4.0 \
          -Wl,--major-os-version,4 -Wl,--minor-os-version,0
LIBS    = -lkernel32 -luser32 -lgdi32 -lcomdlg32 -lgcc

$(TARGET): main.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f $(TARGET)

.PHONY: clean
