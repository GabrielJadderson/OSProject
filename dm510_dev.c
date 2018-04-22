/* Prototype module for second mandatory DM510 assignment */
#ifndef __KERNEL__
#  define __KERNEL__
#endif
#ifndef MODULE
#  define MODULE
#endif

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/wait.h>
/* #include <asm/uaccess.h> */
#include <linux/uaccess.h>
#include <linux/semaphore.h>
/* #include <asm/system.h> */
//#include <asm/switch_to.h> //UNCOMMENT THIS
#include <asm-generic/switch_to.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/sched/signal.h>
#include <linux/ioctl.h>

/* Prototypes - this would normally go in a .h file */
static int dm510_open(struct inode*, struct file*);
static int dm510_release(struct inode*, struct file*);
static ssize_t dm510_read(struct file*, char*, size_t, loff_t*);
static ssize_t dm510_write(struct file*, const char*, size_t, loff_t*);
long dm510_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static void dm510_clean_everything(void);
#define DEVICE_NAME "dm510_dev" /* Dev name as it appears in /proc/devices */
#define MAJOR_NUMBER 254
#define MIN_MINOR_NUMBER 0 //device number 0
#define MAX_MINOR_NUMBER 1 //device number 1

#define DEVICE_COUNT 2
/* end of what really should have been in a .h file */

/* file operations struct */
static struct file_operations dm510_fops = { //in linux the file operations is an abstraction that is used accessing file functionalities, here we map each of our functions to their respective function. e.g. when a developer outside the kernel opens a file for reading, he will call dm510_open and dm510_read etc.
	.owner = THIS_MODULE,
	.read = dm510_read,
	.write = dm510_write,
	.open = dm510_open,
	.release = dm510_release,
	.unlocked_ioctl = dm510_ioctl
};

//====================================== DEVICES ======================================
typedef struct dm510_device { //this struct represents a single device instance, here we'll store our variables that are relevant to each device.
	int device_id; //the id of the device
	int buffer_mode; //indicates whether we are reading or writing to a buffer. 0 for reading 1 for writing and 2 for both.
	struct cdev cdev; //the linux character driver

} dm510_device_t;

dm510_device_t devices[DEVICE_COUNT]; //our two devices, was unsure if whether to allocate it with kmalloc or on the stack, since the kernel has limited amount of memory and is shared by a lot of programs. I believe it's still 1 gb of memory, 128 mb for vmalloc and lowmem has rest, but lowmem gives memory in chunks and in powers of two, so you don't get what you exactly want, but a power of two of the amount you want. kmalloc and kzmalloc uses lowmem

dev_t initial_device;
//====================================== DEVICES ======================================


//====================================== BUFFERS ======================================
//REFACTOR
typedef struct dm510_device_buffer {
	int size;
	char *input_channel;
	dm510_device_t *current_device;
} dm510_device_buffer_t;
//REFACOTR

struct semaphore semaphore_buffer_0; //a semaphore for our first buffer
struct semaphore semaphore_buffer_1; //a semaphore for out second buffer

dm510_device_buffer_t *dm510_buffer_0; //buffer 1
dm510_device_buffer_t *dm510_buffer_1; // buffer 2

int write_subscribers = 0; //when a thread wants to write it subscribes and this is incremented
int read_subscribers = 0; //when a thread wants to read it subscribes and this is incremented

int BUFFER_SIZE = 1024;

//====================================== BUFFERS ======================================

//REMOVEEEEEEEEEEEEEEE
static void setup_cdev_entry(struct dm510_device *dev, int index) //what is index value used for exactly here?
{
	int result; //result is used for checking if it succeeds. As it returns -1 upon failure, it can be used to check if we
	int devno;   //succeeded in registering the devices
	result = initial_device + index;
	devno = initial_device + index;

	cdev_init(&dev->cdev, &dm510_fops);
	dev->cdev.owner = THIS_MODULE;
	result = cdev_add(&dev->cdev, devno, 1);
	if (result < 0) {
		printk(KERN_NOTICE "Error %d dm510 device%d", result, index);
	}
}
//REMOVEEEEEEEEEEEEEEE

//REFACTOR
static void dm510_init_buffers(void)
{


	//allocate both buffers.
	dm510_buffer_0 = (dm510_device_buffer_t*)kmalloc(sizeof(dm510_device_buffer_t), GFP_KERNEL);
	dm510_buffer_1 = (dm510_device_buffer_t*)kmalloc(sizeof(dm510_device_buffer_t), GFP_KERNEL);

	//assign the default buffer size of 1024
	dm510_buffer_0->size = BUFFER_SIZE;
	dm510_buffer_1->size = BUFFER_SIZE;

	//allocate their input_channels
	dm510_buffer_0->input_channel = (char *)kmalloc(sizeof(char) * BUFFER_SIZE, GFP_KERNEL);
	dm510_buffer_1->input_channel = (char *)kmalloc(sizeof(char) * BUFFER_SIZE, GFP_KERNEL);

	//init semaphores
	sema_init(&semaphore_buffer_0, 1); //value of 1 indicates that only 1 active thread/program can access it.
	sema_init(&semaphore_buffer_1, 1);

	printk(KERN_INFO "DM510: BUFFERS INITIALIZED\n");

	//wait_queue_head_t from book/the scull driver chapter 5


	/*
	buffer1 = (struct dm510_buffer *) kmalloc(sizeof(struct dm510_buffer), GFP_KERNEL);
	if (!buffer1) // Allocation failed
	{
		return -ENOMEM;
	}

	buffer1->buffer = (char *)kmalloc(sizeof(char) * BUFFER_SIZE, GFP_KERNEL);
	if (!buffer1->buffer) // Allocation failed
	{
		kfree(buffer1);
		return -ENOMEM;
	}
	buffer1->size = BUFFER_SIZE;
	buffer1->index = 0;

	init_MUTEX(&buffer1->sem);
	init_waitqueue_head(&buffer1->read_wait_queue);
	init_waitqueue_head(&buffer1->write_wait_queue);


	buffer2 = (struct dm510_buffer *) kmalloc(sizeof(struct dm510_buffer), GFP_KERNEL);
	if (!buffer2) // Allocation failed
	{
		kfree(buffer1->buffer);
		kfree(buffer1);
		return -ENOMEM;
	}
	buffer2->buffer = (char *)kmalloc(sizeof(char) * BUFFER_SIZE, GFP_KERNEL);
	if (!buffer2->buffer) // Allocation failed
	{
		kfree(buffer1->buffer);
		kfree(buffer1);
		kfree(buffer2);
		return -ENOMEM;
	}
	buffer2->size = BUFFER_SIZE;
	buffer2->index = 0;

	init_MUTEX(&buffer2->sem);
	init_waitqueue_head(&buffer2->read_wait_queue);
	init_waitqueue_head(&buffer2->write_wait_queue);
	*/

}
//REFACOTR


/* called when module is loaded */
int dm510_init_module(void) {

	/* initialization code belongs here */
	dev_t device_first = 0;

	//MKDEV(MAJOR_NUMBER, MIN_MINOR_NUMBER);
	int return_result = alloc_chrdev_region(&device_first, MIN_MINOR_NUMBER, DEVICE_COUNT, DEVICE_NAME); //try to allocate the devices, explained in detail in the report.
	if (return_result < 0) {
		printk(KERN_ALERT "DM510: Failed to allocate character devices.\n");
		return -1;
	}
	else {
		printk(KERN_INFO "DM510: Sucessfully allocated character devices.\n");
		initial_device = device_first; //we need to deallocate it later on so we keep track of it.
	}



	printk(KERN_INFO "DM510: Hello from your device!\n");
	//test fra bogen
	printk(KERN_INFO "DM510: The process is \"%s\" (pid %i)\n", current->comm, current->pid);


	//setup the buffers
	dm510_init_buffers();







	return 0;
}

/* Called when module is unloaded */
void dm510_cleanup_module(void) {

	/* clean up code belongs here */
	dm510_clean_everything();
	printk(KERN_INFO "DM510: Module unloaded.\n");
}


/* Called when a process tries to open the device file */
static int dm510_open(struct inode *inode, struct file *filp) {

	/* device claiming code belongs here */

	return 0;
}


/* Called when a process closes the device file. */
static int dm510_release(struct inode *inode, struct file *filp) {

	/* device release code belongs here */

	return 0;
}


/* Called when a process, which already opened the dev file, attempts to read from it. */
static ssize_t dm510_read(struct file *filp,
	char *buf,      /* The buffer to fill with data     */
	size_t count,   /* The max number of bytes to read  */
	loff_t *f_pos)  /* The offset in the file           */
{

	/* read code belongs here */

	return 0; //return number of bytes read
}


/* Called when a process writes to dev file */
static ssize_t dm510_write(struct file *filp,
	const char *buf,/* The buffer to get data from      */
	size_t count,   /* The max number of bytes to write */
	loff_t *f_pos)  /* The offset in the file           */
{

	/* write code belongs here */

	return 0; //return number of bytes written
}

/* called by system call icotl */
long dm510_ioctl(
	struct file *filp,
	unsigned int cmd,   /* command passed from the user */
	unsigned long arg) /* argument of the command */
{
	/* ioctl code belongs here */
	printk(KERN_INFO "DM510: ioctl called.\n");

	/*
	switch (cmd)
	{
	case PRINT_AUTHORS:
		printk(KERN_INFO "DM510: Gabriel Jadderson (gajad16) & Siver Al-Khayat (Sialk16) & Jens Kofoed Laurberg (jlaur16)\n");
		break;
	case PRINT_DEVICES:
		for (size_t i = 0; i < DEVICE_COUNT; i++) {
			printk(KERN_INFO "DM510: DEVICE INFORMATION\nDEVICE_NAME: %s_%d\nDEVICE_BUFFER_MODE: %d\n", DEVICE_NAME, devices[i].device_id, devices[i].buffer_mode);
		}
		break;
	case PRINT_PID:
		printk(KERN_INFO "DM510: (pid %i)\n", current->pid);
		break;
	default: break;
	}
	*/

	return 0; //has to be changed
}

static void dm510_clean_buffers(void)
{
	kfree(dm510_buffer_0->input_channel);
	kfree(dm510_buffer_1->input_channel);

	kfree(dm510_buffer_0);
	kfree(dm510_buffer_1);
	printk(KERN_WARNING "DM510: BUFFERS CLEANED\n");
}

static void dm510_clean_devices(void)
{

}

static void dm510_clean_everything(void) //should probably lock everything before calling this, so that we can clean without anyone adding more/trying to acces the memory after we've freed the memory. For this a global lock is maybe required.
{
	dm510_clean_buffers();
	dm510_clean_devices();
}

module_init(dm510_init_module);
module_exit(dm510_cleanup_module);

MODULE_AUTHOR("Gabriel Jadderson (gajad16) & Siver Al-Khayat (Sialk16) & Jens Kofoed Laurberg (jlaur16)");
MODULE_LICENSE("GPL");