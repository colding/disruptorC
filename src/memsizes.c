/*
 *    Copyright (C) 2012-2013, Jules Colding <jcolding@gmail.com>.
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

#include "ac_config.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>

#if defined __APPLE__
    #include <sys/sysctl.h>
#elif defined __linux__
    #include <stdio.h>
#elif (defined(__FreeBSD__) || defined(__NetBSD__))
    #include <sys/param.h>
#else
    #error unrecognized platform
#endif

int
main(int argc, char **argv)
{
        int fd;
        const long page_size = sysconf(_SC_PAGESIZE);
        size_t cache_line_size = 0;

        if (-1 == page_size)
                abort();

        if (unlink("memsizes.h")) {
                if (ENOENT != errno)
                        abort();
        }

#if defined __APPLE__
        size_t sizeof_line_size = sizeof(cache_line_size);
        if (sysctlbyname("hw.cachelinesize", &cache_line_size, &sizeof_line_size, 0, 0))
                abort();
#elif defined __linux__
        FILE *p = NULL;
        p = fopen("/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size", "r");
        if (p) {
                if (1 != fscanf(p, "%zu", &cache_line_size))
			abort();
                fclose(p);
        } else {
                abort();
        }
#elif (defined(__FreeBSD__) || defined(__NetBSD__))
        cache_line_size = CACHE_LINE_SIZE;
#endif
        if (0 == cache_line_size)
                abort();

        fd = open("memsizes.h", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        dprintf(fd, "#ifndef MEM_SIZES_H        \n");
        dprintf(fd, "#define MEM_SIZES_H      \n\n");
        dprintf(fd, "#ifdef PAGE_SIZE          \n");
        dprintf(fd, "#undef PAGE_SIZE          \n");
        dprintf(fd, "#endif                    \n");
        dprintf(fd, "#define PAGE_SIZE (%ld) \n\n", page_size);
        dprintf(fd, "#ifdef CACHE_LINE_SIZE          \n");
        dprintf(fd, "#undef CACHE_LINE_SIZE          \n");
        dprintf(fd, "#endif                    \n");
        dprintf(fd, "#define CACHE_LINE_SIZE (%zu) \n\n", cache_line_size);
        dprintf(fd, "#endif /* MEM_SIZES_H */   \n");
        close(fd);

        return EXIT_SUCCESS;
}
