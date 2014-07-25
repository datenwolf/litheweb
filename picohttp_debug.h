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
#ifndef PICOHTTP_DEBUG_H
#define PICOHTTP_DEBUG_H

#if 0
#include <util/debug_utils.h>
#endif

#ifndef debug_printf
#ifdef HOST_DEBUG
#include <stdio.h>
#define debug_printf(...) do{fprintf(stderr, __VA_ARGS__);}while(0)
#else
#define debug_printf(...) do{}while(0)
#endif
#endif

#ifndef debug_putc
#ifdef HOST_DEBUG
#include <stdio.h>
#define debug_putc(x) do{fputc(x, stderr);}while(0)
#else
#define debug_putc(x) do{}while(0)
#endif
#endif

#endif/*PICOHTTP_DEBUG_H*/
