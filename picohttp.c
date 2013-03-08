#include "picohttp.h"

#include <alloca.h>
#include <string.h>
#include <stdio.h>

static void picohttpStatus400BadRequest(
	struct picohttpRequest *req )
{
	fputs("400\n", stderr);
}

static void picohttpStatus404NotFound(
	struct picohttpRequest *req )
{
	char http_header[] = "HTTP/x.x 404 Not Found\r\nServer: picoweb\r\nContent-Type: text/text\r\n\r\n";
	http_header[5] = '0'+req->httpversion.major;
	http_header[7] = '0'+req->httpversion.minor;
	picohttpIoWrite(req->ioops, sizeof(http_header)-1, http_header);
	picohttpIoWrite(req->ioops, sizeof(http_header)-1, http_header);
}

static void picohttpStatus405MethodNotAllowed(
	struct picohttpRequest *req )
{
	fputs("405\n", stderr);
}

static void picohttpStatus414RequestURITooLong(
	struct picohttpRequest *req )
{
	char http_header[] = "HTTP/x.x 414 URI Too Long\r\nServer: picoweb\r\nContent-Type: text/text\r\n\r\n";
	http_header[5] = '0'+req->httpversion.major;
	http_header[7] = '0'+req->httpversion.minor;
	picohttpIoWrite(req->ioops, sizeof(http_header)-1, http_header);
	picohttpIoWrite(req->ioops, sizeof(http_header)-1, http_header);
}

static void picohttpStatus500InternalServerError(
	struct picohttpRequest *req )
{
	fputs("500\n", stderr);
}

static void picohttpStatus501NotImplemented(
	struct picohttpRequest *req )
{
	fputs("501\n", stderr);
}

static void picohttpStatus505HTTPVersionNotSupported(
	struct picohttpRequest *req )
{
	fputs("505\n", stderr);
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
	 * path component (i.e. between '/'-es).
	 * picohttp terminates the path once it encounters the first query 
	 * component.
	 */
	/* Deliberately discarding const qualifier! */
	for(char *urliter = (char*)req->url ;; urliter++) {
		if( ch < 0 ) {
			return -500;
		}
		if( '?' == ch ||
		    picohttpIsLWS(ch) ) {
			break;
		}
		if( '%' == ch ) {
			ch = picohttpIoGetPercentCh(req->ioops);
			if( ch < 0 ) {
				return -500;
			}
		}
		if( !ch ) {
			return -400;
		}

		if( urliter - req->url >= url_max_length ) {
			return -414;
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
			return -500;
		}
		if( '&' == ch )
			continue;


		for(char *variter = var ;; variter++) {
			if( ch < 0 ) {
				return -500;
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
					return -500;
				}
			}
			if( !ch ) {
				return -400;
			}

			if( variter - var >= var_max_length ) {
/* variable name in request longer than longest
 * variable name accepted by route --> skip to next variable */
				do {
					ch = picohttpIoGetch(req->ioops);
					if( ch < 0 ) {
						return -500;
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
		return -500;
	}

	return ch;
}

static int16_t picohttpProcessHTTPVersion (
	struct picohttpRequest * const req,
	int16_t ch )
{
	if( !picohttpIsCRLF(ch) ) {
		for(uint8_t i = 0; i < 5; i++) {
			if("HTTP/"[i] != (char)ch ) {
				if( ch < 0 ) {
					return -500;
				}
				return -400;
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
			return -500;
		}
		if( ch != '.' ) {
			return -400;
		}
		ch = picohttpIoB10ToU8(
			&req->httpversion.minor,
			req->ioops,
			0 );
		if( ch < 0 ) {
			return -500;
		}

		ch = picohttpIoSkipSpace(req->ioops, ch);
		if( ch < 0 ) {
			return -500;
		}
	}
	ch = picohttpIoSkipOverCRLF(req->ioops, ch);
	if( ch < 0 ) {
		return -500;
	}
	if( !ch ) {
		return -400;
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
				return -500;
			}
		}

		ch = picohttpIoSkipOverCRLF(req->ioops, ch);
		if( 0 > ch ) {
			return -500;
		}
		if( !ch ) {
			return -400;
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

	request.method = picohttpProcessRequestMethod(ioops);
	if( !request.method ) {
		picohttpStatus501NotImplemented(&request);
		return;
	}
	if( 0 > request.method ) {
		picohttpStatus500InternalServerError(&request);
		return;
	}

	int16_t ch;
	if( 0 > (ch = picohttpIoSkipSpace(ioops, 0)) ) {
		picohttpStatus500InternalServerError(&request);
		return;
	}

	if( 0 > (ch = picohttpProcessURL(&request, url_max_length, ch)) )
		goto http_error;

	if( !picohttpMatchRoute(&request, routes) || !request.route ) {
		picohttpStatus404NotFound(&request);
		return;
	}
	if( !(request.route->allowed_methods & request.method) ) {
		picohttpStatus405MethodNotAllowed(&request);
		return;
	}

	if( 0 > (ch = picohttpProcessQuery(&request, ch)) )
		goto http_error;

	if( 0 > (ch = picohttpProcessHTTPVersion (&request, ch)) )
		goto http_error;

	if( request.httpversion.major > 1 ||
	    request.httpversion.minor > 1 ) {
		picohttpStatus505HTTPVersionNotSupported(&request);
		return;
	}

	if( 0 > (ch = picohttpProcessHeaders(&request, ch)) )
		goto http_error;

	request.route->handler(&request);
	return;

http_error:
	switch(-ch) {
	case 400: picohttpStatus400BadRequest(&request); break;
	case 404: picohttpStatus404NotFound(&request); break;
	case 405: picohttpStatus405MethodNotAllowed(&request); break;
	case 500: picohttpStatus500InternalServerError(&request); break;
	}
}

