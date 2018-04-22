#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/ioctl.h>
#include "dm510.h"

int main(int argc, char *argv[])
{

	int fd = open("/dev/dm510-0", O_RDWR);
	printf("dm510_0:\n");
	printf("set buffer size result: %d\n", ioctl(fd, DM510_SET_BUFFER, 2048)); //set buffer size 2048, default is 1024
	printf("set readers result: %d\n", ioctl(fd, DM510_SET_READERS, 42)); //set number of read subscribers to 42, default is 10000
	printf("print readers %d\n", ioctl(fd, DM510_PRINT_AUTHORS)); //set number of read subscribers to 42, default is 10000
	printf("\ndm510_1:\n");
	int fd2 = open("/dev/dm510-1", O_RDWR);
	printf("set buffer size result: %d\n", ioctl(fd2, DM510_SET_BUFFER, 2048)); //set buffer size 2048, default is 1024
	printf("set readers result: %d\n", ioctl(fd2, DM510_SET_READERS, 42)); //set number of read subscribers to 42, default is 10000
	printf("print readers %d\n", ioctl(fd2, DM510_PRINT_AUTHORS)); //set number of read subscribers to 42, default is 10000

	return 0;
}