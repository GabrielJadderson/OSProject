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
#include <asm/switch_to.h> //UNCOMMENT THIS
//#include <asm-generic/switch_to.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/sched/signal.h>
#include <linux/ioctl.h>
#include "dm510.h"

/* Prototypes - this would normally go in a .h file */
static int dm510_open(struct inode*, struct file*);
static int dm510_release(struct inode*, struct file*);
static ssize_t dm510_read(struct file*, char*, size_t, loff_t*);
static ssize_t dm510_write(struct file*, const char*, size_t, loff_t*);
long dm510_ioctl(struct file* filp, unsigned int cmd, unsigned long arg);
static void dm510_clean_everything(void);
#define DEVICE_NAME "dm510_dev" /* Dev name as it appears in /proc/devices */
#define MAJOR_NUMBER 254
#define MIN_MINOR_NUMBER 0 //device number 0
#define MAX_MINOR_NUMBER 1 //device number 1

#define DEVICE_COUNT 2
/* end of what really should have been in a .h file */

/* file operations struct */
static struct file_operations dm510_fops = {
	//in linux the file operations is an abstraction that is used accessing file functionalities, here we map each of our functions to their respective function. e.g. when a developer outside the kernel opens a file for reading, he will call dm510_open and dm510_read etc.
	.owner = THIS_MODULE,
	.read = dm510_read,
	.write = dm510_write,
	.open = dm510_open,
	.release = dm510_release,
	.unlocked_ioctl = dm510_ioctl
};


//====================================== BUFFERS ======================================
//REFACTOR
typedef struct dm510_device_buffer
{
	int size;
	char* input_channel;
	struct semaphore semaphore_buffer;
} dm510_device_buffer;

//REFACOTR

int write_subscribers = 0; //when a thread wants to write it subscribes and this is incremented
int read_subscribers = 0; //when a thread wants to read it subscribes and this is incremented
int max_read_subscribers = 10000;

int BUFFER_SIZE = 4096;

//====================================== BUFFERS ======================================

//====================================== DEVICES ======================================
typedef struct dm510_device
{
	//this struct represents a single device instance, here we'll store our variables that are relevant to each device.
	int device_mode; //mode 1 is reading mode 2 is writing

	int device_id; //the id of the device
	struct cdev cdev; //the linux character driver

	wait_queue_head_t inq, outq; /* read and write queues */

	dm510_device_buffer **rp, **wp; /* where to read, where to write */
	int nreaders, nwriters; /* number of openings for r/w */
	struct fasync_struct* async_queue; /* asynchronous readers */
	struct mutex mutex; /* mutual exclusion semaphore */

	struct mutex wrlock; //we only use mutexes as UML is single core environment. this is write lock
	struct mutex rlock; // this is a read locka
	struct mutex metadata_lock;
	int writers; //used to keep tracks of current reads
	int readers; //used to keep tracks of current reads
	int max_readers; //arbitrary max amount of readers. ioctl should offer to increase size

} dm510_device;

static dm510_device dm510_devices[DEVICE_COUNT];
//our two devices, was unsure if whether to allocate it with kmalloc or on the stack, since the kernel has limited amount of memory and is shared by a lot of programs. I believe it's still 1 gb of memory, 128 mb for vmalloc and lowmem has rest, but lowmem gives memory in chunks and in powers of two, so you don't get what you exactly want, but a power of two of the amount you want. kmalloc and kzmalloc uses lowmem

static dm510_device_buffer* buffer_0; //buffer 1
static dm510_device_buffer* buffer_1; // buffer 2


dev_t initial_device;
//====================================== DEVICES ======================================

/*
* Set up a cdev entry.
*/
static void dm510_setup_cdev(struct dm510_device* dev, int index)
{
	int err, devno = initial_device + index;

	cdev_init(&dev->cdev, &dm510_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add(&dev->cdev, devno, 1);
	if (err)
		printk(KERN_NOTICE "Error %d dm510 device%d", err, index);
}

//REFACTOR
static void dm510_init_buffers(void)
{
	//allocate both buffers.
	buffer_0 = (dm510_device_buffer*)kmalloc(sizeof(dm510_device_buffer), GFP_KERNEL);
	buffer_1 = (dm510_device_buffer*)kmalloc(sizeof(dm510_device_buffer), GFP_KERNEL);

	//assign the default buffer size of 4096
	buffer_0->size = 0; //current size
	buffer_1->size = 0; //current size

	//allocate their input_channels
	buffer_0->input_channel = (char *)kmalloc(sizeof(char) * BUFFER_SIZE, GFP_KERNEL);
	buffer_1->input_channel = (char *)kmalloc(sizeof(char) * BUFFER_SIZE, GFP_KERNEL);

	//init semaphores
	sema_init(&(buffer_0->semaphore_buffer), 1); //value of 1 indicates that only 1 active thread/program can access it.
	sema_init(&(buffer_1->semaphore_buffer), 1);

	printk(KERN_INFO "DM510: BUFFERS INITIALIZED\n");

	//wait_queue_head_t from book/the scull driver chapter 5


	//init_MUTEX(&buffer2->sem);
	//init_waitqueue_head(&buffer2->read_wait_queue);
	//init_waitqueue_head(&buffer2->write_wait_queue);
}

//REFACOTR


/*
 * Initialize the pipe devs; return how many we did.
 */
int dm510_p_init(dev_t firstdev)
{
	int i;

	initial_device = firstdev;

	for (i = 0; i < DEVICE_COUNT; i++)
	{
		init_waitqueue_head(&(dm510_devices[i].inq));
		init_waitqueue_head(&(dm510_devices[i].outq));
		mutex_init(&dm510_devices[i].mutex);
		dm510_setup_cdev(dm510_devices + i, i);
	}
	return 0;
}


/* called when module is loaded */
int dm510_init_module(void)
{
	dev_t device_first;

	//MKDEV(MAJOR_NUMBER, MIN_MINOR_NUMBER);
	int return_result = alloc_chrdev_region(&device_first, MIN_MINOR_NUMBER, DEVICE_COUNT, DEVICE_NAME);
	//try to allocate the devices, explained in detail in the report.
	if (return_result < 0)
	{
		printk(KERN_ALERT "DM510: Failed to allocate character devices.\n");
		return -1;
	}
	else
	{
		initial_device = device_first; //we need to deallocate it later on so we keep track of it.
	}

	printk(KERN_INFO "DM510: This process has been assigned \"%s\" (pid %i)\n", current->comm, current->pid);


	//setup the buffers
	dm510_init_buffers();

	dm510_p_init(device_first);


	printk(KERN_INFO "DM510: Hello from your device!\n");


	return 0;
}

/* Called when module is unloaded */
void dm510_cleanup_module(void)
{
	/* clean up code belongs here */
	dm510_clean_everything();
	printk(KERN_INFO "DM510: Module unloaded.\n");
}


/* Called when a process tries to open the device file */
static int dm510_open(struct inode* inode, struct file* filp)
{
	int min = iminor(filp->f_inode);
	/* device claiming code belongs here */
	printk(KERN_INFO "DM510: OPEN CALLED ON: %s%d\n", DEVICE_NAME, min);

	struct dm510_device* dev;

	dev = container_of(inode->i_cdev, struct dm510_device, cdev);
	filp->private_data = dev;

	mutex_lock(&dev->mutex);


	if (filp->f_mode & FMODE_READ)
	{
		dev->device_mode = 1; //1 for reading
		read_subscribers++;
		dev->nreaders++;
	}
	if (filp->f_mode & FMODE_WRITE)
	{
		dev->device_mode = 2; //2 for writing
		dev->nwriters++;
		write_subscribers++;
	}

	if (min == MIN_MINOR_NUMBER) //read from buffer 0 write to buffer 1; just like in the project description.
	{
		dev->rp = &(buffer_0);
		dev->wp = &(buffer_1);
	}
	else if (min == MAX_MINOR_NUMBER) //read from buffer 1 write to buffer 0;
	{
		dev->rp = &(buffer_1);
		dev->wp = &(buffer_0);
	}
	mutex_unlock(&dev->mutex);

	return 0;
}


/* Called when a process closes the device file. */
static int dm510_release(struct inode* inode, struct file* filp)
{
	struct dm510_device* dev = filp->private_data;

	/* device release code belongs here */
	printk(KERN_INFO "DM510: dm510_release\n");
	return 0;
}

static char* dm510_swap_backbuffer(char* A, char* B)
{
	char* temp = A;
	A = B;
	return temp;
}

static char* dm510_copy_backbuffer(char* pipe, int limit)
{
	//*read = strlen(in);
	// +1 because of '\0' at the end
	//char* copy = kmalloc(*read + 1, GFP_KERNEL);
	//strcpy(copy, in);
	char* back = kmalloc(sizeof(char) * BUFFER_SIZE, GFP_KERNEL);
	memcpy(back, pipe + limit, BUFFER_SIZE - limit);
	return back;
}

static void dm510_lock(dm510_device_buffer *buffer) //used for locking a unifying lock function is better to debug.
{
	//mutex_lock(&dev->mutex);
	down(&buffer->semaphore_buffer);
}

static void dm510_unlock(dm510_device_buffer *buffer) //used for unlocking a unifying unlock function is better to debug.
{
	//mutex_unlock(&dev->mutex);
	//wake_up_interruptible(&dev->outq);
	up(&buffer->semaphore_buffer); // Release the lock
}

/* Called when a process, which already opened the dev file, attempts to read from it. */
static ssize_t dm510_read(struct file* filp,
	char* buf, /* The buffer to fill with data     */
	size_t count, /* The max number of bytes to read  */
	loff_t* f_pos) /* The offset in the file           */
{
	if (access_ok(VERIFY_READ, buf, count))
	{
		struct dm510_device* dev = filp->private_data;
		dm510_device_buffer* buffer = *(dev->rp);
		int limit = min(count, &buffer->size);
		char* backbuffer = dm510_copy_backbuffer(buffer->input_channel, limit);
		char* outbuffer = dm510_swap_backbuffer(buffer->input_channel, backbuffer);
		int bytes = copy_to_user(buf, outbuffer, count);
		kfree(outbuffer);
		return count - bytes; //return number of bytes read
	}
	else return 0;
}

/* Called when a process writes to dev file */
static ssize_t dm510_write(struct file* filp,
	const char* buf, /* The buffer to get data from      */
	size_t count, /* The max number of bytes to write */
	loff_t* f_pos) /* The offset in the file           */
{
	if (access_ok(VERIFY_WRITE, buf, count))
	{
		struct dm510_device* dev = filp->private_data;
		dm510_device_buffer* buffer = *(dev->wp);
		if (buffer->size != BUFFER_SIZE)
		{
			dm510_lock(buffer);
			int limit = min(count, BUFFER_SIZE - buffer->size);
			int bytes = copy_from_user(buffer->input_channel + buffer->size, buf, limit);
			buffer->size += limit - bytes;
			dm510_unlock(buffer);
			return limit - bytes; //return number of bytes written
		}
	}
	return 1;
}

/* called by system call icotl */
long dm510_ioctl(
	struct file* filp,
	unsigned int cmd, /* command passed from the user */
	unsigned long arg) /* argument of the command */
{
	/* ioctl code belongs here */

	switch (cmd)
	{
	case DM510_SET_BUFFER:
		if (arg > 0)
		{
			BUFFER_SIZE = arg;
			printk(KERN_INFO "DM510: Buffer size has been updated to %d\n", BUFFER_SIZE);
			return 61;
		}
		return 65;
		break;

	case DM510_SET_READERS:
		if (arg > 0)
		{
			max_read_subscribers = arg;
			printk(KERN_INFO "DM510: Maximum amount of readers set to %d\n", max_read_subscribers);
			return 62;
		}
		return 65;
		break;

	case DM510_PRINT_AUTHORS:
		printk(KERN_INFO "Gabriel Jadderson (gajad16) & Siver Al-Khayat (Sialk16) & Jens Kofoed Laurberg (jlaur16)\n");
		break;

	default: break;
	}

	return 69; //has to be changed
}

static void dm510_clean_buffers(void)
{
	kfree(buffer_0->input_channel);
	kfree(buffer_1->input_channel);

	kfree(buffer_0);
	kfree(buffer_1);
	printk(KERN_WARNING "DM510: BUFFERS CLEANED\n");
}

static void dm510_clean_devices(void)
{
	int i = 0;
	for (i = 0; i < DEVICE_COUNT; i++)
		cdev_del(&(dm510_devices[i].cdev));
	unregister_chrdev_region(initial_device, DEVICE_COUNT);
}

static void dm510_clean_everything(void)
//should probably lock everything before calling this, so that we can clean without anyone adding more/trying to acces the memory after we've freed the memory. For this a global lock is maybe required.
{
	dm510_clean_buffers();
	dm510_clean_devices();
}

module_init(dm510_init_module);
module_exit(dm510_cleanup_module);

MODULE_AUTHOR("Gabriel Jadderson (gajad16) & Siver Al-Khayat (Sialk16) & Jens Kofoed Laurberg (jlaur16)");
MODULE_LICENSE("GPL");