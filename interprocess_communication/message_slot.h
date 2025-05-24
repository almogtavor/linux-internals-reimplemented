#ifndef MESSAGE_SLOT_H
#define MESSAGE_SLOT_H

#include <linux/ioctl.h>

#define MSG_SLOT_CHANNEL _IOW('M', 1, unsigned int)
#define MSG_SLOT_SET_CEN _IOW('M', 2, unsigned int)

#define MESSAGE_MAX_LEN 128
#define DEVICE_NAME "message_slot"
#define MAJOR_NUM 235

#endif //MESSAGE_SLOT_H
