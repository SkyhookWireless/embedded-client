#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>
#include <unistd.h>
#include <ctype.h>

#include "libel.h"
#include "libez.h"
#include "elgconfig.h"

/* returns number of result bytes that were successfully parsed */
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

int32_t bin2hex(char *buff, int32_t buff_len, uint8_t *data, int32_t data_len)
{
    const char *hex = "0123456789ABCDEF";

    char *p;
    int32_t i;

    if (buff_len < 2 * data_len)
        return -1;

    p = buff;

    for (i = 0; i < data_len; i++) {
        *p++ = hex[data[i] >> 4 & 0x0F];
        *p++ = hex[data[i] & 0x0F];
    }
    if (p - buff <= buff_len)
        *p++ = '\0';

    return 0;
}

int load_config(char *filename, Config_t *config)
{
    char line[100];
    char *p;
    int val;

    printf("%s(): %s\n", __FUNCTION__, filename);
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        printf("Error: unable to open config file - %s\n", filename);
        return -1;
    }

    //	memset(config, 0, sizeof(Config_t));
    while (fgets(line, sizeof(line), fp)) {
        if (strlen(line) < 4)
            continue;

        if ((p = strchr(line, '#')) != NULL)
            *p = '\0';

        if (sscanf(line, "SCAN_FILE %256s", config->scan_file) == 1) {
            continue;
        }

        if (sscanf(line, "NUM_SCANS %d", &val) == 1) {
            config->num_scans = (int32_t)val;
            continue;
        }
    }
    fclose(fp);

    return 0;
}

void print_config(Config_t *config)
{
    char key[KEY_SIZE * 2 + 1];
    bin2hex(key, KEY_SIZE * 2, config->key, KEY_SIZE);
    key[KEY_SIZE * 2] = '\0';
    char device[MAX_DEVICE_ID * 2 + 1] = { '\0' };
    bin2hex(device, sizeof(device), config->device_id, config->device_len);

    XPS_log_debug("Scan File: %s", config->scan_file);
    XPS_log_debug("# of scans: %d", config->num_scans);
}
