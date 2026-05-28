// SPDX-License-Identifier: GPL-2.0
/*
 * AgentOS — /dev/hac Character Device
 *
 * Provides the Supervisor Agent (and privileged user-space programs)
 * with direct access to HAC hypercalls via ioctl, and exposes the
 * EL2↔EL1 IPC ring buffer for event delivery (poll/read interface).
 *
 * ioctl interface:
 *   HAC_IOCTL_HVC   — issue an arbitrary HVC call
 *   HAC_IOCTL_CAPS  — get/set capability mask for a VMID
 *
 * read() interface:
 *   Returns the next pending IPC message from the EL2→EL1 ring.
 *   Blocks until a message is available (interruptible wait).
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/io.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include "hac_hypercalls.h"

/* ---------------------------------------------------------------
 * ioctl commands
 * --------------------------------------------------------------- */
#define HAC_IOC_MAGIC   'H'
#define HAC_IOCTL_HVC   _IOWR(HAC_IOC_MAGIC, 1, struct hac_hvc_call)
#define HAC_IOCTL_CAPS  _IOWR(HAC_IOC_MAGIC, 2, struct hac_cap_req)

struct hac_hvc_call {
    __u64 id;                /* HAC_HVC_* */
    __u64 args[7];           /* x1..x7 */
    __s64 retval;            /* filled by kernel */
};

struct hac_cap_req {
    __u32 vmid;
    __u32 cap_mask;          /* read: current mask; write: new mask */
    __u8  op;                /* 0=get 1=set 2=grant_bit 3=revoke_bit */
    __u8  _pad[3];
};

/* ---------------------------------------------------------------
 * Global IPC shared memory mapping
 * --------------------------------------------------------------- */
static struct hac_ipc_shm __iomem *g_ipc;
static DECLARE_WAIT_QUEUE_HEAD(hac_event_wq);

/* ---------------------------------------------------------------
 * file_operations
 * --------------------------------------------------------------- */
static int hac_dev_open(struct inode *inode, struct file *filp)
{
    /* Only root / CAP_SYS_ADMIN may open /dev/hac */
    if (!capable(CAP_SYS_ADMIN)) return -EPERM;
    return 0;
}

static long hac_dev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    switch (cmd) {
    case HAC_IOCTL_HVC: {
        struct hac_hvc_call call;
        if (copy_from_user(&call, (void __user *)arg, sizeof(call)))
            return -EFAULT;

        /* Issue hypercall — use 5-arg helper (most common case) */
        register u64 x0 asm("x0") = call.id;
        register u64 x1 asm("x1") = call.args[0];
        register u64 x2 asm("x2") = call.args[1];
        register u64 x3 asm("x3") = call.args[2];
        register u64 x4 asm("x4") = call.args[3];
        register u64 x5 asm("x5") = call.args[4];
        register u64 x6 asm("x6") = call.args[5];
        register u64 x7 asm("x7") = call.args[6];
        asm volatile("hvc #0"
            : "+r"(x0)
            : "r"(x1),"r"(x2),"r"(x3),"r"(x4),"r"(x5),"r"(x6),"r"(x7)
            : "memory");
        call.retval = (s64)x0;

        if (copy_to_user((void __user *)arg, &call, sizeof(call)))
            return -EFAULT;
        return 0;
    }
    case HAC_IOCTL_CAPS: {
        struct hac_cap_req req;
        if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
            return -EFAULT;
        switch (req.op) {
        case 0:  /* get */
            req.cap_mask = (u32)hac_hvc1(HAC_HVC_GET_CAPS, req.vmid);
            break;
        case 1:  /* set — grant all specified bits */
            hac_hvc2(HAC_HVC_GRANT_CAP, req.vmid, req.cap_mask);
            break;
        case 2:  /* grant bit */
            hac_hvc2(HAC_HVC_GRANT_CAP, req.vmid, req.cap_mask);
            break;
        case 3:  /* revoke bit */
            hac_hvc2(HAC_HVC_REVOKE_CAP, req.vmid, req.cap_mask);
            break;
        default:
            return -EINVAL;
        }
        if (copy_to_user((void __user *)arg, &req, sizeof(req)))
            return -EFAULT;
        return 0;
    }
    default:
        return -ENOTTY;
    }
}

/* read(): deliver next IPC event from EL2→EL1 ring */
static ssize_t hac_dev_read(struct file *filp, char __user *buf,
                              size_t len, loff_t *off)
{
    struct hac_ipc_msg msg;
    int ret;

    if (len < sizeof(msg)) return -EINVAL;

    /* Block until a message is available */
    ret = wait_event_interruptible(hac_event_wq,
            (READ_ONCE(g_ipc->el2_head) != READ_ONCE(g_ipc->el2_tail)));
    if (ret) return -EINTR;

    /* Read from ring */
    u32 tail = READ_ONCE(g_ipc->el2_tail);
    if (tail == READ_ONCE(g_ipc->el2_head)) return 0; /* spurious wake */

    memcpy_fromio(&msg, &g_ipc->el2_ring[tail], sizeof(msg));
    smp_store_release(&g_ipc->el2_tail, (tail + 1) % IPC_RING_CAPACITY);

    if (copy_to_user(buf, &msg, sizeof(msg))) return -EFAULT;
    return sizeof(msg);
}

static __poll_t hac_dev_poll(struct file *filp, poll_table *wait)
{
    poll_wait(filp, &hac_event_wq, wait);
    if (READ_ONCE(g_ipc->el2_head) != READ_ONCE(g_ipc->el2_tail))
        return EPOLLIN | EPOLLRDNORM;
    return 0;
}

static const struct file_operations hac_fops = {
    .owner          = THIS_MODULE,
    .open           = hac_dev_open,
    .unlocked_ioctl = hac_dev_ioctl,
    .read           = hac_dev_read,
    .poll           = hac_dev_poll,
};

static struct miscdevice hac_miscdev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "hac",
    .fops  = &hac_fops,
    .mode  = 0600,
};

/* Periodic work to wake up readers when IPC ring has data */
static void hac_ipc_poll_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(hac_poll_work, hac_ipc_poll_work);

static void hac_ipc_poll_work(struct work_struct *work)
{
    if (g_ipc && READ_ONCE(g_ipc->el2_head) != READ_ONCE(g_ipc->el2_tail))
        wake_up(&hac_event_wq);
    schedule_delayed_work(&hac_poll_work, msecs_to_jiffies(4)); /* 250 Hz */
}

/* ---------------------------------------------------------------
 * Platform driver
 * --------------------------------------------------------------- */
static int hvc_hac_probe(struct platform_device *pdev)
{
    /* Map IPC shared memory region */
    g_ipc = (struct hac_ipc_shm __iomem *)
            ioremap(HAC_IPC_SHMEM_PA, HAC_IPC_SHMEM_SIZE);
    if (!g_ipc) {
        dev_err(&pdev->dev, "Failed to map HAC IPC region\n");
        return -ENOMEM;
    }

    /* Validate magic */
    u32 magic = ioread32(&g_ipc->magic);
    if (magic != IPC_SHM_MAGIC) {
        dev_warn(&pdev->dev, "HAC IPC magic mismatch: 0x%08x\n", magic);
        /* Not fatal — HAC may still be initializing */
    }

    int ret = misc_register(&hac_miscdev);
    if (ret) { iounmap(g_ipc); return ret; }

    schedule_delayed_work(&hac_poll_work, msecs_to_jiffies(100));

    dev_info(&pdev->dev, "/dev/hac registered (IPC @ 0x%llx)\n",
             (u64)HAC_IPC_SHMEM_PA);
    return 0;
}

static int hvc_hac_remove(struct platform_device *pdev)
{
    cancel_delayed_work_sync(&hac_poll_work);
    misc_deregister(&hac_miscdev);
    if (g_ipc) { iounmap(g_ipc); g_ipc = NULL; }
    return 0;
}

static const struct of_device_id hvc_hac_of_match[] = {
    { .compatible = "agentos,hvc-hac" }, { }
};
MODULE_DEVICE_TABLE(of, hvc_hac_of_match);

static struct platform_driver hvc_hac_driver = {
    .probe  = hvc_hac_probe,
    .remove = hvc_hac_remove,
    .driver = { .name = "hvc-hac", .of_match_table = hvc_hac_of_match },
};
module_platform_driver(hvc_hac_driver);
MODULE_DESCRIPTION("AgentOS /dev/hac — HAC control interface");
MODULE_LICENSE("GPL v2");
