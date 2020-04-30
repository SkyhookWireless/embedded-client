/*! \file sample_client/config.h
 *  \brief Skyhook Embedded Library
 *
 * Copyright (c) 2019 Skyhook, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */
#ifndef TEST_CONFIG_H
#define TEST_CONFIG_H

#include "../libel/config.h"

typedef struct client_config {
    char *filename;
    uint16_t port;
    uint16_t partner_id;

    char server[80];

    uint8_t device_id[MAX_DEVICE_ID];
    uint8_t device_len;
    uint8_t key[AES_SIZE];
} Config_t;

uint32_t hex2bin(char *hexstr, uint32_t hexlen, uint8_t *result, uint32_t reslen);

int32_t bin2hex(char *result, int32_t reslen, uint8_t *bin, int32_t binlen);

int load_config(char *filename, Config_t *config);

void print_config(Config_t *config);

#endif
