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
		printk(KERN_INFO "DM510: Sucessfully allocated character devices.\n");
		initial_device = device_first; //we need to deallocate it later on so we keep track of it.
	}


	//test fra bogen
	//printk(KERN_INFO "DM510: The process is \"%s\" (pid %i)\n", current->comm, current->pid);

	//printk(KERN_WARNING "DM510: \n%d\n %d\n%d\n", RESIZE_BUFFER, MAX_READERS, PRINT_AUTHORS);


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


	//REFACTOR
	if (dev->device_mode == 1) //read, much faster than filp->f_mode
	{
		dev->readers = 0;
	}
	else if (dev->device_mode == 2) //write
	{
		dev->writers = 0;
	}


	/* device release code belongs here */
	printk(KERN_INFO "DM510: dm510_release\n");
	return 0;
}

static char* dm510_copy_backbuffer(char* in, int max_size, int* read)
{
	*read = strlen(in);
	// +1 because of '\0' at the end
	char* copy = kmalloc(*read + 1, GFP_KERNEL);
	strcpy(copy, in);
	return copy;
}

/*
ssize_t helloworld_driver_read(struct file * filep, char *buff, size_t count, loff_t * offp)
{
	int device_data_length;
	device_data_length = strlen(helloworld_driver_data);

	// No more data to read.
	if (*offp >= device_data_length)
		return 0;

	//We copy either the full data or the number of bytes asked from userspace, depending on whatever is the smallest.

	if ((count + *offp) > device_data_length)
		count = device_data_length;

	// function to copy kernel space buffer to user space
	if (copy_to_user(buff, helloworld_driver_data, count) != 0) {
		printk(KERN_ALERT "Kernel Space to User Space copy failure");
		return -EFAULT;
	}

	// Increment the offset to the number of bytes read
	*offp += count;

	// Return the number of bytes copied to the user space
	return count;
}

ssize_t helloworld_driver_write(struct file * filep, const char *buff, size_t count, loff_t * offp)
{
	// Free the previouosly stored data
	if (helloworld_driver_data)
		kfree(helloworld_driver_data);

	// Allocate new memory for holding the new data
	helloworld_driver_data = kmalloc((count * (sizeof(char *))), GFP_KERNEL);

	// function to copy user space buffer to kernel space
	if (copy_from_user(helloworld_driver_data, buff, count) != 0) {
		printk(KERN_ALERT "User Space to Kernel Space copy failure");
		return -EFAULT;
	}

	// Since our device is an array, we need to do this to terminate the string. Last character will get overwritten by a \0. Incase of an actual file, we will probably update the file size, IIUC.

	helloworld_driver_data[count] = '\0';

	// Return the number of bytes actually written.
	return count;
}

*/


/* Called when a process, which already opened the dev file, attempts to read from it. */
static ssize_t dm510_read(struct file* filp,
	char* buf, /* The buffer to fill with data     */
	size_t count, /* The max number of bytes to read  */
	loff_t* f_pos) /* The offset in the file           */
{
	if (access_ok(VERIFY_READ, buf, count))
	{
		//printk(KERN_ALERT "VERIFIED READ");

		if (count <= *f_pos)
		{
			return 0;
		}



		/* read code belongs here */
		//printk(KERN_INFO "DM510: dm510_read\n");

		struct dm510_device* dev = filp->private_data;
		//printk(KERN_INFO "reached here 1\n");
		dm510_device_buffer* buffer = *(dev->rp);
		//printk(KERN_INFO "reached here 2\n");
		int max_read = min(count, &buffer->size);

		char* new_buffer = kmalloc(sizeof(char) * BUFFER_SIZE, GFP_KERNEL);
		//printk(KERN_INFO "reached here 3\n");
		memcpy(new_buffer, buffer->input_channel + max_read, BUFFER_SIZE - max_read);
		//printk(KERN_INFO "reached here 4\n");
		char* old_buffer = buffer->input_channel;
		//printk(KERN_INFO "reached here 5\n");
		buffer->input_channel = new_buffer;
		//printk(KERN_INFO "reached here 6\n");
		up(&buffer->semaphore_buffer);
		//printk(KERN_INFO "reached here 7\n");
		wake_up_interruptible(&dev->inq);
		//printk(KERN_INFO "reached here 8\n");
		int copy_res = copy_to_user(buf, old_buffer, count);
		//printk(KERN_INFO "reached here 9\n");
		kfree(old_buffer);
		//printk(KERN_INFO "reached here 10\n");
		return count - copy_res; //return number of bytes read
	}
	else return 13;

	/*
	//mutex_lock(&dev->mutex);

	int read;

	char* copy = dm510_copy_backbuffer(buffer->input_channel, count, &read);

	if (count <= *f_pos)
	{
		return 0;
	}

	if ((count + *f_pos) > read)
		count = read;

	//function to copy kernel space buffer to user space
	if (copy_to_user(buf, copy, count) != 0)
	{
		printk(KERN_ALERT "Kernel Space to User Space copy failure");
		return -EFAULT;
	}

	// Increment the offset to the number of bytes read
	*f_pos += count;

	kfree(copy); // at the end, free it again.
	//mutex_unlock(&dev->mutex);

	// finally, awake any writer
	wake_up_interruptible(&dev->outq);

	// Return the number of bytes copied to the user space
	return count;
	*/
}


/* Called when a process writes to dev file */
static ssize_t dm510_write(struct file* filp,
	const char* buf, /* The buffer to get data from      */
	size_t count, /* The max number of bytes to write */
	loff_t* f_pos) /* The offset in the file           */
{
	if (access_ok(VERIFY_WRITE, buf, count))
	{
		//printk(KERN_ALERT "VERIFIED WRITE");

		if (count <= *f_pos)
		{
			return 0;
		}

		struct dm510_device* dev = filp->private_data;

		dm510_device_buffer* buffer = *(dev->wp);

		down_interruptible(&buffer->semaphore_buffer);

		if (buffer->size == BUFFER_SIZE)
		{
			up(&buffer->semaphore_buffer);
		}
		else
		{
			int max_write = min(count, BUFFER_SIZE - buffer->size);
			int copy_res = copy_from_user(buffer->input_channel + buffer->size, buf, max_write);

			// copy_res is the number of chars not copied to user
			buffer->size += max_write - copy_res;

			up(&buffer->semaphore_buffer); // Release the lock
			wake_up_interruptible(&dev->outq);
			return max_write - copy_res; //return number of bytes written
		}
	}
	return 1;

	/*
	struct dm510_device *dev = filp->private_data;
	int result;

	if (mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;

	// ok, space is there, accept something
	count = min(count, 0);

	if (copy_from_user(dev->wp, buf, count)) {
		mutex_unlock(&dev->mutex);
		return -EFAULT;
	}

	dev->wp += count;
	if (dev->wp == dev->end)
		dev->wp = dev->buffer;
	mutex_unlock(&dev->mutex);

	// finally, awake any reader
	wake_up_interruptible(&dev->inq);


	if (dev->async_queue)
		kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
	return count;


	printk(KERN_INFO "DM510: dm510_write\n");
	return 0; //return number of bytes written
	*/
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