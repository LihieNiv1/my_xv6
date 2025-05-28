#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    if (argc != 1)
    {
        fprintf(2, "Usage: pingpong\n");
        exit(1);
    }
    int pipe_1[2];
    int pipe_2[2];
    if (pipe(pipe_1) < 0 || pipe(pipe_2) < 0)
    {
        fprintf(2, "Error in pipe\n");
        exit(1);
    }
    int my_proc = fork();
    if (my_proc == 0) // child
    {
        char child_buffer[1] = {0};
        close(pipe_1[1]);
        close(pipe_2[0]);
        if (read(pipe_1[0], child_buffer, 1) < 0)
        {
            fprintf(2, "Child: Error in read\n");
            close(pipe_1[0]);
            close(pipe_2[1]);
            exit(1);
        }
        close(pipe_1[0]);
        fprintf(1, "%d: received ping\n", getpid());
        if (write(pipe_2[1], child_buffer, 1) < 0)
        {
            fprintf(2, "Child: Error in write\n");
            close(pipe_2[1]);
            exit(1);
        }
        close(pipe_2[1]);
        exit(0);
    }
    char parent_buffer[1] = {(char)0x42};
    close(pipe_1[0]);
    close(pipe_2[1]);
    if (write(pipe_1[1], parent_buffer, 1) < 0)
    {
        fprintf(2, "Parent: Error in write\n");
        close(pipe_1[1]);
        close(pipe_2[0]);
        wait(&my_proc);
        exit(1);
    }
    close(pipe_1[1]);
    if (read(pipe_2[0], parent_buffer, 1) < 0)
    {
        fprintf(2, "Parent: Error in read\n");
        close(pipe_2[0]);
        wait(&my_proc);
        exit(1);
    }
    fprintf(1, "%d: received pong\n", getpid());
    close(pipe_2[0]);
    wait(&my_proc);
    exit(0);
}