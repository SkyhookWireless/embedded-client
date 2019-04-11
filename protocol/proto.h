#ifndef ELG_PB_H
#define ELG_PB_H

#include "elg.pb.h"

// TODO: Delete these accessor prototypes after they become available in one of Geoff's header file.
//
typedef void Sky_ctx_t;

uint32_t get_num_aps(Sky_ctx_t* ctx);
uint8_t* get_ap_mac(Sky_ctx_t* ctx, uint32_t idx);
bool get_ap_is_connected(Sky_ctx_t* ctx, uint32_t idx);
int64_t get_ap_channel(Sky_ctx_t* ctx, uint32_t idx);
int64_t get_ap_rssi(Sky_ctx_t* ctx, uint32_t idx);
int64_t get_ap_age(Sky_ctx_t* ctx, uint32_t idx);

uint32_t get_num_gsm(Sky_ctx_t *ctx);
int64_t get_gsm_ci(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_gsm_mcc(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_gsm_mnc(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_gsm_lac(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_gsm_rssi(Sky_ctx_t *ctx, uint32_t idx);
bool get_gsm_is_connected(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_gsm_age(Sky_ctx_t *ctx, uint32_t idx);

uint32_t get_num_nbiot(Sky_ctx_t *ctx);
int64_t get_nbiot_ecellid(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_nbiot_mcc(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_nbiot_mnc(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_nbiot_tac(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_nbiot_rssi(Sky_ctx_t *ctx, uint32_t idx);
bool get_nbiot_is_connected(Sky_ctx_t *ctx, uint32_t idx);
int64_t get_nbiot_age(Sky_ctx_t *ctx, uint32_t idx);

// Encode and encrypt request into buffer.
int32_t serialize_request(Sky_ctx_t* ctx,
                          uint8_t* buf,
                          size_t buf_len,
                          uint32_t partner_id,
                          uint8_t aes_key[16],
                          uint8_t* device_id,
                          uint32_t device_id_length);

// Decrypt and decode response info from buffer.
int32_t deserialize_response(uint8_t* buf,
                             size_t buf_len,
                             uint8_t aes_key[16],
                             Rs* rs);

#endif
