all: pianoterm.c
	cc pianoterm.c -lasound -o pianoterm
run: all
	./pianoterm
