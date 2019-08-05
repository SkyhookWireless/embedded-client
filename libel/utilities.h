/*! \file libel/utilities.h
 *  \brief Skyhook Embedded Library workspace structures
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
#ifndef SKY_UTILITIES_H
#define SKY_UTILITIES_H

#if SKY_DEBUG
#define LOGFMT(...) logfmt(__FILE__, __FUNCTION__, __VA_ARGS__);
#else
#define LOGFMT(...)
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
int logfmt(
    const char *file, const char *function, Sky_ctx_t *ctx, Sky_log_level_t level, char *fmt, ...);
#endif
void dump_workspace(Sky_ctx_t *ctx);
void dump_cache(Sky_ctx_t *ctx);

int32_t get_num_beacons(Sky_ctx_t *ctx, Sky_beacon_type_t t);
int get_base_beacons(Sky_ctx_t *ctx, Sky_beacon_type_t t);

uint32_t get_ctx_partner_id(Sky_ctx_t *ctx);
uint8_t *get_ctx_aes_key(Sky_ctx_t *ctx);
uint32_t get_ctx_aes_key_id(Sky_ctx_t *ctx);
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

int sky_rand_fn(uint8_t *rand_buf, uint32_t bufsize);
#endif
