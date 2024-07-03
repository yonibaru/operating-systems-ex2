#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>
#include <signal.h>
#include <sys/un.h>

#define BUFFER_SIZE 1024

void handle_alarm(int sig){
    printf("Timeout. Closing UDP server...\n");
    exit(EXIT_SUCCESS);
}

void extractTypeAndPath(const char *input, char *type, char *path) {
    strncpy(type, input, 5);
    type[5] = '\0'; // Ensure type string is null-terminated
    strcpy(path, input + 5);
}

void extractTypeAndPort(const char *input, char *type, int *port) {
    strncpy(type, input, 4);
    type[4] = '\0'; // Ensure type string is null-terminated
    *port = atoi(input + 4);
}

//Server functions
void create_tcp_server(int e_param, char mode_param,int portno,int parent_to_child_pipe,int child_to_parent_pipe,int frwrd_fd,char *frwrd_type,struct sockaddr_in *udp_frwrd_addr,struct sockaddr_un *uds_frwrd_addr){
    if(mode_param == 'o' && frwrd_fd == -1){
        printf("Error setting up client.\n");
        exit(EXIT_FAILURE);
    }
    // Preparing server
    int server_fd,client_fd,clilen,max_fd;
    char socket_buffer[BUFFER_SIZE];
    char process_buffer[BUFFER_SIZE];
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t udp_frwrd_addr_len = sizeof(*udp_frwrd_addr);
    socklen_t uds_frwrd_addr_len = sizeof(*uds_frwrd_addr);

    ssize_t bytes_read;
    int n;
    int reuseaddr = 1;
    fd_set readfds;

    // Setting up socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(1);
    }

    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(int)) < 0) {
        perror("setsockopt");
        exit(1);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    // Binding to a socket
    if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        exit(1);
    }

    // Listening to the socket
    listen(server_fd, 5);
    printf("TCP Server started...\n");
    clilen = sizeof(cli_addr);


    // Accept a single incoming connection.
    client_fd = accept(server_fd, (struct sockaddr *) &cli_addr, &clilen);
    if (client_fd < 0) {
        perror("accept");
        exit(1);
    }

    printf("Connection with client established. Awaiting input...\n");
    // Server loop
    while(1){

        FD_ZERO(&readfds); //Clear fds to prepare for reading
        FD_SET(STDIN_FILENO, &readfds); //Add STDIN to the set
        FD_SET(client_fd, &readfds); // Add client fd to the set 
        max_fd = client_fd > STDIN_FILENO ? client_fd : STDIN_FILENO; // Determine the maximum of fds (select needs to know this?)

        // select waits for any fd activity  
        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0 && (errno != EINTR)) {
            perror("Error on select");
            close(server_fd);
            close(client_fd);
            if(mode_param == 'o'){
                close(frwrd_fd);
            }
            exit(EXIT_FAILURE);
        }

        
        if(!e_param){ // Do if the parameter -e is missing:
            // Check if something happened on stdin
            if (FD_ISSET(STDIN_FILENO, &readfds)) {
                memset(socket_buffer, 0, BUFFER_SIZE);
                if (read(STDIN_FILENO, socket_buffer, BUFFER_SIZE) > 0) {
                    write(parent_to_child_pipe, socket_buffer, strlen(socket_buffer)); // Send stdin input to child
                    memset(process_buffer, 0, BUFFER_SIZE); 
                    bytes_read = read(child_to_parent_pipe, process_buffer, sizeof(process_buffer) - 1);
                    if (bytes_read > 0) {
                        process_buffer[bytes_read] = '\0'; // Null-terminate the string
                        printf("Child process responding to STDIN: %s\n",process_buffer); //Output goes to STDOUT
                    }
                    // Reset buffer
                    memset(process_buffer, 0, BUFFER_SIZE); 
                }
            }
        }
    
        // Check if something happened on client_fd
        if(FD_ISSET(client_fd, &readfds)){
            memset(socket_buffer, 0, BUFFER_SIZE);
            // Continuously read each chunk of data from socket INTO the buffer
            if((n = read(client_fd, socket_buffer, BUFFER_SIZE)) > 0) {
                if (strncmp(socket_buffer, "close", 5) == 0) {
                    if(mode_param == 'o'){
                        close(frwrd_fd);
                    }
                    close(client_fd);
                    close(server_fd);
                    close(parent_to_child_pipe);
                    close(child_to_parent_pipe);
                    printf("Closing server.\n");
                    exit(EXIT_SUCCESS);
                } else {
                    write(parent_to_child_pipe, socket_buffer, n); // We have the input in the buffer and we need to feed it to the child process. This is the desired behavior across all modes.
                    memset(process_buffer, 0, BUFFER_SIZE); 
                    bytes_read = read(child_to_parent_pipe, process_buffer, sizeof(process_buffer) - 1);
                    if (bytes_read > 0) {
                        process_buffer[bytes_read] = '\0'; // Null-terminate the string
                        if(mode_param == 'b'){
                            //Output goes back to the client
                            write(client_fd, process_buffer, sizeof(process_buffer) - 1); // Send response back to client
                        }else if(mode_param == 'i'){
                            //Output goes to STDOUT
                            printf("Child process responds to TCP Client: %s\n",process_buffer);
                        }else if(mode_param == 'o'){
                            //Output goes to ip add argument
                            if(strcmp(frwrd_type,"TCPC") == 0){
                                send(frwrd_fd,process_buffer,sizeof(process_buffer) - 1,0);
                            }else if(strcmp(frwrd_type,"UDPC") == 0){
                                sendto(frwrd_fd, process_buffer, sizeof(process_buffer) -1, 0, (struct sockaddr *)udp_frwrd_addr, udp_frwrd_addr_len);
                            }else if(strcmp(frwrd_type,"UDSCD") == 0){
                                sendto(frwrd_fd, process_buffer, sizeof(process_buffer) -1, 0, (struct sockaddr *)uds_frwrd_addr, uds_frwrd_addr_len);
                            }else if(strcmp(frwrd_type,"UDSCS") == 0){
                                send(frwrd_fd,process_buffer,strlen(process_buffer),0);
                            }
                        }
                    }
                    // Reset buffer
                    memset(socket_buffer, 0, BUFFER_SIZE); 
                    memset(process_buffer, 0, BUFFER_SIZE);

                }
            }
        }
    }
    // Disallow any new incoming connections.
    close(client_fd);
    close(parent_to_child_pipe);
    close(child_to_parent_pipe);
    close(server_fd);
    if(mode_param == 'o'){
        close(frwrd_fd);
    }
}

void create_udp_server(int e_param, char mode_param, int portno, int parent_to_child_pipe,int child_to_parent_pipe,int frwrd_fd,char *frwrd_type,struct sockaddr_in *udp_frwrd_addr,int timeout_count,struct sockaddr_un *uds_frwrd_addr){
    if(mode_param == 'o' && frwrd_fd == -1){
        printf("Error setting up client.\n");
        exit(EXIT_FAILURE);
    }
    int sock_fd,max_fd;
    struct sockaddr_in serv_addr,cli_addr;
    socklen_t cli_addr_len = sizeof(cli_addr);
    socklen_t udp_frwrd_addr_len = sizeof(*udp_frwrd_addr);
    char socket_buffer[BUFFER_SIZE];
    char process_buffer[BUFFER_SIZE];
    fd_set readfds;
    ssize_t bytes_read,recv_len;
    socklen_t uds_frwrd_addr_len = sizeof(*uds_frwrd_addr);

    sock_fd = socket(AF_INET,SOCK_DGRAM,0);
    if(sock_fd < 0){
        perror("socket");
        exit(EXIT_FAILURE);
    }
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(portno);
    if (bind(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Error in binding");
        exit(1);
    }
    printf("UDP Server started...\n");
    if(timeout_count != 0){
        signal(SIGALRM, handle_alarm);
        alarm(timeout_count);
    }

    while(1){
        FD_ZERO(&readfds); //Clear fds to prepare for reading
        FD_SET(STDIN_FILENO, &readfds); //Add STDIN to the set
        FD_SET(sock_fd, &readfds); // Add client fd to the set 
        max_fd = sock_fd > STDIN_FILENO ? (sock_fd + 1) : (STDIN_FILENO + 1); // Determine the maximum of fds (select needs to know this?)

        // select() waits for any fd activity  
        if (select(max_fd, &readfds, NULL, NULL, NULL) < 0 && (errno != EINTR)) {
            perror("Error on select");
            close(sock_fd);
            if(mode_param == 'o'){
                close(frwrd_fd);
            }
            exit(EXIT_FAILURE);
        }

        
        if(!e_param){ // Do if the parameter -e is missing:
            // Check if something happened on stdin
            if (FD_ISSET(STDIN_FILENO, &readfds)) {
                memset(socket_buffer, 0, BUFFER_SIZE);
                if (read(STDIN_FILENO, socket_buffer, BUFFER_SIZE) > 0) {
                    write(parent_to_child_pipe, socket_buffer, strlen(socket_buffer)); // Send stdin input to child
                    memset(process_buffer, 0, BUFFER_SIZE); 
                    bytes_read = read(child_to_parent_pipe, process_buffer, sizeof(process_buffer) - 1);
                    if (bytes_read > 0) {
                        process_buffer[bytes_read] = '\0'; // Null-terminate the string
                        printf("Child process responding to STDIN: %s\n",process_buffer); //Output goes to STDOUT
                    }
                    // Reset buffer
                    memset(process_buffer, 0, BUFFER_SIZE); 
                }
            }
        }
    
        // Check if something happened on client_fd
        if(FD_ISSET(sock_fd, &readfds)){
            memset(socket_buffer, 0, BUFFER_SIZE);
            // Continuously read each chunk of data from socket INTO the buffer
            if ((recv_len = recvfrom(sock_fd, socket_buffer, sizeof(socket_buffer), 0,(struct sockaddr *)&cli_addr, &cli_addr_len)) < 0) {
                perror("Error in recvfrom");
                exit(1);
            }
            if (strncmp(socket_buffer, "close", 5) == 0) {
                    if(mode_param == 'o'){
                        close(frwrd_fd);
                    }
                    close(sock_fd);
                    close(parent_to_child_pipe);
                    close(child_to_parent_pipe);
                    printf("Closing server.\n");
                    exit(EXIT_SUCCESS);
            } else{
                write(parent_to_child_pipe, socket_buffer, strlen(socket_buffer)); // We have the input in the buffer and we need to feed it to the child process. This is the desired behavior across all modes.
                memset(process_buffer, 0, BUFFER_SIZE); 
                bytes_read = read(child_to_parent_pipe, process_buffer, sizeof(process_buffer) - 1);
                if (bytes_read > 0) {
                    process_buffer[bytes_read] = '\0'; // Null-terminate the string
                    if(mode_param == 'b'){
                        //Output goes back to the client
                        sendto(sock_fd, process_buffer, sizeof(process_buffer) - 1,0,(struct sockaddr *)&cli_addr, cli_addr_len); // Send response back to client
                    }else if(mode_param == 'i'){
                        //Output goes to STDOUT
                        printf("Child process responds to UDP Client: %s\n",process_buffer);
                    }else if(mode_param == 'o'){
                        //Output goes to ip add argument
                          if(strcmp(frwrd_type,"TCPC") == 0){
                                send(frwrd_fd,process_buffer,sizeof(process_buffer) - 1,0);
                            }else if(strcmp(frwrd_type,"UDPC") == 0){
                                sendto(frwrd_fd, process_buffer, sizeof(process_buffer) -1, 0, (struct sockaddr *)udp_frwrd_addr, udp_frwrd_addr_len);
                            }else if(strcmp(frwrd_type,"UDSCD") == 0){
                                sendto(frwrd_fd, process_buffer, sizeof(process_buffer) -1, 0, (struct sockaddr *)uds_frwrd_addr, uds_frwrd_addr_len);
                            }else if(strcmp(frwrd_type,"UDSCS") == 0){
                                send(frwrd_fd,process_buffer,strlen(process_buffer),0);
                            }
                    }
                }
                // Reset buffer
                memset(socket_buffer, 0, BUFFER_SIZE); 
                memset(process_buffer, 0, BUFFER_SIZE);

            }
        }

    }
    close(sock_fd);
    if(mode_param == 'o'){
        close(frwrd_fd);
    }
}

//UDSSD
void create_uds_datagram_server(int e_param, char mode_param,int parent_to_child_pipe,int child_to_parent_pipe,int frwrd_fd,char *frwrd_type,struct sockaddr_in *udp_frwrd_addr,char *socket_path,struct sockaddr_un *uds_frwrd_addr){
    if((mode_param == 'o' && frwrd_fd == -1) || mode_param == 'b'){
        printf("Error setting up client.\n");
        exit(EXIT_FAILURE);
    }
    int sock_fd,max_fd;
    struct sockaddr_un server_addr, client_addr;
    char socket_buffer[BUFFER_SIZE];
    char process_buffer[BUFFER_SIZE];
    socklen_t client_addr_len = sizeof(struct sockaddr_un);
    socklen_t udp_frwrd_addr_len = sizeof(*udp_frwrd_addr);
    fd_set readfds;
    ssize_t bytes_read,recv_len;
    socklen_t uds_frwrd_addr_len = sizeof(*uds_frwrd_addr);

    if ((sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }


    memset(&server_addr, 0, sizeof(struct sockaddr_un));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, socket_path, sizeof(server_addr.sun_path) - 1);


    // Bind socket to the path
    if (bind(sock_fd, (struct sockaddr *) &server_addr, sizeof(struct sockaddr_un)) == -1) {
        perror("bind");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }
    printf("Started a UNIX Domain socket datagram server at given path...\n");

    while(1){
        FD_ZERO(&readfds); //Clear fds to prepare for reading
        FD_SET(STDIN_FILENO, &readfds); //Add STDIN to the set
        FD_SET(sock_fd, &readfds); // Add client fd to the set 
        max_fd = sock_fd > STDIN_FILENO ? (sock_fd + 1) : (STDIN_FILENO + 1); // Determine the maximum of fds (select needs to know this?)

        // select() waits for any fd activity  
        if (select(max_fd, &readfds, NULL, NULL, NULL) < 0 && (errno != EINTR)) {
            perror("Error on select");
            close(sock_fd);
            if(mode_param == 'o'){
                close(frwrd_fd);
            }
            exit(EXIT_FAILURE);
        }

        
        if(!e_param){ // Do if the parameter -e is missing:
            // Check if something happened on stdin
            if (FD_ISSET(STDIN_FILENO, &readfds)) {
                memset(socket_buffer, 0, BUFFER_SIZE);
                if (read(STDIN_FILENO, socket_buffer, BUFFER_SIZE) > 0) {
                    if (strncmp(socket_buffer, "close", 5) == 0) {
                        if(mode_param == 'o'){
                            close(frwrd_fd);
                        }
                        close(sock_fd);
                        unlink(socket_path);
                        close(parent_to_child_pipe);
                        close(child_to_parent_pipe);
                        printf("Closing server.\n");
                        exit(EXIT_SUCCESS);
                    }
                    write(parent_to_child_pipe, socket_buffer, strlen(socket_buffer)); // Send stdin input to child
                    memset(process_buffer, 0, BUFFER_SIZE); 
                    bytes_read = read(child_to_parent_pipe, process_buffer, sizeof(process_buffer) - 1);
                    if (bytes_read > 0) {
                        process_buffer[bytes_read] = '\0'; // Null-terminate the string
                        printf("Child process responding to STDIN: %s\n",process_buffer); //Output goes to STDOUT
                    }
                    // Reset buffer
                    memset(process_buffer, 0, BUFFER_SIZE); 
                }
            }
        }
    
        // Check if something happened on client_fd
        if(FD_ISSET(sock_fd, &readfds)){
            memset(socket_buffer, 0, BUFFER_SIZE);
            // Continuously read each chunk of data from socket INTO the buffer
            if ((recv_len = recvfrom(sock_fd, socket_buffer, BUFFER_SIZE, 0,(struct sockaddr *) &client_addr, &client_addr_len)) == -1) {
                perror("Error in recvfrom");
                exit(1);
            }
            if (strncmp(socket_buffer, "close", 5) == 0) {
                    if(mode_param == 'o'){
                        close(frwrd_fd);
                    }
                    close(sock_fd);
                    unlink(socket_path);
                    close(parent_to_child_pipe);
                    close(child_to_parent_pipe);
                    printf("Closing server.\n");
                    exit(EXIT_SUCCESS);
            } else{
                socket_buffer[recv_len] = '\0';
                write(parent_to_child_pipe, socket_buffer, strlen(socket_buffer)); // We have the input in the buffer and we need to feed it to the child process. This is the desired behavior across all modes.
                memset(process_buffer, 0, BUFFER_SIZE); 
                bytes_read = read(child_to_parent_pipe, process_buffer, sizeof(process_buffer) - 1);
                if (bytes_read > 0) {
                    process_buffer[bytes_read] = '\0'; // Null-terminate the string
                    //No mode b in this..
                    if(mode_param == 'i'){
                        //Output goes to STDOUT
                        printf("Child process responds to UDP Client: %s\n",process_buffer);
                    }else if(mode_param == 'o'){
                        //Output goes to ip add argument
                        if(strcmp(frwrd_type,"TCPC") == 0){
                            send(frwrd_fd,process_buffer,sizeof(process_buffer) - 1,0);
                        }else if(strcmp(frwrd_type,"UDPC") == 0){
                            sendto(frwrd_fd, process_buffer, sizeof(process_buffer) -1, 0, (struct sockaddr *)udp_frwrd_addr, udp_frwrd_addr_len);
                        }else if(strcmp(frwrd_type,"UDSCD") == 0){
                            sendto(frwrd_fd, process_buffer, sizeof(process_buffer) -1, 0, (struct sockaddr *)uds_frwrd_addr, uds_frwrd_addr_len);
                        }else if(strcmp(frwrd_type,"UDSCS") == 0){
                            send(frwrd_fd,process_buffer,strlen(process_buffer),0);
                        }
                    }
                }
                // Reset buffer
                memset(socket_buffer, 0, BUFFER_SIZE); 
                memset(process_buffer, 0, BUFFER_SIZE);

            }
        }

    }

    if(mode_param == 'o'){
        close(frwrd_fd);
    }
    close(sock_fd);
    unlink(socket_path);
}

//UDSSS
void create_uds_stream_server(int e_param, char mode_param,int parent_to_child_pipe,int child_to_parent_pipe,int frwrd_fd,char *frwrd_type,struct sockaddr_in *udp_frwrd_addr,char *socket_path,struct sockaddr_un *uds_frwrd_addr){
    if((mode_param == 'o' && frwrd_fd == -1)){
        printf("Error setting up client.\n");
        exit(EXIT_FAILURE);
    }
    int sock_fd,client_fd,max_fd;
    struct sockaddr_un server_addr, client_addr;
    char socket_buffer[BUFFER_SIZE];
    char process_buffer[BUFFER_SIZE];
    socklen_t client_addr_len = sizeof(struct sockaddr_un);
    socklen_t udp_frwrd_addr_len = sizeof(*udp_frwrd_addr);
    fd_set readfds;
    ssize_t bytes_read,recv_len;
    socklen_t uds_frwrd_addr_len = sizeof(*uds_frwrd_addr);

    if ((sock_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }


    memset(&server_addr, 0, sizeof(struct sockaddr_un));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, socket_path, sizeof(server_addr.sun_path) - 1);


    // Bind socket to the path
    if (bind(sock_fd, (struct sockaddr *) &server_addr, sizeof(struct sockaddr_un)) == -1) {
        perror("bind");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }
    printf("Started a UNIX Domain socket stream server at given path...\n");
    if(listen(sock_fd,SOMAXCONN) == -1){
        perror("listen");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    if ((client_fd = accept(sock_fd, (struct sockaddr *)&client_addr, &client_addr_len)) == -1) {
        perror("accept");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }
    
    while(1){
        FD_ZERO(&readfds); //Clear fds to prepare for reading
        FD_SET(STDIN_FILENO, &readfds); //Add STDIN to the set
        FD_SET(client_fd, &readfds); // Add client fd to the set 
        max_fd = client_fd > STDIN_FILENO ? (client_fd + 1) : (STDIN_FILENO + 1); // Determine the maximum of fds (select needs to know this?)

        // select() waits for any fd activity  
        if (select(max_fd, &readfds, NULL, NULL, NULL) < 0 && (errno != EINTR)) {
            perror("Error on select");
            close(sock_fd);
            if(mode_param == 'o'){
                close(frwrd_fd);
            }
            exit(EXIT_FAILURE);
        }

        
        if(!e_param){ // Do if the parameter -e is missing:
            // Check if something happened on stdin
            if (FD_ISSET(STDIN_FILENO, &readfds)) {
                memset(socket_buffer, 0, BUFFER_SIZE);
                if (read(STDIN_FILENO, socket_buffer, BUFFER_SIZE) > 0) {
                    if (strncmp(socket_buffer, "close", 5) == 0) {
                        if(mode_param == 'o'){
                            close(frwrd_fd);
                        }
                        close(sock_fd);
                        unlink(socket_path);
                        close(parent_to_child_pipe);
                        close(child_to_parent_pipe);
                        printf("Closing server.\n");
                        exit(EXIT_SUCCESS);
                    }
                    write(parent_to_child_pipe, socket_buffer, strlen(socket_buffer)); // Send stdin input to child
                    memset(process_buffer, 0, BUFFER_SIZE); 
                    bytes_read = read(child_to_parent_pipe, process_buffer, sizeof(process_buffer) - 1);
                    if (bytes_read > 0) {
                        process_buffer[bytes_read] = '\0'; // Null-terminate the string
                        printf("Child process responding to STDIN: %s\n",process_buffer); //Output goes to STDOUT
                    }
                    // Reset buffer
                    memset(process_buffer, 0, BUFFER_SIZE); 
                }
            }
        }
    
        // Check if something happened on client_fd
        if(FD_ISSET(client_fd, &readfds)){
            memset(socket_buffer, 0, BUFFER_SIZE);
            // Continuously read each chunk of data from socket INTO the buffer
            if((recv_len = recv(client_fd, socket_buffer, BUFFER_SIZE, 0)) == -1) {
                perror("recv");
                close(client_fd);
                close(sock_fd);
                unlink(socket_path);
                exit(EXIT_SUCCESS);
            }
            if (strncmp(socket_buffer, "close", 5) == 0) {
                    if(mode_param == 'o'){
                        close(frwrd_fd);
                    }
                    close(sock_fd);
                    unlink(socket_path);
                    close(parent_to_child_pipe);
                    close(child_to_parent_pipe);
                    printf("Closing server.\n");
                    exit(EXIT_SUCCESS);
            } else{
                socket_buffer[recv_len] = '\0';
                write(parent_to_child_pipe, socket_buffer, strlen(socket_buffer)); // We have the input in the buffer and we need to feed it to the child process. This is the desired behavior across all modes.
                memset(process_buffer, 0, BUFFER_SIZE); 
                bytes_read = read(child_to_parent_pipe, process_buffer, sizeof(process_buffer) - 1);
                if (bytes_read > 0) {
                    process_buffer[bytes_read] = '\0'; // Null-terminate the string
                    //No mode b in this..
                    if(mode_param == 'b'){
                        send(client_fd, process_buffer, strlen(process_buffer), 0);
                    }else if(mode_param == 'i'){
                        //Output goes to STDOUT
                        printf("Child process responds to UDP Client: %s\n",process_buffer);
                    }else if(mode_param == 'o'){
                        //Output goes to ip add argument
                        //If frwrd client is TCP...
                        if(strcmp(frwrd_type,"TCPC") == 0){
                            send(frwrd_fd,process_buffer,sizeof(process_buffer) - 1,0);
                        }else if(strcmp(frwrd_type,"UDPC") == 0){
                            sendto(frwrd_fd, process_buffer, sizeof(process_buffer) -1, 0, (struct sockaddr *)udp_frwrd_addr, udp_frwrd_addr_len);
                        }else if(strcmp(frwrd_type,"UDSCD") == 0){
                            sendto(frwrd_fd, process_buffer, sizeof(process_buffer) -1, 0, (struct sockaddr *)uds_frwrd_addr, uds_frwrd_addr_len);
                        }else if(strcmp(frwrd_type,"UDSCS") == 0){
                            send(frwrd_fd,process_buffer,strlen(process_buffer),0);
                        }
                    }
                }
                // Reset buffer
                memset(socket_buffer, 0, BUFFER_SIZE); 
                memset(process_buffer, 0, BUFFER_SIZE);

            }
        }

    }

    if(mode_param == 'o'){
        close(frwrd_fd);
    }
    close(sock_fd);
    unlink(socket_path);
}

//Clients
int create_tcp_client(int frwrd_port, char *frwrd_ip){
    int frwrd_fd;
    struct sockaddr_in frwrd_addr;
    // Create socket
    if ((frwrd_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        exit(EXIT_FAILURE);
    }

    frwrd_addr.sin_family = AF_INET;
    frwrd_addr.sin_port = htons(frwrd_port);

    if (strcmp(frwrd_ip, "localhost") == 0) {
        strcpy(frwrd_ip, "127.0.0.1");
    }

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, frwrd_ip, &frwrd_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        exit(EXIT_FAILURE);
    }

    // Connect to the server
    if (connect(frwrd_fd, (struct sockaddr *)&frwrd_addr, sizeof(frwrd_addr)) < 0) {
        perror("Connection to external server failed");
        exit(EXIT_FAILURE);
    }

    return frwrd_fd;
}

int create_udp_client(int frwrd_port,char *frwrd_ip,struct sockaddr_in *server_addr){
   int sock_fd;

    // Create socket
    if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set up server address
    memset(server_addr, 0, sizeof(struct sockaddr_in));
    server_addr->sin_family = AF_INET;
    server_addr->sin_port = htons(frwrd_port); 
    if (strcmp(frwrd_ip, "localhost") == 0) {
        strcpy(frwrd_ip, "127.0.0.1");
    }

    if (inet_pton(AF_INET, frwrd_ip, &server_addr->sin_addr) <= 0) {
        perror("Invalid address or address not supported");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }
    return sock_fd;
}

int create_uds_datagram_client(char *socket_path,struct sockaddr_un *server_addr){
    int sock_fd;
    sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock_fd == -1) {
        perror("socket error");
        return -1;
    }
    memset(server_addr, 0, sizeof(struct sockaddr_un));

    server_addr->sun_family = AF_UNIX;
    strncpy(server_addr->sun_path, socket_path, sizeof(server_addr->sun_path) - 1);

    return sock_fd;
}

//UDSCS
int create_uds_stream_client(char *socket_path){
    int sockfd;
    struct sockaddr_un addr;
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket error");
        return -1;
    }

    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(struct sockaddr_un)) == -1) {
        perror("connect error");
        close(sockfd);
        return -1;
    }
    return sockfd;
}


int main(int argc, char *argv[]){
    char frwrd_type[6];
    char frwrd_ip[50];
    int frwrd_port;
    char server_type[6];
    int server_port;
    char uds_path[100];

    char mode;
    int e_mode = 0;
    int opt;
    char *child_params = NULL;
    char *i_string = NULL;
    char *o_string = NULL;
    char *b_string = NULL;
    char *t_string = NULL;
    int t_count;
    // Parameters:
    // -e : Do not allow input from STDIN, input is only from the client.
    // -i : Output is forwarded to STDOUT.
    // -b : Output is forwarded to connect client.
    // -o : Output is forwarded to another server, given in the parameters.
    if (argc > 1 && strcmp(argv[1], "-e") != 0) {
        child_params = argv[1]; // Capture the first argument as the exec string
    }

    while ((opt = getopt(argc, argv, "e:i:o:b:t:")) != -1) {
        switch (opt) {
            case 'e':
                child_params = optarg;
                e_mode = 1;
                break;
            case 'i':
                i_string = optarg;
                break;
            case 'o':
                o_string = optarg;
                break;
            case 'b':
                b_string = optarg;
                break;
            case 't':
                t_string = optarg;
                t_count = atoi(t_string);
                break;
            default:
                fprintf(stderr, "Usage: %s -e <child_params> [-i <TYPE><PORT> [-o <TYPE><HOST>,<PORT>]] [-b <TYPE><PORT>]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
 
    //Check for weird combination of parameters.
    if(o_string != NULL && i_string == NULL){
        printf("Incorrect combination of parameters.\n");
        exit(EXIT_FAILURE);
    }
    if(b_string != NULL && (i_string != NULL || o_string != NULL)){
        printf("Incorrect combination of parameters.\n");
        exit(EXIT_FAILURE);
    }
    
    //Assigning the correct modes
    if(b_string != NULL){
        mode = 'b';
        //Supported Servers
        if(strncmp(b_string,"UDPS",4) == 0){
            strcpy(server_type,"UDPS");
            sscanf(b_string, "%4d", &server_port);
        }else if(strncmp(b_string,"TCPS",4) == 0){
            strcpy(server_type,"TCPS");
            sscanf(b_string, "%4d", &server_port);
        }else if(strncmp(b_string,"UDSSD",5) == 0){
            exit(EXIT_FAILURE); //UDSSD Doesn't support -b
        }else if(strncmp(b_string,"UDSSS",5) == 0){
            strcpy(server_type,"UDSSS");
            sscanf(b_string, "UDSSS%s", uds_path);
        }

    } else if (i_string != NULL && o_string == NULL){
        mode = 'i';
        //Supported Servers
        if(strncmp(i_string,"UDPS",4) == 0){
            strcpy(server_type,"UDPS");
            sscanf(i_string, "UDPS%d", &server_port);
        }else if(strncmp(i_string,"TCPS",4) == 0){
            strcpy(server_type,"TCPS");
            sscanf(i_string, "TCPS%d", &server_port);
        }else if(strncmp(i_string,"UDSSD",5) == 0){
            strcpy(server_type,"UDSSD");
            sscanf(i_string, "UDSSD%s", uds_path);
        }else if(strncmp(i_string,"UDSSS",5) == 0){
            strcpy(server_type,"UDSSS");
            sscanf(i_string, "UDSSS%s", uds_path); 
        }

    } else if (i_string != NULL && o_string != NULL){
        mode = 'o';
        //Supported Servers
        if(strncmp(i_string,"UDPS",4) == 0){
            strcpy(server_type,"UDPS");
            sscanf(i_string, "UDPS%d", &server_port);
        }else if(strncmp(i_string,"TCPS",4) == 0){
            strcpy(server_type,"TCPS");
            sscanf(i_string, "TCPS%d", &server_port);
        }else if(strncmp(i_string,"UDSSD",5) == 0){
            strcpy(server_type,"UDSSD");
            sscanf(i_string, "UDSSD%s", uds_path);
        }else if(strncmp(i_string,"UDSSS",5) == 0){
            strcpy(server_type,"UDSSS");
            sscanf(i_string, "UDSSS%s", uds_path);
        }

        //Supported forward clients
        if(strncmp(o_string,"UDPC",4) == 0){
            sscanf(o_string, "%4s%[^,],%d", frwrd_type, frwrd_ip, &frwrd_port);
        }else if(strncmp(o_string,"TCPC",4) == 0){
            sscanf(o_string, "%4s%[^,],%d", frwrd_type, frwrd_ip, &frwrd_port);
        }else if(strncmp(o_string,"UDSCD",5) == 0){
            strcpy(frwrd_type,"UDSCD");
            sscanf(o_string, "UDSCD%s", uds_path);
        }else if(strncmp(o_string,"UDSCS",5) == 0){
            strcpy(frwrd_type,"UDSCS");
            sscanf(o_string, "UDSCS%s", uds_path);
        }

    }

    int parent_to_child_pipe[2];
    int child_to_parent_pipe[2];

    if(pipe(parent_to_child_pipe) == -1 || pipe(child_to_parent_pipe) == -1){
        perror("pipe");
        exit(EXIT_FAILURE);
    }
 
    // Fork to create a process of tictactoe.
    pid_t pid = fork();

    if(pid == -1){
        perror("fork");
        exit(EXIT_FAILURE);
    }else if(pid > 0){ // Parent process
        close(parent_to_child_pipe[0]); // Close the read end of the parent-to-child pipe
        close(child_to_parent_pipe[1]); // Close the write end of the child-to-parent pipe
        
        int frwrd_fd = -1;
        struct sockaddr_in frwrd_addr; //for udp client
        struct sockaddr_un uds_frwrd_addr;
        //Start client
        if(mode == 'o'){
            if(strcmp(frwrd_type,"UDPC") == 0){
                frwrd_fd = create_udp_client(frwrd_port,frwrd_ip,&frwrd_addr);
            }else if(strcmp(frwrd_type,"TCPC") == 0){
                frwrd_fd = create_tcp_client(frwrd_port,frwrd_ip);
            }else if(strcmp(frwrd_type,"UDSCD") == 0){
                frwrd_fd = create_uds_datagram_client(uds_path,&uds_frwrd_addr);
            }else if(strcmp(frwrd_type,"UDSCS") == 0){
                frwrd_fd = create_uds_stream_client(uds_path);
            }else{
                printf("Incorrect server type.\n");
                exit(EXIT_FAILURE);
            }
        }
        // Start Server
        printf("SERVER_TYPE: %s\n",server_type);
        if(strcmp(server_type,"UDPS") == 0){
            create_udp_server(e_mode,mode,server_port,parent_to_child_pipe[1],child_to_parent_pipe[0],frwrd_fd,frwrd_type,&frwrd_addr,t_count,&uds_frwrd_addr);
        }else if(strcmp(server_type,"TCPS") == 0){
            create_tcp_server(e_mode,mode,server_port,parent_to_child_pipe[1],child_to_parent_pipe[0],frwrd_fd,frwrd_type,&frwrd_addr,&uds_frwrd_addr);
        }else if(strcmp(server_type,"UDSSD") == 0){
            printf("PATH ARGUMENT: %s\n",uds_path);
            create_uds_datagram_server(e_mode,mode,parent_to_child_pipe[1],child_to_parent_pipe[0],frwrd_fd,frwrd_type,&frwrd_addr,uds_path,&uds_frwrd_addr);
        }else if(strcmp(server_type,"UDSSS") == 0){
            create_uds_stream_server(e_mode,mode,parent_to_child_pipe[1],child_to_parent_pipe[0],frwrd_fd,frwrd_type,&frwrd_addr,uds_path,&uds_frwrd_addr);
        }else{
            printf("Incorrect server type.\n");
            exit(EXIT_FAILURE);
        }
        return 0;

    }else{ // Child process
        close(parent_to_child_pipe[1]); // Close the write end of the parent-to-child pipe
        close(child_to_parent_pipe[0]); // Close the read end of the child-to-parent pipe
        char *program = strtok(child_params, " ");
        char execute_program[256] = "./";
        strcat(execute_program, program);
        char *argument = strtok(NULL, " ");
        if (program == NULL || argument == NULL) {
            fprintf(stderr, "Error: Invalid command format.\n");
            exit(1);
        }
        if (dup2(child_to_parent_pipe[1], STDERR_FILENO) == -1) {
            perror("dup2 stdout");
            exit(EXIT_FAILURE);
        }
        if (dup2(parent_to_child_pipe[0], STDIN_FILENO) == -1) {
            perror("dup2");
            exit(EXIT_FAILURE);
        }
        close(child_to_parent_pipe[1]);
        close(parent_to_child_pipe[0]);

    
        execlp(execute_program, program, argument, (char *)NULL);
        perror("execlp error");
        exit(1);
    }
 
}