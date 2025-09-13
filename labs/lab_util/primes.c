#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void prime_sieve(int p_left_read_fd) {
    int num;
    int prime;

    if (read(p_left_read_fd, &prime, sizeof(int)) == 0) {
        close(p_left_read_fd);
        exit(0);
    }

    printf("prime %d\n", prime);

    int p_right[2];
    pipe(p_right);

    int pid = fork();

    if (pid == 0) {
        close(p_right[1]);
        prime_sieve(p_right[0]);
    } else {
        close(p_right[0]);
        while (read(p_left_read_fd, &num, sizeof(int)) > 0) {
            if (num%prime != 0) {
                write(p_right[1], &num, sizeof(int));
            }
        }
        close(p_left_read_fd);
        close(p_right[1]);
        wait(0);
        exit(0);
    }
}

int main() {
    int p[2];
    pipe(p);

    int pid = fork();

    if (pid == 0) {
        close(p[1]);
        prime_sieve(p[0]);
    } else {
        close(p[0]);
        for (int i = 2; i <= 35; i++) {
            write(p[1], &i, sizeof(int)); //把 i 的 4 个字节写进 p[1] 所代表的管道缓冲区中
        }
        close(p[1]);
        wait(0); // 等待子进程结束
    }
    exit(0);
}