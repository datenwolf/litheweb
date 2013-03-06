#include "picohttp.h"

#include <alloca.h>
#include <string.h>

static void picohttpStatus400BadRequest(
	struct picohttpRequest *req )
{
}

static void picohttpStatus404NotFound(
	struct picohttpRequest *req )
{
}

static void picohttpStatus405MethodNotAllowed(
	struct picohttpRequest *req )
{
}

static void picohttpStatus414RequestURITooLong(
	struct picohttpRequest *req )
{
}

static void picohttpStatus500InternalServerError(
	struct picohttpRequest *req )
{
}

static void picohttpStatus501NotImplemented(
	struct picohttpRequest *req )
{
}

static void picohttpStatus505HTTPVersionNotSupported(
	struct picohttpRequest *req )
{
}

static int16_t picohttpIoSkipSpace(
	struct picohttpIoOps const * const ioops )
{
	int16_t ch;
	while(' ' == (char)(ch = picohttpIoGetch(ioops)));
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
		ch = ((chr)&0x0f);
	} else if(
	    chr >= 'a' && chr <= 'f' ) {
		ch = (((chr)&0x0f) + 9);
	}

	if( 0 > (chr = picohttpIoGetch(ioops)))
		return chr;

	chr |= 0x20;
	if( chr >= '0' && chr <= '9' ) {
		ch |= ((chr)&0x0f) << 4;
	} else if(
	    chr >= 'a' && chr <= 'f' ) {
		ch |= (((chr)&0x0f) + 9) << 4;
	}

	return ch;
}

int picohttpMatchRoute(
	struct picohttpRequest * const req,
	struct picohttpURLRoute const * const routes )
{
}

void picohttpProcessRequest(
	struct picohttpIoOps const * const ioops,
	struct picohttpURLRoute const * const routes )
{
	char *url, *var;
	struct picohttpRequest request = {0,};
	size_t url_max_length = 0;
	size_t var_max_length = 0;

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

	/* Poor mans string matching tree; trade RAM for code */
	switch( picohttpIoGetch(ioops) ) {
	case 'H': switch( picohttpIoGetch(ioops) ) {
		case 'E': switch( picohttpIoGetch(ioops) ) {
			case 'A': switch( picohttpIoGetch(ioops) ) {
				case 'D':
					request.method = PICOHTTP_METHOD_HEAD;
					break;
				}
			} break;
		} break;
	case 'G': switch( picohttpIoGetch(ioops) ) {
		case 'E': switch( picohttpIoGetch(ioops) ) {
			case 'T':
				request.method = PICOHTTP_METHOD_GET;
			} break;
		} break;
	case 'P': switch( picohttpIoGetch(ioops) ) {
		case 'O': switch( picohttpIoGetch(ioops) ) {
			case 'S': switch( picohttpIoGetch(ioops) ) {
				case 'T':
					request.method = PICOHTTP_METHOD_POST;
					break;
				}
			} break;
		} break;
	}
	if( !request.method ) {
		picohttpStatus501NotImplemented(&request);
		return;
	}

	int16_t ch;

	ch = picohttpIoSkipSpace(ioops);
	if( ch < 0 ) {
		picohttpStatus500InternalServerError(&request);
		return;
	}
	
	url[0] = (char)ch;
	/* copy url up to the first query variable; note that this is not
	 * fully compliant to RFC 6874, which permits query components in each
	 * path component (i.e. between '/'-es). */
	for(char *urliter = url+1 ;; urliter++) {
		if( urliter - url >= url_max_length ) {
			picohttpStatus414RequestURITooLong(&request);
			return;
		}
		ch = picohttpIoGetch(ioops);
		if( ch < 0 ) {
			picohttpStatus500InternalServerError(&request);
			return;
		}
		if( '?' == (char)ch || ' ' == (char)ch ) {
			break;
		}
		if( '%' == (char)ch ) {
			ch = picohttpIoGetPercentCh(ioops);
		}
		if(ch < 0) {
			picohttpStatus500InternalServerError(&request);
			return;
		}

		*urliter = (char)ch;	
	}

	if( !picohttpMatchRoute(&request, routes) ) {
		picohttpStatus404NotFound(&request);
		return;
	}
	if( !(request.route->allowed_methods & request.method) ) {
		picohttpStatus405MethodNotAllowed(&request);
		return;
	}

	if(request.route->get_vars) {
		for(size_t j = 0; request.route->get_vars[j].name; j++) {
			size_t var_length = strlen(
				request.route->get_vars[j].name );
			if( var_length > var_max_length ) {
				var_max_length = var_length;
			}
		}
	}
	var = alloca(var_max_length+1);

	while('?' == (char)ch || '&' ==(char)ch) {
		memset(var, 0, var_max_length+1);
		ch = picohttpIoGetch(ioops);

		if( ch < 0 ) {
			picohttpStatus500InternalServerError(&request);
			return;
		}

		if( '&' == (char)ch )
			continue;

		if( '%' == (char)ch ) {
			ch = picohttpIoGetPercentCh(ioops);
		}
		if(ch < 0) {
			picohttpStatus500InternalServerError(&request);
			return;
		}

		var[0] = ch;
		for(char *variter = var+1 ;; variter++) {
			if( variter - var >= var_max_length ) {
				/* variable name longer than longest accepted
				 * variable name --> skip to next variable */
				do {
					ch = picohttpIoGetch(ioops);
					if( ch < 0 ) {
						picohttpStatus500InternalServerError(&request);
						return;
					}
				} while ( '&' != ch );
				continue;
			}

			ch = picohttpIoGetch(ioops);
			if( ch < 0 ) {
				picohttpStatus500InternalServerError(&request);
				return;
			}
		}
	}

	ch = picohttpIoSkipSpace(ioops);
	if( ch < 0 ) {
		picohttpStatus500InternalServerError(&request);
		return;
	}
	for(uint8_t i = 0; i < 4; i++) {
		if("HTTP"[i] != (char)(ch = picohttpIoGetch(ioops)) ) {
			if( ch < 0 ) {
				picohttpStatus500InternalServerError(&request);
			} else {
				picohttpStatus400BadRequest(&request);
			}
			return;
		}
	}

	if( PICOHTTP_MAJORVERSION(request.httpversion) > 1 ||
	    PICOHTTP_MINORVERSION(request.httpversion) > 1 ) {
		picohttpStatus505HTTPVersionNotSupported(&request);
		return;
	}

	request.route->handler(&request);
}
