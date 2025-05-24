#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "message_slot.h"


int main(int argc, char *argv[])
{
    int fd;
    unsigned int channel, censor;
    const char *msg;
    size_t len;

    if (argc != 5) {
        fprintf(stderr, "Usage: %s <file> <channel> <censor 0|1> <message>\n", argv[0]);
        return 1;
    }

    channel = (unsigned int)strtoul(argv[2], NULL, 0);
    censor  = (unsigned int)strtoul(argv[3], NULL, 0);
    msg     = argv[4];
    len     = strlen(msg);

    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (ioctl(fd, MSG_SLOT_SET_CEN, &censor)) {
        perror("ioctl SET_CEN");
        close(fd);
        return 1;
    }

    if (ioctl(fd, MSG_SLOT_CHANNEL, &channel)) {
        perror("ioctl CHANNEL");
        close(fd);
        return 1;
    }

    if (write(fd, msg, len) != (ssize_t)len) {
        perror("write");
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}