#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
int main() {
    int counter = 0;
    int rc = fork();
    counter++;
    
    if (rc == 0) {
        rc = fork();
        counter++;
    } if (rc > 0){
        counter++;
    }
    
    printf("hello!\n");
    printf("counter = %d\n", counter);
    return 0;
}