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
#define MEM_CLEAR 0x1

struct awcloud_seconds {
	struct device        *device;
	struct class         *class;
	struct cdev          cdev;
	atomic_t             counter;
	struct timer_list    timer;
};

static struct awcloud_seconds *dev;
static unsigned int major;
static unsigned int num_devices;
module_param(major, uint, 0444);
module_param(num_devices, uint, 0444);

static void awcloud_seconds_handler(unsigned long arg)
{
	mod_timer(&dev->timer, jiffies + HZ);
	atomic_inc(&dev->counter);
	pr_info("Current jiffies is %ld\n", jiffies);
}

static int open_awcloud_seconds(struct inode *inodep, struct file *filp)
{
	struct awcloud_seconds *dev = container_of(
		inodep->i_cdev, struct awcloud_seconds, cdev);

	filp->private_data = dev;
	init_timer(&dev->timer);
	dev->timer.function = &awcloud_seconds_handler;
	dev->timer.expires = jiffies + HZ;
	add_timer(&dev->timer);
	atomic_set(&dev->counter, 0);
	return 0;
}

static int release_awcloud_seconds(struct inode *inodep, struct file *filp)
{

	struct awcloud_seconds *dev =
		(struct awcloud_seconds *)filp->private_data;

	del_timer(&dev->timer);
	return 0;
}

static ssize_t read_awcloud_seconds(struct file *filp,
	char __user *user_buffer, size_t count, loff_t *ppos)
{
	int ret = 0;
	struct awcloud_seconds *dev =
		(struct awcloud_seconds *)filp->private_data;

	ret = atomic_read(&dev->counter);
	if (put_user(ret, (int *)user_buffer)) {
		pr_err("Failed to copy to user\n");
		return -EFAULT;
	}
	return sizeof(unsigned int);
}

const static struct file_operations awcloud_seconds_fops = {
	.owner          = THIS_MODULE,
	.open           = open_awcloud_seconds,
	.release        = release_awcloud_seconds,
	.read           = read_awcloud_seconds,
};

static int awcloud_seconds_setup_chrdev(struct awcloud_seconds *dev, int index)
{
	int result = 0;
	char device_name[10];
	dev_t dev_id = MKDEV(major, index);

	dev->cdev.owner = THIS_MODULE;
	cdev_init(&dev->cdev, &awcloud_seconds_fops);
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

	return result;

device_create_err:
	cdev_del(&dev->cdev);
cdev_add_err:
	return result;
}

static void release_awcloud_seconds_chrdev(
	struct awcloud_seconds *dev, int index)
{
	dev_t dev_id = MKDEV(major, index);

	device_destroy(dev->class, dev_id);
	cdev_del(&dev->cdev);
}

static int __init awcloud_seconds_init(void)
{
	int result = 0;
	int index = 0;
	int tmp[3] = {0};
	dev_t dev_id;
	struct class *class = NULL;

	if (0 >= num_devices) {
		num_devices = 1;
	}

	dev = kzalloc(
		sizeof(struct awcloud_seconds) * num_devices, GFP_KERNEL);
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
		tmp[index] = awcloud_seconds_setup_chrdev(dev+index, index);
		result |= tmp[index];
	}

	if (result) {
		for (index = 0; index < num_devices; index++) {
			if (!tmp[index]) {
				release_awcloud_seconds_chrdev(
					dev+index, index);
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

static void __exit awcloud_seconds_exit(void)
{
	int index = 0;

	for (index = 0; index < num_devices; index++) {
		release_awcloud_seconds_chrdev(dev+index, index);
	}

	class_destroy(dev->class);
	unregister_chrdev_region(MKDEV(major, 0), num_devices);
	kfree(dev);
}

module_init(awcloud_seconds_init);
module_exit(awcloud_seconds_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zhangjl@awcloud.com");
