#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <signal.h>

int main() 
{
    char s1[15];
    long enough = syscall(548, s1, 15);
    if (enough != -1)
        printf("%s", s1);
    else
        printf("%ld\n", enough);
    char s2[10];
    long not_enough = syscall(548, s2, 10);
    if (not_enough != -1)
        printf("%s", s2);
    else
        printf("%ld\n", not_enough);
    while(1) {}
}
