/*! \file sample_client/sample_client.c
 *  \brief Sample Client - Skyhook Embedded Library
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
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <math.h>
#include "../.submodules/tiny-AES128-C/aes.h"

#define SKY_LIBEL 1
#include "libel.h"

#include "send.h"
#include "config.h"

#define SCAN_LIST_SIZE 5

/* scan list definitions (platform dependant) */
struct ap_scan {
    char mac[MAC_SIZE * 2];
    uint32_t age;
    uint32_t channel;
    int16_t rssi;
};

/* some rssi values intentionally out of range */
struct ap_scan aps[] = /* clang-format off */ 
                    { { "283B8264E08B", 300, 3660, -8 },
                      { "826AB092DC99", 300, 3660, -130 },
                      { "283B823629F0", 300, 3660, -90 },
                      { "283B821C712A", 300, 3660, -77 },
                      { "0024D2E08E5D", 300, 3660, -92 },
                      { "283B821CC232", 300, 3660, -91 },
                      { "74DADA5E1015", 300, 3660, -88 },
                      { "FC75164E9CA2", 300, 3660, -88 },
                      { "B482FEA46221", 300, 3660, -89 },
                      { "EC22809E00DB", 300, 3660, -90 } };
/* clang-format on */

/*! \brief check for saved cache state
 *
 *  @param nv_space pointer to saved cache state
 *  @param client_id
 *
 *  @returns NULL for failure to restore cache, pointer to cache otherwise
 */
void *nv_cache(void *nv_space, uint16_t client_id)
{
    char cache_name[16];
    sprintf(cache_name, "nv_cache_%d", client_id);

    struct {
        uint32_t magic;
        uint32_t size;
        uint32_t time;
        uint32_t crc32;
    } tmp;
    FILE *fio;

    if ((fio = fopen(cache_name, "r")) != NULL) {
        if (fread((void *)&tmp, sizeof(tmp), 1, fio) == 1 &&
            tmp.crc32 == sky_crc32(&tmp.magic, (uint8_t *)&tmp.crc32 - (uint8_t *)&tmp.magic)) {
            nv_space = malloc(tmp.size);
            rewind(fio);
            if (fread(nv_space, tmp.size, 1, fio) == 1)
                return nv_space;
        }
    }
    return NULL;
}

/*! \brief save cache state
 *
 *  @param p - pointer to cache
 *  @param client_id - client id
 *
 *  @returns 0 for success or negative number for error
 */
Sky_status_t nv_cache_save(void *p, uint16_t client_id)
{
    char cache_name[16];
    sprintf(cache_name, "nv_cache_%d", client_id);

    FILE *fio;
    struct {
        uint32_t magic;
        uint32_t size;
        uint32_t time;
        uint32_t crc32;
    } *c = p;

    if (c->crc32 == sky_crc32(&c->magic, (uint8_t *)&c->crc32 - (uint8_t *)&c->magic)) {
        if ((fio = fopen(cache_name, "w+")) != NULL) {
            if (fwrite(p, c->size, 1, fio) == 1) {
                printf("nv_cache_save: cache size %d\n", c->size);
                return 0;
            } else
                printf("fwrite failed\n");
        } else
            printf("fopen failed\n");
    } else
        printf("nv_cache_save: failed to validate cache\n");
    return -1;
}

/*! \brief generate random byte sequence
 *
 *  @param rand_buf pointer to buffer where rand bytes are put
 *  @param bufsize length of rand bytes
 *
 *  @returns 0 for failure, length of rand sequence for success
 */
int rand_bytes(uint8_t *rand_buf, uint32_t bufsize)
{
    int i;

    if (!rand_buf)
        return 0;

    for (i = 0; i < bufsize; i++)
        rand_buf[i] = rand() % 256;
    return bufsize;
}

/*! \brief logging function
 *
 *  @param level log level of this message
 *  @param s this message
 *
 *  @returns 0 for success or negative number for error
 */
int logger(Sky_log_level_t level, char *s)
{
    printf("Skyhook libEL %s: %.*s\n",
        level == SKY_LOG_LEVEL_CRITICAL ?
            "CRIT" :
            level == SKY_LOG_LEVEL_ERROR ?
            "ERRR" :
            level == SKY_LOG_LEVEL_WARNING ? "WARN" :
                                             level == SKY_LOG_LEVEL_DEBUG ? "DEBG" : "UNKN",
        100, s);
    return 0;
}

/*! \brief time function
 *
 *  @param t where to save the time
 *
 *  @returns the time in seconds since the epoc (linux time)
 */
static time_t mytime(time_t *t)
{
    if (t != NULL) {
        return time(t);
    } else
        return time(NULL);
}

/*! \brief validate fundamental functionality of the Embedded Library
 *
 *  @param argc count
 *  @param argv vector of arguments
 *
 *  @returns 0 for success or negative number for error
 */
int main(int argc, char *argv[])
{
    int i;
    Sky_errno_t sky_errno = -1;
    Sky_ctx_t *ctx;
    Sky_status_t ret_status;
    uint32_t *p;
    uint32_t bufsize;
    void *pstate;
    void *prequest;
    uint8_t *response;
    uint32_t request_size;
    uint32_t response_size;
    uint32_t timestamp;
    Sky_location_t loc;
    void *nv_space = NULL;
    char *configfile = NULL;

    Config_t config;
    int ret_code = 0;
    if (argc > 1)
        configfile = argv[1];
    else
        configfile = "sample_client.conf";

    /* Load the configuration */
    ret_code = load_config(configfile, &config);
    if (ret_code == -1)
        exit(-1);
    // print_config(&config);

    /* Comment in order to disable cache loading */
    nv_space = nv_cache(nv_space, 1);

    timestamp = mytime(NULL); /* time scans were prepared */
    /* Initialize the Skyhook resources */
    if (sky_open(&sky_errno, config.device_mac, MAC_SIZE, config.partner_id, config.partner_id,
            config.key, nv_space, SKY_LOG_LEVEL_ALL, &logger, &rand_bytes, &mytime) == SKY_ERROR) {
        printf("sky_open returned bad value, Can't continue\n");
        exit(-1);
    }

    /* Get the size of workspace needed */
    bufsize = sky_sizeof_workspace();
    if (bufsize == 0 || bufsize > 4096) {
        printf("sky_sizeof_workspace returned bad value, Can't continue\n");
        exit(-1);
    }

    /* Allocate and initialize workspace */
    ctx = (Sky_ctx_t *)(p = malloc(bufsize));
    memset(p, 0, bufsize);

    /* Start new request */
    if (sky_new_request(ctx, bufsize, &sky_errno) != ctx) {
        printf("sky_new_request() returned bad value\n");
        printf("sky_errno contains '%s'\n", sky_perror(sky_errno));
    }

    /* Add APs to the request */
    for (i = 0; i < sizeof(aps) / sizeof(struct ap_scan); i++) {
        uint8_t mac[MAC_SIZE];
        hex2bin(aps[i].mac, MAC_SIZE * 2, mac, MAC_SIZE);
        ret_status = sky_add_ap_beacon(
            ctx, &sky_errno, mac, timestamp - aps[i].age, aps[i].rssi, aps[i].channel, 1);
        if (ret_status == SKY_SUCCESS)
            printf("AP #%d added\n", i);
        else
            printf("sky_add_ap_beacon sky_errno contains '%s'", sky_perror(sky_errno));
    }

    /* Add GSM cell */
    ret_status = sky_add_cell_gsm_beacon(ctx, &sky_errno,
        16101, // lac
        14962, // ci
        603, // mcc
        1, // mnc
        timestamp - 315, // timestamp
        -100, // rssi
        0); // serving

    if (ret_status == SKY_SUCCESS)
        printf("Cell added\n");
    else
        printf("Error adding GSM cell: '%s'\n", sky_perror(sky_errno));
    sky_add_gnss(ctx, &sky_errno, 41.722651, 76.388422, 100, 219, 40, 0, 0, timestamp - 100);
#if 0
    /* Add LTE cell */
    ret_status = sky_add_cell_lte_beacon(ctx, &sky_errno,
        12345, // tac
        27907073, // eucid
        311, // mcc
        480, // mnc
        timestamp - 315, // timestamp
        -100, // rssi
        1); // serving

    if (ret_status == SKY_SUCCESS)
        printf("Cell added\n");
    else
        printf("Error adding LTE cell: '%s'\n", sky_perror(sky_errno));

    /* Add NBIOT cell */
    ret_status = sky_add_cell_nb_iot_beacon(ctx, &sky_errno,
        311, // mcc
        480, // mnc
        209979678, // eucid
        25187, // tac
        timestamp - 315, // timestamp
        -143, // rssi
        0); // serving

    if (ret_status == SKY_SUCCESS)
        printf("Cell added\n");
    else
        printf("Error adding NBIOT cell: '%s'\n", sky_perror(sky_errno));
#endif
    /* Determine how big the network request buffer must be, and allocate a */
    /* buffer of that length. This function must be called for each request. */
    ret_status = sky_sizeof_request_buf(ctx, &request_size, &sky_errno);

    if (ret_status == SKY_ERROR) {
        printf("Error getting size of request buffer: %s\n", sky_perror(sky_errno));
        exit(-1);
    } else
        printf("Required buffer size = %d\n", request_size);

    prequest = malloc(request_size);

    /* Finalize the request. This will return either SKY_FINALIZE_LOCATION, in */
    /* which case the loc parameter will contain the location result which was */
    /* obtained from the cache, or SKY_FINALIZE_REQUEST, which means that the */
    /* request buffer must be sent to the Skyhook server. */
    Sky_finalize_t finalize =
        sky_finalize_request(ctx, &sky_errno, prequest, request_size, &loc, &response_size);

    if (finalize == SKY_FINALIZE_ERROR) {
        printf("sky_finalize_request sky_errno contains '%s'", sky_perror(sky_errno));
        if (sky_close(&sky_errno, &pstate))
            printf("sky_close sky_errno contains '%s'\n", sky_perror(sky_errno));
        exit(-1);
    }

    switch (finalize) {
    case SKY_FINALIZE_REQUEST:
        /* Need to send the request to the server. */
        response = malloc(response_size * sizeof(uint8_t));
        printf("server=%s, port=%d\n", config.server, config.port);
        printf("Sending request of length %d to server\n", request_size);

        int32_t rc = send_request((char *)prequest, (int)request_size, response, response_size,
            config.server, config.port);

        if (rc > 0)
            printf("Received response of length %d from server\n", rc);
        else {
            printf("Bad response from server\n");
            ret_status = sky_close(&sky_errno, &pstate);
            if (ret_status != SKY_SUCCESS)
                printf("sky_close sky_errno contains '%s'\n", sky_perror(sky_errno));
            exit(-1);
        }

        /* Decode the response from server or cache */
        ret_status = sky_decode_response(ctx, &sky_errno, response, bufsize, &loc);

        if (ret_status != SKY_SUCCESS)
            printf("sky_decode_response error: '%s'\n", sky_perror(sky_errno));
        break;
    case SKY_FINALIZE_LOCATION:
        /* Location was found in the cache. No need to go to server. */
        printf("Location found in cache\n");
        break;
    case SKY_FINALIZE_ERROR:
        printf("Error finalizing request\n");
        exit(-1);
        break;
    }

    printf("Skyhook location: status: %s, lat: %d.%06d, lon: %d.%06d, hpe: %d, source: %d\n",
        sky_pserver_status(loc.location_status), (int)loc.lat,
        (int)fabs(round(1000000 * (loc.lat - (int)loc.lat))), (int)loc.lon,
        (int)fabs(round(1000000 * (loc.lon - (int)loc.lon))), loc.hpe, loc.location_source);

    ret_status = sky_close(&sky_errno, &pstate);

    if (ret_status != SKY_SUCCESS)
        printf("sky_close sky_errno contains '%s'\n", sky_perror(sky_errno));

    if (pstate != NULL)
        nv_cache_save(pstate, 1);

    printf("Done.\n\n");
}
