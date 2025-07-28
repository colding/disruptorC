/*
 *    Copyright (C) 2012-2025, Jules Colding <jcolding@gmail.com>.
 *
 *    All Rights Reserved.
 */

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     (1) Redistributions of source code must retain the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer.
 *
 *     (2) Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 *     (3) Neither the name of the copyright holder nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef DISRUPTORC_TYPES_H
#define DISRUPTORC_TYPES_H

#include <inttypes.h>

#ifdef HAVE_CONFIG_H
    #include "ac_config.h"
#endif
#include "memsizes.h"

/*
 * Cacheline padded counter.
 */
struct count_t {
        uint_fast64_t count;
        uint8_t padding[(CACHE_LINE_SIZE > sizeof(uint_fast64_t)) ? (CACHE_LINE_SIZE - sizeof(uint_fast64_t)) : (sizeof(uint_fast64_t) % CACHE_LINE_SIZE)];
} __attribute__((aligned(CACHE_LINE_SIZE)));

/*
 * Cacheline padded cursor into ring buffer. Wrapping around forever.
 */
struct cursor_t {
        uint_fast64_t sequence;
        uint8_t padding[(CACHE_LINE_SIZE > sizeof(uint_fast64_t)) ? (CACHE_LINE_SIZE - sizeof(uint_fast64_t)) : (sizeof(uint_fast64_t) % CACHE_LINE_SIZE)];
} __attribute__((aligned(CACHE_LINE_SIZE)));

#endif //  DISRUPTORC_TYPES_H
