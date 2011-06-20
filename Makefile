all: zx81

zx81: zx81.o simz80.o
	gcc -o zx81 zx81.o simz80.o `sdl-config --cflags --libs` -framework Cocoa

zx81.o: zx81.c zx81rom.h
	gcc -O3 -c zx81.c

simz80.o: simz80.c simz80.h
	gcc -O3 -c simz80.c

zx81rom.h: zx81.rom file2c
	./file2c zx81.rom rom > zx81rom.h

file2c: file2c.o
	gcc -o file2c file2c.o

file2c.o: file2c.c
	gcc -O3 -c file2c.c

clean:
	rm -f zx81 zx81.o simz80.o zx81rom.h file2c file2c.o
