/*! \file libel/libez.c
 *  \brief sky entry points - Skyhook Easy Embedded Library
 *
 * Copyright (c) 2020 Skyhook, Inc.
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
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <alloca.h>
#define SKY_LIBEL
#include "libel.h"
#include "libez.h"
#include <inttypes.h>
#include <sys/time.h>
#define __USE_POSIX199309
#define _POSIX_C_SOURCE 199309L
#include <unistd.h>
#include "aes.h"

/* A monotonically increasing version number intended to track the client
 * software version, and which is sent to the server in each request. Clumsier
 * than just including the Git version string (since it will need to be updated
 * manually for every release) but cheaper bandwidth-wise.
 */
#define SW_VERSION 1

/*! \brief Make a request to Skyhook library and return result
 *
 *  @param num_aps the number of aps in the scan
 *  @param aps array of ap beacon info
 *  @param num_cells the number of cells in the scan
 *  @param cells array of cell beacon info
 *  @param gnss the gnss fix if any
 *  @param location_result where to place result
 *
 *  @return XPS_STATUS_OK for success, error otherwise
 *
 *  XPS_locate uses Skyhook Embedded Client to pass beacon scan information to skyhook API server
 *  and passes the result back to the user
 */
XPS_StatusCode_t XPS_locate(uint16_t num_aps, XPS_ScannedAP_t *aps, uint16_t num_cells,
    XPS_ScannedCell_t *cells, XPS_GNSS_t *gnss, XPS_Location_t *location_result)
{
    int i;

    /* Skyhook libel api */
    Sky_errno_t sky_errno = -1;
    Sky_ctx_t *ctx;
    uint32_t bufsize;
    void *prequest;
    uint8_t *response = NULL;
    uint32_t request_size;
    uint32_t response_size;
    Sky_status_t ret_status;
    Sky_location_t loc;

    /* Configuration */
    char *config_value;
    uint8_t device_id[MAX_DEVICE_ID] = { 0 };
    int device_id_len = 0;
    int partner_id = 0;
    char *server = NULL;
    uint16_t port = 0;
    uint8_t aes_key[AES_SIZE] = { 0 };
    int loglevel = SKY_LOG_LEVEL_ERROR;

    if (num_aps == 0 && num_cells == 0)
        return XPS_STATUS_ERROR_BAD_BEACON_DATA;

    if (aps == NULL || cells == NULL || location_result == NULL)
        return XPS_STATUS_ERROR_INTERNAL;

    // Verify configuration
    // Device ID - hex binary (often 6 bytes mac)
    if (XPS_get_option("device_id", &config_value) != XPS_STATUS_OK) {
        XPS_log_error("device_id");
        return XPS_STATUS_ERROR_NOT_CONFIGURED;
    } else {
        device_id_len =
            XPS_hex2bin(config_value, strlen(config_value), device_id, sizeof(device_id));
    }
    // Partner ID - decimal held in 32 bit int
    if (XPS_get_option("Partner_id", &config_value) != XPS_STATUS_OK) {
        XPS_log_error("partner id");
        return XPS_STATUS_ERROR_NOT_CONFIGURED;
    } else {
        partner_id = atoi(config_value);
    }
    // Key AES 16 byte hex
    if (XPS_get_option("key", &config_value) != XPS_STATUS_OK) {
        XPS_log_error("key");
        return XPS_STATUS_ERROR_NOT_CONFIGURED;
    } else {
        if (XPS_hex2bin(config_value, strlen(config_value), aes_key, sizeof(aes_key)) != AES_SIZE)
            return XPS_STATUS_ERROR_NOT_CONFIGURED;
    }
    // Server asci hostname or ip address
    if (XPS_get_option("server", &config_value) != XPS_STATUS_OK) {
        XPS_log_error("server");
        loglevel = SKY_LOG_LEVEL_ERROR; // default value if not set
    } else {
        server = config_value;
    }
    // Port - decimal held in uint16_t
    if (XPS_get_option("port", &config_value) != XPS_STATUS_OK) {
        port = 9756;
    } else {
        port = atoi(config_value);
    }
    // Log Level - decimal int, 1 critical, 2 error, 3 warning, 4 debug
    if (XPS_get_option("loglevel", &config_value) != XPS_STATUS_OK) {
        loglevel = SKY_LOG_LEVEL_ERROR; // default value if not set
    } else {
        loglevel = atoi(config_value);
    }

    // Initialize the Skyhook resources
    if (sky_open(&sky_errno, device_id, device_id_len, partner_id, aes_key, NULL, loglevel,
            &XPS_skylogger, NULL, NULL) == SKY_ERROR) {
        XPS_log_error("sky_open returned bad value, Can't continue");
        return XPS_STATUS_ERROR_INTERNAL;
    }

    // Get the size of workspace needed
    bufsize = sky_sizeof_workspace();
    if (bufsize == 0 || bufsize > 10239) {
        XPS_log_error("sky_sizeof_workspace returned bad value, Can't continue");
        return XPS_STATUS_ERROR_INTERNAL;
    }

    // Allocate and initialize workspace
    ctx = (Sky_ctx_t *)alloca(bufsize);
    memset((void *)ctx, 0, bufsize);

    // Start new request
    if (sky_new_request(ctx, bufsize, &sky_errno) != ctx) {
        XPS_log_error("sky_new_request(): '%s'", sky_perror(sky_errno));
    }

    // Add APs to the request
    for (i = 0; i < num_aps; i++) {
        if (sky_add_ap_beacon(ctx, &sky_errno, aps[i].mac, aps[i].timestamp, aps[i].rssi,
                aps[i].freq, aps[i].is_connected) != SKY_SUCCESS) {
            XPS_log_error("sky_add_ap_beacon sky_errno contains '%s'", sky_perror(sky_errno));
            return XPS_STATUS_ERROR_BAD_BEACON_DATA;
        }
    }

    // Add Cell to the request
    ret_status = SKY_SUCCESS;
    for (i = 0; i < num_cells; i++) {
        switch (cells[i].type) {
        case XPS_CELL_TYPE_GSM:
            ret_status = sky_add_cell_gsm_beacon(ctx, &sky_errno, cells[i].id4, cells[i].id3,
                cells[i].id1, cells[i].id2, cells[i].timestamp, cells[i].ss, cells[i].is_connected);
            break;
        case XPS_CELL_TYPE_LTE:
            ret_status = sky_add_cell_lte_beacon(ctx, &sky_errno, cells[i].id4, cells[i].id3,
                cells[i].id1, cells[i].id2, cells[i].timestamp, cells[i].ss, cells[i].is_connected);
            break;
        case XPS_CELL_TYPE_NB_IOT:
            ret_status = sky_add_cell_nb_iot_beacon(ctx, &sky_errno, cells[i].id1, cells[i].id2,
                cells[i].id3, cells[i].id4, cells[i].timestamp, cells[i].ss, cells[i].is_connected);
            break;
        case XPS_CELL_TYPE_UMTS:
            ret_status = sky_add_cell_umts_beacon(ctx, &sky_errno, cells[i].id4, cells[i].id3,
                cells[i].id1, cells[i].id2, cells[i].timestamp, cells[i].ss, cells[i].is_connected);
            break;
        case XPS_CELL_TYPE_CDMA:
            ret_status = sky_add_cell_cdma_beacon(ctx, &sky_errno, cells[i].id1, cells[i].id2,
                cells[i].id3, cells[i].timestamp, cells[i].ss, cells[i].is_connected);
            break;
        default:
            XPS_log_error("Unknow cell type passed to %s", __FUNCTION__);
            return XPS_STATUS_ERROR_BAD_BEACON_DATA;
        }
    }
    if (ret_status != SKY_SUCCESS) {
        XPS_log_error("sky_add_cell_beacon sky_errno contains '%s'", sky_perror(sky_errno));
        return XPS_STATUS_ERROR_BAD_BEACON_DATA;
    }

    // Add GNSS to the request
    if (gnss->lat != 0.0 && gnss->lon != 0.0) {
        if (sky_add_gnss(ctx, &sky_errno, gnss->lat, gnss->lon, gnss->hpe, gnss->altitude,
                gnss->vpe, gnss->speed, gnss->bearing, gnss->nsat, gnss->timestamp) != SKY_SUCCESS)
            XPS_log_warning("sky_add_gnss sky_errno contains '%s'", sky_perror(sky_errno));
    }

    /* Determine how big the network request buffer must be, and allocate a */
    /* buffer of that length. This function must be called for each request. */
    if (sky_sizeof_request_buf(ctx, &request_size, &sky_errno) == SKY_ERROR) {
        XPS_log_error("Error getting size of request buffer: %s", sky_perror(sky_errno));
        return XPS_STATUS_ERROR_INTERNAL;
    }

    prequest = alloca(request_size);

    // Finalize the request by check the cache
    switch (sky_finalize_request(ctx, &sky_errno, prequest, request_size, &loc, &response_size)) {
    case SKY_FINALIZE_LOCATION:
        // XPS_log_debug("Location - lat: %.6f, lon: %.6f, hpe: %d, source: %s", loc.lat, loc.lon,
        //     loc.hpe, XPS_determine_source(loc.location_source));
        break;
    default:
    case SKY_FINALIZE_ERROR:
        XPS_log_error("sky_finalize_request sky_errno contains '%s'", sky_perror(sky_errno));
        if (sky_close(&sky_errno, NULL))
            XPS_log_error("sky_close sky_errno contains '%s'", sky_perror(sky_errno));
        return XPS_STATUS_ERROR_INTERNAL;
        break;
    case SKY_FINALIZE_REQUEST:
        response = alloca(response_size);
        int32_t rc = XPS_send_request(
            (char *)prequest, (int)request_size, response, response_size, server, port);
        if (rc <= 0) {
            XPS_log_error("Bad response from server");
            return -rc;
        }

        // Decode the response from server or cache
        if (sky_decode_response(ctx, &sky_errno, response, bufsize, &loc) == SKY_SUCCESS)
            XPS_log_debug("Location - lat: %.6f, lon: %.6f, hpe: %d, source: %s", loc.lat, loc.lon,
                loc.hpe, XPS_determine_source_str(loc.location_source));
        else if (sky_errno == SKY_ERROR_LOCATION_UNKNOWN) {
            XPS_log_warning("Unable to determine location");
            sky_close(&sky_errno, NULL);
            return XPS_STATUS_ERROR_LOCATION_CANNOT_BE_DETERMINED;
        } else {
            XPS_log_error("sky_decode_response sky_errno contains '%s'", sky_perror(sky_errno));
            sky_close(&sky_errno, NULL);
            return XPS_STATUS_ERROR_INTERNAL;
        }
        break;
    }
    if (sky_close(&sky_errno, NULL) != SKY_SUCCESS)
        return XPS_STATUS_ERROR_INTERNAL;

    location_result->location_source = XPS_determine_source(loc.location_source);
    location_result->lat = loc.lat;
    location_result->lon = loc.lon;
    location_result->hpe = loc.hpe;
    location_result->timestamp = (time_t)loc.time;

    XPS_log_debug("XPS_locate allocated %d bytes on stack to process location",
        bufsize + request_size + response_size);

    return XPS_STATUS_OK;
}
