#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char *
fmtname(char *path)
{
    static char buf[DIRSIZ + 1];
    char *p;
    // Find first character after last slash.
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;

    // Return blank-padded name.
    if (strlen(p) >= DIRSIZ)
        return p;
    memmove(buf, p, strlen(p));
    memset(buf + strlen(p), 0, DIRSIZ - strlen(p));
    return buf;
}

void find(char *dir_path, char *pattern)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if ((fd = open(dir_path, 0)) < 0)
    {
        fprintf(2, "find: cannot open %s\n", dir_path);
        return;
    }

    if (fstat(fd, &st) < 0)
    {
        fprintf(2, "find: cannot stat %s\n", dir_path);
        close(fd);
        return;
    }

    if (st.type == T_DIR)
    {
        if (strlen(dir_path) + 1 + DIRSIZ + 1 > sizeof buf)
        {
            printf("find: path too long\n");
        }
        else
        {
            strcpy(buf, dir_path);
            p = buf + strlen(buf);
            *p++ = '/';
            while (read(fd, &de, sizeof(de)) == sizeof(de))
            {
                if (de.inum == 0)
                    continue;
                memmove(p, de.name, DIRSIZ);
                p[DIRSIZ] = 0;
                if (stat(buf, &st) < 0)
                {
                    printf("find: cannot stat %s\n", buf);
                    continue;
                }
                if (st.type == T_DIR)
                {

                    if (strcmp(".", fmtname(buf)) && strcmp("..", fmtname(buf)))
                    {
                        find(buf, pattern);
                    }
                }
                else if (!strcmp(fmtname(buf), pattern))
                    printf("%s\n", buf);
            }
        }
    }
    close(fd);
}

int main(int argc, char *argv[])
{

    if (argc == 2)
    {
        find(".", argv[1]);
        exit(0);
    }
    if (argc > 3)
    {
        fprintf(2, "Usage: find <directory> <pattern>");
        exit(1);
    }
    else
    {
        find(argv[1], argv[2]);
    }
    exit(0);
}
