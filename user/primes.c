#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define MAX_NUM_PRIME 35

int handle_child(int read_fd)
{
    int cur_int[MAX_NUM_PRIME];
    int status;
    if ((status = read(read_fd, cur_int, 4)) == 0)
    {
        close(read_fd);
        exit(0);
    }
    if (status < 0)
    {
        fprintf(2, "Error in read\n");
        close(read_fd);
        exit(1);
    }
    int first_int = cur_int[0];
    int num_filled = 1;
    fprintf(1, "prime %d\n", first_int);

    while (num_filled < MAX_NUM_PRIME && (status = read(read_fd, cur_int + num_filled, 4)) > 0)
    {
        num_filled++;
    }
    close(read_fd);
    if (status != 0)
    {
        fprintf(2, "Error in read\n");
        exit(1);
    }
    int my_pipe[2];
    if (pipe(my_pipe) != 0)
    {
        fprintf(2, "Error in pipe\n");
        close(my_pipe[1]);
        close(my_pipe[0]);
        exit(1);
    }
    int child = fork();
    if (child < 0)
    {
        fprintf(2, "Error in fork\n");
        close(my_pipe[1]);
        close(my_pipe[0]);
        exit(1);
    }
    if (child == 0)
    {
        close(my_pipe[1]);
        handle_child(my_pipe[0]);
    }
    for (int i = 1; i < num_filled; i++)
    {
        if (cur_int[i] % first_int != 0)
        {
            status = write(my_pipe[1], cur_int + i, 4);
            if (status != 4)
            {
                fprintf(2, "Error in write\n");
                close(my_pipe[1]);
                wait(&child);
                exit(1);
            }
        }
    }
    close(my_pipe[1]);
    wait(&child);
    exit(0);
}

int main(int argc, char *argv[])
{
    int numbers[MAX_NUM_PRIME - 1];
    if (argc != 1)
    {
        fprintf(2, "Usage: primes\n");
        exit(1);
    }
    for (int i = 0; i < MAX_NUM_PRIME - 1; i++)
    {
        numbers[i] = i + 2;
    }
    int my_pipe[2];
    if (pipe(my_pipe) != 0)
    {
        fprintf(2, "Error in pipe\n");
        close(my_pipe[1]);
        close(my_pipe[0]);
        exit(1);
    }
    int child;
    if ((child = fork()) == 0)
    {
        close(my_pipe[1]);
        handle_child(my_pipe[0]);
    }
    if (child < 0)
    {
        fprintf(2, "Error in fork\n");
        close(my_pipe[1]);
        close(my_pipe[0]);
        exit(1);
    }
    for (int i = 0; i < MAX_NUM_PRIME - 1; i++)
    {
        write(my_pipe[1], numbers + i, 4);
    }
    close(my_pipe[1]);
    wait(&child);
    exit(0);
}