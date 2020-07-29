#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>

int main(int argc, char * argv[])
{
	struct sockaddr_nl nls;
	struct pollfd pfd;
	char buffer[512];

	memset(&nls, 0, sizeof(struct sockaddr_nl));
	nls.nl_family = AF_NETLINK;
	nls.nl_pid = getpid();
	nls.nl_groups = -1;

	pfd.events = POLLIN;
	pfd.fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);

	if (-1 == pfd.fd) {
		printf("ERROR: not root\n");
		return -1;
	}

	if (bind(pfd.fd, (void*)&nls, sizeof(struct sockaddr_nl))) {
		printf("ERROR: bind failed\n");
		return -1;
	}

	while ( -1 != poll(&pfd, 1, -1)) {
		int i, len = recv(pfd.fd, buffer, 1024, MSG_DONTWAIT);
		if (-1 == len) {
			printf("Recv Nothing\n");
			return -1;
		}

		i = 0;
		while (i < len) {
			printf("%s\n", buffer+i);
			i += strlen(buffer + i) + 1;
		}
	}

	printf("POLL \n");

	return 0;
}
