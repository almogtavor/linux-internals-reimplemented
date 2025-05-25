#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "message_slot.h"


int main(int argc, char *argv[]) {
    unsigned int channel_id, censor_mode;
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <file> <channel> <censor 0|1> <message>\n", argv[0]);
        return 1;
    }

    channel_id = (unsigned int) strtoul(argv[2], NULL, 0);
    censor_mode = (unsigned int) strtoul(argv[3], NULL, 0);
    const char *msg = argv[4];
    const size_t len = strlen(msg);
    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("an error occurred during open");
        return 1;
    }
    if (ioctl(fd, MSG_SLOT_SET_CEN, &censor_mode)) { // set censorship mode to the value specified in args
        perror("an error occurred during ioctl (when SET_CEN)");
        close(fd);
        return 1;
    }
    if (ioctl(fd, MSG_SLOT_CHANNEL, &channel_id)) {
        perror("an error occurred during ioctl (setting CHANNEL)");
        close(fd);
        return 1;
    }
    // strlen(msg) returns the number of characters before the null terminator so i don't include the terminating null char
    if (write(fd, msg, len) != (ssize_t) len) {
        perror("an error occurred during write");
        close(fd);
        return 1;
    }
    close(fd);
    return 0;
}
