/*! \file libel/utilities.h
 *  \brief Skyhook Embedded Library workspace structures
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
#ifndef SKY_UTILITIES_H
#define SKY_UTILITIES_H

#if SKY_DEBUG
#define LOGFMT(...) logfmt(__FILE__, __FUNCTION__, __VA_ARGS__);
#define LOG_BUFFER(c, l, b, s) log_buffer(__FILE__, __FUNCTION__, c, l, b, s);
#else
#define LOGFMT(...)
#define LOG_BUFFER(c, l, b, s)
#endif
Sky_status_t sky_return(Sky_errno_t *sky_errno, Sky_errno_t code);
int validate_workspace(Sky_ctx_t *ctx);
int validate_cache(Sky_cache_t *c, Sky_loggerfn_t logf);
int validate_mac(uint8_t mac[6], Sky_ctx_t *ctx);
Sky_status_t add_to_cache(Sky_ctx_t *ctx, Sky_location_t *loc);
int get_from_cache(Sky_ctx_t *ctx);
int find_best_match(Sky_ctx_t *ctx, bool put);
Sky_status_t add_beacon(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, Beacon_t *b, bool is_connected);
#if SKY_DEBUG
const char *sky_basename(const char *path);
int logfmt(
    const char *file, const char *function, Sky_ctx_t *ctx, Sky_log_level_t level, char *fmt, ...);
int log_buffer(const char *file, const char *function, Sky_ctx_t *ctx, Sky_log_level_t level,
    void *buffer, uint32_t bufsize);
#endif
void dump_workspace(Sky_ctx_t *ctx);
void dump_cache(Sky_ctx_t *ctx);
void config_defaults(Sky_cache_t *c);
int32_t get_num_beacons(Sky_ctx_t *ctx, Sky_beacon_type_t t);
int32_t get_num_cells(Sky_ctx_t *ctx);
int get_base_beacons(Sky_ctx_t *ctx, Sky_beacon_type_t t);

uint32_t get_ctx_partner_id(Sky_ctx_t *ctx);
uint8_t *get_ctx_aes_key(Sky_ctx_t *ctx);
uint8_t *get_ctx_device_id(Sky_ctx_t *ctx);
uint32_t get_ctx_id_length(Sky_ctx_t *ctx);

int32_t get_num_aps(Sky_ctx_t *ctx);
uint8_t *get_ap_mac(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_ap_freq(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_ap_rssi(Sky_ctx_t *ctx, uint32_t idx);
bool get_ap_is_connected(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_ap_age(Sky_ctx_t *ctx, uint32_t idx);

int32_t get_num_cdma(Sky_ctx_t *ctx);
int64_t get_cdma_sid(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_cdma_nid(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_cdma_bsid(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_cdma_rssi(Sky_ctx_t *ctx, uint32_t idx);
bool get_cdma_is_connected(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_cdma_age(Sky_ctx_t *ctx, uint32_t idx);

int32_t get_num_gsm(Sky_ctx_t *ctx);
int64_t get_gsm_ci(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_gsm_mcc(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_gsm_mnc(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_gsm_lac(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_gsm_rssi(Sky_ctx_t *ctx, uint32_t idx);
bool get_gsm_is_connected(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_gsm_age(Sky_ctx_t *ctx, uint32_t idx);

int32_t get_num_lte(Sky_ctx_t *ctx);
int64_t get_lte_mcc(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_lte_mnc(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_lte_e_cellid(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_lte_tac(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_lte_rssi(Sky_ctx_t *ctx, uint32_t idx);
bool get_lte_is_connected(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_lte_age(Sky_ctx_t *ctx, uint32_t idx);

int32_t get_num_nbiot(Sky_ctx_t *ctx);
int64_t get_nbiot_mcc(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_nbiot_mnc(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_nbiot_ecellid(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_nbiot_tac(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_nbiot_lac(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_nbiot_rssi(Sky_ctx_t *ctx, uint32_t idx);
bool get_nbiot_is_connected(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_nbiot_age(Sky_ctx_t *ctx, uint32_t idx);

int32_t get_num_umts(Sky_ctx_t *ctx);
int64_t get_umts_lac(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_umts_ucid(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_umts_mcc(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_umts_mnc(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_umts_rssi(Sky_ctx_t *ctx, uint32_t idx);
bool get_umts_is_connected(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_umts_age(Sky_ctx_t *ctx, uint32_t idx);

int32_t get_num_gnss(Sky_ctx_t *ctx);
float get_gnss_lat(Sky_ctx_t *ctx, uint32_t idx);
float get_gnss_lon(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_gnss_hpe(Sky_ctx_t *ctx, uint32_t idx);
float get_gnss_alt(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_gnss_vpe(Sky_ctx_t *ctx, uint32_t idx);
float get_gnss_speed(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_gnss_bearing(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_gnss_nsat(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_gnss_age(Sky_ctx_t *ctx, uint32_t idx);
Beacon_t* get_cell(Sky_ctx_t *ctx, uint32_t idx);
int16_t get_cell_type(Beacon_t* cell);
int64_t get_cell_id1(Beacon_t* cell);
int64_t get_cell_id2(Beacon_t* cell);
int64_t get_cell_id3(Beacon_t* cell);
int64_t get_cell_id4(Beacon_t* cell);
int64_t get_cell_id5(Beacon_t* cell);
int64_t get_cell_id6(Beacon_t* cell);
bool get_cell_connected_flag(Sky_ctx_t *ctx, Beacon_t* cell);
int64_t get_cell_rssi(Beacon_t* cell);
int64_t get_cell_age(Beacon_t* cell);

int sky_rand_fn(uint8_t *rand_buf, uint32_t bufsize);
#endif
