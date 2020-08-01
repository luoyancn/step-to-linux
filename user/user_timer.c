#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char *argv[])
{
	int fd;
	int counter = 0;
	int old_counter = 0;

	fd = open("/dev/awcloud0", O_RDONLY);
	if (0 > fd) {
		printf("Cannot open the device\n");
		return -1;
	}
	while (1) {
		read(fd, &counter, sizeof(unsigned int));
		if (counter != old_counter) {
			printf("seconds after open devices:%d\n",
				counter);
			old_counter = counter;
		}
	}

	close(fd);
	return 0;
}
