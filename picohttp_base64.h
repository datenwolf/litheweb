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
