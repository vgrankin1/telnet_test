#pragma once

#include <stdint.h>
#include <unistd.h>

/*Число подпоследовательностей*/
#define SEQ_NUM 3

typedef struct _seq_op_t
{
	uint64_t value, step;
    char carry;/**/
}seq_op_t;


void seq_op_t_init(seq_op_t *seq_o, const int n);
int seq_op_t_have_no_zeroes(seq_op_t *seq_o, const int n);
uint64_t seq_op_t_iterate(seq_op_t *seq_o, const int n);
int sequentate(seq_op_t *seq_o, const int n, const int sock_fd, const int binaryf, const int fastf);
