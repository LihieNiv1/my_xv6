#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

#define MAX_CMD_LEN 100

int exec_xargs(char *argv[])
{
    int child = fork();
    if (child == 0)
    {
        if (exec(argv[0], argv) != 0)
        {
            fprintf(2, "xargs: error while executing %s\n", argv[0]);
            exit(1);
        }
    }
    if (child < 0)
        exit(1);
    wait(&child);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(2, "Usage: xargs <command> <args>\n");
        exit(1);
    }
    char line_arg[MAX_CMD_LEN + 1] = {0};
    uint offset = 0;
    for (int i = 0; i < argc - 1; i++)
    {
        argv[i] = argv[i + 1];
    }
    argv[argc - 1] = line_arg;
    int status = 0;
    while ((status = read(0, line_arg + offset, 1)) == 1)
    {
        if (line_arg[offset] == '\n')
        {
            line_arg[offset] = 0;
            exec_xargs(argv);
            offset = 0;
        }
        else if (offset == MAX_CMD_LEN)
        {
            fprintf(2, "xargs: line too long\n");
            exit(1);
        }
        else
        {
            offset++;
        }
    }
    if (status < 0)
    {
        fprintf(2, "Error in read\n");
        exit(1);
    }
    exit(0);
}