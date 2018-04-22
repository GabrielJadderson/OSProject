#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
	int fd = open("/dev/dm510-0", O_RDWR);
	perror("w open");
	close(fd);

	int fd2 = open("/dev/dm510-1", O_RDWR);
	perror("r open");
	close(fd2);
}