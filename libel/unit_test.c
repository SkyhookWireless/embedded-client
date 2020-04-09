/*! \file libel/unit_test.c
 *  \brief unit tests - Skyhook Embedded Library
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
#include <alloca.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <math.h>
#include "../.submodules/tiny-AES128-C/aes.h"
#define SKY_LIBEL 1
#include "libel.h"
#include "crc32.h"

Sky_cache_t nv_space;

/* Example assumes a scan with 100 AP beacons
 */
#define SCAN_LIST_SIZE 100

/* Set to true to test with fake(bad) Network Time
 *     When true, should see "add_to_cache: Error appropriate with fake network time"
 */
#define FAKE_NETWORK_TIME false

/*! \brief time function
 *
 *  @param t where to save the time
 *
 *  @returns the time in seconds since the epoc (linux time)
 */

static time_t mytime(time_t *t)
{
#if FAKE_NETWORK_TIME
    /* truncate actual time to skew it much older making cache operations fail */
    if (t != NULL) {
        *t = time(NULL) & 0x0fffffff;
        return *t;
    } else
        return time(NULL) & 0x0fffffff;
#else
    if (t != NULL)
        return time(t);
    else
        return time(NULL);
#endif
}

/*! \brief set mac address. 30% are virtual AP
 *
 *  @param mac pointer to mac address
 *
 *  @returns 0 for success or negative number for error
 */
void set_mac(uint8_t *mac)
{
    uint8_t refs[][MAC_SIZE] = /* clang-format off */
    { { 0xd4, 0x85, 0x64, 0xb2, 0xf5, 0x7e },
      { 0xe4, 0x75, 0x64, 0xb2, 0xf5, 0x7e },
      { 0xf4, 0x65, 0x64, 0xb2, 0xf5, 0x7e },
      { 0x14, 0x55, 0x64, 0xb2, 0xf5, 0x7e },
      { 0x24, 0x45, 0x64, 0xb2, 0xf5, 0x7e } };
    /* clang-format on */

    if (rand() % 3 == 0) {
        /* Virtual MAC */
        memcpy(mac, refs[0], sizeof(refs[0]));
        mac[rand() % 2 + 4] ^= (0x01 << (rand() % 8));
        printf("Virt MAC\n");
    } else if (rand() % 3 != 0) {
        /* rand MAC */
        memcpy(mac, refs[rand() % sizeof(refs) / MAC_SIZE], sizeof(refs[0]));
        if (rand() % 3 == 0) {
            mac[rand() % 3] = (rand() % 256);
            printf("Rand MAC\n");
        } else {
            printf("Known MAC\n");
        }
    } else {
        /* Non Virtual MAC */
        uint8_t ref[] = { 0xd4, 0x85, 0x64, 0xb2, 0xf5, 0x7e };
        memcpy(mac, ref, sizeof(ref));
        mac[rand() % 3] = (rand() % 256);
        mac[rand() % 3 + 3] = (rand() % 256);
        printf("Non-Virt MAC\n");
    }
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
    printf("libEL %s: %.*s\n",
        level == SKY_LOG_LEVEL_CRITICAL ?
            "CRIT" :
            level == SKY_LOG_LEVEL_ERROR ?
            "ERRR" :

            level == SKY_LOG_LEVEL_WARNING ? "WARN" :
                                             level == SKY_LOG_LEVEL_DEBUG ? "DEBG" : "UNKN",
        SKY_LOG_LENGTH, s);
    return 0;
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

/*! \brief cound cache lines in use
 *
 *  @returns number of cache lines not empty
 */
int beacons_in_cache_rssi(Sky_cache_t *c)
{
    int i, j, total = 0;

    for (i = 0; i < CACHE_SIZE; i++)
        for (j = 0; j < c->cacheline[i].ap_len; j++)
            total += c->cacheline[i].beacon[j].ap.rssi;
    return total;
}

/*! \brief check for saved cache state
 *
 *   If state found, initialize random number generator based on number of beacons in saved state
 *
 *  @returns NULL for failure to restore cache, pointer to cache otherwise
 */
void *nv_cache(void)
{
    FILE *fio;
    uint8_t *p = (void *)&nv_space;

    if ((fio = fopen("nv_cache", "r")) != NULL) {
        if (fread(p, sizeof(Sky_header_t), 1, fio) == 1 && nv_space.header.magic == SKY_MAGIC &&
            nv_space.header.crc32 ==
                sky_crc32(&nv_space.header.magic,
                    (uint8_t *)&nv_space.header.crc32 - (uint8_t *)&nv_space.header.magic)) {
            if (fread(p + sizeof(Sky_header_t), nv_space.header.size - sizeof(Sky_header_t), 1,
                    fio) == 1) {
                if (validate_cache(&nv_space, &logger)) {
                    printf("validate_cache: Restoring Cache\n");
                    /* Randomize if restoring cache from previous run */
                    srand((unsigned)beacons_in_cache_rssi(&nv_space));
                    printf("Rand( %d )\n", beacons_in_cache_rssi(&nv_space));
                    return &nv_space;
                } else
                    printf("validate_cache: false\n");
            }
        }
    }
    printf("cache restore: failed\n");
    return NULL;
}

/*! \brief save cache state
 *
 *  @param p pointer to cache buffer
 *
 *  @returns 0 for success or negative number for error
 */
Sky_status_t nv_cache_save(void *p)
{
    FILE *fio;
    Sky_cache_t *c = p;

    if (validate_cache(c, &logger)) {
        if ((fio = fopen("nv_cache", "w+")) != NULL) {
            if (fwrite(p, c->header.size, 1, fio) == 1) {
                printf("nv_cache_save: cache size %d (%lu)\n", c->header.size, sizeof(Sky_cache_t));
                return SKY_SUCCESS;
            } else
                printf("fwrite failed\n");
        } else
            printf("fopen failed\n");
    } else
        printf("nv_cache_save: failed to validate cache\n");
    return SKY_ERROR;
}

/*! \brief validate fundamental functionality of the Embedded library
 *
 *  @param ac arg count
 *  @param av arg vector
 *
 *  @returns 0 for success or negative number for error
 */
int main(int ac, char **av)
{
    int i;
    Sky_errno_t sky_errno = SKY_ERROR_MAX;
    Sky_ctx_t *ctx;
    uint32_t *p;
    uint32_t bufsize;
    uint8_t aes_key[AES_SIZE] = { 0xd4, 0x85, 0x64, 0xb2, 0xf5, 0x7e, 0xd4, 0x85, 0x64, 0xb2, 0xf5,
        0x7e, 0xd4, 0x85, 0x64, 0xb2 };
    uint8_t mac[MAC_SIZE] = { 0xd4, 0x85, 0x64, 0xb2, 0xf5, 0x7e };
    time_t timestamp = time(NULL);
    void *pstate;
    uint32_t response_size;
    Sky_beacon_type_t t;
    Beacon_t b[25];
    Sky_location_t loc;

    if (sky_open(&sky_errno, mac /* device_id */, MAC_SIZE, 1, aes_key, nv_cache(),
            SKY_LOG_LEVEL_ALL, &logger, NULL, &mytime) == SKY_ERROR) {
        printf("sky_open returned bad value, Can't continue\n");
        exit(-1);
    }
    /* Test sky_sizeof_workspace */
    bufsize = sky_sizeof_workspace();

    /* sky_sizeof_workspace should return a value below 5k and above 0 */
    if (bufsize == 0 || bufsize > 4096) {
        printf("sky_sizeof_workspace returned bad value, Can't continue\n");
        exit(-1);
    }

    /* allocate workspace */
    ctx = (Sky_ctx_t *)(p = alloca(bufsize));

    /* initialize the workspace */
    memset(p, 0, bufsize);

    if (sky_new_request(ctx, bufsize, &sky_errno) != ctx) {
        printf("sky_new_request() returned bad value\n");
        printf("sky_errno contains '%s'\n", sky_perror(sky_errno));
    }

    printf("ctx: magic:%08X size:%08X crc:%08X\n", ctx->header.magic, ctx->header.size,
        ctx->header.crc32);

    for (i = 0; i < sizeof(b) / sizeof(b[0]); i++) {
        b[i].ap.magic = BEACON_MAGIC;
        b[i].ap.type = SKY_BEACON_AP;
        set_mac(b[i].ap.mac);
        b[i].ap.freq = (rand() % 4000) + 2000;
        b[i].ap.rssi = -rand() % 128;
    }

    for (i = 0; i < sizeof(b) / sizeof(b[0]); i++) {
        if (sky_add_ap_beacon(ctx, &sky_errno, b[i].ap.mac, timestamp - (rand() % 3), b[i].ap.rssi,
                b[i].ap.freq, 1)) {
            printf("sky_add_ap_beacon sky_errno contains '%s'\n", sky_perror(sky_errno));
        } else {
            printf(
                "Added Test Beacon % 2d: Type: AP(%d), MAC %02X:%02X:%02X:%02X:%02X:%02X rssi: %-4d %-4d MHz\n",
                i, b[i].ap.type, b[i].ap.mac[0], b[i].ap.mac[1], b[i].ap.mac[2], b[i].ap.mac[3],
                b[i].ap.mac[4], b[i].ap.mac[5], b[i].ap.rssi, b[i].ap.freq);
        }
    }

    for (i = 0; i < 3; i++) {
        b[i].nbiot.magic = BEACON_MAGIC;
        b[i].nbiot.type = SKY_BEACON_NBIOT;
        b[i].nbiot.mcc = 200 + (rand() % 599);
        b[i].nbiot.mnc = rand() % 999;
        b[i].nbiot.e_cellid = rand() % 268435456;
        b[i].nbiot.tac = rand();
        b[i].nbiot.rssi = -(44 + (rand() % 112));
    }

    for (i = 0; i < 3; i++) {
        if (sky_add_cell_nb_iot_beacon(ctx, &sky_errno, b[i].nbiot.mcc, b[i].nbiot.mnc,
                b[i].nbiot.e_cellid, b[i].nbiot.tac, timestamp, b[i].nbiot.rssi, 1)) {
            printf("sky_add_nbiot_beacon sky_errno contains '%s'\n", sky_perror(sky_errno));
        } else {
            printf(
                "Added Test Beacon % 2d: Type: NB-IoT(%d), mcc: %d, mnc: %d, e_cellid: %d, tac: %d, rssi: %d\n",
                i, b[i].nbiot.type, b[i].nbiot.mcc, b[i].nbiot.mnc, b[i].nbiot.e_cellid,
                b[i].nbiot.tac, b[i].nbiot.rssi);
        }
    }

    for (i = 0; i < 2; i++) {
        b[i].gsm.magic = BEACON_MAGIC;
        b[i].gsm.type = SKY_BEACON_GSM;
        b[i].gsm.lac = rand() % 65535;
        b[i].gsm.ci = rand() % 65535;
        b[i].gsm.mcc = 200 + (rand() % 599);
        b[i].gsm.mnc = rand() % 999;
        b[i].gsm.rssi = -(32 + (rand() % 96));
    }

    for (i = 0; i < 2; i++) {
        if (sky_add_cell_gsm_beacon(ctx, &sky_errno, b[i].gsm.lac, b[i].gsm.ci, b[i].gsm.mcc,
                b[i].gsm.mnc, timestamp, b[i].gsm.rssi, 1)) {
            printf("sky_add_gsm_beacon sky_errno contains '%s'\n", sky_perror(sky_errno));
        } else {
            printf(
                "Added Test Beacon % 2d: Type: GSM(%d), lac: %d, ui: %d, mcc: %d, mnc: %d, rssi: %d\n",
                i, b[i].gsm.type, b[i].gsm.lac, b[i].gsm.ci, b[i].gsm.mcc, b[i].gsm.mnc,
                b[i].gsm.rssi);
        }
    }

    /* Determine how big the network request buffer must be, and allocate a */
    /* buffer of that length. This function must be called for each request. */
    if (sky_sizeof_request_buf(ctx, &bufsize, &sky_errno) == SKY_ERROR) {
        printf("Error getting size of request buffer: %s\n", sky_perror(sky_errno));
        exit(-1);
    } else
        printf("Required buffer size = %d\n", bufsize);

    switch (sky_finalize_request(ctx, &sky_errno, malloc(bufsize), bufsize, &loc, &response_size)) {
    case SKY_FINALIZE_LOCATION:

        printf("sky_finalize_request: GPS: %d.%06d,%d.%06d,%d\n", (int)loc.lat,
            (int)fabs(round(1000000 * (loc.lat - (int)loc.lat))), (int)loc.lon,
            (int)fabs(round(1000000 * (loc.lon - (int)loc.lon))), loc.hpe);

        if (sky_close(&sky_errno, &pstate))
            printf("sky_close sky_errno contains '%s'\n", sky_perror(sky_errno));
        if (pstate != NULL)
            nv_cache_save(pstate);
        exit(0);
        break;
    default:
    case SKY_FINALIZE_ERROR:
        printf("sky_finalize_request sky_errno contains '%s'\n", sky_perror(sky_errno));
        if (sky_close(&sky_errno, &pstate))
            printf("sky_close sky_errno contains '%s'\n", sky_perror(sky_errno));
        exit(-1);
        break;
    case SKY_FINALIZE_REQUEST:
        break;
    }
    dump_workspace(ctx);

    for (t = SKY_BEACON_AP; t != SKY_BEACON_MAX; t++) {
        printf("get_num_beacons: %d, %d\n", t, i = get_num_beacons(ctx, t));
        if (t == SKY_BEACON_AP)
            for (i--; i >= 0; i--) {
                uint8_t *m = get_ap_mac(ctx, i);
                m = m;

                printf("get_ap_mac:       %d MAC %02X:%02X:%02X:%02X:%02X:%02X\n", i, m[0], m[1],
                    m[2], m[3], m[4], m[5]);
                printf("get_ap_freq:   %d, %lld\n", i, (long long)get_ap_freq(ctx, i));
                printf("get_ap_rssi:      %d, %lld\n", i, (long long)get_ap_rssi(ctx, i));
                printf("get_ap_is_connected:      %d, %d\n", i, get_ap_is_connected(ctx, i));
                printf("get_ap_age:      %d, %lld\n", i, (long long)get_ap_age(ctx, i));
                ctx->beacon[i].ap.property.used = (rand() % 9 > 2);
            }
        if (t == SKY_BEACON_GSM)
            for (i--; i >= 0; i--) {
                printf("get_gsm_ci:       %d, %d\n", i, (int)get_gsm_ci(ctx, i));
                printf("get_gsm_mcc:       %d, %d\n", i, (int)get_gsm_mcc(ctx, i));
                printf("get_gsm_mnc:       %d, %d\n", i, (int)get_gsm_mnc(ctx, i));
                printf("get_gsm_lac:       %d, %d\n", i, (int)get_gsm_lac(ctx, i));
                printf("get_gsm_rssi:      %d, %lld\n", i, (long long)get_gsm_rssi(ctx, i));
                printf("get_gsm_is_connected:      %d, %d\n", i, get_gsm_is_connected(ctx, i));
                printf("get_gsm_age:      %d, %lld\n", i, (long long)get_gsm_age(ctx, i));
            }
        if (t == SKY_BEACON_NBIOT)
            for (i--; i >= 0; i--) {
                printf("get_nbiot_mcc:     %d, %d\n", i, (int)get_nbiot_mcc(ctx, i));
                printf("get_nbiot_mnc:     %d, %d\n", i, (int)get_nbiot_mnc(ctx, i));
                printf("get_nbiot_ecellid: %d, %d\n", i, (int)get_nbiot_ecellid(ctx, i));
                printf("get_nbiot_tac:     %d, %d\n", i, (int)get_nbiot_tac(ctx, i));
                printf("get_nbiot_rssi:    %d, %lld\n", i, (long long)get_nbiot_rssi(ctx, i));
                printf("get_nbiot_is_connected:      %d, %d\n", i, get_nbiot_is_connected(ctx, i));
                printf("get_nbiot_age:      %d, %lld\n", i, (long long)get_nbiot_age(ctx, i));
            }
    }

        /* Save to cache with a location */
        /* Set some new dynamic parameters */
        /* Create location and add to cache */
#if 0
    if (sky_decode_response(ctx, &sky_errno, NULL, 0, &loc))
        printf("sky_decode_response sky_errno contains '%s'\n",
            sky_perror(sky_errno))
#else
    loc.lat = -80.0 - ((float)rand() / RAND_MAX * 30.0);
    loc.lon = 30.0 + (30.0 * (float)rand() / RAND_MAX);
    loc.hpe = 30.0 + (500.0 * (float)rand() / RAND_MAX);
    loc.time = (*ctx->gettime)(NULL);
    loc.location_source = SKY_LOCATION_SOURCE_WIFI;
    loc.location_status = SKY_LOCATION_STATUS_SUCCESS;

#if FAKE_NETWORK_TIME
    if (add_to_cache(ctx, &loc) != SKY_SUCCESS)
        printf("add_to_cache: Error appropriate with fake network time\n");
#else
    add_to_cache(ctx, &loc);
#endif
    dump_cache(ctx);
    /* simulate new config from server */
    ctx->cache->config.total_beacons = 8;
    ctx->cache->config.max_ap_beacons = 6;
    ctx->cache->config.cache_match_threshold = 49;

#endif

    if (sky_close(&sky_errno, &pstate))
        printf("sky_close sky_errno contains '%s'\n", sky_perror(sky_errno));
    if (pstate != NULL)
        nv_cache_save(pstate);
}
