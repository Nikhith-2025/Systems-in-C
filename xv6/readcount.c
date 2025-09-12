#include "xv6-riscv_modified/kernel/types.h"
#include "xv6-riscv_modified/kernel/stat.h"
#include "xv6-riscv_modified/user/user.h"

int main(int argc, char *argv[])
{
    int file_descriptor;
    char buffer[100];

    printf("%d\n", getreadcount());

    file_descriptor = open("README", 0); // using README as test file since it exists in xv6
    if (file_descriptor < 0)
    {
        printf("failed to open file\n");
        exit(1);
    }

    read(file_descriptor, buffer, sizeof(buffer));
    close(file_descriptor);

    printf("%d\n", getreadcount());
    exit(0);
}
