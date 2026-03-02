all: pianoterm.c
	cc -g pianoterm.c -o pianoterm
run: all
	./pianoterm
