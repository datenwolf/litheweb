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

#pragma once
#ifndef PICOHTTP_BASE64_H
#define PICOHTTP_BASE64_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t phb64raw_t[3];
typedef char phb64enc_t[4];
typedef uint32_t phb64state_t;

void phb64encode(
	phb64raw_t const raw,
	size_t count,
	phb64enc_t enc);

size_t phb64decode(
	phb64enc_t const enc,
	phb64raw_t raw);

#endif/*PICOHTTP_BASE64_H*/
