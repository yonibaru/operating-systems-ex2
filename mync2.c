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

#define BUFFER_SIZE 1024

int getPortAsInt(const char *arg) {
    const char *prefix = "TCPS";
    size_t prefix_len = strlen(prefix);
    const char *port_str = arg + prefix_len;
    int port = atoi(port_str);  
    return port;
}

char* getForwardInfo(const char *arg) {
    char *prefix = "TCPC";
    size_t prefix_len = strlen(prefix);
    char *port_str = arg + prefix_len;
    return port_str;
}

int main(int argc, char *argv[]){
    char mode;
    char* strForwardInfo;
    char frwrd_ip[50];
    int frwrd_port;
    int e_mode;

    // Ensure correct number of arguments are provided
    if(argc == 5 && strcmp(argv[1], "-e") == 0 && strcmp(argv[3], "-b") == 0) {
        mode = 'b';
        e_mode = 1;
    } else if(argc == 5 && strcmp(argv[1], "-e") == 0 && strcmp(argv[3], "-i") == 0) {
        mode = 'i';
        e_mode = 1;
    } else if(argc == 7 && strcmp(argv[1],"-e") == 0 && strcmp(argv[3],"-i") == 0 && strcmp(argv[5],"-o") == 0) {
        mode = 'o';
        e_mode = 1;
        strForwardInfo = getForwardInfo(argv[6]);
        sscanf(strForwardInfo, "%[^,],%d", frwrd_ip, &frwrd_port); //Trick to extract the ip and the port
    } else if(argc == 4 && strcmp(argv[2], "-b") == 0){
        mode = 'b';
        e_mode = 0;
    } else if(argc == 4 && strcmp(argv[2], "-i") == 0){
        mode = 'i';
        e_mode = 0;
    } else if(argc == 6 && strcmp(argv[2],"-i") == 0 && strcmp(argv[4],"-o") == 0){
        mode = 'o';
        e_mode = 0;
        strForwardInfo = getForwardInfo(argv[5]);
        sscanf(strForwardInfo, "%[^,],%d", frwrd_ip, &frwrd_port); //Trick to extract the ip and the port
    }else{
        printf("Incorrect parameters.\n");
        exit(1);
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
        // Creating a TCP Server.
        int portno;
        if(!e_mode){
            portno = getPortAsInt(argv[3]);
        }else {
            portno = getPortAsInt(argv[4]);
        }

        // Preparing server
        int server_fd,client_fd,frwrd_fd,clilen,max_fd;
        char socket_buffer[BUFFER_SIZE];
        char process_buffer[BUFFER_SIZE];
        char frwrd_buffer[BUFFER_SIZE];
        struct sockaddr_in serv_addr, cli_addr,frwrd_addr;
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

        clilen = sizeof(cli_addr);

        // Setting up TCP Client if the mode is 'o'.
        if(mode == 'o'){
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
                perror("Connection failed");
                exit(EXIT_FAILURE);
            }
        }

        // Accept a single incoming connection.
        client_fd = accept(server_fd, (struct sockaddr *) &cli_addr, &clilen);
        if (client_fd < 0) {
            perror("accept");
            exit(1);
        }

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
                if(mode == 'o'){
                    close(frwrd_fd);
                }
                exit(EXIT_FAILURE);
            }

            
            if(!e_mode){ // Do if the parameter -e is missing:
                // Check if something happened on stdin
                if (FD_ISSET(STDIN_FILENO, &readfds)) {
                    memset(socket_buffer, 0, BUFFER_SIZE);
                    if (read(STDIN_FILENO, socket_buffer, BUFFER_SIZE) > 0) {
                        write(parent_to_child_pipe[1], socket_buffer, strlen(socket_buffer)); // Send stdin input to child
                        memset(process_buffer, 0, BUFFER_SIZE); 
                        bytes_read = read(child_to_parent_pipe[0], process_buffer, sizeof(process_buffer) - 1);
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
                        if(mode == 'o'){
                            close(frwrd_fd);
                        }
                        close(client_fd);
                        close(server_fd);
                        close(parent_to_child_pipe[1]);
                        close(child_to_parent_pipe[0]);
                        printf("Closing server.\n");
                        return 0;
                    } else {
                        write(parent_to_child_pipe[1], socket_buffer, n); // We have the input in the buffer and we need to feed it to the child process. This is the desired behavior across all modes.
                        memset(process_buffer, 0, BUFFER_SIZE); 
                        bytes_read = read(child_to_parent_pipe[0], process_buffer, sizeof(process_buffer) - 1);
                        if (bytes_read > 0) {
                            process_buffer[bytes_read] = '\0'; // Null-terminate the string
                            if(mode == 'b'){
                                //Output goes back to the client
                                write(client_fd, process_buffer, sizeof(process_buffer) - 1); // Send response back to client
                            }else if(mode == 'i'){
                                //Output goes to STDOUT
                                printf("Child process responds to TCP Client: %s\n",process_buffer);
                            }else if(mode == 'o'){
                                //Output goes to ip add argument
                                send(frwrd_fd,process_buffer,sizeof(process_buffer) - 1,0);
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
        close(parent_to_child_pipe[1]);
        close(child_to_parent_pipe[0]);
        close(server_fd);
        if(mode == 'o'){
            close(frwrd_fd);
        }
        return 0;

    }else{ // Child process
        close(parent_to_child_pipe[1]); // Close the write end of the parent-to-child pipe
        close(child_to_parent_pipe[0]); // Close the read end of the child-to-parent pipe
        char *command;
        if(!e_mode){
            command = argv[1];
        } else{
            command = argv[2];
        }
        char *program = strtok(command, " ");
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