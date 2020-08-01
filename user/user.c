#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#if SELECT
#include <sys/select.h>
#elif EPOLL
#include <sys/epoll.h>
#include <strings.h>
#endif

int main(int argc, char *argv[])
{
	int fd;
	int num;

	fd = open("/dev/awcloud0", O_RDWR|O_NONBLOCK);
	if (0 > fd) {
		printf("Cannot open the devices file\n");
		return -1;
	}

#if SELECT
	fd_set read_fds;
	fd_set write_fds;

	while (1) {
		FD_ZERO(&read_fds);
		FD_ZERO(&write_fds);
		FD_SET(fd, &read_fds);
		FD_SET(fd, &write_fds);
		select(fd + 1, &read_fds, &write_fds, NULL, NULL);
		if (FD_ISSET(fd, &read_fds)) {
			printf("Poll monitor: can be read\n");
		}
		if (FD_ISSET(fd, &write_fds)) {
			printf("Poll monitor: can be written\n");
		}
	}
#elif EPOLL
	struct epoll_event ev_awcloud;
	int err;
	int epoll_fd;

	epoll_fd = epoll_create(1);
	if (0 > epoll_fd) {
		perror("epoll_create()");
		return -1;
	}
	bzero(&ev_awcloud, sizeof(struct epoll_event));
	ev_awcloud.events = EPOLLIN | EPOLLPRI;
	err = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev_awcloud);
	if (0 > err) {
		perror("epoll_ctl()");
		return -1;
	}

	err = epoll_wait(epoll_fd, &ev_awcloud, 1, 10000);
	if (0 > err) {
		perror("epoll_wait()");
		return -1;
	} else if (0 == err) {
		printf("No data input with in 10 seconds\n");
	} else {
		printf("FIFO is not empty\n");
	}

	err = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &ev_awcloud);
	if (0 > err) {
		perror("epoll_ctl()");
		return -1;
	}
#elif WRITE
	char content[] = "hello world";
	ssize_t lenth = 0;

	lenth = write(fd, content, sizeof(content)/sizeof(char));
	if (0 > lenth) {
		printf("Cannot write any thing to devices\n");
		close(fd);
		return -1;
	}
#elif READ
	char buffer[1024];
	ssize_t lenth = 0;

	lenth = read(fd, buffer, 1024);
	if (0 > lenth) {
		printf("Cannot read any thing from devices\n");
		close(fd);
		return -1;
	}
	printf("Read the content from device with :%s, %d\n", buffer, lenth);
	lenth = read(fd, buffer, 1024);
	printf("Read the content from device with :%s, %d\n", buffer, lenth);
#elif CLEAR
	if (0 > ioctl(fd, 0x1)) {
		printf("Cannot clean the device content\n");
		close(fd);
		return -1;
	}
#endif
	close(fd);
	return 0;
}
