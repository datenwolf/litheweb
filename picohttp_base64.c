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

#include "picohttp_base64.h"

void phb64encode(
	phb64raw_t const raw,
	size_t count,
	phb64enc_t enc)
{
	enc[1] = 0;
	enc[3] = 0xff;
	enc[2] = (count > 1) ? 0 : 0xff;

	switch(count) {
	default:
		return;
	case 3:
		enc[3] = ((raw[2] & 0x3f));
		enc[2] = ((raw[2] & 0xc0) >> 6);
	case 2:
		enc[2] |= ((raw[1] & 0x0f) << 2);
		enc[1]  = ((raw[1] & 0xf0) >> 4);
	case 1:
		enc[1] |= ((raw[0] & 0x03) << 4);
		enc[0]  = ((raw[0] & 0xfc) >> 2);
	}

	for(int i = 0; i < 4; i++) {
		if( 26 > enc[i] ) {
			enc[i] = enc[i] + 'A';
		} else
		if( 52 > enc[i] ) {
			enc[i] = (enc[i]-26) + 'a';
		} else
		if( 62 > enc[i] ) {
			enc[i] = (enc[i]-52) + '0';
		} else
		switch( enc[i] ) {
		case 62: enc[i] = '+'; break;
		case 63: enc[i] = '/'; break;
		default: enc[i] = '=';
		}
	}
}

size_t phb64decode(
	phb64enc_t const enc,
	phb64raw_t raw)
{
	size_t count = 3;
	phb64enc_t v;
	for(int i = 0; i < 4; i++) {
		if( 'A' <= enc[i] && 'Z' >= enc[i] ) {
			v[i] = enc[i] - 'A';
		} else
		if( 'a' <= enc[i] && 'z' >= enc[i] ) {
			v[i] = enc[i] - 'a' + 26;
		} else
		if( '0' <= enc[i] && '9' >= enc[i] ) {
			v[i] = enc[i] - '0' + 52;
		} else
		switch(enc[i]) {
		case '+': v[i] = 62; break;
		case '/': v[i] = 63; break;
		case 0: /* slightly deviating from the RFC, but reasonable */
		case '=': v[i] = 0;  count--; break;
		default:
			  return 0;
		}
	}

	raw[0] = ((v[0] & 0x3f) << 2) | ((v[1] & 0x30) >> 4);
	raw[1] = ((v[1] & 0x0f) << 4) | ((v[2] & 0x3c) >> 2);
	raw[2] = ((v[2] & 0x03) << 6) | ( v[3] & 0x3f );

	return count;
}

