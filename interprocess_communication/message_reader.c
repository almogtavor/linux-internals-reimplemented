#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "message_slot.h"

int main(int argc, char *argv[])
{
    int fd;
    unsigned int channel;
    char buf[MESSAGE_MAX_LEN];
    ssize_t n;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <file> <channel>\n", argv[0]);
        return 1;
    }

    channel = (unsigned int)strtoul(argv[2], NULL, 0);

    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (ioctl(fd, MSG_SLOT_CHANNEL, &channel)) {
        perror("ioctl CHANNEL");
        close(fd);
        return 1;
    }

    n = read(fd, buf, sizeof(buf));
    if (n < 0) {
        perror("read");
        close(fd);
        return 1;
    }

    if (write(STDOUT_FILENO, buf, n) != n) {
        perror("stdout");
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}