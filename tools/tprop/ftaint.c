/*
 * Copyright (c) 2010 The Pennsylvania State University
 * Systems and Internet Infrastructure Security Laboratory
 *
 * Authors: William Enck <enck@cse.psu.edu>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "ftaint.h"

#define USAGE "Usage: %s [g|s|a] <file> [<hex>]\n"

void usage(const char *prog)
{
    fprintf(stderr, USAGE, prog);
    exit(1);
}

int main(int argc, char *argv[])
{
    u4 tag;

    if (argc != 3 && argc != 4) {
        usage(argv[0]);
    }
    
    if (strlen(argv[1]) != 1) {
        usage(argv[0]);
    }

    // Get the taint
    if (argc == 3) {
        if (argv[1][0] == 'g') {
            tag = getTaintXattr(argv[2]);
            fprintf(stdout, "0x%08x\n", tag);
            return 0;
        } else {
            usage(argv[0]);
        }
    }

    // Set the taint
    tag = strtol(argv[3], NULL, 16);
    if (tag == 0 && errno == ERANGE) {
        usage(argv[0]);
    }

    if (argv[1][0] == 's') {
        setTaintXattr(argv[2], tag);
    } else if (argv[1][0] == 'a') {
        u4 old = getTaintXattr(argv[2]);
        setTaintXattr(argv[2], tag | old);
    } else {
        usage(argv[0]);
    }

    return 0;
}
