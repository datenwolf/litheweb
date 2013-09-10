#include "picohttp.h"

#define picohttpIoWrite(ioops,size,buf) (ioops->write(size, buf, ioops->data))
#define picohttpIoRead(ioops,size,buf)  (ioops->read(size, buf, ioops->data))
#define picohttpIoGetch(ioops)          (ioops->getch(ioops->data))
#define picohttpIoPutch(ioops,c)        (ioops->putch(c, ioops->data))
#define picohttpIoFlush(ioops)          (ioops->flush(ioops->data))

#ifdef HOST_DEBUG
#include <stdio.h>
#define debug_printf(...) do{fprintf(stderr, __VA_ARGS__);}while(0)
#define debug_putc(x) do{fputc(x, stderr);}while(0)
#else
#define debug_printf(...) do{}while(0)
#define debug_putc(x) do{}while(0)
#endif

#include <alloca.h>
#include <string.h>
#include <stdlib.h>

static char const PICOHTTP_STR_CRLF[] = "\r\n";
static char const PICOHTTP_STR_CLSP[] = ": ";
static char const PICOHTTP_STR_HTTP_[] = "HTTP/";
static char const PICOHTTP_STR_HOST[] = "Host";
static char const PICOHTTP_STR_SERVER[] = "Server";
static char const PICOHTTP_STR_PICOWEB[] = "picoweb/0.1";

static char const PICOHTTP_STR_ACCEPT[]    = "Accept";
static char const PICOHTTP_STR_TRANSFER[]  = "Transfer";

static char const PICOHTTP_STR__ENCODING[] = "-Encoding";

static char const PICOHTTP_STR_CONTENT[] = "Content";
static char const PICOHTTP_STR__TYPE[]   = "-Type";
static char const PICOHTTP_STR__LENGTH[] = "-Length";
static char const PICOHTTP_STR__CODING[] = "-Coding";
static char const PICOHTTP_STR__DISPOSITION[] = "-Disposition";

static char const PICOHTTP_STR_APPLICATION_[] = "application/";
static char const PICOHTTP_STR_TEXT_[] = "text/";
static char const PICOHTTP_STR_MULTIPART_[] = "multipart/";

static char const PICOHTTP_STR_FORMDATA[] = "form-data";

static char const PICOHTTP_STR_CACHECONTROL[] = "Cache-Control";

static char const PICOHTTP_STR_CONNECTION[] = "Connection";
static char const PICOHTTP_STR_CLOSE[] = "close";

static char const PICOHTTP_STR_DATE[] = "Date";
static char const PICOHTTP_STR_EXPECT[] = "Expect";

static char const PICOHTTP_STR_BOUNDARY[] = " boundary=";
static char const PICOHTTP_STR_NAME__[] = " name=\"";

static char const PICOHTTP_STR_CHUNKED[] = "chunked";

/* compilation unit local function forward declarations */
static int picohttpProcessHeaders (
	struct picohttpRequest * const req,
	picohttpHeaderFieldCallback headerfieldcallback,
	void * const data,
	int ch );

/* compilation unit local helper functions */
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
#include <djb/byte/fmt.h>
#define picohttp_fmt_uint fmt_ulong
#define picohttp_fmt_int fmt_long
#endif

static char const *picohttpStatusString(int code)
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
	case 422:
		return "Unprocessable Entity";
	case 500:
		return "Internal Server Error";
	case 501:
		return "Not Implemented";
	case 505:
		return "HTTP Version Not Supported";
	}
	return "...";
}

void picohttpStatusResponse(
	struct picohttpRequest *req, int status )
{
	req->status = status;
	char const * const c = picohttpStatusString(req->status);
	picohttpResponseWrite(req, strlen(c), c);
}

static uint8_t picohttpIsCRLF(int ch)
{
	switch(ch) {
	case '\r':
	case '\n':
		return 1;
	}
	return 0;
}

static uint8_t picohttpIsLWS(int ch)
{
	return 
		picohttpIsCRLF(ch) ||
		' ' == ch || '\t' == ch;
}

static int picohttpIoSkipSpace (
	struct picohttpIoOps const * const ioops,
	int ch)
{
	for(;;ch = -1) {
		if(0 > ch) {
			ch = picohttpIoGetch(ioops);
		}

		if( 0 > ch ||
		    ( ' ' != ch && '\t' != ch ) )
			break;
	}
	return ch;
}

static int picohttpIoSkipOverCRLF (
	struct picohttpIoOps const * const ioops,
	int ch)
{
	for(;;ch = -1) {
		if(0 > ch) {
			ch = picohttpIoGetch(ioops);
		}

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

static int picohttpIoB10ToU8 (
	uint8_t *i,
	struct picohttpIoOps const * const ioops,
	int ch )
{
	if( 0 > ch )
		ch = picohttpIoGetch(ioops);

	while( ch >= '0' && ch <= '9' ) {
		*i *= 10;
		*i += (ch & 0x0f);
		ch = picohttpIoGetch(ioops);
	}

	return ch;
}

static int picohttpIoB10ToU64 (
	uint64_t *i,
	struct picohttpIoOps const * const ioops,
	int ch )
{
	if( 0 > ch )
		ch = picohttpIoGetch(ioops);

	while( ch >= '0' && ch <= '9' ) {
		*i *= 10;
		*i += (ch & 0x0f);
		ch = picohttpIoGetch(ioops);
	}

	return ch;
}

static int picohttpIoGetPercentCh(
	struct picohttpIoOps const * const ioops )
{
	char ch=0;
	int chr;
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

int picohttpGetch(struct picohttpRequest * const req)
{
	int ch;
	/* skipping over Chunked Transfer Boundaries
	 * if Chunked Transfer Encoding is used */
	if(req->query.transferencoding == PICOHTTP_CODING_CHUNKED ) {
		if( !req->query.chunklength ) {
			/* this is a new chunk;
			 * read the length and skip to after <CR><LF> */
			if( 0 > (ch = picohttpIoGetch(req->ioops)) ) {
				return -1;
			}
			uint64_t len;
			if( 0 > (ch = picohttpIoB10ToU64(
					&len,
					req->ioops,
					ch))
			) {
				return ch;
			}
			if( 0 > (ch = picohttpIoSkipOverCRLF(req->ioops, ch)) ) {
				return ch;
			}
			req->query.chunklength = len;
			return ch;
		}

		if( req->query.chunklength <= req->received_octets ) {
			/* If this happens the data is corrupted, or
			 * the client is nonconforming, or an attack is
			 * underway, or something entierely different,
			 * or all of that.
			 * Abort processing the query!
			 */
			return -1;
		}
	}

	if( 0 <= (ch = picohttpIoGetch(req->ioops)) ) {
		req->received_octets++;
	} else {
		return ch;
	}

	if(req->query.transferencoding == PICOHTTP_CODING_CHUNKED ) {
		if( !req->query.chunklength <= req->received_octets ) {
			/* end of chunk;
			 * skip over <CR><LF>, make sure the trailing '0' is
			 * there and read the headers, err, I mean footers
			 * (whatever, the header processing code will do for 
			 * chunk footers just fine).
			 */
			if( '0' != (ch = picohttpIoGetch(req->ioops)) ) {
				return -1;
			}
			if( 0 > (ch = picohttpIoGetch(req->ioops)) ) {
				return -1;
			}
			if( 0 > (ch = picohttpProcessHeaders(
					req,
					NULL, NULL,
					ch))
			) {
				return ch;
			}
			
			req->received_octets =
			req->query.chunklength = 0;
		}
	}

	return ch;
}

int picohttpRead(struct picohttpRequest * const req, size_t len, char * const buf)
{
	/* skipping over Chunked Transfer Boundaries
	 * if Chunked Transfer Encoding is used */
	if(req->query.transferencoding == PICOHTTP_CODING_CHUNKED ) {
		if( !req->query.chunklength ) {
			int ch;
			/* this is a new chunk;
			 * read the length and skip to after <CR><LF> */
			if( 0 > (ch = picohttpIoGetch(req->ioops)) ) {
				return -1;
			}
			uint64_t len;
			if( 0 > (ch = picohttpIoB10ToU64(
					&len,
					req->ioops,
					ch))
			) {
				return ch;
			}
			if( 0 > (ch = picohttpIoSkipOverCRLF(req->ioops, ch)) ) {
				return ch;
			}
			req->query.chunklength = len;
			return ch;
		}

		if( req->query.chunklength <= req->received_octets ) {
			/* If this happens the data is corrupted, or
			 * the client is nonconforming, or an attack is
			 * underway, or something entierely different,
			 * or all of that.
			 * Abort processing the query!
			 */
			return -1;
		}
	}

	if( req->received_octets + len > req->query.chunklength ) {
		len = req->query.chunklength - req->received_octets;
	}

	int r = picohttpIoRead(req->ioops, len, buf);

	if(req->query.transferencoding == PICOHTTP_CODING_CHUNKED ) {
		if( !req->query.chunklength <= req->received_octets ) {
			int ch;
			/* end of chunk;
			 * skip over <CR><LF>, make sure the trailing '0' is
			 * there and read the headers, err, I mean footers
			 * (whatever, the header processing code will do for 
			 * chunk footers just fine).
			 */
			if( '0' != (ch = picohttpIoGetch(req->ioops)) ) {
				return -1;
			}
			if( 0 > (ch = picohttpIoGetch(req->ioops)) ) {
				return -1;
			}
			if( 0 > (ch = picohttpProcessHeaders(
					req,
					NULL, NULL,
					ch))
			) {
				return ch;
			}
			
			req->received_octets =
			req->query.chunklength = 0;
		}
	}

	return r;
}

/* TODO:
 * It is possible to do in-place pattern matching on the route definition
 * array, without first reading in the URL and then processing it here.
 *
 * Implement this to improve memory footprint reduction.
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

static int picohttpMatchRoute(
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

static int picohttpProcessRequestMethod (
	struct picohttpIoOps const * const ioops )
{
	int method = 0;

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

static int picohttpProcessURL (
	struct picohttpRequest * const req,
	size_t const url_max_length,
	int ch )
{
	/* copy url up to the first query component; note that this is not
	 * fully compliant to RFC 3986, which permits query components in each
	 * path component (i.e. between '/'-s).
	 * picohttp terminates the path once it encounters the first query 
	 * component.
	 */
	/* Deliberately discarding const qualifier! */
	for(char *urliter = req->url ;; urliter++) {
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

		if( (urliter - req->url) >= url_max_length ) {
			return -PICOHTTP_STATUS_414_REQUEST_URI_TOO_LONG;
		}
		*urliter = ch;
		ch = picohttpIoGetch(req->ioops);
	}
	return ch;
}

static int picohttpProcessQuery (
	struct picohttpRequest * const req,
	int ch )
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
#ifdef PICOWEB_CONFIG_USE_C99VARARRAY
	char var[var_max_length+1];
#else
	char *var = alloca(var_max_length+1);
#endif

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
			debug_printf("set variable '%s'\n", var);
		} else {
		}
	}
	if( 0 > (ch = picohttpIoSkipSpace(req->ioops, ch)) ) {
		return -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;
	}

	return ch;
}

static int picohttpProcessHTTPVersion (
	struct picohttpRequest * const req,
	int ch )
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
			-1 );
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

static int picohttpProcessContentType(
	char const **contenttype)
{
	int ct = 0;
	if(!strncmp(*contenttype,
	            PICOHTTP_STR_APPLICATION_, sizeof(PICOHTTP_STR_APPLICATION_)-1)) {
		ct = PICOHTTP_CONTENTTYPE_APPLICATION;
	}

	if(!strncmp(*contenttype,
	            PICOHTTP_STR_TEXT_, sizeof(PICOHTTP_STR_TEXT_)-1)) {
		ct = PICOHTTP_CONTENTTYPE_TEXT;
	}

	if(!strncmp(*contenttype, PICOHTTP_STR_MULTIPART_,
	            sizeof(PICOHTTP_STR_MULTIPART_)-1)) {
		ct = PICOHTTP_CONTENTTYPE_MULTIPART;
		*contenttype += sizeof(PICOHTTP_STR_MULTIPART_)-1;

		if(!strncmp(*contenttype,PICOHTTP_STR_FORMDATA,
		            sizeof(PICOHTTP_STR_FORMDATA)-1)) {
			*contenttype += sizeof(PICOHTTP_STR_FORMDATA)-1;

			ct = PICOHTTP_CONTENTTYPE_MULTIPART_FORMDATA;
		}
	}

	return ct;
}

static void picohttpProcessHeaderContentType(
	struct picohttpRequest * const req,
	char const *contenttype )
{
	req->query.contenttype = picohttpProcessContentType(&contenttype);

	if( PICOHTTP_CONTENTTYPE_MULTIPART == (req->query.contenttype & 0xf000) ) {
		char *boundary = strstr(contenttype, PICOHTTP_STR_BOUNDARY);
		if(boundary) {
			/* see RFC1521 regarding maximum length of boundary */
			memset(req->query.multipartboundary, 0,
			       PICOHTTP_MULTIPARTBOUNDARY_MAX_LEN+1);
			memcpy(req->query.multipartboundary, "\r\n--", 4);
			strncpy(req->query.multipartboundary+4,
			        boundary + sizeof(PICOHTTP_STR_BOUNDARY)-1,
				PICOHTTP_MULTIPARTBOUNDARY_MAX_LEN);
		}
	}
}

static void picohttpProcessHeaderField(
	void * const data,
	char const *headername,
	char const *headervalue)
{
	struct picohttpRequest * const req = data;
	debug_printf("%s: %s\n", headername, headervalue);
	if(!strncmp(headername,
		    PICOHTTP_STR_CONTENT,
		    sizeof(PICOHTTP_STR_CONTENT)-1)) {
		headername += sizeof(PICOHTTP_STR_CONTENT)-1;
		/* Content Length */
		if(!strncmp(headername,
			    PICOHTTP_STR__LENGTH, sizeof(PICOHTTP_STR__LENGTH)-1)) {
			req->query.contentlength = atol(headervalue);
			return;
		}

		/* Content Type */
		if(!strncmp(headername,
			    PICOHTTP_STR__TYPE, sizeof(PICOHTTP_STR__TYPE)-1)) {
			picohttpProcessHeaderContentType(req, headervalue);
			return;
		}
		return;
	}

	if(!strncmp(headername,
	            PICOHTTP_STR_TRANSFER,
		    sizeof(PICOHTTP_STR_TRANSFER)-1)) {
		headername += sizeof(PICOHTTP_STR_TRANSFER)-1;
		/* Transfer Encoding */
		if(!strncmp(headername,
			    PICOHTTP_STR__ENCODING, sizeof(PICOHTTP_STR__ENCODING)-1)) {
			if(!strncmp(headervalue,
			            PICOHTTP_STR_CHUNKED,
			            sizeof(PICOHTTP_STR_CHUNKED)-1)) {
				req->query.transferencoding = PICOHTTP_CODING_CHUNKED;
				req->query.chunklength = 0;
			}
			return;
		}
		return;
	}
}

static int picohttpProcessHeaders (
	struct picohttpRequest * const req,
	picohttpHeaderFieldCallback headerfieldcallback,
	void * const data,
	int ch )
{
#define PICOHTTP_HEADERNAME_MAX_LEN 32
	char headername[PICOHTTP_HEADERNAME_MAX_LEN+1] = {0,};
#define PICOHTTP_HEADERVALUE_MAX_LEN 224
	char headervalue[PICOHTTP_HEADERVALUE_MAX_LEN+1] = {0,};
	char *hn = headername;
	char *hv = headervalue;

	/* TODO: Add Header handling here */
	while( !picohttpIsCRLF(ch) ) {
		/* Beginning of new header line */
		if( 0 < ch && !picohttpIsCRLF(ch) ){
			/* new header field OR field continuation */
			if( picohttpIsLWS(ch) ) {
				/* continuation */
				/* skip space */
				if( 0 > (ch = picohttpIoSkipSpace(req->ioops, ch)) )
					return -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;

				/* read until EOL */
				for(;
				    0 < ch && !picohttpIsCRLF(ch);
				    hv = (hv - headervalue) <
				         PICOHTTP_HEADERVALUE_MAX_LEN ?
					     hv+1 : 0 ) {
					/* add to header field content */

					if(hv)
						*hv = ch;

					ch = picohttpIoGetch(req->ioops);
				}
			} else {
				if( *headername && *headervalue && headerfieldcallback )
					headerfieldcallback(
						data,
						headername,
						headervalue );
				/* new header field */
				memset(headername, 0, PICOHTTP_HEADERNAME_MAX_LEN+1);
				hn = headername;

				memset(headervalue, 0, PICOHTTP_HEADERVALUE_MAX_LEN+1);
				hv = headervalue;
				/* read until ':' or EOL */
				for(;
				    0 < ch && ':' != ch && !picohttpIsCRLF(ch);
				    hn = (hn - headername) <
				          PICOHTTP_HEADERNAME_MAX_LEN ?
					      hn+1 : 0 ) {
					/* add to header name */
					if(hn)
						*hn = ch;

					ch = picohttpIoGetch(req->ioops);
				}
			}
		} 
		if( 0 > ch  ) {
			return -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;
		}
		if( picohttpIsCRLF(ch) )
			ch = picohttpIoSkipOverCRLF(req->ioops, ch);
		else
			ch = picohttpIoGetch(req->ioops);
		if( 0 > ch ) {
			return -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;
		}
		if( !ch ) {
			return -PICOHTTP_STATUS_400_BAD_REQUEST;
		}
	}
	if( *headername && *headervalue && headerfieldcallback)
		headerfieldcallback(
			data,
			headername,
			headervalue );

	return ch;
}

void picohttpProcessRequest (
	struct picohttpIoOps const * const ioops,
	struct picohttpURLRoute const * const routes )
{

	int ch;
	struct picohttpRequest request = {0,};

	size_t url_max_length = 0;
#if 1
	for(size_t i = 0; routes[i].urlhead; i++) {
		size_t url_length =
			strlen(routes[i].urlhead) +
			routes[i].max_urltail_len;

		if(url_length > url_max_length)
			url_max_length = url_length;
	}
#else
	url_max_length = 512;
#endif
#ifdef PICOWEB_CONFIG_USE_C99VARARRAY
	char url[url_max_length+1];
#else
	char *url = alloca(url_max_length+1);
#endif
	memset(url, 0, url_max_length+1);

	request.url = url;
	request.urltail = 0;
	request.ioops = ioops;
	request.method = 0;
	request.httpversion.major = 1;
	request.httpversion.minor = 0;
	request.sent.header = 0;
	request.sent.octets = 0;
	request.received_octets = 0;

	request.method = picohttpProcessRequestMethod(ioops);
	if( !request.method ) {
		ch = -PICOHTTP_STATUS_501_NOT_IMPLEMENTED;
		goto http_error;
	}
	if( 0 > request.method ) {
		ch = -PICOHTTP_STATUS_500_INTERNAL_SERVER_ERROR;
		goto http_error;
	}

	if( 0 > (ch = picohttpIoSkipSpace(ioops, -1)) ) {
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

	if( 0 > (ch = picohttpProcessHeaders(
			&request,
			picohttpProcessHeaderField,
			&request,
			ch)) )
		goto http_error;

	if( '\r' == ch ) {	
		if( 0 > (ch = picohttpIoGetch(ioops)) )
			goto http_error;
		if( '\n' != ch ) {
			ch = -PICOHTTP_STATUS_400_BAD_REQUEST;
			goto http_error;
		}
	}

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
	/* assert(p < sizeof(tmp)-1); */
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

	/* Connection header -- for now this is "Connection: close" */
	if( 0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_CONNECTION)) ||
	    0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_CLSP)) ||
	    0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_CLOSE)) ||
	    0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_CRLF)) )
		return e;

	if(req->response.contenttype) {
		/* Content-Type header */
		if( 0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_CONTENT)) ||
		    0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR__TYPE)) ||
		    0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_CLSP)) ||
		    0 > (e = picohttpIoWrite(
				req->ioops, strlen(req->response.contenttype),
				req->response.contenttype))  ||
		    0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_CRLF)) )
			return e;
	}

	if(req->response.disposition) {
		/* Content-Type header */
		if( 0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_CONTENT)) ||
		    0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR__DISPOSITION)) ||
		    0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_CLSP)) ||
		    0 > (e = picohttpIoWrite(
				req->ioops, strlen(req->response.disposition),
				req->response.disposition))  ||
		    0 > (e = picohttpIO_WRITE_STATIC_STR(PICOHTTP_STR_CRLF)) )
			return e;
	}

	/* Content-Length header */
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
	void const *buf )
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

int picohttpMultipartGetch(
	struct picohttpMultipart * const mp)
{
	int ch;
	if( mp->finished ) {
		return -1;
	} else
	if( 0 <= mp->mismatch ) {
replay:
		if( mp->replayhead < mp->replay ) {
			ch = mp->req->query.multipartboundary[mp->replayhead];
			mp->replayhead++;
			return ch;
		} else {
			ch = mp->mismatch;
			mp->mismatch = -1;
			mp->replayhead = 0;
			return ch;
		}
	} else {
		ch = picohttpGetch(mp->req);

	/* picohttp's query and header parsing is forgiving
	 * regarding line termination. <CR><LF> or just <LF>
	 * are accepted.
	 * However multipart boundaries are to start with
	 * a <CR><LF> sequence.
	 */

		while( 0 <= ch ) {

			if( mp->req->query.multipartboundary[mp->in_boundary] == ch ) {
				if( 0 == mp->req->query.multipartboundary[mp->in_boundary+1] ) {
					mp->in_boundary = 0;
					/* matched boundary */
					int trail[2] = {0, 0};
					for(int i=0; i<2; i++) {
						trail[i] = picohttpGetch(mp->req);
						if( 0 > trail[i] )
							return -1;
					}
				
					if(trail[0] == '\r' && trail[1] == '\n') {
						mp->finished = 1;
					}

				/* TODO: Technically the last boundary is followed by a
				 * terminating <CR><LF> sequence... we should check for
				 * this as well, just for completeness
				 */
					if(trail[0] == '-' && trail[1] == '-') {
						mp->finished = 2;
					}

					return -1;
				} 
				mp->in_boundary++;
			} else {
				if( mp->in_boundary ) 
				{
					if( '\r' == ch ) {
						mp->replay = mp->in_boundary-1;
						mp->mismatch = mp->req->query.multipartboundary[mp->replay];
						mp->in_boundary = 1;
					} else {
						mp->mismatch = ch;
						mp->replay = mp->in_boundary;
						mp->in_boundary = 0;
					} 

					goto replay;

				} 
				return ch;
			}
			ch = picohttpGetch(mp->req);
		}
	}
	return ch;
} 

int picohttpMultipartRead(
	struct picohttpMultipart * const mp,
	size_t len,
	char * const buf)
{
/* TODO: Replace this with a dedicated variant processing whole buffers?
 *       Probably a lot of code would be shared with the ...Getch variant
 *       and could be placed into a commonly used function.
 */
	if( mp->finished ) {
		return -1;
	}

	int ch;
	size_t i;
	for(i = 0; i < len; i++) {
		if( 0 > (ch = picohttpMultipartGetch(mp)) ) {
			if( mp->finished )
				return i;
			else
				return ch;
		}

		buf[i] = ch;
	}
	return i;
}

static void picohttpProcessMultipartContentType(
	struct picohttpMultipart * const mp,
	char const *contenttype )
{
	mp->contenttype = picohttpProcessContentType(&contenttype);

	if( PICOHTTP_CONTENTTYPE_MULTIPART == (mp->contenttype & 0xf000) ) {
	}
}

static void picohttpProcessMultipartContentDisposition(
	struct picohttpMultipart * const mp,
	char const *disposition )
{
	char const *name = strstr(disposition, PICOHTTP_STR_NAME__);
	if(name) {
		name += sizeof(PICOHTTP_STR_NAME__)-1;
		char const * const nameend = strchr(name, '"');
		if(nameend) {
			size_t len = nameend - name;
			if( PICOHTTP_DISPOSITION_NAME_MAX < len )
				len = PICOHTTP_DISPOSITION_NAME_MAX;

			strncpy(mp->disposition.name, name, len);
		}
	}
}

static void picohttpMultipartHeaderField(
	void * const data,
	char const *headername,
	char const *headervalue)
{
	struct picohttpMultipart * const mp = data;
	if(!strncmp(headername,
		    PICOHTTP_STR_CONTENT,
		    sizeof(PICOHTTP_STR_CONTENT)-1)) {
		headername += sizeof(PICOHTTP_STR_CONTENT)-1;
		/* Content Length
		 * TODO: Is this a header actually defined for multipart bodies?
		 *       Anyway, even if it's not defined, this has not negative
		 *       side effects, so why care about it. Worst it can do it
		 *       be usefull later.
		 */
		if(!strncmp(headername,
			    PICOHTTP_STR__LENGTH, sizeof(PICOHTTP_STR__LENGTH)-1)) {
			return;
		}

		/* Content Disposition */
		if(!strncmp(headername,
			    PICOHTTP_STR__DISPOSITION, sizeof(PICOHTTP_STR__DISPOSITION)-1)) {
			picohttpProcessMultipartContentDisposition(mp, headervalue);
			return;
		}

		/* Content Type */
		if(!strncmp(headername,
			    PICOHTTP_STR__TYPE, sizeof(PICOHTTP_STR__TYPE)-1)) {
			picohttpProcessMultipartContentType(mp, headervalue);
			return;
		}
		return;
	}
}

struct picohttpMultipart picohttpMultipartStart(
	struct picohttpRequest * const req)
{
	struct picohttpMultipart mp = {
		.req = req,
		.finished = 0,
		.contenttype = 0,
		.disposition = { .name = {0,} },
		.in_boundary = 2,
		.replayhead = 2,
		.mismatch = -1,
	};

	return mp;
}

int picohttpMultipartNext(
	struct picohttpMultipart * const mp)
{
	if( 2 == mp->finished ) {
		return -1;
	} 

	for(;;) {
		int ch = picohttpMultipartGetch(mp);
		if( 0 > ch ) {
			if( 2 == mp->finished ) {
				return -1;
			}

			if( 1 == mp->finished ) {
				mp->finished = 0;

				if( 0 > (ch = picohttpGetch(mp->req)) )
					return ch;

				if( 0 > (ch = picohttpProcessHeaders(
						mp->req,
						picohttpMultipartHeaderField,
						mp,
						ch)) )
					return ch;

				if( '\r' == ch ) {	
					if( 0 > (ch = picohttpIoGetch(mp->req->ioops)) )
						return ch;
					if( '\n' != ch ) {
						return -1;
					}
				}
				mp->mismatch = -1;
				mp->in_boundary = 
				mp->replayhead = 0;

				return 0;
			}
		}

	}

	return -1;
}
