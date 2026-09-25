# Toolchain provisoire : MinGW. A confirmer avec Theo (Open Watcom envisage aussi).
CC = i686-w64-mingw32-gcc
CFLAGS = -O2 -Wall -mwindows
TARGET = gastinel-write-legacy.exe

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c

clean:
	rm -f $(TARGET)
