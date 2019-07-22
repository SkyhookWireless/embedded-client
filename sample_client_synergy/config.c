/*! \file sample_client/config.c
 *  \brief Skyhook Embedded Library
 *
 * Copyright 2015-present Skyhook Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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
#include <inttypes.h>
#include <stdbool.h>
#include <unistd.h>

#include "libel.h"
#include "config.h"

// Uncomment if config.c should try to open the sample_client.config file
// #define CONFI_FILE_IO

/*! \brief convert ascii hex string to binary
 *
 *  @param hexstr pointer to the hex string
 *  @param hexlen length of the hex string
 *  @param result pointer to the result buffer
 *  @param reslen length of the result buffer
 *
 *  @return number of result bytes that were successfully parsed
 */
uint32_t hex2bin(char *hexstr, uint32_t hexlen, uint8_t *result, uint32_t reslen)
{
    uint32_t i, j = 0, k = 0;

    for (i = 0; i < hexlen; i++) {
        uint8_t c = (uint8_t)hexstr[i];

        if (c >= '0' && c <= '9')
            c -= '0';
        else if (c >= 'a' && c <= 'f')
            c = (uint8_t)((c - 'a') + 10);
        else if (c >= 'A' && c <= 'F')
            c = (uint8_t)((c - 'A') + 10);
        else
            continue;

        if (k++ & 0x01)
            result[j++] |= c;
        else
            result[j] = c << 4;

        if (j >= reslen)
            break;
    }

    return j;
}

/*! \brief convert binary buffer to hex string
 *
 *  @param result pointer to the hex string
 *  @param reslen length of the hex string
 *  @param bin pointer to the binary buffer
 *  @param binlen length of the binary buffer
 *
 *  @return 0 for success and -1 if result buffer is 
 */
int32_t bin2hex(char *result, int32_t reslen, uint8_t *bin, int32_t binlen)
{
    const char *hex = "0123456789ABCDEF";

    char *p;
    int32_t i;

    if (reslen < 2 * binlen)
        return -1;

    p = result;

    for (i = 0; i < binlen; i++) {
        *p++ = hex[bin[i] >> 4 & 0x0F];
        *p++ = hex[bin[i] & 0x0F];
    }

    return 0;
}

/*! \brief read configuration from a file
 *
 *  @param filename pointer to the filename
 *  @param config pointer to structure where config is stored
 *  @param client_id the client id that must match
 *
 *  @return 0 for success and -1 if result buffer is 
 */
int load_config(char *filename, Config_t *config)
{
    char line[100];
    char str[32];
    char *p;
    int val;
#ifdef CONFIG_FILE_IO
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        printf("Error: unable to open config file - %s", filename);
        return -1;
    }
    while (fgets(line, sizeof(line), fp)) {
        if (strlen(line) < 4)
            continue;

        if ((p = strchr(line, '#')) != NULL)
            *p = '\0';

        if (sscanf(line, "SERVER %256s", config->server) == 1) {
            continue;
        }

        if (sscanf(line, "PORT %d", &val) == 1) {
            config->port = (uint16_t)(val & 0xFFFF);
            continue;
        }

        if (sscanf(line, "KEY %s", str) == 1) {
            hex2bin(str, AES_SIZE * 2, config->key, AES_SIZE);
            continue;
        }

        if (sscanf(line, "PARTNER_ID %d", &val) == 1) {
            config->partner_id = (uint16_t)(val & 0xFFFF);
            continue;
        }

        if (sscanf(line, "DEVICE_MAC %s", str) == 1) {
            hex2bin(str, MAC_SIZE * 2, config->device_mac, MAC_SIZE);
            continue;
        }
    }
    config->filename = filename;
    fclose(fp);
    printf("Config Loaded\n");
#else
    // Configure server here if no file i/o is available
    config->filename = "None";
    config->port = 9758;
    config->partner_id = 2;
    strcpy(config->server, "192.168.254.77");
    hex2bin("123456123456", MAC_SIZE * 2, config->device_mac, MAC_SIZE);
    hex2bin("000102030405060708090a0b0c0d0e0f", AES_SIZE * 2, config->key, AES_SIZE);
#endif
    return 0;
}

/*! \brief print configuration
 *
 *  @param config pointer to structure where config is stored
 *
 *  @return void
 */
void print_config(Config_t *config)
{
    char key[AES_SIZE * 2 + 1];
    bin2hex(key, AES_SIZE * 2, config->key, AES_SIZE);
    key[AES_SIZE * 2] = '\0';
    char device[MAC_SIZE * 2 + 1];
    bin2hex(device, MAC_SIZE * 2, config->device_mac, MAC_SIZE);
    device[MAC_SIZE * 2] = '\0';

    printf("Configuration file: %s\n", config->filename);
    printf("Server: %s\n", config->server);
    printf("Port: %d\n", config->port);
    printf("Key: %32s\n", key);
    printf("Partner Id: %d\n", config->partner_id);
    printf("Device: %12s\n", device);
}
