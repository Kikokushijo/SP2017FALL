/* b05902052 劉家維 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/types.h>

typedef struct {
    char string[500];
} Info;

void mmap_write(const char* map, const char* filename){
    int fd, i;
    time_t current_time;
    char string[500];
    Info *p_map;
    // const char *file = "time_test";
    
    fd = open(map, O_RDWR | O_TRUNC | O_CREAT, 0777); 
    if(fd < 0){
        perror("open");
        exit(-1);
    }
    lseek(fd, sizeof(Info), SEEK_SET);
    write(fd, "", 1);

    p_map = (Info*) mmap(0, sizeof(Info), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    fprintf(stderr, "mmap address:%#x\n",(unsigned int)&p_map); // 0x00000
    close(fd);


    current_time = time(NULL);
    char tmp[100] = {};
    strcpy(tmp, ctime(&current_time));
    tmp[strlen(tmp)-2] = '\0';
    snprintf(string, sizeof(string), "Last Exit CGI: %s, Filename: %s", tmp, filename);
    memcpy(p_map->string, &string , sizeof(string));
    
    fprintf(stderr, "initialize over\n ");
    munmap(p_map, sizeof(Info));
    return;
}


int main(int argc, char **argv){
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
    mmap_write(argv[1], filename);
    sleep(5);
    for (;;){
        char buffer[1024] = {};
        int read_len = read(fd, buffer, 1020);
        if (read_len == 0) break;
        write(STDOUT_FILENO, buffer, strlen(buffer));
    }
    fprintf(stderr, "%s\n", argv[1]);
    exit(0);
}