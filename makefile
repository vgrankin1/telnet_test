telnet_test: main.o
	gcc -o telnet_test main.o

main.o: main.c
	gcc -g -c main.c

clean:
	rm telnet_test main.o
