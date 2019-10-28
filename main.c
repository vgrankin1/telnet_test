

#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <fcntl.h>
#include <sys/select.h>

#include "seq_op_t.h"

/*
Telnet commands
*/
#define TN_IAC          0xFF     /*Interpret as Command*/
#define TN_WILL         0xFB
#define TN_WONT         0xFC
#define TN_DATA_MARK    0xF2
#define TN_DATA_BIN     0x00

/*
seq1 1 2 – задает подпоследовательность 1, 3, 5 и т.д;
seq2 2 3 – задает подпоследовательность 2, 5, 8 и т.д;
seq3 3 4 – задает подпоследовательность 3, 7, 11 и т.д;
export seq – в сокет передается последовательность 1, 2, 3, 3, 5, 7, 5, 8, 11 и т.д.
*/




int clients_socket_cnt(const int *o, const int n)
{
    int cnt = 0;
    for(int i = 0 ; i < n; i++)
        cnt += o[i] != 0;
    return cnt;
}
int* clients_socket_get_free(int *o, const int n, int *ind)
{
    for(int i = 0; i < n; i++)
    {
        if(o[i] == 0)
        {
            *ind = i;
            return o + i;
        }
    }
    return 0;
}
void clients_socket_rm(int *o, const int n, const int socket)
{
    for(int i = 0; i < n; i++)
    {
        if(o[i] == socket)
        {
            o[i] = 0;
            break;
        }
    }
}

typedef struct _client_state_t
{
    seq_op_t seq_o[SEQ_NUM];
    int telnet_binary;
    int telnet_fast;
    int state;
}client_state_t;

int client_func(client_state_t *st, int sock);


int main()
{
    struct sockaddr_in addr;
    int listener;

    
    if((listener = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket() opening problem");
        return 1;
    }
    fcntl(listener, F_SETFL, O_NONBLOCK);

    int enable = 1;
    /*Переиспользование сокета. После завершения, сокет остаеться in charge*/
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
    
    int clients_num = 0;
    int clients_sockets[16];
    client_state_t clst[16];
    for(int i = 0; i < 16; i++)
    {
        //seq_op_t_init(seq_o[i], SEQ_NUM);
        clients_sockets[i] = 0;
    }
   

    int inworking = 1;
    while(inworking)
    {
        fd_set readset;
        FD_ZERO(&readset);
        FD_SET(listener, &readset);


        int mx_set = listener;/*max descriptor, needed for select*/
        for(int i = 0; i < 16; i++)
        {
            if(mx_set < clients_sockets[i])
            mx_set = clients_sockets[i];
        }

        for(int i = 0; i < 16; i++)
        {
            if(clients_sockets[i])
                FD_SET(clients_sockets[i], &readset);
        }

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;//0.1s

     
        
        int ret;
        if((ret = select(mx_set+1, &readset, NULL, NULL, &timeout)) < 0)
        {
            perror("select:");
            break;
        }
        if(ret == 0)
        {
            for(int i = 0; i < 16; i++)
            {
                if(clients_sockets[i] != 0 && clst[i].state)
                {
                    int ret = sequentate(clst[i].seq_o, SEQ_NUM, clients_sockets[i], clst[i].telnet_binary, clst[i].telnet_fast);
                    if(ret <= 0)
                    {
                        close(clients_sockets[i]);
                        clients_socket_rm(clients_sockets, 16, clients_sockets[i]);
                        printf("Disconnected\n");
                        clients_num--;
                    }
                }
            }

            continue;
        }
        
        /*
        */
        if(FD_ISSET(listener, &readset))
        {
            int sock = accept(listener, NULL, NULL);
            if(sock < 0)
            {
                perror("accept:");
                break;
            }
            fcntl(sock, F_SETFL, O_NONBLOCK);

            int index;
            int *client_slot = clients_socket_get_free(clients_sockets, 16, &index);
            if(!client_slot)
            {
                const char *str = "Too many connections\n\r";
                fprintf(stderr, "%s", str);
                send(sock, str, strlen(str), MSG_NOSIGNAL);
                close(sock);
                continue;
            }
            
            seq_op_t_init(clst[index].seq_o, SEQ_NUM);
            clst[index].telnet_binary = 0;
            clst[index].telnet_fast = 0;
            clst[index].state = 0;
            *client_slot = sock;
            clients_num++;
        }

        for(int i = 0; i < 16; i++)
        {
            if(clients_sockets[i] == 0 || FD_ISSET(clients_sockets[i], &readset) == 0)
                continue;/*Not clenst at all or no changes for clients_sockets[i]*/
            int ret = client_func(clst + i, clients_sockets[i]);
            if(ret > 0)
                continue;
            if(ret <= 0)
            {
                close(clients_sockets[i]);
                clients_socket_rm(clients_sockets, 16, clients_sockets[i]);
                printf("Disconnected\n");
                clients_num--;
            }
            if(ret < 0)
            {
                printf("Term\n");
                inworking = 0;
                break;
            }
        } 
    }
    
    close(listener);
    return 0;
}


#define RECV_BUF_SIZE 1024
int client_func(client_state_t *st, int sock)
{
    unsigned char buf[RECV_BUF_SIZE + 1];

    int bytes_read;
    char command[16];
        command[0] = 0;
    if(st->state == 0)//st_reading commands
    {
        bytes_read = recv(sock, buf, RECV_BUF_SIZE, MSG_NOSIGNAL);
        if(bytes_read <= 0)//Disconnected
        {
            return 0;
        }
        buf[bytes_read] = 0;
        printf("recived(%d): %s ", bytes_read, buf);
        for (int i = 0; i < bytes_read; i ++)
            printf(" %02x", (unsigned char)buf[i]);
        printf("\n");

        if(buf[0] == TN_IAC)//telnet command recieved
        {
            const char *str = "telnet commands igonring\n\r";
            printf("%s", str);
            buf[1] = TN_WONT;/*we will not execute the telnet command, send it back*/
            send(sock, buf, 3, MSG_NOSIGNAL);
            send(sock, str, strlen(str), MSG_NOSIGNAL);
            //continue;
            return 1;
        }
        if(strncmp("close", buf, 5) == 0)
        {
            printf("#close recived\n");
            close(sock);
            //return (void*)0;
            return -1;
        }
        else if(strncmp("exit", buf, 4) == 0)
        {
            printf("#exit recived\n");
            //break;
            return 0;
        }
        else if(strncmp("binary", buf, 6) == 0)
        {
            /*
            Смена режима telnet на binary
            */
            buf[0] = TN_IAC;
            buf[1] = TN_WILL;
            buf[2] = TN_DATA_BIN;
            if(send(sock, buf, 3, MSG_NOSIGNAL) != 3)
                return 0;//break;//Disconnected

            bytes_read = recv(sock, buf, 1024, MSG_NOSIGNAL);
            printf("rec %d bytes:", bytes_read);
            for (int i = 0; i < bytes_read; i ++)
                printf(" %02x", (unsigned char)buf[i]);
            printf("\n");

            buf[0] = TN_IAC;
            buf[1] = TN_DATA_MARK;
            if(send(sock, buf, 2, MSG_NOSIGNAL) != 2)
                return 0;//break;//Disconnected
            st->telnet_binary = 1;

            if(send(sock, "OK\n\r", 4, MSG_NOSIGNAL) != 4)
                return 0;//break;//Disconnected
            return 1;//continue;
        }
        else if(strncmp("fast", buf, 4) == 0)
        {
            if(st->telnet_binary)
            {
                st->telnet_fast = 1;
                send(sock, "OK\n\r", 4, MSG_NOSIGNAL);
            }
            else
            {
                const char *str = "Fast mode is only possible with binary\n\r";
                fprintf(stderr, "%s", str);
                send(sock, str, strlen(str), MSG_NOSIGNAL);
            }
            return 1;//continue;
        }
        else if(strncmp("help", buf, 4) == 0)
        {
            const char *str = "\n\rAvailable commands:\n\rhelp \t\t- print this\n\r" \
            "seq1 x y\t- set sequence1 start number x and step y\n\r" \
            "seq2 x y\t- set sequence2 start number x and step y\n\r" \
            "seq3 x y\t- set sequence2 start number x and step y\n\r" \
            "export seq\t- send sequence to telnet\n\r" \
            "close\t\t- close telnet and server\n\r" \
            "exit\t\t- close telnet\n\r"
            "binary\t\t- set telnet mode to binary and sending when export in binary\n\r" \
            "fast\t\t- set sending mode to fast by chunks available in binary mode\n\r\r\n";
            send(sock, str, strlen(str), MSG_NOSIGNAL);
            return 1;//continue;
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
                return 1;//continue;
            }
            unsigned index = command[3] - '0' - 1;
            if(index >= SEQ_NUM)
            {
                const char *str = "Wrong seq number or a command\n\r";
                fprintf(stderr, "%s", str);
                send(sock, str, strlen(str), MSG_NOSIGNAL);
                return 1;//continue;
            }
            st->seq_o[index].value = first;
            st->seq_o[index].step = step;
        }
        else if(strncmp("export seq", buf, 10) == 0)
        {
            if(seq_op_t_have_no_zeroes(st->seq_o, SEQ_NUM) == 0)
            {
                const char *str = "There is no non-zero subsequence\n\r";
                fprintf(stderr, "%s", str);
                send(sock, str, strlen(str), MSG_NOSIGNAL);
                return 1;//continue; 
            }
            /*
            */
           st->state = 1;//st_seq
           return 1;
        }
        else
        {
            const char *str = "No valid command entered\n\r";
            fprintf(stderr, "%s", str);
            send(sock, str, strlen(str), MSG_NOSIGNAL);
        }
    }
    return 1;
}

