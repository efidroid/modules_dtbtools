/* Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * * Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 *  with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <libfdt.h>

#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))
#define ROUNDDOWN(a, b) ((a) & ~((b)-1))

off_t fdsize(int fd)
{
    off_t off;

    off = lseek(fd, 0L, SEEK_END);
    lseek(fd, 0L, SEEK_SET);

    return off;
}

int main(int argc, char **argv)
{
    int rc;
    off_t off;
    void *fdtimg = NULL;
    ssize_t ssize;

    // validate arguments
    if (argc!=3) {
        fprintf(stderr, "Usage: %s fdt.img outdir\n", argv[0]);
        return -EINVAL;
    }

    // open file
    const char *filename = argv[1];
    int fd = open(filename, O_RDONLY);
    if (fd<0) {
        fprintf(stderr, "Can't open file %s\n", filename);
        return fd;
    }

    // get filesize
    off = fdsize(fd);
    if (off<0) {
        fprintf(stderr, "Can't get size of file %s\n", filename);
        rc = (int)off;
        goto close_file;
    }

    // allocate buffer
    fdtimg = malloc(off);
    if (!fdtimg) {
        fprintf(stderr, "Can't allocate buffer of size %lu\n", off);
        rc = -ENOMEM;
        goto close_file;
    }

    // read file into memory
    ssize = read(fd, fdtimg, off);
    if (ssize!=off) {
        fprintf(stderr, "Can't read file %s into buffer\n", filename);
        rc = (int)ssize;
        goto free_buffer;
    }

    void *fdt = fdtimg;
    uint32_t i = 0;
    while (fdt+sizeof(struct fdt_header) < fdt+off) {
        if (fdt_check_header(fdt)) break;
        uint32_t fdtsize = fdt_totalsize(fdt);

        // build filename
        char fdtfilename[PATH_MAX];
        rc = snprintf(fdtfilename, sizeof(fdtfilename), "%s/%u.dtb", argv[2], i++);
        if (rc<0 || (size_t)rc>=sizeof(fdtfilename)) {
            fprintf(stderr, "Can't build filename\n");
            return rc;
        }
        rc = 0;

        printf("write %s\n", fdtfilename);

        // open file
        FILE *f = fopen(fdtfilename, "wb+");
        if (!f) {
            fprintf(stderr, "Can't open file %s\n", fdtfilename);
            return -1;
        }

        // write dtb
        fwrite(fdt, fdtsize, 1, f);

        // close file
        if (fclose(f)) {
            fprintf(stderr, "Can't close file %s\n", fdtfilename);
            return -1;
        }

        fdt += fdtsize;
    }


free_buffer:
    free(fdtimg);

close_file:
    // close file
    if (close(fd)) {
        fprintf(stderr, "Can't close file %s\n", filename);
        return rc;
    }

    if (rc) {
        fprintf(stderr, "ERROR: %s\n", strerror(-rc));
        return rc;
    }

    return rc;
}
