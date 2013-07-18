#define _BSD_SOURCE

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

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

int bsdsock_write(size_t count, char const *buf, void *data)
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

int bsdsock_getch(void *data)
{
	char ch;
	int err;
	if( 1 != (err = bsdsock_read(1, &ch, data)) )
		return err;
	return ch;
}

int bsdsock_putch(char ch, void *data)
{
	return bsdsock_write(1, &ch, data);
}

int bsdsock_flush(void* data)
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
"<a href=\"/test\">/test</a>\n"
"<form action=\"/upload\" enctype=\"multipart/form-data\" method=\"post\">\n"
"<label for=\"name\">Name: </label><input type=\"text\" name=\"name\"></input><br/>\n"
"<label for=\"file1\">File: </label><input type=\"file\" name=\"file1\"></input><br/>\n"
"<label for=\"file2\">File: </label><input type=\"file\" name=\"file2\"></input><br/>\n"
"<input type=\"submit\" value=\"Upload\"></input>\n"
"</form>\n"
"</body></html>\n";

	picohttpResponseWrite(req, sizeof(http_test)-1, http_test);
}

void rhTest(struct picohttpRequest *req)
{
	fprintf(stderr, "handling request /test%s\n", req->urltail);
	char http_header[] = "HTTP/x.x 200 OK\r\nServer: picoweb\r\nContent-Type: text/text\r\n\r\n";
	http_header[5] = '0'+req->httpversion.major;
	http_header[7] = '0'+req->httpversion.minor;
	picohttpResponseWrite(req, sizeof(http_header)-1, http_header);
	char http_test[] = "handling request /test";
	picohttpResponseWrite(req, sizeof(http_test)-1, http_test);
	if(req->urltail) {
		picohttpResponseWrite(req, strlen(req->urltail), req->urltail);
	}
}

void rhUpload(struct picohttpRequest *req)
{
	fprintf(stderr, "handling request /upload%s\n", req->urltail);

	if( PICOHTTP_CONTENTTYPE_MULTIPART_FORMDATA != req->query.contenttype )
		return;

	char http_test[] = "handling request /upload";

	struct picohttpMultipart mp = picohttpMultipartStart(req);
	
	while( !picohttpMultipartNext(&mp) ) {
		fprintf(stderr, "\nprocessing form field \"%s\"\n", mp.disposition.name);
		for(int16_t ch = picohttpMultipartGetch(&mp);
		    0 <= ch;
		    ch = picohttpMultipartGetch(&mp) ) {
			switch(ch) {
			case '\r':
				fputs("--- <CR> ---\n", stderr); break;
			case '\n':
				fputs("--- <LF> ---\n", stderr); break;

			default:
				fputc(ch, stderr);
			}
		}
		if( !mp.finished ) {
			break;
		}
	}
	if( !mp.finished ) {
	}

	picohttpResponseWrite(req, sizeof(http_test)-1, http_test);
	if(req->urltail) {
		picohttpResponseWrite(req, strlen(req->urltail), req->urltail);
	}
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
			.read  = bsdsock_read,
			.write = bsdsock_write,
			.getch = bsdsock_getch,
			.putch = bsdsock_putch,
			.flush = bsdsock_flush,
			.data = &confd
		};

		struct picohttpURLRoute routes[] = {
			{ "/test", 0, rhTest, 16, PICOHTTP_METHOD_GET },
			{ "/upload", 0, rhUpload, 16, PICOHTTP_METHOD_POST },
			{ "/|", 0, rhRoot, 0, PICOHTTP_METHOD_GET },
			{ NULL, 0, 0, 0, 0 }
		};

		picohttpProcessRequest(&ioops, routes);

		shutdown(confd, SHUT_RDWR);
		close(confd);
	}

	return 0;
}

