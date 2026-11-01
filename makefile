# Razer BlackWidow Essential backlight control

main: main.c main.h
	gcc main.c -lhidapi -o main.exe

gui: gui.c main.h
	gcc gui.c -lhidapi -lgdi32 -luser32 -ladvapi32 -lmsimg32 -mwindows -o BWBacklight.exe

all: main gui

clean:
	rm -f main.exe BWBacklight.exe
