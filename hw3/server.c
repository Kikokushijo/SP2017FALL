/* b05902052 劉家維 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>

#include <time.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/mman.h>

#define TIMEOUT_SEC 5           // timeout in seconds for wait for a connection 
#define MAXBUFSIZE  1024        // timeout in seconds for wait for a connection 
#define NO_USE      0           // status of a http request
#define ERROR       -1  
#define READING     1           
#define WRITING     2           
#define ERR_EXIT(a) { perror(a); exit(1); }

typedef struct {
    char hostname[512];         // hostname
    unsigned short port;        // port to listen
    int listen_fd;              // fd to wait for a new connection
} http_server;

typedef struct {
    int conn_fd;                // fd to talk with client
    int status;                 // not used, error, reading (from client)
                                // writing (to client)
    char file[MAXBUFSIZE];      // requested file
    char query[MAXBUFSIZE];     // requested query
    char host[MAXBUFSIZE];      // client host
    char* buf;                  // data sent by/to client
    size_t buf_len;             // bytes used by buf
    size_t buf_size;            // bytes allocated for buf
    size_t buf_idx;             // offset for reading and writing
} http_request;

typedef struct {
    char string[500];
} Info;

static char* logfilenameP;      // log file name
int info_flag = 0;

// Forwards
//
static void init_http_server( http_server *svrP,  unsigned short port );
// initailize a http_request instance, exit for error

static void init_request( http_request* reqP );
// initailize a http_request instance

static void free_request( http_request* reqP );
// free resources used by a http_request instance

static int read_header_and_file( http_request* reqP, int *errP );
// return 0: success, file is buffered in retP->buf with retP->buf_len bytes
// return -1: error, check error code (*errP)
// return 1: continue to it until return -1 or 0
// error code: 
// 1: client connection error 
// 2: bad request, cannot parse request
// 3: method not implemented 
// 4: illegal filename
// 5: illegal query
// 6: file not found
// 7: file is protected

static void set_ndelay( int fd );
// Set NDELAY mode on a socket.

static void add_to_buf( http_request *reqP, char* str, size_t len );

void info_handler(int signo){
    // fprintf(stderr, "INFO!!!\n");
    info_flag = 1;
    return;
}

int isvalid_name(char* str){
    int len = strlen(str);
    if (len == 0) return 0;
    for (int i = 0; i != len; ++i){
        if (!isalpha(str[i]) && !isdigit(str[i]) && str[i] != '_')
            return 0;
    }
    return 1;
}

void mmap_init(const char* map){
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

    snprintf(string, sizeof(string), "Last Exit CGI: -, Filename: -");
    memcpy(p_map->string, &string , sizeof(string));
    
    fprintf(stderr, "initialize over\n ");
    munmap(p_map, sizeof(Info));
    return;
}

int main( int argc, char** argv ) {
    http_server server;         // http server
    http_request* requestP = NULL;// pointer to http requests from client

    int maxfd;                  // size of open file descriptor table

    struct sockaddr_in cliaddr; // used by accept()
    int clilen;

    int conn_fd;                // fd for a new connection with client
    int err;                    // used by read_header_and_file()
    int i, ret, nwritten;
    
    char timebuf[100];
    int buflen;
    char buf[20000];

    int to_CGI[maxfd][2], from_CGI[maxfd][2];
    int openedfd[10000] = {}, pipe2conn[10000] = {}, pipe2pid[10000] = {};
    int running = 0, died = 0;    

    time_t now;

    // Parse args. 
    if ( argc != 3 ) {
        (void) fprintf( stderr, "usage:  %s port# logfile\n", argv[0] );
        exit( 1 );
    }

    logfilenameP = argv[2];
    mmap_init(logfilenameP);

    // Initialize http server
    init_http_server( &server, (unsigned short) atoi( argv[1] ) );
    openedfd[server.listen_fd] = 1;

    maxfd = getdtablesize();
    requestP = ( http_request* ) malloc( sizeof( http_request ) * maxfd );
    if ( requestP == (http_request*) 0 ) {
        fprintf( stderr, "out of memory allocating all http requests\n" );
        exit( 1 );
    }
    for ( i = 0; i < maxfd; i ++ )
        init_request( &requestP[i] );
    requestP[ server.listen_fd ].conn_fd = server.listen_fd;
    requestP[ server.listen_fd ].status = READING;

    fprintf( stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d, logfile %s...\n", server.hostname, server.port, server.listen_fd, maxfd, logfilenameP );
    signal(SIGUSR1, info_handler);
    // Main loop. 
    while (1) {

        // Select Server or CGI
        fd_set fdread;
        FD_ZERO(&fdread);
        for(int i = 0; i != maxfd; ++i){
            if (openedfd[i])
                FD_SET(i, &fdread);
        }

        int validfd = 0;
        struct timeval timeout = {1, 0};
        if((validfd = select(maxfd, &fdread, NULL, NULL, &timeout)) < 0){
            printf("%s\n",strerror(errno));
            return 0;
        }else if (validfd == 0){
            continue;
        }
        // puts("SELECTING...");
        if(FD_ISSET(server.listen_fd, &fdread)){
            clilen = sizeof(cliaddr);
            conn_fd = accept( server.listen_fd, (struct sockaddr *) &cliaddr, (socklen_t *) &clilen );
            if ( conn_fd < 0 ) {
                if ( errno == EINTR || errno == EAGAIN ) continue; // try again 
                if ( errno == ENFILE ) {
                    (void) fprintf( stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd );
                    continue;
                }   
                ERR_EXIT( "accept" )
            }
            requestP[conn_fd].conn_fd = conn_fd;
            requestP[conn_fd].status = READING;             
            strcpy( requestP[conn_fd].host, inet_ntoa( cliaddr.sin_addr ) );
            set_ndelay( conn_fd );

            fprintf( stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host );

            while (1) {
                ret = read_header_and_file( &requestP[conn_fd], &err );
                if ( ret > 0 ) continue;
                else if ( ret < 0 ) {
                    // error for reading http header or requested file
                    fprintf( stderr, "error on fd %d, code %d\n", 
                    requestP[conn_fd].conn_fd, err );
                    requestP[conn_fd].status = ERROR;
                    close( requestP[conn_fd].conn_fd );
                    free_request( &requestP[conn_fd] );
                    break;
                } else if ( ret == 0 ) {

                    // ready for writing
                    fprintf(stderr, "writing (buf %p, idx %d) %d bytes to request fd %d\n", 
                            requestP[conn_fd].buf, (int) requestP[conn_fd].buf_idx,
                            (int) requestP[conn_fd].buf_len, requestP[conn_fd].conn_fd );

                    requestP[conn_fd].buf_len = 0;

                    char buf_query[1024] = {};
                    sscanf(requestP[conn_fd].query, "filename=%s", buf_query);
                    if (strcmp(requestP[conn_fd].file, "info") == 0 && strlen(buf_query) < 5) {
                        // fprintf(stderr, "INFO\n");
                        if (fork() == 0) {
                            // fprintf(stderr, "INFO\n");
                            // signal(SIGUSR1, infoHandler);
                            kill(getppid(), SIGUSR1);
                            ++died;
                            exit(0);
                        }
                        while(!info_flag);
                        // fprintf(stderr, "INFO START %d\n", died);
                        requestP[conn_fd].buf_len = 0;
                        buflen = snprintf( buf, sizeof(buf), "%d processes died previously.\015\012", died );
                        add_to_buf( &requestP[conn_fd], buf, buflen );
                        int pids[1024] = {}, count = 0;
                        for (int i = 4; i != maxfd; ++i){
                            if (openedfd[i])
                                pids[count++] = pipe2pid[i];
                            // fprintf(stderr, "%d\n", i);
                        }
                        if (count > 0){
                            buflen = snprintf( buf, sizeof(buf), "PIDs of Running Processes:");
                            add_to_buf( &requestP[conn_fd], buf, buflen );
                            for (int i = 0; i < count; ++i){
                                buflen = snprintf( buf, sizeof(buf), "%s %d", (i==0)?"":",", pids[i]);
                                add_to_buf( &requestP[conn_fd], buf, buflen );
                                fprintf(stderr, "%d\n", pids[i]);
                            }
                        }else{
                            buflen = snprintf( buf, sizeof(buf), "PIDs of Running Processes: Nothing");
                            add_to_buf( &requestP[conn_fd], buf, buflen );
                        }
                        buflen = snprintf( buf, sizeof(buf), "\015\012");
                        add_to_buf( &requestP[conn_fd], buf, buflen );
                        //
                        int f, i;
                        // time_t current_time;
                        char string[500];
                        Info *p_map;
                        f = open(logfilenameP, O_RDWR);
                        p_map = (Info*)mmap(0, sizeof(Info),  PROT_READ,  MAP_SHARED, f, 0);
                        fprintf(stderr, "%s\n", p_map->string);
                        close(f);
                        buflen = snprintf( buf, sizeof(buf), "%s", p_map->string);
                        // buf[--buflen] = '\0';
                        add_to_buf( &requestP[conn_fd], buf, buflen );
                        //
                        char tmp[3000] = {};
                        strcpy(tmp, requestP[conn_fd].buf);

                        requestP[conn_fd].buf_len = 0;
                        buflen = snprintf( buf, sizeof(buf), "HTTP/1.1 200 OK\015\012Server: SP TOY\015\012" );
                        add_to_buf( &requestP[conn_fd], buf, buflen );
                        now = time( (time_t*) 0 );
                        (void) strftime( timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime( &now ) );
                        buflen = snprintf( buf, sizeof(buf), "Date: %s\015\012", timebuf );
                        add_to_buf( &requestP[conn_fd], buf, buflen );
                        buflen = snprintf( buf, sizeof(buf), "Content-Length: %d\015\012", strlen(tmp) );
                        add_to_buf( &requestP[conn_fd], buf, buflen );
                        buflen = snprintf( buf, sizeof(buf), "Connection: close\015\012\015\012" );
                        add_to_buf( &requestP[conn_fd], buf, buflen );
                        buflen = snprintf( buf, sizeof(buf), "%s\015\012\015\012", tmp );
                        add_to_buf( &requestP[conn_fd], buf, buflen );
                        nwritten = send( requestP[conn_fd].conn_fd, requestP[conn_fd].buf, requestP[conn_fd].buf_len, 0 );
                        fprintf( stderr, "complete writing %d bytes on fd %d\n", nwritten, requestP[conn_fd].conn_fd );
                        close( requestP[conn_fd].conn_fd );
                        free_request( &requestP[conn_fd] );

                        nwritten = send( requestP[conn_fd].conn_fd, requestP[conn_fd].buf, requestP[conn_fd].buf_len, 0 );
                        close( requestP[conn_fd].conn_fd );
                        free_request( &requestP[conn_fd] );
                        ++died;
                        info_flag = 0;
                    }else if (!isvalid_name(requestP[conn_fd].file) || !isvalid_name(buf_query)){
                        requestP[conn_fd].buf_len = 0;
                        buflen = snprintf( buf, sizeof(buf), "HTTP/1.1 400 BAD REQUEST\015\012Server: SP TOY\015\012" );
                        add_to_buf( &requestP[conn_fd], buf, buflen );
                        now = time( (time_t*) 0 );
                        (void) strftime( timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime( &now ) );
                        buflen = snprintf( buf, sizeof(buf), "Date: %s\015\012", timebuf );
                        add_to_buf( &requestP[conn_fd], buf, buflen );
                        buflen = snprintf( buf, sizeof(buf), "Content-Length: 41\015\012");
                        add_to_buf( &requestP[conn_fd], buf, buflen );
                        buflen = snprintf( buf, sizeof(buf), "Connection: close\015\012\015\012" );
                        add_to_buf( &requestP[conn_fd], buf, buflen );
                        buflen = snprintf( buf, sizeof(buf), "400 BAD REQUEST: INVALID REQUEST FILENAME\015\012\015\012" );
                        add_to_buf( &requestP[conn_fd], buf, buflen );
                        nwritten = send( requestP[conn_fd].conn_fd, requestP[conn_fd].buf, requestP[conn_fd].buf_len, 0 );
                        fprintf( stderr, "complete writing %d bytes on fd %d\n", nwritten, requestP[conn_fd].conn_fd );
                        fprintf( stderr, "BAD REQUEST\n");
                        close( requestP[conn_fd].conn_fd );
                        free_request( &requestP[conn_fd] );
                    }else{
                        pipe(to_CGI[conn_fd]);
                        pipe(from_CGI[conn_fd]);
                        pid_t pid;
                        
                        if ((pid = fork()) == 0){
                            dup2(to_CGI[conn_fd][0], STDIN_FILENO);
                            dup2(from_CGI[conn_fd][1], STDOUT_FILENO);
                            close(to_CGI[conn_fd][1]);
                            close(from_CGI[conn_fd][0]);
                            // close(STDIN_FILENO);
                            // close(STDOUT_FILENO);
                            fprintf(stderr, "LOGFILE:%s\n", logfilenameP);
                            execl(requestP[conn_fd].file, requestP[conn_fd].file, logfilenameP, NULL);
                            ++died;
                            exit(3);
                        }
                        close(to_CGI[conn_fd][0]);
                        close(from_CGI[conn_fd][1]);
                        int read_pipe = from_CGI[conn_fd][0];
                        openedfd[read_pipe] = 1;
                        pipe2conn[read_pipe] = conn_fd;
                        pipe2pid[read_pipe] = pid;
                        write(to_CGI[conn_fd][1], buf_query, strlen(buf_query));
                        // fprintf(stderr, "fd:%d WRITTEN:%s\n", to_CGI[conn_fd][1], buf_query);
                        // fprintf(stderr, "PIPING: %d\n", read_pipe);
                        // fprintf(stderr, "Pipe2conn: %d\n", pipe2conn[read_pipe]);
                        // fprintf(stderr, "Pipe2pid: %d\n", pipe2pid[read_pipe]);
                        // puts("FORK FINISH");
                    }
                    
                    // write once only and ignore error
                    // nwritten = write( requestP[conn_fd].conn_fd, requestP[conn_fd].buf, requestP[conn_fd].buf_len );
                    // fprintf( stderr, "complete writing %d bytes on fd %d\n", nwritten, requestP[conn_fd].conn_fd );
                    // close( requestP[conn_fd].conn_fd );
                    // free_request( &requestP[conn_fd] );
                    
                    break;
                }
            }
        }else{
            // fprintf(stderr, "SELECT PIPE\n");
            int pipe_fd;
            pid_t pid;
            for(pipe_fd = 4; pipe_fd != maxfd; ++pipe_fd){
                // fprintf(stderr, "SELECTED: %d\n", pipe_fd);
                if (openedfd[pipe_fd] && FD_ISSET(pipe_fd, &fdread) > 0){
                    conn_fd = pipe2conn[pipe_fd];
                    pid = pipe2pid[pipe_fd];
                    break;
                }
            }
            openedfd[pipe_fd] = 0;
            // fprintf(stderr, "SELECTED: %d\n", pipe_fd);


            int status;
            pid_t ret = waitpid(pid, &status, 0);
            status = WEXITSTATUS(status);
            fprintf(stderr, "PID:%d STATUS:%d\nRET:%d\n", (int)pid, (int)status, (int)ret);
            if (ret <= 0){
                continue;
            }
            if (status == 3){
                requestP[conn_fd].buf_len = 0;
                buflen = snprintf( buf, sizeof(buf), "HTTP/1.1 404 NOT FOUND\015\012Server: SP TOY\015\012" );
                add_to_buf( &requestP[conn_fd], buf, buflen );
                now = time( (time_t*) 0 );
                (void) strftime( timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime( &now ) );
                buflen = snprintf( buf, sizeof(buf), "Date: %s\015\012", timebuf );
                add_to_buf( &requestP[conn_fd], buf, buflen );
                buflen = snprintf( buf, sizeof(buf), "Content-Length: 36\015\012");
                add_to_buf( &requestP[conn_fd], buf, buflen );
                buflen = snprintf( buf, sizeof(buf), "Connection: close\015\012\015\012" );
                add_to_buf( &requestP[conn_fd], buf, buflen );
                buflen = snprintf( buf, sizeof(buf), "404 NOT FOUND: CGI PROGRAM NOT FOUND\015\012\015\012" );
                add_to_buf( &requestP[conn_fd], buf, buflen );
                nwritten = send( requestP[conn_fd].conn_fd, requestP[conn_fd].buf, requestP[conn_fd].buf_len, 0 );
                fprintf( stderr, "complete writing %d bytes on fd %d\n", nwritten, requestP[conn_fd].conn_fd );
                fprintf( stderr, "BAD REQUEST\n");
                close( requestP[conn_fd].conn_fd );
                free_request( &requestP[conn_fd] );
                close(to_CGI[conn_fd][1]);
                close(from_CGI[conn_fd][0]);
                ++died;
            }if (status == 2){
                requestP[conn_fd].buf_len = 0;
                buflen = snprintf( buf, sizeof(buf), "HTTP/1.1 404 NOT FOUND\015\012Server: SP TOY\015\012" );
                add_to_buf( &requestP[conn_fd], buf, buflen );
                now = time( (time_t*) 0 );
                (void) strftime( timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime( &now ) );
                buflen = snprintf( buf, sizeof(buf), "Date: %s\015\012", timebuf );
                add_to_buf( &requestP[conn_fd], buf, buflen );
                buflen = snprintf( buf, sizeof(buf), "Content-Length: 29\015\012");
                add_to_buf( &requestP[conn_fd], buf, buflen );
                buflen = snprintf( buf, sizeof(buf), "Connection: close\015\012\015\012" );
                add_to_buf( &requestP[conn_fd], buf, buflen );
                buflen = snprintf( buf, sizeof(buf), "404 NOT FOUND: FILE NOT FOUND\015\012\015\012" );
                add_to_buf( &requestP[conn_fd], buf, buflen );
                nwritten = send( requestP[conn_fd].conn_fd, requestP[conn_fd].buf, requestP[conn_fd].buf_len, 0 );
                fprintf( stderr, "complete writing %d bytes on fd %d\n", nwritten, requestP[conn_fd].conn_fd );
                fprintf( stderr, "BAD REQUEST\n");
                close( requestP[conn_fd].conn_fd );
                free_request( &requestP[conn_fd] );
                close(to_CGI[conn_fd][1]);
                close(from_CGI[conn_fd][0]);
                ++died;
            }if (status == 1){
                requestP[conn_fd].buf_len = 0;
                buflen = snprintf( buf, sizeof(buf), "HTTP/1.1 404 NOT FOUND\015\012Server: SP TOY\015\012" );
                add_to_buf( &requestP[conn_fd], buf, buflen );
                now = time( (time_t*) 0 );
                (void) strftime( timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime( &now ) );
                buflen = snprintf( buf, sizeof(buf), "Date: %s\015\012", timebuf );
                add_to_buf( &requestP[conn_fd], buf, buflen );
                buflen = snprintf( buf, sizeof(buf), "Content-Length: 32\015\012");
                add_to_buf( &requestP[conn_fd], buf, buflen );
                buflen = snprintf( buf, sizeof(buf), "Connection: close\015\012\015\012" );
                add_to_buf( &requestP[conn_fd], buf, buflen );
                buflen = snprintf( buf, sizeof(buf), "404 NOT FOUND: PERMISSION DENIED\015\012\015\012" );
                add_to_buf( &requestP[conn_fd], buf, buflen );
                nwritten = send( requestP[conn_fd].conn_fd, requestP[conn_fd].buf, requestP[conn_fd].buf_len, 0 );
                fprintf( stderr, "complete writing %d bytes on fd %d\n", nwritten, requestP[conn_fd].conn_fd );
                fprintf( stderr, "BAD REQUEST\n");
                close( requestP[conn_fd].conn_fd );
                free_request( &requestP[conn_fd] );
                close(to_CGI[conn_fd][1]);
                close(from_CGI[conn_fd][0]);
                ++died;
            }else{
                char contbuf[20000] = {};
                int cont_len = read(pipe_fd, contbuf, 19500);
                // fprintf(stderr, "%s\n", buf);
                requestP[conn_fd].buf_len = 0;
                buflen = snprintf( buf, sizeof(buf), "HTTP/1.1 200 OK\015\012Server: SP TOY\015\012" );
                add_to_buf( &requestP[conn_fd], buf, buflen );
                now = time( (time_t*) 0 );
                (void) strftime( timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime( &now ) );
                buflen = snprintf( buf, sizeof(buf), "Date: %s\015\012", timebuf );
                add_to_buf( &requestP[conn_fd], buf, buflen );
                buflen = snprintf( buf, sizeof(buf), "Content-Length: %d\015\012", cont_len );
                add_to_buf( &requestP[conn_fd], buf, buflen );
                buflen = snprintf( buf, sizeof(buf), "Connection: close\015\012\015\012" );
                add_to_buf( &requestP[conn_fd], buf, buflen );
                buflen = snprintf( buf, sizeof(buf), "%s\015\012\015\012", contbuf );
                add_to_buf( &requestP[conn_fd], buf, buflen );
                nwritten = send( requestP[conn_fd].conn_fd, requestP[conn_fd].buf, requestP[conn_fd].buf_len, 0 );
                fprintf( stderr, "complete writing %d bytes on fd %d\n", nwritten, requestP[conn_fd].conn_fd );
                close( requestP[conn_fd].conn_fd );
                free_request( &requestP[conn_fd] );
                close(to_CGI[conn_fd][1]);
                close(from_CGI[conn_fd][0]);
                ++died;
            }


            // puts("FINISH PIPE");


        }

        // puts("FINISH SELECT");

        // Wait for a connection.
            
    }
    free( requestP );
    return 0;
}


// ======================================================================================================
// You don't need to know how the following codes are working

#include <time.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/mman.h>

static void add_to_buf( http_request *reqP, char* str, size_t len );
static void strdecode( char* to, char* from );
static int hexit( char c );
static char* get_request_line( http_request *reqP );
static void* e_malloc( size_t size );
static void* e_realloc( void* optr, size_t size );

static void init_request( http_request* reqP ) {
    reqP->conn_fd = -1;
    reqP->status = 0;           // not used
    reqP->file[0] = (char) 0;
    reqP->query[0] = (char) 0;
    reqP->host[0] = (char) 0;
    reqP->buf = NULL;
    reqP->buf_size = 0;
    reqP->buf_len = 0;
    reqP->buf_idx = 0;
}

static void free_request( http_request* reqP ) {
    if ( reqP->buf != NULL ) {
        free( reqP->buf );
        reqP->buf = NULL;
    }
    init_request( reqP );
}


#define ERR_RET( error ) { *errP = error; return -1; }
// return 0: success, file is buffered in retP->buf with retP->buf_len bytes
// return -1: error, check error code (*errP)
// return 1: read more, continue until return -1 or 0
// error code: 
// 1: client connection error 
// 2: bad request, cannot parse request
// 3: method not implemented 
// 4: illegal filename
// 5: illegal query
// 6: file not found
// 7: file is protected
//
static int read_header_and_file( http_request* reqP, int *errP ) {
    // Request variables
    char* file = (char *) 0;
    char* path = (char *) 0;
    char* query = (char *) 0;
    char* protocol = (char *) 0;
    char* method_str = (char *) 0;
    int r, fd;
    struct stat sb;
    char timebuf[100];
    int buflen;
    char buf[10000];
    time_t now;
    void *ptr;

    // Read in request from client
    while (1) {
        r = read( reqP->conn_fd, buf, sizeof(buf) );
        if ( r < 0 && ( errno == EINTR || errno == EAGAIN ) ) return 1;
        if ( r <= 0 ) ERR_RET( 1 )
        add_to_buf( reqP, buf, r );
        if ( strstr( reqP->buf, "\015\012\015\012" ) != (char*) 0 ||
             strstr( reqP->buf, "\012\012" ) != (char*) 0 ) break;
    }
    // fprintf( stderr, "header: %s\n", reqP->buf );

    // Parse the first line of the request.
    method_str = get_request_line( reqP );
    if ( method_str == (char*) 0 ) ERR_RET( 2 )
    path = strpbrk( method_str, " \t\012\015" );
    if ( path == (char*) 0 ) ERR_RET( 2 )
    *path++ = '\0';
    path += strspn( path, " \t\012\015" );
    protocol = strpbrk( path, " \t\012\015" );
    if ( protocol == (char*) 0 ) ERR_RET( 2 )
    *protocol++ = '\0';
    protocol += strspn( protocol, " \t\012\015" );
    query = strchr( path, '?' );
    if ( query == (char*) 0 )
        query = "";
    else
        *query++ = '\0';

    if ( strcasecmp( method_str, "GET" ) != 0 ) ERR_RET( 3 )
    else {
        strdecode( path, path );
        if ( path[0] != '/' ) ERR_RET( 4 )
        else file = &(path[1]);
    }

    if ( strlen( file ) >= MAXBUFSIZE-1 ) ERR_RET( 4 )
    if ( strlen( query ) >= MAXBUFSIZE-1 ) ERR_RET( 5 )
          
    strcpy( reqP->file, file );
    strcpy( reqP->query, query );

    /*
    if ( query[0] == (char) 0 ) {
        // for file request, read it in buf
        r = stat( reqP->file, &sb );
        if ( r < 0 ) ERR_RET( 6 )

        fd = open( reqP->file, O_RDONLY );
        if ( fd < 0 ) ERR_RET( 7 )

        reqP->buf_len = 0;

        buflen = snprintf( buf, sizeof(buf), "HTTP/1.1 200 OK\015\012Server: SP TOY\015\012" );
        add_to_buf( reqP, buf, buflen );
        now = time( (time_t*) 0 );
        (void) strftime( timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime( &now ) );
        buflen = snprintf( buf, sizeof(buf), "Date: %s\015\012", timebuf );
        add_to_buf( reqP, buf, buflen );
        buflen = snprintf(
            buf, sizeof(buf), "Content-Length: %ld\015\012", (int64_t) sb.st_size );
        add_to_buf( reqP, buf, buflen );
        buflen = snprintf( buf, sizeof(buf), "Connection: close\015\012\015\012" );
        add_to_buf( reqP, buf, buflen );

        ptr = mmap( 0, (size_t) sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
        if ( ptr == (void*) -1 ) ERR_RET( 8 )
        add_to_buf( reqP, ptr, sb.st_size );
        (void) munmap( ptr, sb.st_size );
        close( fd );
        // printf( "%s\n", reqP->buf );
        // fflush( stdout );
        reqP->buf_idx = 0; // writing from offset 0
        return 0;
    }
    */

    return 0;
}


static void add_to_buf( http_request *reqP, char* str, size_t len ) { 
    char** bufP = &(reqP->buf);
    size_t* bufsizeP = &(reqP->buf_size);
    size_t* buflenP = &(reqP->buf_len);

    if ( *bufsizeP == 0 ) {
        *bufsizeP = len + 500;
        *buflenP = 0;
        *bufP = (char*) e_malloc( *bufsizeP );
    } else if ( *buflenP + len >= *bufsizeP ) {
        *bufsizeP = *buflenP + len + 500;
        *bufP = (char*) e_realloc( (void*) *bufP, *bufsizeP );
    }
    (void) memmove( &((*bufP)[*buflenP]), str, len );
    *buflenP += len;
    (*bufP)[*buflenP] = '\0';
}

static char* get_request_line( http_request *reqP ) { 
    int begin;
    char c;

    char *bufP = reqP->buf;
    int buf_len = reqP->buf_len;

    for ( begin = reqP->buf_idx ; reqP->buf_idx < buf_len; ++reqP->buf_idx ) {
        c = bufP[ reqP->buf_idx ];
        if ( c == '\012' || c == '\015' ) {
            bufP[reqP->buf_idx] = '\0';
            ++reqP->buf_idx;
            if ( c == '\015' && reqP->buf_idx < buf_len && 
                bufP[reqP->buf_idx] == '\012' ) {
                bufP[reqP->buf_idx] = '\0';
                ++reqP->buf_idx;
            }
            return &(bufP[begin]);
        }
    }
    fprintf( stderr, "http request format error\n" );
    exit( 1 );
}



static void init_http_server( http_server *svrP, unsigned short port ) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname( svrP->hostname, sizeof( svrP->hostname) );
    svrP->port = port;
   
    svrP->listen_fd = socket( AF_INET, SOCK_STREAM, 0 );
    if ( svrP->listen_fd < 0 ) ERR_EXIT( "socket" )

    bzero( &servaddr, sizeof(servaddr) );
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl( INADDR_ANY );
    servaddr.sin_port = htons( port );
    tmp = 1;
    if ( setsockopt( svrP->listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*) &tmp, sizeof(tmp) ) < 0 ) 
        ERR_EXIT ( "setsockopt " )
    if ( bind( svrP->listen_fd, (struct sockaddr *) &servaddr, sizeof(servaddr) ) < 0 ) ERR_EXIT( "bind" )

    if ( listen( svrP->listen_fd, 1024 ) < 0 ) ERR_EXIT( "listen" )
}

// Set NDELAY mode on a socket.
static void set_ndelay( int fd ) {
    int flags, newflags;

    flags = fcntl( fd, F_GETFL, 0 );
    if ( flags != -1 ) {
        newflags = flags | (int) O_NDELAY; // nonblocking mode
        if ( newflags != flags )
            (void) fcntl( fd, F_SETFL, newflags );
    }
}   

static void strdecode( char* to, char* from ) {
    for ( ; *from != '\0'; ++to, ++from ) {
        if ( from[0] == '%' && isxdigit( from[1] ) && isxdigit( from[2] ) ) {
            *to = hexit( from[1] ) * 16 + hexit( from[2] );
            from += 2;
        } else {
            *to = *from;
        }
    }
    *to = '\0';
}


static int hexit( char c ) {
    if ( c >= '0' && c <= '9' )
        return c - '0';
    if ( c >= 'a' && c <= 'f' )
        return c - 'a' + 10;
    if ( c >= 'A' && c <= 'F' )
        return c - 'A' + 10;
    return 0;           // shouldn't happen
}


static void* e_malloc( size_t size ) {
    void* ptr;

    ptr = malloc( size );
    if ( ptr == (void*) 0 ) {
        (void) fprintf( stderr, "out of memory\n" );
        exit( 1 );
    }
    return ptr;
}


static void* e_realloc( void* optr, size_t size ) {
    void* ptr;

    ptr = realloc( optr, size );
    if ( ptr == (void*) 0 ) {
        (void) fprintf( stderr, "out of memory\n" );
        exit( 1 );
    }
    return ptr;
}
