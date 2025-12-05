/*
 * This file is part of Libsvgtiny
 * Licensed under the MIT License,
 *                http://opensource.org/licenses/mit-license.php
 * Copyright 2008 James Bursa <james@semichrome.net>
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include "svgtiny.h"

/* trivial test program that creates a svg diagram and parses it */
int main(int argc, char *argv[])
{
        FILE *fd;
        struct stat sb;
        char *buffer;
        size_t size;
        size_t n;
        struct svgtiny_diagram *diagram;
        svgtiny_code code;

        if (argc != 2) {
                fprintf(stderr, "Usage: %s FILE\n", argv[0]);
                return 1;
        }

        /* load file into memory buffer */
        fd = fopen(argv[1], "rb");
        if (!fd) {
                perror(argv[1]);
                return 1;
        }

        if (stat(argv[1], &sb)) {
                perror(argv[1]);
                return 1;
        }
        size = sb.st_size;

        buffer = malloc(size);
        if (!buffer) {
                fprintf(stderr, "Unable to allocate %lld bytes\n",
                                (long long) size);
                return 1;
        }

        n = fread(buffer, 1, size, fd);
        if (n != size) {
                perror(argv[1]);
                return 1;
        }

        fclose(fd);

        /* create svgtiny object */
        diagram = svgtiny_create();
        if (!diagram) {
                fprintf(stderr, "svgtiny_create failed\n");
                return 1;
        }

        /* parse */
        code = svgtiny_parse(diagram, buffer, size, argv[1], 1000, 1000);
        if (code != svgtiny_OK) {
                fprintf(stderr, "svgtiny_parse failed: ");
                switch (code) {
                case svgtiny_OUT_OF_MEMORY:
                        fprintf(stderr, "svgtiny_OUT_OF_MEMORY");
                        break;
                case svgtiny_LIBDOM_ERROR:
                        fprintf(stderr, "svgtiny_LIBDOM_ERROR");
                        break;
                case svgtiny_NOT_SVG:
                        fprintf(stderr, "svgtiny_NOT_SVG");
                        break;
                case svgtiny_SVG_ERROR:
                        fprintf(stderr, "svgtiny_SVG_ERROR: line %i: %s",
                                        diagram->error_line,
                                        diagram->error_message);
                        break;
                default:
                        fprintf(stderr, "unknown svgtiny_code %i", code);
                        break;
                }
                fprintf(stderr, "\n");
        }

        free(buffer);

        svgtiny_free(diagram);

        return 0;
}
