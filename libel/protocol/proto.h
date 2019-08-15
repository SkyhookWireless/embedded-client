#ifndef ELG_PB_H
#define ELG_PB_H

#define SKY_LIBEL

#include "el.pb.h"
#include "libel.h"

// Process any configuration overrides
void config_overrides(Sky_ctx_t *ctx, Rs *rs);

// Encode and encrypt request into buffer.
int32_t serialize_request(
    Sky_ctx_t *ctx, uint8_t *request_buf, uint32_t bufsize, uint32_t sw_version, bool rq_config);

// Decrypt and decode response info from buffer.
int32_t deserialize_response(Sky_ctx_t *ctx, uint8_t *buf, uint32_t buf_len, Sky_location_t *loc);

// Calculate the maximum buffer space needed for the ELG server response
int32_t get_maximum_response_size(void);

#endif
