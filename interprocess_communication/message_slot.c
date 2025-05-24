#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/types.h>
#include "message_slot.h"

#include <errno.h>
#include <stddef.h>

int MODULE_DESCRIPTION("Message Slot IPC driver");

// driver data structures
// represents one specific communication channel in the message slot device
struct channel_node {
    unsigned long id;
    char msg[MESSAGE_MAX_LEN];
    size_t len;
    struct channel_node *next; // points to the next channel in the linked list (handles multiple channels per slot)
};

// Each slot corresponds to a unique /dev/message_slotX device file (one per minor). Inside that device there are multiple channels.
struct slot_node {
    int minor;
    struct channel_node *channels;
    struct slot_node *next;
};

// Each open file can independently select its target channel and censorship behavior.
struct fd_private {
    unsigned long channel_id; //Which channel this file descriptor is using. set with IOCTL before read/write (0 == unset)
    int censor; // 0/1
};
// A global linked list head for all allocated (active) slot_nodes (all device minors in use)
static struct slot_node *slots_head = NULL;

// helpers
// Traverse the global list -If minor is found, return that slot. Otherwise create a new slot_node, set minor, and push it to the head of the list
static struct slot_node *slot_get(int minor)
{
    struct slot_node *s;
    for (s = slots_head; s; s = s->next)
        if (s->minor == minor)
            return s;
    // slot doesnt exist, create new
    s = kmalloc(sizeof(*s), GFP_KERNEL);
    if (!s)
        return NULL;
    s->minor = minor;
    s->channels = NULL;
    s->next = slots_head;
    slots_head = s;
    return s;
}

static struct channel_node *channel_get(struct slot_node *slot, unsigned long id, int create)
{
    struct channel_node *c;
    for (c = slot->channels; c; c = c->next)
        if (c->id == id)
            return c;
    if (!create)
        return NULL;
    c = kmalloc(sizeof(*c), GFP_KERNEL); // Allocate new channel_node
    if (!c)
        return NULL;
    c->id = id;
    c->len = 0; // no message yet
    c->next = slot->channels; // insert at head
    slot->channels = c;
    return c;
}

// file operations
static int device_open(struct inode *inode, struct file *file)
{
    struct fd_private *fd_private_data; // for storing per-open-file data like channel and censor setting
    if (!slot_get(iminor(inode))) // ensure slot is created (searches for an existing slot_node. if not found it'll create)
        return -ENOMEM;

    fd_private_data = kmalloc(sizeof(*fd_private_data), GFP_KERNEL);
    if (!fd_private_data)
        return -ENOMEM;
    fd_private_data->channel_id = 0;
    fd_private_data->censor = 0;
    file->private_data = fd_private_data; // Attach this struct to the open file for later access
    return 0;
}

static int device_release(struct inode *inode, struct file *file)
{
    kfree(file->private_data);
    return 0;
}

static long device_ioctl(struct file *file, unsigned int cmd, unsigned long ioctl_param)
{
    struct fd_private *fd_private_data = file->private_data;
    if (cmd != MSG_SLOT_CHANNEL && cmd != MSG_SLOT_SET_CEN)
        return -EINVAL;

    if (cmd == MSG_SLOT_CHANNEL) {
        if (ioctl_param == 0)
            return -EINVAL;
        fd_private_data->channel_id = ioctl_param;
    } else { // censorship command (MSG_SLOT_SET_CEN)
        if (ioctl_param != 0 && ioctl_param != 1)
            return -EINVAL;
        fd_private_data->censor = ioctl_param;
    }
    return 0;
}

static ssize_t device_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
    struct fd_private *fd_private_data = file->private_data;
    struct slot_node *slot;
    struct channel_node *channel;
    char kbuf[MESSAGE_MAX_LEN];
    size_t i;
    if (fd_private_data->channel_id == 0) // Error case 1: No channel has been set
        return -EINVAL;
    if (len == 0 || len > MESSAGE_MAX_LEN) // Error case 2: Message length is 0 or greater than 128
        return -EMSGSIZE;

    // Error case 3: Failure copying from user space
    if (copy_from_user(kbuf, buf, len))
        return -EFAULT;  // errno = EFAULT (you may customize this)

    // Censorship: Replace every 3rd character with '#'
    if (fd_private_data->censor)
        for (i = 2; i < len; i += 3)
            kbuf[i] = '#';

    // Error case 4: Memory allocation or lookup failure
    slot = slot_get(iminor(file_inode(file)));
    if (!slot)
        return -ENOMEM;

    // Error case 4 continued: Channel creation failure
    channel = channel_get(slot, fd_private_data->channel_id, 1);
    if (!channel)
        return -ENOMEM;

    // Save the (possibly censored) message into the channel buffer
    memcpy(channel->msg, kbuf, len);
    channel->len = len;

    return len; // Success: return number of bytes written
}

static ssize_t device_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
    struct fd_private *priv = file->private_data;
    struct slot_node *slot;
    struct channel_node *chan;

    if (priv->channel_id == 0)
        return -EINVAL;

    slot = slot_get(iminor(file_inode(file)));
    if (!slot)
        return -EWOULDBLOCK;

    chan = channel_get(slot, priv->channel_id, 0);
    if (!chan || chan->len == 0)
        return -EWOULDBLOCK;

    if (len < chan->len)
        return -ENOSPC;

    if (copy_to_user(buf, chan->msg, chan->len))
        return -EFAULT;

    return chan->len;MODULE_AUTHOR
}

static struct file_operations fops = {
    .owner          = THIS_MODULE,
    .open           = device_open,
    .release        = device_release,
    .read           = device_read,
    .write          = device_write,
    .unlocked_ioctl = device_ioctl,
};

/* ---------- module init / exit ---------- */
static int __init message_slot_init(void)
{
    int rc = register_chrdev(MAJOR_NUM, DEVICE_NAME, &fops);
    if (rc < 0)
        printk(KERN_ERR "message_slot: failed registering\n");
    return rc;
}

static void __exit message_slot_exit(void)
{
    struct slot_node *s, *s_tmp;
    unregister_chrdev(MAJOR_NUM, DEVICE_NAME);

    /* free all allocated memory */
    for (s = slots_head; s; ) {
        struct channel_node *c, *c_tmp;
        for (c = s->channels; c; ) {
            c_tmp = c->next;
            kfree(c);
            c = c_tmp;
        }
        s_tmp = s->next;
        kfree(s);
        s = s_tmp;
    }
}

module_init(message_slot_init);
module_exit(message_slot_exit);