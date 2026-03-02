all: pianoterm.c
	gcc -g pianoterm.c -o pianoterm
run: all
	./pianoterm
