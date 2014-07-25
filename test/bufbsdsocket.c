/*
    picoweb / litheweb -- a web server and application framework
                          for resource constraint systems.

    Copyright (C) 2012 - 2014 Wolfgang Draxinger

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#define _BSD_SOURCE

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/ip.h>
#include <poll.h>

#include "../picohttp.h"

#define SENDBUF_LEN 256

struct bufbsdsockData {
	char * recvbuf;
	size_t recvbuf_len;
	size_t recvbuf_pos;
	char   sendbuf[SENDBUF_LEN];
	size_t sendbuf_pos;
	int    fd;
};

int bufbsdsock_read(size_t count, char *buf, void *data_)
{
	struct bufbsdsockData *data = data_;

	ssize_t rb = 0;
	ssize_t r = 0;
	do {
		size_t len = 0;

		if( !data->recvbuf || 
		    data->recvbuf_pos >= data->recvbuf_len ) {
			if( data->recvbuf )
				free( data->recvbuf );
			data->recvbuf_len = 0;
			data->recvbuf_pos = 0;

			int avail = 0;
			do {
				struct pollfd pfd = {
					.fd = data->fd,
					.events = POLLIN | POLLPRI,
					.revents = 0
				};

				int const pret = poll(&pfd, 1, -1);
				if( 0 >= pret ) {
					return -1;
				}

				assert(pfd.revents & (POLLIN | POLLPRI));

				if( -1 == ioctl(data->fd, FIONREAD, &avail) ) {
					perror("ioctl(FIONREAD)");
					return -1;
				}
			} while( !avail );

			data->recvbuf = malloc( avail);

			int r;
			while( 0 > (r = read(data->fd, data->recvbuf, avail)) ) {
				if( EINTR == errno )
					continue;

				if( EAGAIN == errno ||
				    EWOULDBLOCK == errno ) {
					usleep(200);
					continue;
				}

				return -1;
			} 
			data->recvbuf_len += r;
		}

		len = data->recvbuf_len - data->recvbuf_pos;
		if( len > count )
			len = count;

		rb += len;
	} while( rb < count );
	return rb;
}

int bufbsdsock_write(size_t count, char const *buf, void *data)
{
	int fd = *((int*)data);
	
	ssize_t wb = 0;
	ssize_t w = 0;
	do {
	} while( wb < count );
	return wb;
}

int16_t bufbsdsock_getch(void *data)
{
	char ch;
	if( 1 != bufbsdsock_read(1, &ch, data) )
		return -1;
	return ch;
}

int bufbsdsock_putch(char ch, void *data)
{
	return bufbsdsock_write(1, &ch, data);
}

int bufbsdsock_flush(void *data)
{
	return 0;
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

void rhRoot(struct picohttpRequest *req)
{
	fprintf(stderr, "handling request /%s\n", req->urltail);

	req->response.contenttype = "text/html";

	char http_test[] =
"<html><head><title>handling request /</title></head><body>\n"
"<a href=\"/test\">/test</a>"
"<form action=\"/upload\" enctype=\"multipart/form-data\" method=\"post\">"
"<label for=\"file\">File: </label><input type=\"file\" name=\"file\"></input>"
"<input type=\"submit\" value=\"Upload\"></input>"
"</form>"
"</body></html>\n";

	picohttpResponseWrite(req, sizeof(http_test)-1, http_test);
}

void rhTest(struct picohttpRequest *req)
{
	fprintf(stderr, "handling request /test%s\n", req->urltail);

	char http_test[] = "handling request /test";
	picohttpResponseWrite(req, sizeof(http_test)-1, http_test);
	if(req->urltail) {
		picohttpResponseWrite(req, strlen(req->urltail), req->urltail);
	}
}

void rhUpload(struct picohttpRequest *req)
{
	fprintf(stderr, "handling request /upload%s\n", req->urltail);

	char http_test[] = "handling request /upload";
	picohttpResponseWrite(req, sizeof(http_test)-1, http_test);
	if(req->urltail) {
		picohttpResponseWrite(req, strlen(req->urltail), req->urltail);
	}
}

static uint8_t const favicon_ico[] = {
0x00,0x00,0x01,0x00,0x01,0x00,0x10,0x10,0x10,0x00,0x01,0x00,0x04,0x00,0x28,0x01,
0x00,0x00,0x16,0x00,0x00,0x00,0x28,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x20,0x00,
0x00,0x00,0x01,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x09,
0x06,0x00,0x21,0x23,0x22,0x00,0x48,0x4a,0x48,0x00,0x70,0x73,0x71,0x00,0x8d,0x90,
0x8e,0x00,0xb4,0xb7,0xb5,0x00,0xd8,0xdc,0xda,0x00,0xfc,0xff,0xfd,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x77,0x77,
0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x76,0x06,0x76,0x06,0x77,0x77,0x77,
0x77,0x74,0x04,0x74,0x04,0x77,0x77,0x77,0x77,0x72,0x42,0x72,0x42,0x77,0x77,0x77,
0x77,0x71,0x70,0x70,0x71,0x77,0x70,0x77,0x77,0x52,0x72,0x32,0x72,0x57,0x70,0x77,
0x77,0x34,0x74,0x04,0x74,0x37,0x70,0x77,0x77,0x16,0x76,0x06,0x76,0x17,0x70,0x40,
0x03,0x77,0x77,0x77,0x77,0x77,0x70,0x26,0x62,0x37,0x77,0x77,0x77,0x77,0x70,0x67,
0x76,0x17,0x77,0x77,0x77,0x77,0x70,0x77,0x77,0x07,0x77,0x77,0x77,0x77,0x70,0x67,
0x76,0x17,0x77,0x77,0x77,0x77,0x70,0x26,0x62,0x37,0x77,0x77,0x77,0x77,0x70,0x40,
0x03,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x77,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

void rhFavicon(struct picohttpRequest *req)
{
	fprintf(stderr, "handling request /favicon.ico\n");

	req->response.contenttype = "image/x-icon";
	req->response.contentlength = sizeof(favicon_ico);
	picohttpResponseWrite(req, sizeof(favicon_ico), favicon_ico);
}

int main(int argc, char *argv[])
{
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if( -1 == sockfd ) {
		perror("socket");
		return -1;
	}
#if 0
	if( atexit(bye) ) {
		return -1;
	}
#endif

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port   = htons(8000),
		.sin_addr   = 0
	};

	int const one = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
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
			.read  = bufbsdsock_read,
			.write = bufbsdsock_write,
			.getch = bufbsdsock_getch,
			.putch = bufbsdsock_putch,
			.flush = bufbsdsock_flush,
			.data = &confd
		};

		struct picohttpURLRoute routes[] = {
			{"/favicon.ico|", 0, rhFavicon, 0, PICOHTTP_METHOD_GET},
			{ "/test", 0, rhTest, 16, PICOHTTP_METHOD_GET },
			{ "/upload|", 0, rhUpload, 0, PICOHTTP_METHOD_GET },
			{ "/|", 0, rhRoot, 0, PICOHTTP_METHOD_GET },
			{ NULL, 0, 0, 0, 0 }
		};

		picohttpProcessRequest(&ioops, routes);

		shutdown(confd, SHUT_RDWR);
		close(confd);
	}

	return 0;
}

