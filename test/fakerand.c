#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/random.h>
#include <fcntl.h>

int main(int argc, char **argv)
{
	int fd = open("/dev/random", O_RDWR);

	int count = 1024;

	if (ioctl(fd, RNDADDTOENTCNT, &count) != 0) {
		printf("RNDADDENTROPY failed: %s\n",strerror(errno));
		return 1;
	}

	return 0;
}
