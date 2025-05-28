#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int handle_child(int read_fd)
{
    int cur_int[35];
    if (read(read_fd, cur_int, 4) == 0)
    {
        close(read_fd);
        exit(0);
    }
    int first_int = cur_int[0];
    int num_filled = 1;
    fprintf(1, "prime %d\n", first_int);

    int status;
    while (num_filled < 35 && (status = read(read_fd, cur_int + num_filled, 4)) > 0)
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
        close(read_fd);
        exit(1);
    }
    int child = fork();
    if (child == 0)
    {
        close(my_pipe[1]);
        handle_child(my_pipe[0]);
    }
    if (status != 0)
    {
        fprintf(2, "Error in read\n");
        close(my_pipe[1]);
        wait(&child);
        exit(1);
    }
    for (int i = 1; i < num_filled; i++)
    {
        if (cur_int[i] % first_int != 0)
        {
            write(my_pipe[1], cur_int + i, 4);
        }
    }
    close(my_pipe[1]);
    wait(&child);
    exit(0);
}

int main(int argc, char *argv[])
{
    int numbers[34];
    for (int i = 0; i < 34; i++)
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
    for (int i = 0; i < 34; i++)
    {
        write(my_pipe[1], numbers + i, 4);
    }
    close(my_pipe[1]);
    wait(&child);
    exit(0);
}