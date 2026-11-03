# Razer BlackWidow Essential backlight control

main: main.c main.h
	gcc main.c -lhidapi -o main.exe

gui: gui.c main.h app.rc app.ico
	windres app.rc -o app_res.o
	gcc gui.c app_res.o -lhidapi -lgdi32 -luser32 -ladvapi32 -lshell32 -lmsimg32 -mwindows -o BWBacklight.exe

all: main gui

clean:
	rm -f main.exe BWBacklight.exe app_res.o
