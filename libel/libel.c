/*! \file libel/libel.c
 *  \brief sky entry points - Skyhook Embedded Library
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
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#define SKY_LIBEL
#include "libel.h"
#include "proto.h"

#define SW_VERSION 0

/*! \brief keep track of when the user has opened the library */
static uint32_t sky_open_flag = 0;

/*! \brief keep track of the device ID */
static Sky_cache_t cache;

/*! \brief keep track of logging function */
static Sky_randfn_t sky_rand_bytes;
static Sky_loggerfn_t sky_logf;
static Sky_log_level_t sky_min_level;
static Sky_timefn_t sky_time;

/* Local functions */
static bool validate_device_id(uint8_t *device_id, uint32_t id_len);
static bool validate_partner_id(uint32_t partner_id);
static bool validate_aes_key_id(uint32_t aes_key_id);
static bool validate_aes_key(uint8_t aes_key[AES_SIZE]);

/*! \brief Initialize Skyhook library and verify access to resources
 *
 *  @param sky_errno if sky_open returns failure, sky_errno is set to the error code
 *  @param device_id Device unique ID (example mac address of the device)
 *  @param id_len length if the Device ID, typically 6, Max 16 bytes
 *  @param partner_id Skyhook assigned credentials
 *  @param aes_key_id Skyhook assigned credentials
 *  @param aes_key Skyhook assigned encryption key
 *  @param state_buf pointer to a state buffer (provided by sky_close) or NULL
 *  @param min_level logging function is called for msg with equal or greater level
 *  @param logf pointer to logging function
 *  @param rand_bytes pointer to random function
 *  @param gettime pointer to time function
 *
 *  @return sky_status_t SKY_SUCCESS or SKY_ERROR
 *
 *  sky_open can be called many times with the same parameters. This does
 *  nothing and returns SKY_SUCCESS. However, sky_close must be called
 *  in order to change the parameter values. Device ID length will
 *  be truncated to 16 if larger, without causing an error.
 */
Sky_status_t sky_open(Sky_errno_t *sky_errno, uint8_t *device_id, uint32_t id_len,
    uint32_t partner_id, uint32_t aes_key_id, uint8_t aes_key[16], void *state_buf,
    Sky_log_level_t min_level, Sky_loggerfn_t logf, Sky_randfn_t rand_bytes, Sky_timefn_t gettime)
{
    Sky_cache_t *sky_state = state_buf;
    int i = 0;
    int j = 0;

    /* Only consider up to 16 bytes. Ignore any extra */
    id_len = (id_len > MAX_DEVICE_ID) ? 16 : id_len;

    if (sky_state != NULL && !validate_cache(sky_state, logf))
        sky_state = NULL;

    sky_min_level = min_level;
    sky_logf = logf;
    sky_rand_bytes = rand_bytes == NULL ? sky_rand_fn : rand_bytes;
    sky_time = (gettime == NULL) ? &time : gettime;

    /* if open already */
    if (sky_open_flag && sky_state) {
        /* parameters must be the same (no-op) or fail */
        if (memcmp(device_id, sky_state->sky_device_id, id_len) == 0 &&
            id_len == sky_state->sky_id_len && sky_state->header.size == sizeof(cache) &&
            partner_id == sky_state->sky_partner_id && aes_key_id == sky_state->sky_aes_key_id &&
            memcmp(aes_key, sky_state->sky_aes_key, sizeof(sky_state->sky_aes_key)) == 0)
            return sky_return(sky_errno, SKY_ERROR_NONE);
        else
            return sky_return(sky_errno, SKY_ERROR_ALREADY_OPEN);
    } else if (sky_state)
        cache = *sky_state;
    else {
        cache.newest = NULL;
        cache.header.magic = SKY_MAGIC;
        cache.header.size = sizeof(cache);
        cache.header.time = (uint32_t)(*sky_time)(NULL);
        cache.header.crc32 = sky_crc32(
            &cache.header.magic, (uint8_t *)&cache.header.crc32 - (uint8_t *)&cache.header.magic);
        cache.len = CACHE_SIZE;
        for (i = 0; i < CACHE_SIZE; i++) {
            for (j = 0; j < TOTAL_BEACONS; j++) {
                cache.cacheline[i].time = 0;
                cache.cacheline[i].len = 0;
                cache.cacheline[i].beacon[j].h.magic = BEACON_MAGIC;
                cache.cacheline[i].beacon[j].h.type = SKY_BEACON_MAX;
            }
        }
    }

    /* Sanity check */
    if (!validate_device_id(device_id, id_len) || !validate_partner_id(partner_id) ||
        !validate_aes_key_id(aes_key_id) || !validate_aes_key(aes_key))
        return sky_return(sky_errno, SKY_ERROR_BAD_PARAMETERS);

    cache.sky_id_len = id_len;
    memcpy(cache.sky_device_id, device_id, id_len);
    cache.sky_partner_id = partner_id;
    cache.sky_aes_key_id = aes_key_id;
    memcpy(cache.sky_aes_key, aes_key, sizeof(cache.sky_aes_key));
    sky_open_flag = true;

    if (logf != NULL)
        (*logf)(SKY_LOG_LEVEL_DEBUG, "Skyhook Embedded Library (Version: " VERSION ")");

    return sky_return(sky_errno, SKY_ERROR_NONE);
}

/*! \brief Determines the size of the non-volatile memory state buffer
 *
 *  @param sky_state Pointer to state buffer
 *
 *  @return Size of state buffer or 0 to indicate that the buffer was invalid
 */
int32_t sky_sizeof_state(void *sky_state)
{
    Sky_cache_t *c = sky_state;

    /* Cache space required
     *
     * header - Magic number, size of space, checksum
     * body - number of entries
     */
    if (!validate_cache(c, NULL))
        return 0;
    else
        return c->header.size;
}

/*! \brief Determines the size of the workspace required to build request
 *
 *  @return Size of state buffer or 0 to indicate that the buffer was invalid
 */
int32_t sky_sizeof_workspace(void)
{
    /* Total space required
     *
     * header - Magic number, size of space, checksum
     * body - number of beacons, beacon data, gnss, request buffer
     */
    return sizeof(Sky_ctx_t);
}

/*! \brief Initializes the workspace provided ready to build a request
 *
 *  @param workspace_buf Pointer to workspace provided by user
 *  @param bufsize Workspace buffer size (from sky_sizeof_workspace)
 *  @param sky_errno Pointer to error code
 *
 *  @return Pointer to the initialized workspace context buffer or NULL
 */
Sky_ctx_t *sky_new_request(void *workspace_buf, uint32_t bufsize, Sky_errno_t *sky_errno)
{
    int i;
    Sky_ctx_t *ctx = (Sky_ctx_t *)workspace_buf;

    if (!sky_open_flag) {
        sky_return(sky_errno, SKY_ERROR_NEVER_OPEN);
        return NULL;
    }
    if (bufsize != sky_sizeof_workspace() || workspace_buf == NULL) {
        sky_return(sky_errno, SKY_ERROR_BAD_PARAMETERS);
        return NULL;
    }

    /* update header in workspace */
    ctx->header.magic = SKY_MAGIC;
    ctx->header.size = bufsize;
    ctx->header.time = (uint32_t)(*sky_time)(NULL);
    ctx->header.crc32 = sky_crc32(
        &ctx->header.magic, (uint8_t *)&ctx->header.crc32 - (uint8_t *)&ctx->header.magic);

    ctx->cache = &cache;
    ctx->min_level = sky_min_level;
    ctx->logf = sky_logf;
    ctx->rand_bytes = sky_rand_bytes;
    ctx->gettime = sky_time;
    ctx->len = 0; /* empty */
    ctx->ap_len = 0; /* empty */
    ctx->gps.lat = NAN; /* empty */
    for (i = 0; i < TOTAL_BEACONS; i++) {
        ctx->beacon[i].h.magic = BEACON_MAGIC;
        ctx->beacon[i].h.type = SKY_BEACON_MAX;
    }
    ctx->connected = -1; /* all unconnected */
    if (ctx->cache->len) {
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "%d cachelines present", ctx->cache->len)
        dump_cache(ctx);
    }
    return ctx;
}

/*! \brief  Adds the wifi ap information to the request context
 *
 *  @param ctx Skyhook request context
 *  @param sky_errno skyErrno is set to the error code
 *  @param mac pointer to mac address of the Wi-Fi beacon
 *  @param timestamp time in seconds (from 1970 epoch) indicating when the scan was performed, (time_t)-1 if unknown
 *  @param rssi Received Signal Strength Intensity, -10 through -127, -1 if unknown
 *  @param channel center frequency of channel in MHz, -1 if unknown
 *  @param is_connected this beacon is currently connected, false if unknown
 *
 *  @return SKY_SUCCESS or SKY_ERROR and sets sky_errno with error code
 */
Sky_status_t sky_add_ap_beacon(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, uint8_t mac[6],
    time_t timestamp, int16_t rssi, int32_t channel, bool is_connected)
{
    Beacon_t b;

    if (!sky_open_flag)
        return sky_return(sky_errno, SKY_ERROR_NEVER_OPEN);

    if (!validate_workspace(ctx))
        return sky_return(sky_errno, SKY_ERROR_BAD_WORKSPACE);

    /* Create AP beacon */
    b.h.magic = BEACON_MAGIC;
    b.h.type = SKY_BEACON_AP;
    memcpy(b.ap.mac, mac, MAC_SIZE);
    /* If beacon has meaningful timestamp */
    /* scan was before sky_new_request and since Mar 1st 2019 */
    if (ctx->header.time > timestamp && timestamp > TIMESTAMP_2019_03_01)
        b.ap.age = ctx->header.time - timestamp;
    else
        b.ap.age = 0;
    if (channel < 2400 || channel > 6000)
        channel = 0; /* 0's not sent to server */
    if (rssi > -10 || rssi < -127)
        rssi = -1;
    b.ap.freq = channel;
    b.ap.rssi = rssi;
    b.ap.in_cache = false;

    return add_beacon(ctx, sky_errno, &b, is_connected);
}

/*! \brief Add an lte cell beacon to request context
 *
 *  @param ctx Skyhook request context
 *  @param sky_errno skyErrno is set to the error code
 *  @param tac lte tracking area code identifier (1-65,535),0 if unknown
 *  @param e_cellid lte beacon identifier 28bit (0-268,435,456)
 *  @param mcc mobile country code (200-799)
 *  @param mnc mobile network code (0-999)
 *  @param timestamp time in seconds (from 1970 epoch) indicating when the scan was performed, (time_t)-1 if unknown
 *  @param rsrp Received Signal Receive Power, range -140 to -40dbm, -1 if unknown
 *  @param is_connected this beacon is currently connected, false if unknown
 *
 *  @return SKY_SUCCESS or SKY_ERROR and sets sky_errno with error code
 */
Sky_status_t sky_add_cell_lte_beacon(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, uint16_t tac,
    uint32_t e_cellid, uint16_t mcc, uint16_t mnc, time_t timestamp, int16_t rsrp,
    bool is_connected)
{
    Beacon_t b;

    if (!sky_open_flag)
        return sky_return(sky_errno, SKY_ERROR_NEVER_OPEN);

    if (!validate_workspace(ctx))
        return sky_return(sky_errno, SKY_ERROR_BAD_WORKSPACE);

    if (ctx->len > (TOTAL_BEACONS - 1)) /* room for one more? */
        return sky_return(sky_errno, SKY_ERROR_TOO_MANY);

    /* Create LTE beacon */
    b.h.magic = BEACON_MAGIC;
    b.h.type = SKY_BEACON_LTE;
    /* If beacon has meaningful timestamp */
    /* scan was before sky_new_request and since Mar 1st 2019 */
    if (ctx->header.time > timestamp && timestamp > TIMESTAMP_2019_03_01)
        b.lte.age = ctx->header.time - timestamp;
    else
        b.lte.age = 0;
    if (rsrp > -40 || rsrp < -140)
        rsrp = -1;
    b.lte.tac = tac;
    b.lte.e_cellid = e_cellid;
    b.lte.mcc = mcc;
    b.lte.mnc = mnc;
    b.lte.rssi = rsrp;

    return add_beacon(ctx, sky_errno, &b, is_connected);
}

/*! \brief Adds a gsm cell beacon to the request context
 *
 *  @param ctx Skyhook request context
 *  @param sky_errno skyErrno is set to the error code
 *  @param lac gsm location area code identifier (1-65,535)
 *  @param ci gsm cell identifier (0-65,535)
 *  @param mcc mobile country code (200-799)
 *  @param mnc mobile network code  (0-999)
 *  @param timestamp time in seconds (from 1970 epoch) indicating when the scan was performed, (time_t)-1 if unknown
 *  @param rssi Received Signal Strength Intensity, range -128 to -32dbm, -1 if unknown
 *  @param is_connected this beacon is currently connected, false if unknown
 *
 *  @return SKY_SUCCESS or SKY_ERROR and sets sky_errno with error code
 */
Sky_status_t sky_add_cell_gsm_beacon(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, uint16_t lac,
    uint32_t ci, uint16_t mcc, uint16_t mnc, time_t timestamp, int16_t rssi, bool is_connected)
{
    Beacon_t b;

    if (!sky_open_flag)
        return sky_return(sky_errno, SKY_ERROR_NEVER_OPEN);

    if (!validate_workspace(ctx))
        return sky_return(sky_errno, SKY_ERROR_BAD_WORKSPACE);

    if (ctx->len > (TOTAL_BEACONS - 1)) /* room for one more? */
        return sky_return(sky_errno, SKY_ERROR_TOO_MANY);

    /* Create GSM beacon */
    b.h.magic = BEACON_MAGIC;
    b.h.type = SKY_BEACON_GSM;
    /* If beacon has meaningful timestamp */
    /* scan was before sky_new_request and since Mar 1st 2019 */
    if (ctx->header.time > timestamp && timestamp > TIMESTAMP_2019_03_01)
        b.gsm.age = ctx->header.time - timestamp;
    else
        b.gsm.age = 0;
    if (rssi > -32 || rssi < -128)
        rssi = -1;
    b.gsm.lac = lac;
    b.gsm.ci = ci;
    b.gsm.mcc = mcc;
    b.gsm.mnc = mnc;
    b.gsm.rssi = rssi;

    return add_beacon(ctx, sky_errno, &b, is_connected);
}

/*! \brief Adds a umts cell beacon to the request context
 *
 *  @param ctx Skyhook request context
 *  @param sky_errno skyErrno is set to the error code
 *  @param lac umts location area code identifier (1-65,535), 0 if unknown
 *  @param ucid umts cell identifier 28bit (0-268,435,456)
 *  @param mcc mobile country code (200-799)
 *  @param mnc mobile network code  (0-999)
 *  @param timestamp time in seconds (from 1970 epoch) indicating when the scan was performed, (time_t)-1 if unknown
 *  @param rscp Received Signal Code Power, range -120dbm to -20dbm, -1 if unknown
 *  @param is_connected this beacon is currently connected, false if unknown
 *
 *  @return SKY_SUCCESS or SKY_ERROR and sets sky_errno with error code
 */
Sky_status_t sky_add_cell_umts_beacon(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, uint16_t lac,
    uint32_t ucid, uint16_t mcc, uint16_t mnc, time_t timestamp, int16_t rscp, bool is_connected)
{
    Beacon_t b;

    if (!sky_open_flag)
        return sky_return(sky_errno, SKY_ERROR_NEVER_OPEN);

    if (!validate_workspace(ctx))
        return sky_return(sky_errno, SKY_ERROR_BAD_WORKSPACE);

    if (ctx->len > (TOTAL_BEACONS - 1)) /* room for one more? */
        return sky_return(sky_errno, SKY_ERROR_TOO_MANY);

    /* Create UMTS beacon */
    b.h.magic = BEACON_MAGIC;
    b.h.type = SKY_BEACON_UMTS;
    /* If beacon has meaningful timestamp */
    /* scan was before sky_new_request and since Mar 1st 2019 */
    if (ctx->header.time > timestamp && timestamp > TIMESTAMP_2019_03_01)
        b.umts.age = ctx->header.time - timestamp;
    else
        b.umts.age = 0;
    if (rscp > -20 || rscp < -120)
        rscp = -1;
    b.umts.lac = lac;
    b.umts.ucid = ucid;
    b.umts.mcc = mcc;
    b.umts.mnc = mnc;
    b.umts.rssi = rscp;

    return add_beacon(ctx, sky_errno, &b, is_connected);
}

/*! \brief Adds a cdma cell beacon to the request context
 *
 *  @param ctx Skyhook request context
 *  @param sky_errno skyErrno is set to the error code
 *  @param sid cdma system identifier (0-32767)
 *  @param nid cdma network identifier(0-65535)
 *  @param bsid cdma base station identifier (0-65535)
 *  @param timestamp time in seconds (from 1970 epoch) indicating when the scan was performed, (time_t)-1 if unknown
 *  @param rssi Received Signal Strength Intensity
 *  @param is_connected this beacon is currently connected, false if unknown
 *
 *  @return SKY_SUCCESS or SKY_ERROR and sets sky_errno with error code
 */
Sky_status_t sky_add_cell_cdma_beacon(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, uint32_t sid,
    uint16_t nid, uint16_t bsid, time_t timestamp, int16_t rssi, bool is_connected)
{
    Beacon_t b;

    if (!sky_open_flag)
        return sky_return(sky_errno, SKY_ERROR_NEVER_OPEN);

    if (!validate_workspace(ctx))
        return sky_return(sky_errno, SKY_ERROR_BAD_WORKSPACE);

    if (ctx->len > (TOTAL_BEACONS - 1)) /* room for one more? */
        return sky_return(sky_errno, SKY_ERROR_TOO_MANY);

    /* Create CDMA beacon */
    b.h.magic = BEACON_MAGIC;
    b.h.type = SKY_BEACON_CDMA;
    /* If beacon has meaningful timestamp */
    /* scan was before sky_new_request and since Mar 1st 2019 */
    if (ctx->header.time > timestamp && timestamp > TIMESTAMP_2019_03_01)
        b.cdma.age = ctx->header.time - timestamp;
    else
        b.cdma.age = 0;
    if (rssi > -49 || rssi < -140)
        rssi = -1;
    b.cdma.sid = sid;
    b.cdma.nid = nid;
    b.cdma.bsid = bsid;
    b.cdma.rssi = rssi;

    return add_beacon(ctx, sky_errno, &b, is_connected);
}

/*! \brief Adds a nb_iot cell beacon to the request context
 *
 *  @param ctx Skyhook request context
 *  @param sky_errno skyErrno is set to the error code
 *  @param mcc mobile country code (200-799)
 *  @param mnc mobile network code  (0-999)
 *  @param e_cellid nbiot beacon identifier (0-268,435,456)
 *  @param tac nbiot tracking area code identifier (1-65,535), 0 if unknown
 *  @param timestamp time in seconds (from 1970 epoch) indicating when the scan was performed, (time_t)-1 if unknown
 *  @param nrsrp Narrowband Reference Signal Received Power, range -156 to -44dbm, -1 if unknown
 *  @param is_connected this beacon is currently connected, false if unknown
 *
 *  @return SKY_SUCCESS or SKY_ERROR and sets sky_errno with error code
 */
Sky_status_t sky_add_cell_nb_iot_beacon(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, uint16_t mcc,
    uint16_t mnc, uint32_t e_cellid, uint32_t tac, time_t timestamp, int16_t nrsrp,
    bool is_connected)
{
    Beacon_t b;

    if (!sky_open_flag)
        return sky_return(sky_errno, SKY_ERROR_NEVER_OPEN);

    if (!validate_workspace(ctx))
        return sky_return(sky_errno, SKY_ERROR_BAD_WORKSPACE);

    if (ctx->len > (TOTAL_BEACONS - 1)) /* room for one more? */
        return sky_return(sky_errno, SKY_ERROR_TOO_MANY);

    /* Create NB IoT beacon */
    b.h.magic = BEACON_MAGIC;
    b.h.type = SKY_BEACON_NBIOT;
    /* If beacon has meaningful timestamp */
    /* scan was before sky_new_request and since Mar 1st 2019 */
    if (ctx->header.time > timestamp && timestamp > TIMESTAMP_2019_03_01)
        b.nbiot.age = ctx->header.time - timestamp;
    else
        b.nbiot.age = 0;
    if (nrsrp > -44 || nrsrp < -156)
        nrsrp = -1;
    b.nbiot.mcc = mcc;
    b.nbiot.mnc = mnc;
    b.nbiot.e_cellid = e_cellid;
    b.nbiot.tac = tac;
    b.nbiot.rssi = nrsrp;

    return add_beacon(ctx, sky_errno, &b, is_connected);
}

/*! \brief Adds the position of the device from GNSS to the request context
 *
 *  @param ctx Skyhook request context
 *  @param sky_errno skyErrno is set to the error code
 *  @param lat device latitude
 *  @param lon device longitude
 *  @param hpe pointer to horizontal Positioning Error in meters with 68% confidence, 0 if unknown
 *  @param altitude pointer to altitude above mean sea level, in meters, NaN if unknown
 *  @param vpe pointer to vertical Positioning Error in meters with 68% confidence, 0 if unknown
 *  @param speed pointer to speed in meters per second, Nan if unknown
 *  @param bearing pointer to bearing of device in degrees, counterclockwise from north
 *  @param timestamp time in seconds (from 1970 epoch) indicating when the scan was performed, (time_t)-1 if unknown
 *  @param nsat number of satelites used to determine the location, 0 if unknown
 *
 *  @return SKY_SUCCESS or SKY_ERROR and sets sky_errno with error code
 */
Sky_status_t sky_add_gnss(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, float lat, float lon,
    uint16_t hpe, float altitude, uint16_t vpe, float speed, float bearing, uint16_t nsat,
    time_t timestamp)
{
    if (!validate_workspace(ctx))
        return sky_return(sky_errno, SKY_ERROR_BAD_WORKSPACE);

    ctx->gps.lat = lat;
    ctx->gps.lon = lon;
    ctx->gps.hpe = hpe;
    ctx->gps.alt = altitude;
    ctx->gps.vpe = vpe;
    ctx->gps.speed = speed;
    ctx->gps.bearing = bearing;
    ctx->gps.nsat = nsat;
    /* location was determined before sky_new_request and since Mar 1st 2019 */
    if (ctx->header.time > timestamp && timestamp > TIMESTAMP_2019_03_01)
        ctx->gps.age = ctx->header.time - timestamp;
    else
        ctx->gps.age = 0;
    return sky_return(sky_errno, SKY_ERROR_NONE);
}

/*! \brief generate a Skyhook request from the request context
 *
 *  @param ctx Skyhook request context
 *  @param sky_errno skyErrno is set to the error code
 *  @param request_buf Request to send to Skyhook server
 *  @param bufsize Request size in bytes
 *  @param loc where to save device latitude, longitude etc from cache if known
 *  @param response_size the space required to hold the server response
 *
 *  @return SKY_FINALIZE_REQUEST, SKY_FINALIZE_LOCATION or
 *          SKY_FINALIZE_ERROR and sets sky_errno with error code
 */
Sky_finalize_t sky_finalize_request(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, void *request_buf,
    uint32_t bufsize, Sky_location_t *loc, uint32_t *response_size)
{
    int c, rc;

    if (!validate_workspace(ctx)) {
        *sky_errno = SKY_ERROR_BAD_WORKSPACE;
        return SKY_FINALIZE_ERROR;
    }

    /* There must be at least one beacon */
    if (ctx->len == 0) {
        *sky_errno = SKY_ERROR_NO_BEACONS;
        return SKY_FINALIZE_ERROR;
    }

    /* check cache against beacons for match */
    if ((c = get_from_cache(ctx)) >= 0) {
        if (loc != NULL)
            *loc = ctx->cache->cacheline[c].loc;
        *sky_errno = SKY_ERROR_NONE;
        return SKY_FINALIZE_LOCATION;
    }

    if (request_buf == NULL) {
        *sky_errno = SKY_ERROR_BAD_PARAMETERS;
        return SKY_FINALIZE_ERROR;
    }

    /* encode request */
    rc = serialize_request(ctx, request_buf, bufsize, SW_VERSION);

    if (rc > 0) {
        *response_size = get_maximum_response_size();

        *sky_errno = SKY_ERROR_NONE;

        return SKY_FINALIZE_REQUEST;
    } else {
        *sky_errno = SKY_ERROR_ENCODE_ERROR;

        return SKY_FINALIZE_ERROR;
    }
}

/*! \brief Determines the required size of the network request buffer
 *
 *  @param ctx Skyhook request context
 *  @param size parameter which will be set to the size value
 *  @param sky_errno skyErrno is set to the error code
 *
 *  @return SKY_SUCCESS or SKY_ERROR and sets sky_errno with error code
 */
Sky_status_t sky_sizeof_request_buf(Sky_ctx_t *ctx, uint32_t *size, Sky_errno_t *sky_errno)
{
    int rc;

    if (!validate_workspace(ctx))
        return sky_return(sky_errno, SKY_ERROR_BAD_WORKSPACE);

    if (size == NULL)
        return sky_return(sky_errno, SKY_ERROR_BAD_PARAMETERS);

    /* encode request into the bit bucket, just to determine the length of the
     * encoded message */
    rc = serialize_request(ctx, NULL, 0, SW_VERSION);

    if (rc > 0) {
        *size = (uint32_t)rc;
        return sky_return(sky_errno, SKY_ERROR_NONE);
    } else
        return sky_return(sky_errno, SKY_ERROR_ENCODE_ERROR);
}

/*! \brief decodes a Skyhook server response
 *
 *  @param ctx Skyhook request context
 *  @param sky_errno skyErrno is set to the error code
 *  @param response_buf buffer holding the skyhook server response
 *  @param bufsize Request size in bytes
 *  @param loc where to save device latitude, longitude etc from cache if known
 *
 *  @return SKY_SUCCESS or SKY_ERROR and sets sky_errno with error code
 */
Sky_status_t sky_decode_response(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, void *response_buf,
    uint32_t bufsize, Sky_location_t *loc)
{
    if (loc == NULL || response_buf == NULL || bufsize == 0)
        return sky_return(sky_errno, SKY_ERROR_BAD_PARAMETERS);

    /* decode response to get lat/lon */
    if (deserialize_response(ctx, response_buf, bufsize, loc) < 0) {
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Response decode failure")
        return sky_return(sky_errno, SKY_ERROR_DECODE_ERROR);
    } else if (loc->location_status != SKY_LOCATION_STATUS_SUCCESS) {
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Server error. Status: %s",
            sky_pserver_status(loc->location_status))
        return sky_return(sky_errno, SKY_ERROR_SERVER_ERROR);
    }
    loc->time = (*ctx->gettime)(NULL);

    /* Add location and current beacons to Cache */
    if (add_to_cache(ctx, loc) == SKY_ERROR) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "failed to add to cache")
        return sky_return(sky_errno, SKY_ERROR_ADD_CACHE);
    }

    return sky_return(sky_errno, SKY_ERROR_NONE);
}

/*! \brief returns a string which describes the meaning of sky_errno codes
 *
 *  @param sky_errno Error code for which to provide descriptive string
 *
 *  @return pointer to string or NULL if the code is invalid
 */
char *sky_perror(Sky_errno_t sky_errno)
{
    register char *str = NULL;
    switch (sky_errno) {
    case SKY_ERROR_NONE:
        str = "No error";
        break;
    case SKY_ERROR_NEVER_OPEN:
        str = "Must open first";
        break;
    case SKY_ERROR_ALREADY_OPEN:
        str = "Must close before opening with new parameters";
        break;
    case SKY_ERROR_BAD_PARAMETERS:
        str = "Validation of parameters failed";
        break;
    case SKY_ERROR_TOO_MANY:
        str = "Too many beacons";
        break;
    case SKY_ERROR_BAD_WORKSPACE:
        str = "The workspace buffer is corrupt";
        break;
    case SKY_ERROR_BAD_STATE:
        str = "The state buffer is corrupt";
        break;
    case SKY_ERROR_ENCODE_ERROR:
        str = "The request could not be encoded";
        break;
    case SKY_ERROR_DECODE_ERROR:
        str = "The response could not be decoded";
        break;
    case SKY_ERROR_RESOURCE_UNAVAILABLE:
        str = "Can\'t allocate non-volatile storage";
        break;
    case SKY_ERROR_CLOSE:
        str = "Failed to cleanup resources during close";
        break;
    case SKY_ERROR_BAD_KEY:
        str = "AES_Key is not valid format";
        break;
    case SKY_ERROR_NO_BEACONS:
        str = "At least one beacon must be added";
        break;
    case SKY_ERROR_ADD_CACHE:
        str = "failed to add entry in cache";
        break;
    case SKY_ERROR_GET_CACHE:
        str = "failed to get entry from cache";
        break;
    case SKY_ERROR_LOCATION_UNKNOWN:
        str = "server failed to determine location";
        break;
    case SKY_ERROR_SERVER_ERROR:
        str = "server responded with an error";
        break;
    default:
        str = "Unknown error code";
        break;
    }
    return str;
}

/*! \brief returns a string which describes the meaning of Sky_loc_status_t codes
 *
 *  @param status Error code for which to provide descriptive string
 *
 *  @return pointer to string or NULL if the code is invalid
 */
char *sky_pserver_status(Sky_loc_status_t status)
{
    register char *str = NULL;
    switch (status) {
    case SKY_LOCATION_STATUS_SUCCESS:
        str = "Server success";
        break;
    case SKY_LOCATION_STATUS_UNSPECIFIED_ERROR:
        str = "Server reports unspecified error";
        break;
    case SKY_LOCATION_STATUS_BAD_PARTNER_ID_ERROR:
        str = "Server reports bad partner id error";
        break;
    case SKY_LOCATION_STATUS_DECODE_ERROR:
        str = "Server reports error decoding request body";
        break;
    case SKY_LOCATION_STATUS_API_SERVER_ERROR:
        str = "Server error determining location";
        break;
    default:
        str = "Unknown server status";
        break;
    }
    return str;
}

/*! \brief returns a string which describes the meaning of Sky_beacon_type_t
 *
 *  @param b beacon type
 *
 *  @return pointer to string or NULL if the code is invalid
 */
char *sky_pbeacon(Beacon_t *b)
{
    register char *str = NULL;
    switch (b->h.type) {
    case SKY_BEACON_AP:
        str = "Wi-Fi";
        break;
    case SKY_BEACON_BLE:
        str = "Bluetooth";
        break;
    case SKY_BEACON_CDMA:
        str = "CDMA";
        break;
    case SKY_BEACON_GSM:
        str = "GSM";
        break;
    case SKY_BEACON_LTE:
        str = "LTE";
        break;
    case SKY_BEACON_NBIOT:
        str = "NB-IoT";
        break;
    case SKY_BEACON_UMTS:
        str = "UMTS";
        break;
    default:
        str = "Unknown";
        break;
    }
    return str;
}

/*! \brief clean up library resourses
 *
 *  @param sky_errno skyErrno is set to the error code
 *  @param sky_state pointer to where the state buffer reference should be
 * stored
 *
 *  @return SKY_SUCCESS or SKY_ERROR and sets sky_errno with error code
 */
Sky_status_t sky_close(Sky_errno_t *sky_errno, void **sky_state)
{
    if (!sky_open_flag)
        return sky_return(sky_errno, SKY_ERROR_NEVER_OPEN);

    sky_open_flag = false;

    if (sky_state != NULL)
        *sky_state = &cache;
    return sky_return(sky_errno, SKY_ERROR_NONE);
}

/*******************************************************************************
 * Static helper functions
 ******************************************************************************/

/*! \brief sanity check the device_id
 *
 *  @param device_id this is expected to be a binary mac address
 *  @param id_len length of device_id
 *
 *  @return true or false
 */
static bool validate_device_id(uint8_t *device_id, uint32_t id_len)
{
    if (device_id == NULL)
        return false;
    else
        return true; /* TODO check upper bound? */
}

/*! \brief sanity check the partner_id
 *
 *  @param partner_id this is expected to be in the range (1 - ???)
 *
 *  @return true or false
 */
static bool validate_partner_id(uint32_t partner_id)
{
    if (partner_id == 0)
        return false;
    else
        return true; /* TODO check upper bound? */
}

/*! \brief sanity check the aes_key_id
 *
 *  @param aes_key_id this is expected to be in the range (1 - ???)
 *
 *  @return true or false
 */
static bool validate_aes_key_id(uint32_t aes_key_id)
{
    if (aes_key_id == 0)
        return false;
    else
        return true; /* TODO check upper bound? */
}

/*! \brief sanity check the aes_key
 *
 *  @param aes_key this is expected to be a binary 16 byte value
 *
 *  @return true or false
 */
static bool validate_aes_key(uint8_t aes_key[AES_SIZE])
{
    if (aes_key == NULL)
        return false;
    else
        return true; /* TODO check for non-trivial values? e.g. zero */
}
