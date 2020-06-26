#ifndef ELGCONFIG_H
#define ELGCONFIG_H

#define MAC_SIZE 6
#define KEY_SIZE 16

typedef struct client_config {
    uint16_t port;
    uint16_t partner_id;
    uint16_t client_id;
    uint16_t rate;
    int32_t num_scans;

    char server[80];
    char scan_file[256];

    uint8_t device_id[MAX_DEVICE_ID];
    uint8_t device_len;
    uint8_t key[KEY_SIZE];
    int8_t use_cache;
    int8_t lib_log_level;
} Config_t;

uint32_t hex2bin(char *hexstr, uint32_t hexlen, uint8_t *result, uint32_t reslen);

int32_t bin2hex(char *buff, int32_t buff_len, uint8_t *data, int32_t data_len);

int load_config(char *filename, Config_t *config);

void print_config(Config_t *config);

#endif
