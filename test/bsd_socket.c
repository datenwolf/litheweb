#define _BSD_SOURCE

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>

#include "../picohttp.h"

int bsdsock_read(size_t count, char *buf, void *data)
{
	int fd = *((int*)data);
	
	ssize_t rb = 0;
	ssize_t r = 0;
	do {
		r = read(fd, buf+rb, count-rb);
		if( 0 < r ) {
			rb += r;
			continue;
		}
		if( !r ) {
			break;
		}

		if( EAGAIN == errno ||
		    EWOULDBLOCK == errno ) {
			usleep(100);
			continue;
		}
		return -3 + errno;
	} while( rb < count );
	return rb;
}

int bsdsock_write(size_t count, char *buf, void *data)
{
	int fd = *((int*)data);
	
	ssize_t wb = 0;
	ssize_t w = 0;
	do {
		w = write(fd, buf+wb, count-wb);
		if( 0 < w ) {
			wb += w;
			continue;
		}
		if( !w ) {
			break;
		}

		if( EAGAIN == errno ||
		    EWOULDBLOCK == errno ) {
			usleep(100);
			continue;
		}
		return -3 + errno;
	} while( wb < count );
	return wb;
}

int16_t bsdsock_getch(void *data)
{
	char ch;
	if( 1 != bsdsock_read(1, &ch, data) )
		return -1;
	return ch;
}

int bsdsock_putch(char ch, void *data)
{
	return bsdsock_write(1, &ch, data);
}

int sockfd = -1;

void bye(void)
{
	fputs("exiting\n", stderr);
	int const one = 1;
	/* allows for immediate reuse of address:port
	 * after program termination */
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	shutdown(sockfd, SHUT_RDWR);
	close(sockfd);
}

int main(int argc, char *argv[])
{
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if( -1 == sockfd ) {
		perror("socket");
		return -1;
	}
	if( atexit(bye) ) {
		return -1;
	}

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port   = htons(8000),
		.sin_addr   = 0
	};

	if( -1 == bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) ) {
		perror("bind");
		return -1;
	}

	if( -1 == listen(sockfd, 2) ) {
		perror("listen");
		return -1;
	}

	for(;;) {
		socklen_t addrlen = 0;
		int confd = accept(sockfd, (struct sockaddr*)&addr, &addrlen);
		if( -1 == confd ) {
			if( EAGAIN == errno ||
			    EWOULDBLOCK == errno ) {
				usleep(1000);
				continue;
			} else {
				perror("accept");
				return -1;
			}
		}

		struct picohttpIoOps ioops = {
			.read  = bsdsock_read,
			.write = bsdsock_write,
			.getch = bsdsock_getch,
			.putch = bsdsock_putch,
			.data = &confd
		};

		char const hellostr[] = "Hello World!\n";
		write(confd, hellostr, sizeof(hellostr));
		shutdown(confd, SHUT_RDWR);
		close(confd);
	}

	return 0;
}
