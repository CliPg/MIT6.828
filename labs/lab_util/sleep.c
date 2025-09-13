#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// a tick is about 1 second
int main(int argc, char *argv[]) {

    if (argc < 2) {
        fprintf(2, "Usage: sleep [num of ticks]\n");
        exit(1);
    }

    int num = atoi(argv[1]);
    if (num < 0) {
        num = 0;
    }

    sleep(num);
    exit(0);
}