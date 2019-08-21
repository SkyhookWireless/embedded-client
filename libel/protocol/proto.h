#ifndef ELG_PB_H
#define ELG_PB_H

#define SKY_LIBEL

#include "libel.h"
#include "el.pb.h"

// Encode and encrypt request into buffer.
int32_t serialize_request(
    Sky_ctx_t *ctx, uint8_t *request_buf, uint32_t bufsize, uint32_t sw_version, bool config);

// Decrypt and decode response info from buffer.
int32_t deserialize_response(Sky_ctx_t *ctx, uint8_t *buf, uint32_t buf_len, Sky_location_t *loc);

// Calculate the maximum buffer space needed for the ELG server response
int32_t get_maximum_response_size(void);

// Process any configuration overrides
bool config_overrides(Sky_cache_t *c, Rs *rs);
void config_defaults(Sky_cache_t *c);

#endif
