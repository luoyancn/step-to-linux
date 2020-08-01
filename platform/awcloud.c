#include <linux/version.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/poll.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0)
#include <linux/device.h>
#else
#include <linux/sched/signal.h>
#endif

#define DEV_NAME "awcloud"
#define BUFFER_LEN 4096
#define MEM_CLEAR 0x1

struct awcloud_platform {
	unsigned int         used_len;
	struct device        *device;
	struct class         *class;
	struct fasync_struct *async_queue;
	char                 buffer[BUFFER_LEN];
	struct semaphore     sem;
	struct cdev          cdev;
	wait_queue_head_t    r_wait;
	wait_queue_head_t    w_wait;
};

static struct awcloud_platform *dev;
static unsigned int major;
static unsigned int num_devices;
module_param(major, uint, 0444);
module_param(num_devices, uint, 0444);

static int open_awcloud_platform(struct inode *inodep, struct file *filp)
{
	struct awcloud_platform *dev = container_of(
		inodep->i_cdev, struct awcloud_platform, cdev);
	filp->private_data = dev;
	return 0;
}

static int fasync_awcloud_platform(int fd, struct file *filp, int on)
{
	struct awcloud_platform *dev = (struct awcloud_platform *)filp->private_data;

	return fasync_helper(fd, filp, on, &dev->async_queue);
}


static int release_awcloud_platform(struct inode *inodep, struct file *filp)
{
	fasync_awcloud_platform(-1, filp, 0);
	return 0;
}

static ssize_t read_awcloud_platform(struct file *filp,
	char __user *user_buffer, size_t count, loff_t *ppos)
{
	int ret = 0;
	struct awcloud_platform *dev = (struct awcloud_platform *)filp->private_data;

	DECLARE_WAITQUEUE(wait, current);

	down(&dev->sem);
	add_wait_queue(&dev->r_wait, &wait);

	if (0 == dev->used_len) {
		if (filp->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto again_err;
		}
		__set_current_state(TASK_INTERRUPTIBLE);
		up(&dev->sem);
		schedule();

		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			goto out;
		}
		down(&dev->sem);
	}

	if (count > dev->used_len) {
		count = dev->used_len;
	}

	if (copy_to_user(user_buffer, dev->buffer, count)) {
		ret = -EFAULT;
		goto copy_to_user_err;
	}

	//memcpy(dev->buffer, dev->buffer+count, dev->used_len-count);
	dev->used_len -= count;
#if defined(__arm__)
	pr_info("Read %d bytes, current lenth is %d\n", count, dev->used_len);
#else
	pr_info("Read %ld bytes, current lenth is %d\n", count, dev->used_len);
#endif
	wake_up_interruptible(&dev->w_wait);
	if (dev->async_queue) {
		kill_fasync(&dev->async_queue, SIGIO, POLL_OUT);
		pr_debug("%s kill SIGIO", __func__);
	}
	ret = count;

again_err:
copy_to_user_err:
	up(&dev->sem);

out:
	remove_wait_queue(&dev->r_wait, &wait);
	set_current_state(TASK_RUNNING);

	return ret;
}

static ssize_t write_awcloud_platform(struct file *filp,
	const char __user *user_buffer, size_t count, loff_t *ppos)
{
	int ret = 0;
	struct awcloud_platform *dev = (struct awcloud_platform *)filp->private_data;
	DECLARE_WAITQUEUE(wait, current);

	pr_info(
#if defined(__arm__)
		"Calling the write function with count %d, and ppos %lld\n",
#else
		"Calling the write function with count %ld, and ppos %lld\n",
#endif
		count, *ppos);

	down(&dev->sem);
	add_wait_queue(&dev->w_wait, &wait);

	if (BUFFER_LEN == dev->used_len) {
		if (filp->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto again_err;
		}
		__set_current_state(TASK_INTERRUPTIBLE);
		up(&dev->sem);
		schedule();
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			goto out;
		}
		down(&dev->sem);
	}

	if (count > BUFFER_LEN - dev->used_len) {
		count = BUFFER_LEN - dev->used_len;
	}

	if (copy_from_user(dev->buffer+dev->used_len, user_buffer, count)) {
		ret = -EFAULT;
		goto copy_from_user_err;
	}

	dev->used_len += count;
	wake_up_interruptible(&dev->r_wait);
	ret = count;

	if (dev->async_queue) {
		kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
		pr_debug("%s kill SIGIO", __func__);
	}

again_err:
copy_from_user_err:
	up(&dev->sem);

out:
	remove_wait_queue(&dev->w_wait, &wait);
	set_current_state(TASK_RUNNING);

	return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
int ioctl_awcloud_platform(struct inode *inodep,
	struct file *filp, unsigned int cmd, unsigned long arg)
{
#else
static long ioctl_awcloud_platform(struct file *filp,
	unsigned int cmd, unsigned long arg)
{
	//struct inode *inodep = file_inode(filp);
#endif
	struct awcloud_platform *dev = (struct awcloud_platform *)filp->private_data;

	pr_info("Calling the ioctl function\n");
	switch (cmd) {
	case MEM_CLEAR:
		if (down_interruptible(&dev->sem)) {
			return -ERESTARTSYS;
		}

		memset(dev->buffer, 0, BUFFER_LEN);
		dev->used_len = 0;

		up(&dev->sem);

		pr_info("Set Kernel Buffer to Zero\n");
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static loff_t llseek_awcloud_platform(struct file *filp,
	loff_t offset, int whence)
{
	loff_t ret = 0;
	struct awcloud_platform *dev = (struct awcloud_platform *)filp->private_data;

	pr_info(
		"Calling the llseek function of this device,"
		"and offset is %lld , whence is %d\n", offset, whence);
	switch (whence) {
	case SEEK_SET:
		if (offset < 0) {
			ret = -EINVAL;
			break;
		}
		if ((unsigned int)offset > BUFFER_LEN) {
			ret = -EINVAL;
			break;
		}
		filp->f_pos = (unsigned int) offset;
		ret = filp->f_pos;
		break;
	case SEEK_CUR:
		if ((filp->f_pos + offset) > BUFFER_LEN) {
			ret = -EINVAL;
			break;
		}
		if ((filp->f_pos + offset) < 0) {
			ret = -EINVAL;
			break;
		}
		filp->f_pos += offset;
		ret = filp->f_pos;
		break;
	case SEEK_END:
		if ((filp->f_pos + offset) > BUFFER_LEN) {
			ret = -EINVAL;
			break;
		}
		if ((filp->f_pos + offset) < 0) {
			ret = -EINVAL;
			break;
		}
		if ((dev->used_len+offset) > BUFFER_LEN) {
			ret = -EINVAL;
			break;
		}
		filp->f_pos = dev->used_len + offset;
		ret = filp->f_pos;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
unsigned int poll_awcloud_platform(
	struct file *filp, struct poll_table_struct *wait)
{
	unsigned int mask = 0;
#else
__poll_t poll_awcloud_platform(
	struct file *filp, struct poll_table_struct *wait)
{
	__poll_t mask = 0;
#endif
	struct awcloud_platform *dev = (struct awcloud_platform *)filp->private_data;

	down(&dev->sem);
	poll_wait(filp, &dev->r_wait, wait);
	poll_wait(filp, &dev->w_wait, wait);
	if (dev->used_len) {
		mask |= POLLIN | POLLRDNORM;
	}
	if (BUFFER_LEN != dev->used_len) {
		mask |= POLLOUT | POLLWRNORM;
	}
	up(&dev->sem);

	return mask;
}

const static struct file_operations awcloud_platform_fops = {
	.owner          = THIS_MODULE,
	.open           = open_awcloud_platform,
	.release        = release_awcloud_platform,
	.read           = read_awcloud_platform,
	.write          = write_awcloud_platform,
	.llseek         = llseek_awcloud_platform,
	.poll           = poll_awcloud_platform,
	.fasync         = fasync_awcloud_platform,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)
	.ioctl          = ioctl_awcloud_platform,
#else
	.compat_ioctl   = ioctl_awcloud_platform,
	.unlocked_ioctl = ioctl_awcloud_platform,
#endif
};

static int awcloud_platform_setup_chrdev(struct awcloud_platform *dev, int index)
{
	int result = 0;
	char device_name[10];
	dev_t dev_id = MKDEV(major, index);

	dev->cdev.owner = THIS_MODULE;
	cdev_init(&dev->cdev, &awcloud_platform_fops);
	if (cdev_add(&dev->cdev, dev_id, 1)) {
		pr_err("Failed to add char dev into system\n");
		result = -1;
		goto cdev_add_err;
	}

	sprintf(device_name, DEV_NAME"%d", index);
	dev->device = device_create(
		dev->class, NULL, dev_id, NULL, device_name);
	if (IS_ERR(dev->device)) {
		result = -1;
		goto device_create_err;
	}

	memset(dev->buffer, 0, BUFFER_LEN);
	dev->used_len = 0;

	sema_init(&(dev->sem), 1);
	init_waitqueue_head(&dev->r_wait);
	init_waitqueue_head(&dev->w_wait);
	return result;

device_create_err:
	cdev_del(&dev->cdev);
cdev_add_err:
	return result;
}

static void release_awcloud_platform_chrdev(struct awcloud_platform *dev, int index)
{
	dev_t dev_id = MKDEV(major, index);

	device_destroy(dev->class, dev_id);
	cdev_del(&dev->cdev);
}

static int __init awcloud_platform_init(void)
{
	int result = 0;
	int index = 0;
	int tmp[3] = {0};
	dev_t dev_id;
	struct class *class = NULL;

	if (0 >= num_devices) {
		num_devices = 1;
	}

	dev = kzalloc(sizeof(struct awcloud_platform) * num_devices, GFP_KERNEL);
	if (!dev) {
		result = -ENOMEM;
		goto finally;
	}

	if (major > 0) {
		dev_id = MKDEV(major, 0);
		result = register_chrdev_region(
			dev_id, num_devices, DEV_NAME);
	} else {
		result = alloc_chrdev_region(
			&dev_id, 0, num_devices, DEV_NAME);
	}

	if (result) {
		pr_err("Failed to alloc the char dev number\n");
		result = -EFAULT;
		goto alloc_dev_id_err;
	}

	major = MAJOR(dev_id);
	class = class_create(THIS_MODULE, DEV_NAME);
	if (IS_ERR(class)) {
		result = -1;
		goto class_create_err;
	}

	for (index = 0; index < num_devices; index++) {
		(dev+index)->class = class;
		tmp[index] = awcloud_platform_setup_chrdev(dev+index, index);
		result |= tmp[index];
	}

	if (result) {
		for (index = 0; index < num_devices; index++) {
			if (!tmp[index]) {
				release_awcloud_platform_chrdev(dev+index, index);
			}
		}
		goto setup_chrdev_err;
	}

	return result;

setup_chrdev_err:
	class_destroy(class);
class_create_err:
	unregister_chrdev_region(MKDEV(major, 0), num_devices);
alloc_dev_id_err:
	kfree(dev);
finally:
	return result;
}

static void __exit awcloud_platform_exit(void)
{
	int index = 0;

	for (index = 0; index < num_devices; index++) {
		release_awcloud_platform_chrdev(dev+index, index);
	}

	class_destroy(dev->class);
	unregister_chrdev_region(MKDEV(major, 0), num_devices);
	kfree(dev);
}

module_init(awcloud_platform_init);
module_exit(awcloud_platform_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zhangjl@awcloud.com");
