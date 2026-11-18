// SPDX-License-Identifier: GPL-2.0
/* Development CAN transport only; no physical ECU is represented here. */
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netlink.h>
#include <linux/poll.h>
#include <linux/skbuff.h>
#include <linux/uaccess.h>
#include <linux/timekeeping.h>
#include <linux/version.h>
#define CAN_BOUNDARY_IMPLEMENTATION
#include "can_boundary.h"
#include "can_datagram.h"
#include <net/net_namespace.h>

#define FRAME_SIZE 10
#define QUEUE_SIZE 32
#define CAN_PROTOCOL 21

static DEFINE_MUTEX(queue_lock);
static DECLARE_WAIT_QUEUE_HEAD(queue_ready);
static unsigned char queue[QUEUE_SIZE][FRAME_SIZE];
static unsigned int head, count;
static can_u64 queue_seq[QUEUE_SIZE], admission_seq, inflight_seq;
static can_u64 loss_count, generated_seq;
static struct can_boundary_state boundary;

static can_u64 boundary_now(void)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
    return ktime_get_boot_ns();
#else
    return ktime_get_boottime_ns();
#endif
}
static int boundary_advance(can_u64 *value)
{
    if (*value == ~(can_u64)0) { boundary.invalid = 1; return -EOVERFLOW; }
    ++*value; return 0;
}
static bool model_open, model_ever_opened;
static struct sock *can_socket;

static long can_ioctl(struct file *file, unsigned int request, unsigned long arg)
{
    unsigned char frame[FRAME_SIZE];
    unsigned int tail;
    if (request != 1)
        return -ENOTTY;
    if (copy_from_user(frame, (void __user *)arg, sizeof(frame)))
        return -EFAULT;
    if (frame[1] != 8)
        return -EINVAL;
    mutex_lock(&queue_lock);
    if (can_boundary_tick(&boundary, boundary_now()) || can_boundary_is_frozen(&boundary)) {
        mutex_unlock(&queue_lock);
        return -EAGAIN;
    }
    if (!model_open) {
        mutex_unlock(&queue_lock);
        return -ENODEV;
    }
    if (count == QUEUE_SIZE) {
        mutex_unlock(&queue_lock);
        return -EAGAIN;
    }
    if (boundary_advance(&admission_seq)) {
        mutex_unlock(&queue_lock);
        return -EOVERFLOW;
    }
    tail = (head + count) % QUEUE_SIZE;
    queue_seq[tail] = admission_seq;
    memcpy(queue[tail], frame, sizeof(frame));
    count++;
    mutex_unlock(&queue_lock);
    wake_up_interruptible(&queue_ready);
    return 0;
}

/* The separate model device is restricted by DAC and SELinux policy. */
static ssize_t can_read(struct file *file, char __user *buf, size_t size, loff_t *pos)
{
    unsigned char frame[FRAME_SIZE];
    int err;
    if (size < FRAME_SIZE)
        return -EINVAL;
    for (;;) {
        mutex_lock(&queue_lock);
        if (boundary.stage == CAN_BOUNDARY_FROZEN || boundary.stage == CAN_BOUNDARY_EXPIRED) {
            mutex_unlock(&queue_lock); return -EAGAIN;
        }
        if (count && !inflight_seq)
            break;
        mutex_unlock(&queue_lock);
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
        err = wait_event_interruptible(queue_ready, READ_ONCE(count) && !READ_ONCE(inflight_seq));
        if (err)
            return err;
    }
    memcpy(frame, queue[head], sizeof(frame));
    if (copy_to_user(buf, frame, sizeof(frame))) {
        mutex_unlock(&queue_lock);
        return -EFAULT;
    }
    inflight_seq = queue_seq[head];
    head = (head + 1) % QUEUE_SIZE;
    count--;
    mutex_unlock(&queue_lock);
    return FRAME_SIZE;
}

static ssize_t can_write(struct file *file, const char __user *buf, size_t size, loff_t *pos)
{
    unsigned char *data;
    enum can_datagram_kind kind;
    struct sk_buff *skb;
    if (size < CAN_FRAME_SIZE || size > CAN_DATAGRAM_MAX)
        return -EINVAL;
    skb = alloc_skb(size, GFP_KERNEL);
    if (!skb)
        return -ENOMEM;
    data = skb_put(skb, size);
    if (copy_from_user(data, buf, size)) {
        kfree_skb(skb);
        return -EFAULT;
    }
    kind = can_datagram_kind(data, size);
    if (kind == CAN_DATAGRAM_INVALID) {
        kfree_skb(skb);
        return -EINVAL;
    }
    NETLINK_CB(skb).portid = 0;
    NETLINK_CB(skb).dst_group = 1;
    /* Serialize broadcast with model reset/close and its queue purge. */
    mutex_lock(&queue_lock);
    if (can_boundary_tick(&boundary, boundary_now()) || !boundary.initialized ||
        (kind == CAN_DATAGRAM_FRAME && data[0] == 0x9a &&
         (((data[8] >> 2) & 7) + 1 != boundary.mode)) ||
        boundary_advance(&generated_seq)) {
        mutex_unlock(&queue_lock);
        kfree_skb(skb);
        return -EIO;
    }
    netlink_broadcast(can_socket, skb, 0, 1, GFP_KERNEL);
    mutex_unlock(&queue_lock);
    return size;
}

static const struct file_operations can_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = can_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = can_ioctl,
#endif
    .llseek = no_llseek,
};
static int model_open_device(struct inode *inode, struct file *file)
{
    int err = 0;
    mutex_lock(&queue_lock);
    if (model_open)
        err = -EBUSY;
    else if (can_boundary_open(&boundary, boundary_now()))
        err = -EBUSY;
    else {
        netlink_reset_multicast(can_socket, 1);
        file->private_data = (void *)(unsigned long)!model_ever_opened;
        model_ever_opened = true;
        model_open = true;
    }
    mutex_unlock(&queue_lock);
    return err;
}

static int model_close_device(struct inode *inode, struct file *file)
{
    mutex_lock(&queue_lock);
    model_open = false;
    if ((count || inflight_seq) && boundary_advance(&loss_count)) boundary.invalid = 1;
    inflight_seq = 0;
    netlink_reset_multicast(can_socket, 1);
    head = 0;
    count = 0;
    mutex_unlock(&queue_lock);
    return 0;
}

static unsigned int model_poll(struct file *file, poll_table *wait)
{
    unsigned int mask = 0;
    poll_wait(file, &queue_ready, wait);
    mutex_lock(&queue_lock);
    if (count && !inflight_seq && boundary.stage != CAN_BOUNDARY_FROZEN && boundary.stage != CAN_BOUNDARY_EXPIRED)
        mask = POLLIN | POLLRDNORM;
    mutex_unlock(&queue_lock);
    return mask;
}

static int boundary_errno(int status)
{
    if (!status) return 0;
    if (status == CAN_BOUNDARY_STALE) return -ESTALE;
    if (status == CAN_BOUNDARY_BUSY) return -EAGAIN;
    if (status == CAN_BOUNDARY_FROZEN_ERROR) return -EBUSY;
    return -EINVAL;
}
static long boundary_ioctl(unsigned long arg)
{
    struct can_boundary_control c;
    int result = 0;
    can_u64 now;
    BUILD_BUG_ON(sizeof(c) != 176);
    if (copy_from_user(&c, (void __user *)arg, sizeof(c))) return -EFAULT;
    if (c.magic != CAN_BOUNDARY_MAGIC || c.version != CAN_BOUNDARY_VERSION || c.flags ||
        (c.operation != CAN_BOUNDARY_FINISH && c.semantic_pending)) return -EINVAL;
    mutex_lock(&queue_lock);
    now = boundary_now();
    if (c.operation == CAN_BOUNDARY_WITNESS) {
        (void)can_boundary_tick(&boundary, now);
        if (c.nonce && (c.nonce != boundary.nonce || c.session != boundary.nonce_session || c.epoch != boundary.nonce_epoch)) result = CAN_BOUNDARY_STALE;
    } else if (c.operation == CAN_BOUNDARY_INIT) {
        result = can_boundary_init(&boundary,c.session,c.epoch,c.mode,now);
    } else if (c.operation == CAN_BOUNDARY_COMMIT || c.operation == CAN_BOUNDARY_COMMIT_RAW) {
        if ((c.operation == CAN_BOUNDARY_COMMIT_RAW && (!inflight_seq || c.raw_seq != inflight_seq)) ||
            (c.operation == CAN_BOUNDARY_COMMIT && (inflight_seq || c.raw_seq))) result = CAN_BOUNDARY_STALE;
        else {
            result = can_boundary_commit(&boundary,c.session,c.epoch,c.mode,now);
            if (!result && c.operation == CAN_BOUNDARY_COMMIT_RAW) inflight_seq = 0;
        }
    } else if (c.operation == CAN_BOUNDARY_BEGIN) {
        result = can_boundary_begin(&boundary,c.session,c.epoch,c.nonce,c.deadline_ns,now);
    } else if (c.operation == CAN_BOUNDARY_FINISH) {
        bool already_frozen = boundary.stage == CAN_BOUNDARY_FROZEN;
        result = can_boundary_finish(&boundary,c.session,c.epoch,c.nonce,
                                     (can_u64)count + c.semantic_pending,inflight_seq,now);
        if (!result && !already_frozen) boundary.fence_generated_seq=generated_seq;
    } else if (c.operation == CAN_BOUNDARY_RESET_QUEUE) {
        result = can_boundary_tick(&boundary, now);
        if (!result && (!c.session || c.session != boundary.session || c.epoch != boundary.epoch))
            result = CAN_BOUNDARY_STALE;
        if (!result && can_boundary_is_frozen(&boundary))
            result = CAN_BOUNDARY_FROZEN_ERROR;
        if (!result && (count || inflight_seq) && boundary_advance(&loss_count))
            result = CAN_BOUNDARY_INVALID;
        if (!result) {
            head = 0;
            count = 0;
            inflight_seq = 0;
            netlink_reset_multicast(can_socket, 1);
        }
    } else if (c.operation == CAN_BOUNDARY_ABORT) {
        bool already_aborted = boundary.stage == CAN_BOUNDARY_ABORTED;
        result = c.session == boundary.nonce_session && c.epoch == boundary.nonce_epoch ? can_boundary_abort(&boundary,c.nonce,now) : CAN_BOUNDARY_STALE;
        if (!result && !already_aborted) {
            if ((count || inflight_seq) && boundary_advance(&loss_count)) result=CAN_BOUNDARY_INVALID;
            head=0; count=0; inflight_seq=0;
            netlink_reset_multicast(can_socket,1);
        }
    } else result = CAN_BOUNDARY_INVALID;
    c.flags=1; c.session=boundary.session; c.epoch=boundary.epoch;
    c.nonce=boundary.nonce; c.deadline_ns=boundary.deadline_ns; c.raw_seq=admission_seq;
    c.mode=boundary.mode; c.semantic_pending=0; c.open_seq=boundary.open_seq;
    c.commit_seq=boundary.commit_seq; c.on_count=boundary.on_count;
    c.first_mode=boundary.first_mode; c.first_ns=boundary.first_ns;
    c.fence_seq=boundary.fence_seq; c.fence_ns=boundary.fence_ns;
    c.queued=count; c.inflight=inflight_seq; c.loss=loss_count;
    c.generated_seq=generated_seq; c.now_ns=now;
    c.fence_generated_seq=boundary.fence_generated_seq;
    c.stage=boundary.stage; c.invalid=boundary.invalid || !boundary.initialized;
    mutex_unlock(&queue_lock);
    if (result) return boundary_errno(result);
    wake_up_interruptible(&queue_ready);
    return copy_to_user((void __user *)arg,&c,sizeof(c)) ? -EFAULT : 0;
}
static long model_ioctl(struct file *file, unsigned int request, unsigned long arg)
{
    if (request == 3)
        return arg ? boundary_ioctl(arg) : (long)file->private_data;
    return -ENOTTY;
}

static const struct file_operations model_fops = {
    .owner = THIS_MODULE,
    .open = model_open_device,
    .release = model_close_device,
    .poll = model_poll,
    .unlocked_ioctl = model_ioctl,
    .read = can_read,
    .write = can_write,
    .llseek = no_llseek,
};
static struct miscdevice model_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "cis_can_model",
    .fops = &model_fops,
    .mode = 0600,
};
static struct miscdevice can_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "cis_can",
    .fops = &can_fops,
};

static int __init can_init(void)
{
    struct netlink_kernel_cfg cfg = {
        .groups = 1,
        .flags = NL_CFG_F_NONROOT_RECV,
    };
    int err;
    can_socket = netlink_kernel_create(&init_net, CAN_PROTOCOL, &cfg);
    if (!can_socket)
        return -EADDRINUSE;
    err = misc_register(&can_device);
    if (err) {
        netlink_kernel_release(can_socket);
        return err;
    }
    err = misc_register(&model_device);
    if (err) {
        misc_deregister(&can_device);
        netlink_kernel_release(can_socket);
    }
    return err;
}
static void __exit can_exit(void)
{
    misc_deregister(&model_device);
    misc_deregister(&can_device);
    netlink_kernel_release(can_socket);
}
module_init(can_init);
module_exit(can_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Voyemu development CAN transport, not a physical ECU");
