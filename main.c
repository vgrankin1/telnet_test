

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>




/*
Telnet commands
*/
#define TN_IAC          0xFF     /*Interpret as Command*/
#define TN_WILL         0xFB
#define TN_DATA_MARK    0xF2
#define TN_DATA_BIN     0x00

/*
seq1 1 2 – задает подпоследовательность 1, 3, 5 и т.д;
seq2 2 3 – задает подпоследовательность 2, 5, 8 и т.д;
seq3 3 4 – задает подпоследовательность 3, 7, 11 и т.д;
export seq – в сокет передается последовательность 1, 2, 3, 3, 5, 7, 5, 8, 11 и т.д.
*/

/*Число подпоследовательностей*/
#define SEQ_NUM 3

typedef struct _seq_op_t
{
	uint64_t value, step;
    char carry;/**/
}seq_op_t;

void seq_op_t_init(seq_op_t *seq_o, const int n);
int seq_op_t_have_no_zeroes(seq_op_t *seq_o, const int n);
void sequentate(seq_op_t *seq_o, const int n, const int sock_fd, const int binaryf);
void *new_connection(void *args);

int main()
{
    struct sockaddr_in addr;
    int listener;

    
    if((listener = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket() opening problem");
        return 1;
    }

    int enable = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind() :");
        return 2;
    }
    if(listen(listener, 1) != 0)
    {
        perror("listen() :");
        close(listener);
        return 3;
    }
    
   

    int inworking = 1;
    while(inworking)
    {
        int sock;
        if( (sock = accept(listener, NULL, NULL) ) < 0)
        {
            perror("accept() :");
            return 4;
        }
        
        inworking = (intptr_t)new_connection( (void*)(intptr_t)sock);        
    }
    
    close(listener);
    return 0;
}

void *new_connection(void *args)
{
    char buf[1025];

    seq_op_t seq_o[SEQ_NUM];
    seq_op_t_init(seq_o, SEQ_NUM);
    int telnet_binary = 0;
    int sock = (int)(intptr_t)args;

    while(1)
    {
        int bytes_read;
        char command[16];
        command[0] = 0;

        bytes_read = recv(sock, buf, 1024, 0);
        if(bytes_read <= 0)
               break;
        buf[bytes_read] = 0;
        printf("recived(%d): %s ", bytes_read, buf);
        for (int i = 0; i < bytes_read; i ++)
            printf(" %02x", (unsigned char)buf[i]);
        printf("\n");

        if(strncmp("close", buf, 5) == 0)
        {
            printf("#close recived\n");
            close(sock);
            return (void*)0;
        }
        else if(strncmp("exit", buf, 4) == 0)
        {
            printf("#exit recived\n");
            break;
        }
        else if(strncmp("binary", buf, 6) == 0)
        {
            /*
            Смена режима telnet на binary
            */
            buf[0] = TN_IAC;
            buf[1] = TN_WILL;
            buf[2] = TN_DATA_BIN;
            send(sock, buf, 3, MSG_NOSIGNAL);

            bytes_read = recv(sock, buf, 1024, 0);
            printf("rec %d bytes:", bytes_read);
            for (int i = 0; i < bytes_read; i ++)
                printf(" %02x", (unsigned char)buf[i]);
            printf("\n");

            buf[0] = TN_IAC;
            buf[1] = TN_DATA_MARK;
            send(sock, buf, 2, MSG_NOSIGNAL);
            telnet_binary = 1;

            send(sock, "OK\n\r", 4, MSG_NOSIGNAL);
            continue;
        }

        if(strncmp("seq", buf, 3) == 0)
        {
            uint64_t first = 0;
            uint64_t step = 0;
            if (sscanf(buf, "%8s %lu %lu", command, &first, &step) != 3)/*seqX x x*/
            {
                const char *str = "Wrong arguments for seq* command\n\r";
                fprintf(stderr, "%s", str);
                send(sock, str, strlen(str), MSG_NOSIGNAL);
                continue;
            }
            unsigned index = command[3] - '0' - 1;
            if(index >= SEQ_NUM)
            {
                const char *str = "Wrong seq number or a command\n\r";
                fprintf(stderr, "%s", str);
                send(sock, str, strlen(str), MSG_NOSIGNAL);
                continue;
            }
            seq_o[index].value = first;
            seq_o[index].step = step;
        }
        else if(strncmp("export seq", buf, 10) == 0)
        {
            if(seq_op_t_have_no_zeroes(seq_o, SEQ_NUM) == 0)
            {
                const char *str = "There is no non-zero subsequence\n\r";
                fprintf(stderr, "%s", str);
                send(sock, str, strlen(str), MSG_NOSIGNAL);
                continue; 
            }
            /*
            */
            sequentate(seq_o, SEQ_NUM, sock, telnet_binary);
        }
        else
        {
            const char *str = "No valid command entered\n\r";
            fprintf(stderr, "%s", str);
            send(sock, str, strlen(str), MSG_NOSIGNAL);
        }
    }
    close(sock);
    return (void*)1;
}

/**/
void seq_op_t_init(seq_op_t *seq_o, const int n)
{
    for(int i = 0; i < n; i++ )
    {
        seq_o[i].value = 0;
        seq_o[i].step = 0;
        seq_o[i].carry = 0;
    }
}

/*

*/
int seq_op_t_have_no_zeroes(seq_op_t *seq_o, const int n)
{
    for(int i = 0; i < n; i++)
    {
        if(seq_o[i].value != 0 && seq_o[i].step != 0)
            return 1;
    }
    return 0;
}

/*return i = min(seq_o)*/
int seq_op_t_min(seq_op_t *seq_o, const int n)
{
	uint64_t tmp = UINT64_MAX;
    int ci = -1;
	for (int i = 0; i < n; i++)
	{
		if (seq_o[i].carry != 0 || seq_o[i].value == 0 || seq_o[i].step == 0)/*Игнорирование нулевых значений и переполненных (иначе только переполненные под последовательности будут обрабатываться*/
			continue;
		if (seq_o[i].value <= tmp)
		{
			tmp = seq_o[i].value;
			ci = i;
		}
	}
	if (ci == -1)/*Видимо все подпоследовательности переполенны*/
	{
		for (int i = 0; i < n; i++)
			seq_o[i].carry = 0;
		return seq_op_t_min(seq_o, n);/*Нужно, чобы была как минимум 1 подпоследовательность с value&step != 0*/
	}
    return ci;
}

/*
Выполнить одну итерацию последовтельности
*/
uint64_t seq_op_t_iterate(seq_op_t *seq_o, const int n)
{
    uint64_t value;
    int ci = seq_op_t_min(seq_o, n);
	
	value = seq_o[ci].value;
	seq_o[ci].value += seq_o[ci].step;
	if (seq_o[ci].value < value)/*Произошло переполнение, помечаем*/
		seq_o[ci].carry = 1;
    return value;
}

/*
    seq_op_t *seq_o - подпоследовательности
    n - число подпоследовательностей
    sock_fd - сокет
    binary - флаг передачи в сокет двоичных данных
*/
void sequentate(seq_op_t *seq_o, const int n, const int sock_fd, const int binaryf)
{
	for (;;)
	{
		uint64_t value = seq_op_t_iterate(seq_o, n);
		int writed;

        if(binaryf)
        {
            char buf[10];
            *((uint64_t*)buf) = value;
            buf[8] = ',';
            buf[9] = ' ';
            writed = send(sock_fd, buf, 10, MSG_NOSIGNAL);
            if(10 != writed)
            {
                perror("10 != writed **** disconecting\n");
                break;
            }
        }
        else
        {
            char buf[32];/*32 > log10(2^64)==19.2*/
            sprintf(buf, "%lu, ", value);
            size_t buf_sz = strlen(buf);
            int writed = send(sock_fd, buf, buf_sz, MSG_NOSIGNAL);
            if(buf_sz != writed)
            {
                perror("buf_sz != writed **** disconecting\n");
                break;
            }
        }
        
        usleep(1000*10);
	}
}