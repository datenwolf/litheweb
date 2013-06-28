#pragma once
#ifndef PICOHTTP_H_HEADERGUARD
#define PICOHTTP_H_HEADERGUARD

#include <stddef.h>
#include <stdint.h>

/* max 70 for boundary + 4 chars for "<CR><LF>--" */
#define PICOHTTP_MULTIPARTBOUNDARY_MAX_LEN 74
#define PICOHTTP_DISPOSITION_NAME_MAX 48

#define PICOHTTP_METHOD_GET  1
#define PICOHTTP_METHOD_HEAD 2
#define PICOHTTP_METHOD_POST 4

#define PICOHTTP_CONTENTTYPE_APPLICATION	0x1000
#define PICOHTTP_CONTENTTYPE_APPLICATION_OCTETSTREAM 0x1000

#define PICOHTTP_CONTENTTYPE_AUDIO		0x2000
#define PICOHTTP_CONTENTTYPE_IMAGE		0x3000
#define PICOHTTP_CONTENTTYPE_MESSAGE		0x4000
#define PICOHTTP_CONTENTTYPE_MODEL		0x5000

#define PICOHTTP_CONTENTTYPE_MULTIPART		0x6000
#define PICOHTTP_CONTENTTYPE_MULTIPART_FORMDATA	0x6004

#define PICOHTTP_CONTENTTYPE_TEXT		0x7000
#define PICOHTTP_CONTENTTYPE_TEXT_CSV		0x7003
#define PICOHTTP_CONTENTTYPE_TEXT_HTML		0x7004
#define PICOHTTP_CONTENTTYPE_TEXT_PLAIN		0x7006

#define PICOHTTP_CONTENTTYPE_VIDEO		0x8000

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
	int16_t (*getch)(void*); // returns negative value on error
	int (*putch)(char, void*);
	int (*flush)(void*);
	void *data;
};

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
		uint16_t contenttype;
		size_t contentlength;
		uint8_t contentencoding;
		uint8_t transferencoding;
		char multipartboundary[PICOHTTP_MULTIPARTBOUNDARY_MAX_LEN+1];
		char prev_ch[5];
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
	size_t received_octets;
	struct {
		size_t octets;
		uint8_t header;
	} sent;
};

struct picohttpMultipart {
	struct picohttpRequest *req;
	uint8_t finished;
	uint16_t contenttype;
	struct {
		char name[PICOHTTP_DISPOSITION_NAME_MAX+1];
	} disposition;
	int in_boundary;
	int replay;
	int replayhead;
};

typedef void (*picohttpHeaderFieldCallback)(
	void * const data,
	char const *headername,
	char const *headervalue);

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

int16_t picohttpGetch(struct picohttpRequest * const req);

struct picohttpMultipart picohttpMultipartStart(
	struct picohttpRequest * const req);

int picohttpMultipartNext(
	struct picohttpMultipart * const mp);

int16_t picohttpMultipartGetch(
	struct picohttpMultipart * const mp);

int picohttpMultipartRead(
	struct picohttpMultipart * const mp,
	size_t len,
	char * const buf);

#endif/*PICOHTTP_H_HEADERGUARD*/
