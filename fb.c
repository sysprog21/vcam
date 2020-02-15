#include <linux/proc_fs.h>
#include <linux/spinlock.h>

#include "fb.h"
#include "videobuf.h"

static int vcamfb_open(struct inode *ind, struct file *file)
{
    unsigned long flags = 0;

    struct vcam_device *dev = PDE_DATA(ind);
    if (!dev) {
        pr_err("Private data field of PDE not initilized.\n");
        return -ENODEV;
    }

    spin_lock_irqsave(&dev->in_fh_slock, flags);
    if (dev->fb_isopen) {
        spin_unlock_irqrestore(&dev->in_fh_slock, flags);
        return -EBUSY;
    }
    dev->fb_isopen = true;
    spin_unlock_irqrestore(&dev->in_fh_slock, flags);

    file->private_data = dev;

    return 0;
}

static int vcamfb_release(struct inode *ind, struct file *file)
{
    unsigned long flags = 0;
    struct vcam_device *dev = PDE_DATA(ind);

    spin_lock_irqsave(&dev->in_fh_slock, flags);
    dev->fb_isopen = false;
    spin_unlock_irqrestore(&dev->in_fh_slock, flags);
    dev->in_queue.pending->filled = 0;
    return 0;
}

static ssize_t vcamfb_write(struct file *file,
                            const char __user *buffer,
                            size_t length,
                            loff_t *offset)
{
    struct vcam_in_queue *in_q;
    struct vcam_in_buffer *buf;
    size_t waiting_bytes;
    size_t to_be_copyied;
    unsigned long flags = 0;
    void *data;

    struct vcam_device *dev = file->private_data;
    if (!dev) {
        pr_err("Private data field of file not initialized yet.\n");
        return 0;
    }

    waiting_bytes = dev->input_format.sizeimage;

    in_q = &dev->in_queue;

    buf = in_q->pending;
    if (!buf) {
        pr_err("Pending pointer set to NULL\n");
        return 0;
    }

    /* Reset buffer if last write is too old */
    if (buf->filled && (((int32_t) jiffies - buf->jiffies) / HZ)) {
        pr_debug("Reseting jiffies, difference %d\n",
                 ((int32_t) jiffies - buf->jiffies));
        buf->filled = 0;
    }
    buf->jiffies = jiffies;

    /* Fill the buffer */
    /* TODO: implement real buffer handling */
    to_be_copyied = length;
    if ((buf->filled + to_be_copyied) > waiting_bytes)
        to_be_copyied = waiting_bytes - buf->filled;

    data = buf->data;
    if (!data) {
        pr_err("NULL pointer to framebuffer");
        return 0;
    }

    if (copy_from_user(data + buf->filled, (void *) buffer, to_be_copyied) !=
        0) {
        pr_warn("Failed to copy_from_user!");
    }
    buf->filled += to_be_copyied;

    if (buf->filled == waiting_bytes) {
        spin_lock_irqsave(&dev->in_q_slock, flags);
        swap_in_queue_buffers(in_q);
        spin_unlock_irqrestore(&dev->in_q_slock, flags);
    }

    return to_be_copyied;
}

static struct file_operations vcamfb_fops = {
    .owner = THIS_MODULE,
    .open = vcamfb_open,
    .release = vcamfb_release,
    .write = vcamfb_write,
};

struct proc_dir_entry *init_framebuffer(const char *proc_fname,
                                        struct vcam_device *dev)
{
    struct proc_dir_entry *procf;

    pr_debug("Creating framebuffer for /dev/%s\n", proc_fname);
    procf = proc_create_data(proc_fname, 0666, NULL, &vcamfb_fops, dev);
    if (!procf) {
        pr_err("Failed to create procfs entry\n");
        /* FIXME: report -ENODEV */
        goto failure;
    }

failure:
    return procf;
}

void destroy_framebuffer(const char *proc_fname)
{
    if (!proc_fname)
        return;

    pr_debug("Destroying framebuffer %s\n", proc_fname);
    remove_proc_entry(proc_fname, NULL);
}
