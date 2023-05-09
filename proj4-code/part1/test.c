#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

int main() {
    int res = open("test.txt", "r");
    printf("%d\n", res);
    perror("stat");
}
