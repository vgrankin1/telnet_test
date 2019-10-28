telnet_test: main.o seq_op_t.o
	gcc -o telnet_test main.o seq_op_t.o

main.o: main.c
	gcc -g -c main.c

seq_op_t.o: seq_op_t.c
	gcc -g -c seq_op_t.c

clean:
	rm telnet_test main.o seq_op_t.o
