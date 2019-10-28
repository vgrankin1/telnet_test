
#include "seq_op_t.h"

#include <string.h>
#include <stdio.h>
#include <sys/socket.h>

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
int sequentate(seq_op_t *seq_o, const int n, const int sock_fd, const int binaryf, const int fastf)
{
    if(binaryf)
    {
        if(fastf)
        {
            char buf[8*1024];
            for(int i, j = 0; j < 2; j++)/*sending 2 packets*/
            {
                i = 0;
                for(; i < 8*1024; i += 10)
                {
                    uint64_t value = seq_op_t_iterate(seq_o, n);
                    *((uint64_t*)(buf + i)) = value;
                    buf[i + 8] = ',';
                    buf[i + 9] = ' ';
                }
                int writed = send(sock_fd, buf, i, MSG_NOSIGNAL);
                if(i != writed)
                {
                    perror("i != writed **** disconecting\n");
                    return 0;//break;
                }
            }
        }
        else
        {
            char buf[10];
            //for(;;)
            {
                uint64_t value = seq_op_t_iterate(seq_o, n);
                *((uint64_t*)buf) = value;
                buf[8] = ',';
                buf[9] = ' ';
                int writed = send(sock_fd, buf, 10, MSG_NOSIGNAL);
                if(10 != writed)
                {
                    perror("10 != writed **** disconecting\n");
                    return 0;//break;
                }
                usleep(1000*10);
            }
        }
    }
    else
    {
        char buf[32];/*32 > log10(2^64)==19.2*/
        for(int i =0; i < 10; i++)
        {
            uint64_t value = seq_op_t_iterate(seq_o, n);
            sprintf(buf, "%lu, ", value);
            size_t buf_sz = strlen(buf);
            int writed = send(sock_fd, buf, buf_sz, MSG_NOSIGNAL);
            if(buf_sz != writed)
            {
                perror("buf_sz != writed **** disconecting\n");
                return 0;//break;
            }
        }
        usleep(1000*10);
    }
    return 1;
}