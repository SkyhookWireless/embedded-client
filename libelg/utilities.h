/*! \file libelg/utilities.h
 *  \brief Skyhook ELG API workspace structures
 *
 * Copyright 2019 Skyhook Inc.
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

Sky_status_t sky_return(Sky_errno_t *sky_errno, Sky_errno_t code);
int validate_workspace(Sky_ctx_t *ctx);
int validate_cache(Sky_cache_t *c);
Sky_status_t add_beacon(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, Beacon_t *b,
			bool is_connected);
int logfmt(Sky_ctx_t *ctx, Sky_log_level_t level, const char *fmt, ...);
int32_t get_num_beacons(Sky_ctx_t *ctx, Sky_beacon_type_t t);
int get_base_beacons(Sky_ctx_t *ctx, Sky_beacon_type_t t);
int32_t get_num_aps(Sky_ctx_t *ctx);
uint8_t *get_ap_mac(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_ap_channel(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_ap_rssi(Sky_ctx_t *ctx, uint32_t idx);
int32_t get_num_gsm(Sky_ctx_t *ctx);
uint64_t get_gsm_ci(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_gsm_mcc(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_gsm_mnc(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_gsm_lac(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_gsm_rssi(Sky_ctx_t *ctx, uint32_t idx);
int32_t get_num_nbiot(Sky_ctx_t *ctx);
int64_t get_nbiot_mcc(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_nbiot_mnc(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_nbiot_ecellid(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_nbiot_tac(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_nbiot_lac(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_nbiot_rssi(Sky_ctx_t *ctx, uint32_t idx);

#endif
