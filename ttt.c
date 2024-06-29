#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]){
    if(argc != 2){
        printf("Incorrect parameter amount.\n");
        exit(1);
    }
    
    printf("argument received: %s\n", argv[1]);
    int number;
    
    // Delete, this is just a driver code for the incompleted ttt.
    while(1) {
        // printf("HEY CLIENT!!");
        if (scanf("%d", &number) == 1) {
            fprintf(stderr,"You entered: %d\n", number);
        } else {
            // Clear the input buffer in case of invalid input
            while (getchar() != '\n');
            printf("Invalid input. Exiting...\n");
            break;
        }
    }
    
    return 0;
}