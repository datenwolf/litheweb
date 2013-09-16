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
