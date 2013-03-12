#include "picohttp.h"

#include <alloca.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

static char const PICOHTTP_STR_CRLF[] = "\r\n";
static char const PICOHTTP_STR_CLSP[] = ": ";
static char const PICOHTTP_STR_HTTP_[] = "HTTP/";
static char const PICOHTTP_STR_SERVER[] = "Server";
static char const PICOHTTP_STR_PICOWEB[] = "picoweb/0.1";

static char const PICOHTTP_STR_ACCEPT[]    = "Accept";
static char const PICOHTTP_STR__ENCODING[] = "-Encoding";

static char const PICOHTTP_STR_CONTENT[] = "Content";
static char const PICOHTTP_STR__TYPE[]   = "-Type";
static char const PICOHTTP_STR__LENGTH[] = "-Length";
static char const PICOHTTP_STR__CODING[] = "-Coding";

static char const PICOHTTP_STR_CACHECONTROL[] = "Cache-Control";

static char const PICOHTTP_STR_DATE[] = "Date";

static char const PICOHTTP_STR_EXPECT[] = "Expect";

#if !defined(PICOHTTP_CONFIG_HAVE_LIBDJB)
/* Number formating functions modified from libdjb by
 * Daniel J. Bernstein, packaged at http://www.fefe.de/djb/
 */
static size_t picohttp_fmt_uint(char *dest, unsigned int i)
{
	register unsigned int len, tmp, len2;
	/* first count the number of bytes needed */
	for(len = 1, tmp = i;
	    tmp > 9;
	    ++len )
		tmp/=10;

	if( dest )
		for(tmp = i, dest += len, len2 = len+1;
		    --len2;
		    tmp /= 10 )
			*--dest = ( tmp % 10 ) + '0';
	return len;
}

static size_t picohttp_fmt_int(char *dest,int i) {
	if( i < 0 ) {
		if( dest )
			*dest++='-';
		return picohttp_fmt_uint(dest, -i) + 1;
	}
	return picohttp_fmt_uint(dest, i);
}
#else
#define picohttp_fmt_uint fmt_ulong
#define picohttp_fmt_int fmt_long
#endif

static char const * const picohttpStatusString(int16_t code)
{
	switch(code) {
	case 200:
		return "OK";
	case 400:
		return "Bad Request";
	case 404:
		return "Not Found";
	case 414:
		return "Request URI Too Long";
	case 500:
		return "Internal Server Error";
	case 501:
		return "Not Implemented";
	case 505:
		return "HTTP Version Not Supported";
	}
	return "...";
}

static void picohttpStatusResponse(
	struct picohttpRequest *req, int16_t status )
{
	req->status = status;
	char const * const c = picohttpStatusString(req->status);
	picohttpResponseWrite(req, strlen(c), c);
}

static uint8_t picohttpIsCRLF(int16_t ch)
{
	switch(ch) {
	case '\r':
	case '\n':
		return 1;
	}
	return 0;
}

static uint8_t picohttpIsLWS(int16_t ch)
{
	return 
		picohttpIsCRLF(ch) ||
		' ' == ch || '\t' == ch;
}

static int16_t picohttpIoSkipSpace (
	struct picohttpIoOps const * const ioops,
	int16_t ch)
{
	for(;;ch = 0) {
		if(!ch)
			ch = picohttpIoGetch(ioops);
		if( 0 >= ch ||
		    ( ' ' != ch && '\t' != ch ) )
			break;
	}
	return ch;
}

static int16_t picohttpIoSkipOverCRLF (
	struct picohttpIoOps const * const ioops,
	int16_t ch)
{
	for(;;ch = 0) {
		if(!ch)
			ch = picohttpIoGetch(ioops);
		if( ch < 0 ) {
			return -1;
		}
		if( ch == '\n' ) {
			break;
		}
		if( ch == '\r' ) {
			ch = picohttpIoGetch(ioops);
			if( ch < 0 ) {
				return -1;
			}
			if( ch != '\n' ) {
				return 0;
			}
			break;
		}
	}
	ch = picohttpIoGetch(ioops);
	return ch;
}

static int16_t picohttpIoB10ToU8 (
	uint8_t *i,
	struct picohttpIoOps const * const ioops,
	int16_t ch )
{
	if( !ch )
		ch = picohttpIoGetch(ioops);

	while( ch >= '0' && ch <= '9' ) {
		*i *= 10;
		*i += (ch & 0x0f);
		ch = picohttpIoGetch(ioops);
	}

	return ch;
}

static int16_t picohttpIoGetPercentCh(
	struct picohttpIoOps const * const ioops )
{
	char ch;
	int16_t chr;
	if( 0 > (chr = picohttpIoGetch(ioops)))
		return chr;

	chr |= 0x20;
	if( chr >= '0' && chr <= '9' ) {
		ch = ((chr)&0x0f)<<4;
	} else if(
	    chr >= 'a' && chr <= 'f' ) {
		ch = (((chr)&0x0f) + 9)<<4;
	}

	if( 0 > (chr = picohttpIoGetch(ioops)))
		return chr;

	chr |= 0x20;
	if( chr >= '0' && chr <= '9' ) {
		ch |= ((chr)&0x0f);
	} else if(
	    chr >= 'a' && chr <= 'f' ) {
		ch |= (((chr)&0x0f) + 9);
	}

	return ch;
}

/* TODO:
 * It is possible to do in-place pattern matching on the route definition
 * array, without first reading in the URL and then processing it here.
 *
 * Implement this to imporove memory footprint reduction.
 */
static size_t picohttpMatchURL(
	char const * const urlhead,
	char const * const url )
{
	size_t len_urlhead = strlen(urlhead);
	size_t j;
	for(j = 0; j < len_urlhead; j++) {
		if( '|' == urlhead[j] ) {
			/* hard URL termination */
			if( url[j] ) {
				return 0;
			}
			break;
		}

		if( '\\' == urlhead[j] ) {
			/* soft URL termination, i.e. URL may be terminated
			 * by an optional '/' character */
			if( url[j] && !( url[j] == '/' && !url[j+1] ) ) {
				return 0;
			}
			break;
		}

		if( urlhead[j] != url[j] ) {
			return 0;
		}
	}
	if( url[j] && url[j] != '/' ) {
		return 0;
	}
	return j;
}

static int8_t picohttpMatchRoute(
	struct picohttpRequest * const req,
	struct picohttpURLRoute const * const routes )
{
	struct picohttpURLRoute const *r;
	for(size_t i = 0; (r = routes + i)->urlhead; i++) {
		size_t l;
		if( (l = picohttpMatchURL(r->urlhead, req->url)) && 
		    req->method & r->allowed_methods ) {
			req->route = r;
			req->urltail = req->url[l] ? req->url+l : 0;
			return 1;
		}
	}	
	return 0;
}

static int16_t picohttpProcessRequestMethod (
	struct picohttpIoOps const * const ioops )
{
	int16_t method = 0;
	/* Poor man's string matching tree; trade RAM for code */
	switch( picohttpIoGetch(ioops) ) {
	case 'H': switch( picohttpIoGetch(ioops) ) {
		case 'E': switch( picohttpIoGetch(ioops) ) {
			case 'A': switch( picohttpIoGetch(ioops) ) {
				case 'D':
					method = PICOHTTP_METHOD_HEAD;
					break;
				case -1:
					method = -1;
					break;
				} break;
			case -1:
				method = -1;
				break;
			} break;
		case -1:
			method = -1;
			break;
		} break;
	case 'G': switch( picohttpIoGetch(ioops) ) {
		case 'E': switch( picohttpIoGetch(ioops) ) {
			case 'T':
				method = PICOHTTP_METHOD_GET;
				break;
			case -1:
				method = -1;
				break;
			} break;
		case -1:
			method = -1;
			break;
		} break;
	case 'P': switch( picohttpIoGetch(ioops) ) {
		case 'O': switch( picohttpIoGetch(ioops) ) {
			case 'S': switch( picohttpIoGetch(ioops) ) {
				case 'T':
					method = PICOHTTP_METHOD_POST;
					break;
				case -1:
					method = -1;
					break;
				} break;
			case -1:
				method = -1;
				break;
			} break;
		case -1:
			method = -1;
			break;
		} break;
	case -1:
		method = -1;
		break;
	}
	return method;
}

static int16_t picohttpProcessURL (
	struct picohttpRequest * const req,
	size_t const url_max_length,
	int16_t ch )
{
	/* copy url up to the first query component; note that this is not
	 * fully compliant to RFC 3986, which permits query components in each
	 * path component (i.e. between '/'-s).
	 * picohttp terminates the path once it encounters the first query 
	 * component.
	 */
	/* Deliberately discarding const qualifier! */
	for(char *urliter = (char*)req->url ;; urliter++) {
		if( ch < 0 ) {
			return -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;
		}
		if( '?' == ch ||
		    picohttpIsLWS(ch) ) {
			break;
		}
		if( '%' == ch ) {
			ch = picohttpIoGetPercentCh(req->ioops);
			if( ch < 0 ) {
				return -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;
			}
		}
		if( !ch ) {
			return -PICOHTTP_STATUS_400_BAD_REQUEST;
		}

		if( urliter - req->url >= url_max_length ) {
			return -PICOHTTP_STATUS_414_REQUEST_URI_TOO_LONG;
		}
		*urliter = ch;

		ch = picohttpIoGetch(req->ioops);
	}
	return ch;
}

static int16_t picohttpProcessQuery (
	struct picohttpRequest * const req,
	int16_t ch )
{
	size_t var_max_length = 0;
	if(req->route->get_vars) {
		for(size_t j = 0; req->route->get_vars[j].name; j++) {
			size_t var_length = strlen(
				req->route->get_vars[j].name );
			if( var_length > var_max_length ) {
				var_max_length = var_length;
			}
		}
	}
	char *var = alloca(var_max_length+1);

	while('?' == ch || '&' == ch) {
		memset(var, 0, var_max_length+1);
		ch = picohttpIoGetch(req->ioops);

		if( ch < 0 ) {
			return -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;
		}
		if( '&' == ch )
			continue;


		for(char *variter = var ;; variter++) {
			if( ch < 0 ) {
				return -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;
			}
			if( '='  == ch ||
			    '#'  == ch ||
			    '&'  == ch ||
			    picohttpIsLWS(ch) ) {
				break;
			}
			if( '%' == ch ) {
				ch = picohttpIoGetPercentCh(req->ioops);
				if( ch < 0 ) {
					return -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;
				}
			}
			if( !ch ) {
				return -PICOHTTP_STATUS_400_BAD_REQUEST;
			}

			if( variter - var >= var_max_length ) {
/* variable name in request longer than longest
 * variable name accepted by route --> skip to next variable */
				do {
					ch = picohttpIoGetch(req->ioops);
					if( ch < 0 ) {
						return -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;
					}
				} while(!( '&' == ch ||
				           picohttpIsLWS(ch) ));
				continue;
			}
			*variter = ch;

			ch = picohttpIoGetch(req->ioops);
		}
		if( '=' == ch ) {
		} else {
		}
	}
	if( 0 > (ch = picohttpIoSkipSpace(req->ioops, ch)) ) {
		return -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;
	}

	return ch;
}

static int16_t picohttpProcessHTTPVersion (
	struct picohttpRequest * const req,
	int16_t ch )
{
	if( !picohttpIsCRLF(ch) ) {
		for(uint8_t i = 0; i < 5; i++) {
			if(PICOHTTP_STR_HTTP_[i] != (char)ch ) {
				if( ch < 0 ) {
					return -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;
				}
				return -PICOHTTP_STATUS_400_BAD_REQUEST;
			}
			ch = picohttpIoGetch(req->ioops);
		}

		req->httpversion.major = 0;
		req->httpversion.minor = 0;
		ch = picohttpIoB10ToU8(
			&req->httpversion.major,
			req->ioops,
			ch );
		if( ch < 0 ) {
			return -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;
		}
		if( ch != '.' ) {
			return -PICOHTTP_STATUS_400_BAD_REQUEST;
		}
		ch = picohttpIoB10ToU8(
			&req->httpversion.minor,
			req->ioops,
			0 );
		if( ch < 0 ) {
			return -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;
		}

		ch = picohttpIoSkipSpace(req->ioops, ch);
		if( ch < 0 ) {
			return -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;
		}
	}
	ch = picohttpIoSkipOverCRLF(req->ioops, ch);
	if( ch < 0 ) {
		return -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;
	}
	if( !ch ) {
		return -PICOHTTP_STATUS_400_BAD_REQUEST;
	}

	return ch;
}

static int16_t picohttpProcessHeaders (
	struct picohttpRequest * const req,
	int16_t ch )
{
#define PICOHTTP_HEADERNAME_MAX_LEN 32
	char headername[PICOHTTP_HEADERNAME_MAX_LEN] = {0,};

	/* FIXME: Add Header handling here */
	while( !picohttpIsCRLF(ch) ) {
		fprintf(stderr, "\n>>> 0x%02x ", (int)ch, stderr);

		while( !picohttpIsCRLF( ch=picohttpIoSkipSpace(req->ioops, ch)) ){
			fputc(ch, stderr);
			if( 0 > ( ch=picohttpIoGetch(req->ioops) ) ) {
				return -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;
			}
		}

		ch = picohttpIoSkipOverCRLF(req->ioops, ch);
		if( 0 > ch ) {
			return -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;
		}
		if( !ch ) {
			return -PICOHTTP_STATUS_400_BAD_REQUEST;
		}
	}
	fputc('\n', stderr);
	return ch;
}

void picohttpProcessRequest (
	struct picohttpIoOps const * const ioops,
	struct picohttpURLRoute const * const routes )
{
	char *url;
	int16_t ch;
	struct picohttpRequest request = {0,};
	size_t url_max_length = 0;

	for(size_t i = 0; routes[i].urlhead; i++) {
		size_t url_length =
			strlen(routes[i].urlhead) +
			routes[i].max_urltail_len;

		if(url_length > url_max_length)
			url_max_length = url_length;
	}
	url = alloca(url_max_length+1);
	memset(url, 0, url_max_length+1);

	request.url = url;
	request.urltail = 0;
	request.ioops = ioops;
	request.method = 0;
	request.httpversion.major = 1;
	request.httpversion.minor = 0;
	request.sent.header = 0;
	request.sent.octets = 0;

	request.method = picohttpProcessRequestMethod(ioops);
	if( !request.method ) {
		ch = -PICOHTTP_STATUS_501_NOT_IMPLEMENTED;
		goto http_error;
	}
	if( 0 > request.method ) {
		ch = -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;
		goto http_error;
	}

	if( 0 > (ch = picohttpIoSkipSpace(ioops, 0)) ) {
		ch = -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;
		goto http_error;
	}

	if( 0 > (ch = picohttpProcessURL(&request, url_max_length, ch)) )
		goto http_error;

	if( !picohttpMatchRoute(&request, routes) || !request.route ) {
		ch = -PICOHTTP_STATUS_404_NOT_FOUND;
		goto http_error;
	}
	if( !(request.route->allowed_methods & request.method) ) {
		ch = -PICOHTTP_STATUS_405_METHOD_NOT_ALLOWED;
		goto http_error;
	}

	if( 0 > (ch = picohttpProcessQuery(&request, ch)) )
		goto http_error;

	if( 0 > (ch = picohttpProcessHTTPVersion (&request, ch)) )
		goto http_error;

	if( request.httpversion.major > 1 ||
	    request.httpversion.minor > 1 ) {
		ch = -PICOHTTP_STATUS_505_HTTP_VERSION_NOT_SUPPORTED;
		goto http_error;
	}

	if( 0 > (ch = picohttpProcessHeaders(&request, ch)) )
		goto http_error;

	request.status = PICOHTTP_STATUS_200_OK;
	request.route->handler(&request);

	picohttpIoFlush(request.ioops);
	return;

http_error:
	picohttpStatusResponse(&request, -ch);
	picohttpIoFlush(request.ioops);
}

int picohttpResponseSendHeaders (
	struct picohttpRequest * const req )
{
#define picohttpIO_WRITE_STATIC_STR(x) \
	(picohttpIoWrite(req->ioops, sizeof(x)-1, x))

	char tmp[16] = {0,};
	char const *c;
	int e;

	if(req->sent.header)
		return 0;

	if(!req->response.contenttype) {
		req->response.contenttype = "text/plain";
	}

#if defined(PICOHTTP_CONFIG_USE_SNPRINTF)
	snprintf(tmp, sizeof(tmp)-1, "%s%d.%d %d ",
	         PICOHTTP_STR_HTTP_,
	         req->httpversion.major,
		 req->httpversion.minor,
		 req->status);
#else
	if( 0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_HTTP_)) )
		return e;

	size_t p = 0;
	p += picohttp_fmt_uint(tmp+p, req->httpversion.major);
	tmp[p] = '.'; p++;
	p += picohttp_fmt_uint(tmp+p, req->httpversion.minor);
	tmp[p] = ' '; p++;
	p += picohttp_fmt_uint(tmp+p, req->status);
	tmp[p] = ' '; p++;
	assert(p < sizeof(tmp)-1);
#endif
	/* HTTP status line */
	c = picohttpStatusString(req->status);
	if( 0 > (e = picohttpIoWrite(req->ioops, strlen(tmp), tmp)) ||
	    0 > (e = picohttpIoWrite(req->ioops, strlen(c), c)) ||
	    0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_CRLF)) )
		return e;

	/* Server header */
	if( 0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_SERVER)) ||
	    0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_CLSP)) ||
	    0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_PICOWEB)) ||
	    0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_CRLF)) )
		return e;

	/* Content-Type header */
	if( 0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_CONTENT)) ||
	    0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR__TYPE)) ||
	    0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_CLSP)) ||
	    0 > (e = picohttpIoWrite(
			req->ioops, strlen(req->response.contenttype),
			req->response.contenttype))  ||
	    0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_CRLF)) )
		return e;

	if( req->response.contentlength ){
		p = picohttp_fmt_uint(tmp, req->response.contentlength);
		if( 0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_CONTENT)) ||
		    0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR__LENGTH)) ||
		    0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_CLSP)) ||
		    0 > (e = picohttpIoWrite(req->ioops, p, tmp))  ||
		    0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_CRLF)) )
			return e;
	}

	if( 0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_CRLF)) )
		return e;

	return req->sent.header = 1;

#undef picohttpIO_WRITE_STATIC_STR
}

int picohttpResponseWrite (
	struct picohttpRequest * const req,
	size_t len,
	char const *buf )
{
	int e;

	if( !req->sent.header )
		picohttpResponseSendHeaders(req);

	if( req->response.contentlength > 0 ) {
		if(req->sent.octets >= req->response.contentlength)
			return -1;

		if(req->sent.octets + len < req->sent.octets) /* int overflow */
			return -2;

		if(req->sent.octets + len >= req->response.contentlength)
			len = req->response.contentlength - req->sent.octets;
	}
	
	if( PICOHTTP_METHOD_HEAD == req->method )
		return 0;

	if( 0 > (e = picohttpIoWrite(req->ioops, len, buf)) )
		return e;

	req->sent.octets += len;
	return len;
}

