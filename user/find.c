#include "user/user.h"
#include "kernel/stat.h"
#include "kernel/types.h"
#include "kernel/fs.h"

#define MAX_PATH 512

void find(char *cur_path, const char *name)
{
    char *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(cur_path, 0)) < 0)
    {
        fprintf(2, "find: cannot open %s\n", cur_path);
        return;
    }
    if(fstat(fd, &st) < 0)
    {
        fprintf(2, "find: cannot stat %s\n", cur_path);
        close(fd);
        return;
    }
    
    p = cur_path + strlen(cur_path);
    if(*(p - 1) != '/') // ensure the path ends with '/'
    {
        *p++ = '/';
        *p = 0;
    }
    while(read(fd, &de, sizeof(de)) == sizeof(de))
    {
        // get the path(cur_path (+ '/') + de.name) to the current entry
        char *tmp = p; // point to the position after '/'
        if(de.inum == 0) continue; // empty entry, skip it
        if(strlen(cur_path) + 1 + DIRSIZ + 1 > MAX_PATH)
        {
            fprintf(2, "find: path too long\n");
            continue;
        }
        memmove(tmp, de.name, DIRSIZ);
        tmp[DIRSIZ] = 0; // null-terminate the string

        if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
            continue; // skip . and .., avoid infinite recursion

        // ! most crucial part: patch the name with the file(or directory) name
        if(strcmp(de.name, name) == 0)
        {
            // found
            printf("%s\n", cur_path);
        }

        if(stat(cur_path, &st) < 0)
        {
            fprintf(2, "find: cannot stat %s\n", cur_path);
            continue;
        }
        if(st.type == T_FILE)
        {
            continue; // file, skip to next entry
        }
        else if(st.type == T_DIR)
        {
            find(cur_path, name); // directory, recursively find
        }

        *p = 0; // restore cur_path to the state before appending de.name
    }

    return;
}

int main(int argc, char *argv[])
{
    if(argc != 3)
    {
        fprintf(2, "find: usage format 'find <path> <name>'\n");
        exit(1);
    }

    // argv[1] path, argv[2] name
    // thoughts:
    // 1. check and open the path
    // 2. read the directory entry one by one
    // 3. patch the name of the file or directory
    // 3.1. file: compare the name
    // 3.2. directory: recursive call(back to step 2)

    // situation 1: find . name
    // situation 2: find dirname name
    //  dirname formats: /..path../dirname | ./..path../dirname | path../dirname
    //  dirname can be subfixed by `/` or not
    //  open can handle these formats(it can handle both absolute and relative path)

    // check the validity of argv[1](the path) and argv[2](the name)
    if(strlen(argv[1]) + 1 > 512)
    {
        fprintf(2, "find: path too long\n");
        exit(1);
    }
    else if(strlen(argv[2]) + 1 > 512)
    {
        fprintf(2, "find: name too long\n");
        exit(1);
    }

    int fd;
    struct stat st;
    if((fd = open(argv[1], 0)) < 0)
    {
        fprintf(2, "find: no directory %s\n", argv[1]);
        exit(1);
    }
    else if(fstat(fd, &st) < 0)
    {
        fprintf(2, "find: cannot stat %s\n", argv[1]);
        exit(1);
    }
    else if(st.type != T_DIR)
    {
        fprintf(2, "find: %s is not a directory\n", argv[1]);
        exit(1);
    }
    close(fd);

    char buf[MAX_PATH];
    strcpy(buf, argv[1]);
    find(buf, argv[2]);
    exit(0);
}