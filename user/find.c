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

int find(char *dir_path, char *pattern)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if ((fd = open(dir_path, 0)) < 0)
    {
        fprintf(2, "find: cannot open %s\n", dir_path);
        return 1;
    }

    if (fstat(fd, &st) < 0)
    {
        fprintf(2, "find: cannot stat %s\n", dir_path);
        close(fd);
        return 1;
    }

    if (st.type == T_DIR)
    {
        if (strlen(dir_path) + 1 + DIRSIZ + 1 > sizeof buf) // check over length of path
        {
            fprintf(2, "find: path too long\n");
            close(fd);
            return 1;
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
                    fprintf(2, "find: cannot stat %s\n", buf);
                    return 1;
                }
                if (st.type == T_DIR)
                {

                    if (strcmp(".", fmtname(buf)) && strcmp("..", fmtname(buf)))
                    {
                        if (find(buf, pattern) < 0)
                        {
                            close(fd);
                            return 1;
                        }
                    }
                }
                else if (!strcmp(fmtname(buf), pattern))
                    printf("%s\n", buf);
            }
        }
    }
    else
    {
        close(fd);
        return 1;
    }
    close(fd);
    return 0;
}

int main(int argc, char *argv[])
{

    if (argc == 2)
    {
        exit(find(".", argv[1]));
    }
    if (argc > 3 || argc == 1)
    {
        fprintf(2, "Usage: find <directory> <pattern>");
        exit(1);
    }
    exit(find(argv[1], argv[2]));
}
