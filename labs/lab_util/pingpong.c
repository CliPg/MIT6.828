#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main() {
    /* 0:read, 1:write
     * p1: parent to child
     * p2: child to parent
    */
    int p1[2]; 
    int p2[2]; 

    pipe(p1);
    pipe(p2);

    int pid = fork();

    if (pid < 0) {
        fprintf(2, "ERROR\n");
        exit(1);
    } else if (pid == 0) {
        // child process
        close(p1[1]);
        close(p2[0]);
        
        char buf;
        read(p1[0], &buf, 1);
        printf("%d: received ping\n", getpid());
        write(p2[1], &buf, 1);

        close(p1[0]);
        close(p2[1]);

        exit(0);
    } else {
        // parent process
        close(p1[0]);
        close(p2[1]);

        char buf;
        write(p1[1], &buf, 1);
        read(p2[0], &buf, 1);
        printf("%d: received pong\n", getpid());
        
        close(p1[1]);
        close(p2[0]);

        exit(0);
    }

}