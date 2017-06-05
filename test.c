
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>     

#define WG_CMD_MAGIC 		'x'
#define WG_26_MODE			_IO(WG_CMD_MAGIC, 0x01)
#define WG_34_MODE			_IO(WG_CMD_MAGIC, 0x02)
#define WG_66_MODE			_IO(WG_CMD_MAGIC, 0x03)
#define WG_UNKNOWN_MODE 	_IO(WG_CMD_MAGIC, 0x07)


int main(int argc, char **argv)
{
	int ret;
	int fd;
	unsigned char test_id[5] = {0x1C,0xBE,0x99,0x1A,0x14};  //123456789012
	unsigned long long id = 123456789012ULL;
	unsigned long long read_id = 0;
	fd_set readset;
	int maxfd;
	FD_ZERO(&readset);

	fd = open("/dev/wiegand", O_RDWR);
	if (fd < 0)
	{
		printf("can't open!\n");
		return -1;
	}

	FD_SET(fd, &readset);

	if(!fork()){
		while(1){
			int err = select(fd+1, &readset, NULL, NULL, NULL);
			if (err < 0) {
				printf("select error\n");
			} else if (err == 0) {
				continue;
			}

			if (FD_ISSET(fd, &readset)) {
				ret = read(fd, &read_id, sizeof(read_id));
				printf("read_id = %llu\n",read_id);
				read_id = 0;
			}
		}
	}
	while (1)
	{
		ret = ioctl(fd, WG_26_MODE, test_id);
		sleep(10);
		ret = ioctl(fd, WG_34_MODE, test_id);
		sleep(10);
		ret = ioctl(fd, WG_66_MODE, &id);
		sleep(10);
	}
	
	return 0;
}


