#pragma once
#ifndef PICOHTTP_H_HEADERGUARD
#define PICOHTTP_H_HEADERGUARD

#include <stddef.h>
#include <stdint.h>

#define PICOHTTP_MULTIPARTBOUNDARY_MAX_LEN 70

#define PICOHTTP_MAJORVERSION(x) ( (x & 0x7f00) >> 8 )
#define PICOHTTP_MINORVERSION(x) ( (x & 0x007f) )

#define PICOHTTP_METHOD_GET  1
#define PICOHTTP_METHOD_HEAD 2
#define PICOHTTP_METHOD_POST 3

#define PICOHTTP_CONTENTTYPE_APPLICATION	0
#define PICOHTTP_CONTENTTYPE_AUDIO		1
#define PICOHTTP_CONTENTTYPE_IMAGE		2
#define PICOHTTP_CONTENTTYPE_MESSAGE		3
#define PICOHTTP_CONTENTTYPE_MODEL		4
#define PICOHTTP_CONTENTTYPE_MULTIPART		5
#define PICOHTTP_CONTENTTYPE_TEXT		6
#define PICOHTTP_CONTENTTYPE_VIDEO		7

#define PICOHTTP_CONTENTTYPE_TEXT_SUBTYPE_CSV		3
#define PICOHTTP_CONTENTTYPE_TEXT_SUBTYPE_HTML		4
#define PICOHTTP_CONTENTTYPE_TEXT_SUBTYPE_PLAIN	6

#define PICOHTTP_CONTENTTYPE_MULTIPART_SUBTYPE_FORM_DATA	4

#define PICOHTTP_CODING_IDENTITY 0
#define PICOHTTP_CODING_COMPRESS 1
#define PICOHTTP_CODING_DEFLATE  2
#define PICOHTTP_CODING_GZIP     4
#define PICOHTTP_CODING_CHUNKED  8

#define PICOHTTP_STATUS_200_OK 200
#define PICOHTTP_STATUS_400_BAD_REQUEST 400
#define PICOHTTP_STATUS_404_NOT_FOUND 404
#define PICOHTTP_STATUS_405_METHOD_NOT_ALLOWED 405
#define PICOHTTP_STATUS_414_REQUEST_URI_TOO_LONG 414
#define PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR 500
#define PICOHTTP_STATUS_501_NOT_IMPLEMENTED 501
#define PICOHTTP_STATUS_505_HTTP_VERSION_NOT_SUPPORTED 505

struct picohttpIoOps {
	int (*read)(size_t /*count*/, char* /*buf*/, void*);
	int (*write)(size_t /*count*/, char const* /*buf*/, void*);
	int16_t (*getch)(void*); // returns -1 on error
	int (*putch)(char, void*);
	int (*flush)(void*);
	void *data;
};

#define picohttpIoWrite(ioops,size,buf) (ioops->write(size, buf, ioops->data))
#define picohttpIoRead(ioops,size,buf)  (ioops->read(size, buf, ioops->data))
#define picohttpIoGetch(ioops)          (ioops->getch(ioops->data))
#define picohttpIoPutch(ioops,c)        (ioops->putch(c, ioops->data))
#define picohttpIoFlush(ioops)          (ioops->flush(ioops->data))

enum picohttpVarType {
	PICOHTTP_TYPE_UNDEFINED = 0,
	PICOHTTP_TYPE_INTEGER = 1,
	PICOHTTP_TYPE_REAL = 2,
	PICOHTTP_TYPE_BOOLEAN = 3,
	PICOHTTP_TYPE_TEXT = 4
};

struct picohttpVarSpec {
	char const * const name;
	enum picohttpVarType type;
	size_t max_len;
};

struct picohttpVar {
	struct picohttpVarSpec const *spec;
	union {
		char *text;
		float real;
		int integer;
		uint8_t boolean;
	} value;
	struct picohttpVar *next;
};

struct picohttpRequest;

typedef void (*picohttpHandler)(struct picohttpRequest*);

struct picohttpURLRoute {
	char const * urlhead;
	struct picohttpVarSpec const * get_vars;
	picohttpHandler handler;
	uint16_t max_urltail_len;
	int16_t allowed_methods;
};

#define PICOHTTP_EPOCH_YEAR 1970

struct picohttpDateTime {
	unsigned int Y:7; /* EPOCH + 127 years */
	unsigned int M:4;
	unsigned int D:5;
	unsigned int h:5;
	unsigned int m:6;
	unsigned int s:5; /* seconds / 2 */
};

struct picohttpRequest {
	struct picohttpIoOps const * ioops;
	struct picohttpURLRoute const * route;
	struct picohttpVar *get_vars;
	char *url;
	char *urltail;
	int16_t status;
	int16_t method;
	struct {
		uint8_t major;
		uint8_t minor;
	} httpversion;
	struct {
		struct {
			uint16_t type:4;
			uint16_t subtype:12;
		} contenttype;
		size_t contentlength;
		uint8_t contentencoding;
		uint8_t transferencoding;
		char multipartboundary[PICOHTTP_MULTIPARTBOUNDARY_MAX_LEN+1];
	} query;
	struct {
		char const *contenttype;
		char const *disposition;
		struct picohttpDateTime lastmodified;
		uint16_t max_age;
		size_t contentlength;
		uint8_t contentencoding;
		uint8_t transferencoding;
	} response;
	struct {
		size_t length;
	} currentchunk;
	struct {
		size_t octets;
		uint8_t header;
	} sent;
};

void picohttpProcessRequest(
	struct picohttpIoOps const * const ioops,
	struct picohttpURLRoute const * const routes );

void picohttpStatusResponse(
	struct picohttpRequest *req, int16_t status );

int picohttpResponseSendHeader (
	struct picohttpRequest * const req );

int picohttpResponseWrite (
	struct picohttpRequest * const req,
	size_t len,
	char const *buf );

uint16_t picohttpGetch(
	struct picohttpRequest * const req,
	struct picohttpChunkTransfer * const ct);

int picohttpMultipartNext(
	struct picohttpRequest * const req,
	struct picohttpMultiPart * const mp);

uint16_t picohttpMultipartGetch(
	struct picohttpMultiPart * const mp);

#endif/*PICOHTTP_H_HEADERGUARD*/
