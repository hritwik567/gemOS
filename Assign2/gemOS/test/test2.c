#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

int main (int argc, char *argv[])
{
    int fd1;
    char buf[128];
    fd1 = open(argv[1], O_WRONLY);
    if (fd1 == -1) {
        perror(argv[1]);
        return EXIT_FAILURE;
    }

    /* Enter the data to be written into the file */
    scanf("%127s", buf);

    int x = write(fd1, buf, strlen(buf) + 3);
    /* fd1 is the file descriptor, buf is the character array used to
 hold the data, strlen(buf) informs the function that the number of bytes equal to the length of the
 string in the buffer need to be copied */

    //int x = printf("%10s\n", buf);
    printf("\n\n\n\n\n%d\n",x);
    close(fd1);

    return 0;
}
