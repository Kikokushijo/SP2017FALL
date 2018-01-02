#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>

int main(){
    char filename[1024] = {};
    read(STDIN_FILENO, filename, 1024);
    fprintf(stderr, "READING:%s\n", filename);

    if (filename[strlen(filename) - 1] == '\n')
        filename[strlen(filename) - 1] = 0;

    int fd = open(filename, O_RDONLY);

    if(fd < 0){
        char msg[1024] = {};
        if (errno == EACCES){
            sprintf(msg, "Permission Denied: %s\n", filename);
            fprintf(stderr, "Permission Denied:%s\n", filename);
            write(STDOUT_FILENO, msg, strlen(msg));
            exit(1);
        }
        else if (errno == ENOENT){
            sprintf(msg, "No Such File or Directory: %s\n", filename);
            fprintf(stderr, "No File:%s\n", filename);
            write(STDOUT_FILENO, msg, strlen(msg));
            exit(2);
        }
    }

    for (;;){
        char buffer[1024] = {};
        int read_len = read(fd, buffer, 1020);
        if (read_len == 0) break;
        write(STDOUT_FILENO, buffer, strlen(buffer));
    }
    exit(0);
}