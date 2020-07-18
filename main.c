#include<linux/init.h>
#include<linux/module.h>
#include<linux/version.h>
#include<linux/kernel.h>
#include<linux/types.h>
#include<linux/kdev_t.h>
#include<linux/cdev.h>
#include<linux/fs.h>
#include<linux/device.h>
#include <linux/types.h>
#include <linux/random.h>
#include<linux/errno.h>
#include <linux/uaccess.h>
#include<linux/slab.h> 	
#include <linux/ioctl.h>

#define WR_VALUE_1 _IOW('a','a',int32_t*)
#define WR_VALUE_2 _IOW('a','b',int32_t*)

static dev_t first; // variable for device number
static struct cdev adc; // variable for the adc
static struct class *cls; // variable for the device class

uint16_t val;
uint32_t num, allign;

static int adc_open(struct inode *i, struct file *f)
{
	printk(KERN_INFO "adc : open()\n");
	return 0;
}

static int adc_close(struct inode *i, struct file *f)
{
	printk(KERN_INFO "adc : close()\n");
	return 0;
}

static ssize_t adc_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	printk(KERN_INFO "adc : read()\n");
	get_random_bytes(&val, 2);
	if(allign == 0)
	{
	printk(KERN_INFO "Lower 10-Bits Allignmnet\n");
	val=(val & 0x03FF);
	printk(KERN_INFO "ADC value : %d\n",val);
	copy_to_user(buf, &val, 2);
	}
	
	else if (allign == 1)
	{
	printk(KERN_INFO "Higher 10-Bits Allignmnet\n");
	val=((val<<6) & 0xFFC0);
	printk(KERN_INFO "ADC value : %d\n",val);
	copy_to_user(buf, &val, 2);
	}
	return 0;
}

static long adc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
         switch(cmd) {
                case WR_VALUE_1:
                        copy_from_user(&num ,(int32_t*) arg, sizeof(val));
                        printk(KERN_INFO "ADC Channel = %d\n", num);
                        break;

                case WR_VALUE_2:
                        copy_from_user(&allign ,(int32_t*) arg, sizeof(allign));
                        printk(KERN_INFO "Allignment No. = %d\n", allign);
                        break;
        }
        return 0;
}

//###########################################################################################


static struct file_operations fops =
{
  .owner 	= THIS_MODULE,
  .open 	= adc_open,
  .release 	= adc_close,
  .read 	= adc_read,
  .unlocked_ioctl = adc_ioctl
};
 
//########## INITIALIZATION FUNCTION ##################
static int __init mychar_init(void) 
{
	printk(KERN_INFO "ADC registered");
	

	if (alloc_chrdev_region(&first, 0, 1, "adc") < 0)
	{
		return -1;
	}
	

    if ((cls = class_create(THIS_MODULE, "chardrv")) == NULL)
	{
		unregister_chrdev_region(first, 1);
		return -1;
	}
    if (device_create(cls, NULL, first, NULL, "adc") == NULL)
	{
		class_destroy(cls);
		unregister_chrdev_region(first, 1);
		return -1;
	}
	

    cdev_init(&adc, &fops);
    if (cdev_add(&adc, first, 1) == -1)
	{
		device_destroy(cls, first);
		class_destroy(cls);
		unregister_chrdev_region(first, 1);
		return -1;
	}
	return 0;
}
 
static void __exit mychar_exit(void) 
{
	cdev_del(&adc);
	device_destroy(cls, first);
	class_destroy(cls);
	unregister_chrdev_region(first, 1);
	printk(KERN_INFO "Bye: adc unregistered\n\n");
}
 
module_init(mychar_init);
module_exit(mychar_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Prasanna");
MODULE_DESCRIPTION("My First Driver for ADC");
