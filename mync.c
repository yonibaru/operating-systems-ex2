#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char *argv[]){
    // Ensure correct number of arguments are provided
    if(argc != 3 || strcmp(argv[1], "-e") != 0){
        printf("Incorrect parameters.");
        exit(1);
    }
    
    char *command = argv[2];
    char *program = strtok(command, " "); //When encounters " ", it cuts the string directly 'command' and inputs it into 'program'.

    char execute_program[256] = "./";
    strcat(execute_program, program);

    char *argument = strtok(NULL, " "); //argument to be inputted into ttt.
    if (program == NULL || argument == NULL) {
        fprintf(stderr, "Error: Invalid command format.\n");
        return EXIT_FAILURE;
    }
    // Replaces the current process with a new one "ttt" while passing parameter to it
    execlp(execute_program, program, argument, (char *)NULL);
    perror("execlp error");
    exit(1);
}