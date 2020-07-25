#include <linux/version.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0)
#include <linux/device.h>
#endif

#define DEV_NAME "awcloud"
#define BUFFER_LEN 4096
#define MEM_CLEAR 0x1

struct awcloud_mem {
	dev_t             dev_id;
	unsigned int      major;
	unsigned int      minor;
	unsigned int      used_len;
	struct device     *device;
	struct class      *class;
	struct cdev       *cdev;
	char              buffer[BUFFER_LEN];
};

static struct awcloud_mem *dev;
static unsigned int major;
module_param(major, uint, 0444);

static int open_awcloud_mem(struct inode *inodep, struct file *filp)
{
	filp->private_data = dev;
	return 0;
}

static int release_awcloud_mem(struct inode *inodep, struct file *filp)
{
	return 0;
}

static ssize_t read_awcloud_mem(struct file *filp,
	char __user *user_buffer, size_t count, loff_t *ppos)
{
	int ret = 0;
	struct awcloud_mem *dev = (struct awcloud_mem *)filp->private_data;

	if (*ppos >= BUFFER_LEN) {
		return count ? -ENXIO:0;
	}

	if (count > (BUFFER_LEN - *ppos)) {
		count = BUFFER_LEN - *ppos;
	}

	if (copy_to_user(user_buffer, (void *)(dev->buffer + *ppos), count)) {
		return -EFAULT;
	}

	*ppos += count;
	ret = count;

	return ret;
}

static ssize_t write_awcloud_mem(struct file *filp,
	const char __user *user_buffer, size_t count, loff_t *ppos)
{
	int ret = 0;
	struct awcloud_mem *dev = (struct awcloud_mem *)filp->private_data;

	if (*ppos >= BUFFER_LEN) {
		return count ? -ENXIO:0;
	}

	if (count > BUFFER_LEN - *ppos) {
		count = BUFFER_LEN - *ppos;
	}

	pr_info(
#if defined(__arm__)
		"Calling the write function with count %d, and ppos %lld\n",
#else
		"Calling the write function with count %ld, and ppos %lld\n",
#endif
		count, *ppos);

	if (copy_from_user(dev->buffer + *ppos, user_buffer, count)) {
		ret = -EFAULT;
		return ret;
	}

	*ppos += count;
	if (*ppos > dev->used_len) {
		dev->used_len = *ppos;
	}
	ret = count;

	return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
int ioctl_awcloud_mem(struct inode *inodep,
	struct file *filp, unsigned int cmd, unsigned long arg)
{
#else
static long ioctl_awcloud_mem(struct file *filp,
	unsigned int cmd, unsigned long arg)
{
	//struct inode *inodep = file_inode(filp);
#endif
	struct awcloud_mem *dev = (struct awcloud_mem *)filp->private_data;

	switch (cmd) {
	case MEM_CLEAR:
		memset(dev->buffer, 0, BUFFER_LEN);
		dev->used_len = 0;
		pr_info("Set Kernel Buffer to Zero\n");
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static loff_t llseek_awcloud_mem(struct file *filp,
	loff_t offset, int whence)
{
	loff_t ret = 0;
	struct awcloud_mem *dev = (struct awcloud_mem *)filp->private_data;

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

static const struct file_operations awcloud_mem_fops = {
	.owner   = THIS_MODULE,
	.open    = open_awcloud_mem,
	.release = release_awcloud_mem,
	.read    = read_awcloud_mem,
	.write   = write_awcloud_mem,
	.llseek  = llseek_awcloud_mem,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)
	.ioctl   = ioctl_awcloud_mem,
#else
	.compat_ioctl = ioctl_awcloud_mem,
	.unlocked_ioctl = ioctl_awcloud_mem,
#endif
};

static int awcloud_mem_setup_chrdev(struct awcloud_mem *dev)
{
	int result = 0;

	if (major > 0) {
		dev->dev_id = MKDEV(major, 0);
		result = register_chrdev_region(dev->dev_id, 1, DEV_NAME);
	} else {
		result = alloc_chrdev_region(&(dev->dev_id), 0, 1, DEV_NAME);
	}
	if (result) {
		pr_err("Failed to register the char device\n");
		result = -EFAULT;
		goto chrdev_region_err;
	}

	dev->major = MAJOR(dev->dev_id);
	dev->minor = MINOR(dev->dev_id);

	dev->cdev = cdev_alloc();
	if (!dev->cdev) {
		pr_err("Failed to alloc cdev struct\n");
		result = -EFAULT;
		goto cdev_alloc_err;
	}

	dev->cdev->owner = THIS_MODULE;

	cdev_init(dev->cdev, &awcloud_mem_fops);

	result = cdev_add(dev->cdev, dev->dev_id, 1);
	if (result) {
		pr_err("Failed to add awcloud_mem into Linux system\n");
		result = -EFAULT;
		goto cdev_add_err;
	}

	dev->class = class_create(THIS_MODULE, DEV_NAME);
	if (IS_ERR(dev->class)) {
		result = PTR_ERR(dev->class);
		goto class_create_err;
	}

	dev->device = device_create(dev->class, NULL,
		dev->dev_id, NULL, DEV_NAME);
	if (IS_ERR(dev->device)) {
		result = PTR_ERR(dev->device);
		goto device_create_err;
	}

	memset(dev->buffer, 0, BUFFER_LEN);
	dev->used_len = 0;
	return 0;

device_create_err:
	class_destroy(dev->class);
class_create_err:
	cdev_del(dev->cdev);
cdev_alloc_err:
cdev_add_err:
	unregister_chrdev_region(dev->dev_id, 1);
chrdev_region_err:
	return result;
}

static int __init awcloud_mem_init(void)
{
	int result = 0;

	dev = kzalloc(sizeof(struct awcloud_mem), GFP_KERNEL);
	if (!dev) {
		result = -ENOMEM;
		goto finally;
	}
	result = awcloud_mem_setup_chrdev(dev);
	if (0 > result) {
		kfree(dev);
		goto finally;
	}

finally:
	return result;
}

static void __exit awcloud_mem_exit(void)
{
	device_destroy(dev->class, dev->dev_id);
	class_destroy(dev->class);
	cdev_del(dev->cdev);
	unregister_chrdev_region(dev->dev_id, 1);
	kfree(dev);
}

module_init(awcloud_mem_init);
module_exit(awcloud_mem_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zhangjl@awcloud.com");
