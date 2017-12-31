#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>

int main(){
    while (1){
        // fprintf(stderr, "FORK!!!");
        char filename[1024] = {};
        read(STDIN_FILENO, filename, 1024);
        filename[strlen(filename)] = '\0';
        int fd = open(filename, O_RDONLY);

        if(fd < 0){
            char tmp[1024] = {};
            sprintf(tmp, "No such file or directory: %s\n", filename);
            write(STDOUT_FILENO, tmp, strlen(tmp));
            // perror(tmp);
            exit(-1);
        }

        for (;;){
            char buffer[1024] = {};
            int read_len = read(fd, buffer, 1020);
            if (read_len == 0) break;
            write(STDOUT_FILENO, buffer, strlen(buffer));
        }
    }
}