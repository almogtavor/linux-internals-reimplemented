#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "message_slot.h"

int main(int argc, char *argv[]) {
    unsigned int channel_id;
    char buf[MESSAGE_MAX_LEN];
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <file> <channel>\n", argv[0]);
        return 1;
    }

    channel_id = (unsigned int) strtoul(argv[2], NULL, 0);

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("an error occurred during open");
        return 1;
    }
    if (ioctl(fd, MSG_SLOT_CHANNEL, &channel_id)) {
        perror("an error occurred during ioctl (setting CHANNEL)");
        close(fd);
        return 1;
    }
    const ssize_t n = read(fd, buf, sizeof(buf));
    if (n < 0) {
        perror("an error occurred during read");
        close(fd);
        return 1;
    }
    close(fd);
    if (write(STDOUT_FILENO, buf, n) != n) {
        perror("an error occurred during writing the buffer to stdout");
        return 1;
    }
    return 0;
}
