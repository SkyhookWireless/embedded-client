/*
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
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

#include <pb_encode.h>
#include <pb_decode.h>

#include "el.pb.h"
#include "proto.h"
#include "aes.h"

static bool apply_config_overrides(Sky_cache_t *c, Rs *rs);
static int64_t get_gnss_lat_scaled(Sky_ctx_t *ctx, uint32_t idx);
static int64_t get_gnss_lon_scaled(Sky_ctx_t *ctx, uint32_t idx);
static int64_t get_gnss_alt_scaled(Sky_ctx_t *ctx, uint32_t idx);
static int64_t get_gnss_speed_scaled(Sky_ctx_t *ctx, uint32_t idx);

typedef uint8_t *(*DataGetterb)(Sky_ctx_t *, uint32_t);
typedef int64_t (*DataGetter)(Sky_ctx_t *, uint32_t);
typedef float (*DataGetterf)(Sky_ctx_t *, uint32_t);
typedef int64_t (*DataWrapper)(int64_t);
typedef bool (*EncodeSubmsgCallback)(Sky_ctx_t *, pb_ostream_t *);

static int64_t mac_to_int(Sky_ctx_t *ctx, uint32_t idx)
{
    size_t i;

    // This is a wrapper function around get_ap_mac(). It converts the 8-byte
    // mac array to an uint64_t.
    //
    uint8_t *mac = get_ap_mac(ctx, idx);

    uint64_t ret_val = 0;

    for (i = 0; i < 6; i++)
        ret_val = ret_val * 256 + mac[i];

    return ret_val;
}

static int64_t flip_sign(int64_t value)
{
    return -value;
}

static bool encode_repeated_int_field(Sky_ctx_t *ctx, pb_ostream_t *ostream, uint32_t tag,
    uint32_t num_elems, DataGetter getter, DataWrapper wrapper)
{
    size_t i;

    // Get and encode the field size.
    pb_ostream_t substream = PB_OSTREAM_SIZING;

    // Encode field tag.
    if (!pb_encode_tag(ostream, PB_WT_STRING, tag))
        return false;

    for (i = 0; i < num_elems; i++) {
        int64_t data = getter(ctx, i);

        if (wrapper != NULL)
            data = wrapper(data);

        if (!pb_encode_varint(&substream, data))
            return false;
    }

    if (!pb_encode_varint(ostream, substream.bytes_written))
        return false;

    // Now encode the field for real.
    for (i = 0; i < num_elems; i++) {
        int64_t data = getter(ctx, i);

        if (wrapper != NULL)
            data = wrapper(data);

        if (!pb_encode_varint(ostream, data))
            return false;
    }

    return true;
}

static bool encode_vap_data(
    Sky_ctx_t *ctx, pb_ostream_t *ostream, uint32_t tag, uint32_t num_elems, DataGetterb getter)
{
    size_t i;

    // Get and encode the field size.
    pb_ostream_t substream = PB_OSTREAM_SIZING;

    // Encode field tag.
    if (!pb_encode_tag(ostream, PB_WT_STRING, tag))
        return false;

    for (i = 0; i < num_elems; i++) {
        uint8_t *data = getter(ctx, i);

        /* *data == len, data + 1 == first byte of data */
        if (!pb_encode_string(&substream, data + 1, *data))
            return false;
    }

    // LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "length %d", substream.bytes_written);
    if (!pb_encode_varint(ostream, substream.bytes_written))
        return false;

    // Now encode the field for real.
    for (i = 0; i < num_elems; i++) {
        uint8_t *data = getter(ctx, i);

        /* *data == len, data + 1 == first byte of data */
        if (!pb_encode_string(ostream, data + 1, *data))
            return false;
    }

    return true;
}

static bool encode_connected_field(Sky_ctx_t *ctx, pb_ostream_t *ostream, uint32_t num_beacons,
    uint32_t tag, bool (*callback)(Sky_ctx_t *, uint32_t idx))
{
    bool retval = true;
    size_t i;

    for (i = 0; i < num_beacons; i++) {
        if (callback(ctx, i)) {
            retval = pb_encode_tag(ostream, PB_WT_VARINT, tag) && pb_encode_varint(ostream, i + 1);

            break;
        }
    }

    return retval;
}

static bool encode_optimized_repeated_field(Sky_ctx_t *ctx, pb_ostream_t *ostream,
    uint32_t num_beacons, uint32_t tag1, uint32_t tag2, DataGetter getter)
{
    // Encode fields. Optimization: send only a single common value if
    // all values are the same.
    int64_t value = getter(ctx, 0);
    bool value_all_same = true;
    size_t i;

    for (i = 1; value_all_same && i < num_beacons; i++) {
        if (getter(ctx, i) != value)
            value_all_same = false;
    }

    if (num_beacons > 1 && value_all_same) {
        return pb_encode_tag(ostream, PB_WT_VARINT, tag1) && pb_encode_varint(ostream, value + 1);
    } else {
        return encode_repeated_int_field(ctx, ostream, tag2, num_beacons, getter, NULL);
    }
}

static bool encode_ap_fields(Sky_ctx_t *ctx, pb_ostream_t *ostream)
{
    uint32_t num_beacons = get_num_aps(ctx);

    return encode_connected_field(
               ctx, ostream, num_beacons, Aps_connected_idx_plus_1_tag, get_ap_is_connected) &&
           encode_repeated_int_field(ctx, ostream, Aps_mac_tag, num_beacons, mac_to_int, NULL) &&
           encode_optimized_repeated_field(ctx, ostream, num_beacons, Aps_common_freq_plus_1_tag,
               Aps_frequency_tag, get_ap_freq) &&
           encode_repeated_int_field(
               ctx, ostream, Aps_neg_rssi_tag, num_beacons, get_ap_rssi, flip_sign) &&
           encode_optimized_repeated_field(
               ctx, ostream, num_beacons, Aps_common_age_plus_1_tag, Aps_age_tag, get_ap_age);
}

static bool encode_cdma_fields(Sky_ctx_t *ctx, pb_ostream_t *ostream)
{
    uint32_t num_beacons = get_num_cdma(ctx);

    return encode_connected_field(ctx, ostream, num_beacons, CdmaCells_connected_idx_plus_1_tag,
               get_cdma_is_connected) &&
           encode_repeated_int_field(
               ctx, ostream, CdmaCells_sid_tag, num_beacons, get_cdma_sid, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, CdmaCells_nid_tag, num_beacons, get_cdma_nid, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, CdmaCells_bsid_tag, num_beacons, get_cdma_bsid, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, CdmaCells_neg_rssi_tag, num_beacons, get_cdma_rssi, flip_sign) &&
           encode_optimized_repeated_field(ctx, ostream, num_beacons,
               CdmaCells_common_age_plus_1_tag, CdmaCells_age_tag, get_cdma_age);
}

static bool encode_gsm_fields(Sky_ctx_t *ctx, pb_ostream_t *ostream)
{
    uint32_t num_beacons = get_num_gsm(ctx);

    return encode_connected_field(ctx, ostream, num_beacons, GsmCells_connected_idx_plus_1_tag,
               get_gsm_is_connected) &&
           encode_repeated_int_field(
               ctx, ostream, GsmCells_mcc_tag, num_beacons, get_gsm_mcc, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, GsmCells_mnc_tag, num_beacons, get_gsm_mnc, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, GsmCells_lac_tag, num_beacons, get_gsm_lac, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, GsmCells_ci_tag, num_beacons, get_gsm_ci, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, GsmCells_neg_rssi_tag, num_beacons, get_gsm_rssi, flip_sign) &&
           encode_optimized_repeated_field(ctx, ostream, num_beacons,
               GsmCells_common_age_plus_1_tag, GsmCells_age_tag, get_gsm_age);
}

static bool encode_lte_fields(Sky_ctx_t *ctx, pb_ostream_t *ostream)
{
    uint32_t num_beacons = get_num_lte(ctx);

    return encode_connected_field(ctx, ostream, num_beacons, LteCells_connected_idx_plus_1_tag,
               get_lte_is_connected) &&
           encode_repeated_int_field(
               ctx, ostream, LteCells_mcc_tag, num_beacons, get_lte_mcc, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, LteCells_mnc_tag, num_beacons, get_lte_mnc, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, LteCells_tac_tag, num_beacons, get_lte_tac, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, LteCells_eucid_tag, num_beacons, get_lte_e_cellid, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, LteCells_neg_rssi_tag, num_beacons, get_lte_rssi, flip_sign) &&
           encode_optimized_repeated_field(ctx, ostream, num_beacons,
               LteCells_common_age_plus_1_tag, LteCells_age_tag, get_lte_age);
}
static bool encode_nbiot_fields(Sky_ctx_t *ctx, pb_ostream_t *ostream)
{
    uint32_t num_beacons = get_num_nbiot(ctx);

    return encode_connected_field(ctx, ostream, num_beacons, NbiotCells_connected_idx_plus_1_tag,
               get_nbiot_is_connected) &&
           encode_repeated_int_field(
               ctx, ostream, NbiotCells_mcc_tag, num_beacons, get_nbiot_mcc, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, NbiotCells_mnc_tag, num_beacons, get_nbiot_mnc, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, NbiotCells_tac_tag, num_beacons, get_nbiot_tac, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, NbiotCells_e_cellid_tag, num_beacons, get_nbiot_ecellid, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, NbiotCells_neg_rssi_tag, num_beacons, get_nbiot_rssi, flip_sign) &&
           encode_optimized_repeated_field(ctx, ostream, num_beacons,
               NbiotCells_common_age_plus_1_tag, NbiotCells_age_tag, get_nbiot_age);
}

static bool encode_umts_fields(Sky_ctx_t *ctx, pb_ostream_t *ostream)
{
    uint32_t num_beacons = get_num_umts(ctx);

    return encode_connected_field(ctx, ostream, num_beacons, UmtsCells_connected_idx_plus_1_tag,
               get_umts_is_connected) &&
           encode_repeated_int_field(
               ctx, ostream, UmtsCells_lac_tag, num_beacons, get_umts_lac, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, UmtsCells_ucid_tag, num_beacons, get_umts_ucid, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, UmtsCells_mcc_tag, num_beacons, get_umts_mcc, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, UmtsCells_mnc_tag, num_beacons, get_umts_mnc, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, UmtsCells_neg_rssi_tag, num_beacons, get_umts_rssi, flip_sign) &&
           encode_optimized_repeated_field(ctx, ostream, num_beacons,
               UmtsCells_common_age_plus_1_tag, UmtsCells_age_tag, get_umts_age);
}

static bool encode_gnss_fields(Sky_ctx_t *ctx, pb_ostream_t *ostream)
{
    uint32_t num_gnss = get_num_gnss(ctx);

    return encode_repeated_int_field(
               ctx, ostream, Gnss_lat_tag, num_gnss, get_gnss_lat_scaled, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, Gnss_lon_tag, num_gnss, get_gnss_lon_scaled, NULL) &&
           encode_repeated_int_field(ctx, ostream, Gnss_hpe_tag, num_gnss, get_gnss_hpe, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, Gnss_alt_tag, num_gnss, get_gnss_alt_scaled, NULL) &&
           encode_repeated_int_field(ctx, ostream, Gnss_vpe_tag, num_gnss, get_gnss_vpe, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, Gnss_speed_tag, num_gnss, get_gnss_speed_scaled, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, Gnss_bearing_tag, num_gnss, get_gnss_bearing, NULL) &&
           encode_repeated_int_field(ctx, ostream, Gnss_nsat_tag, num_gnss, get_gnss_nsat, NULL) &&
           encode_repeated_int_field(ctx, ostream, Gnss_age_tag, num_gnss, get_gnss_age, NULL);
}

static bool encode_submessage(
    Sky_ctx_t *ctx, pb_ostream_t *ostream, uint32_t tag, EncodeSubmsgCallback func)
{
    // Get and encode the submessage size.
    pb_ostream_t substream = PB_OSTREAM_SIZING;

    // Encode the submessage tag.
    if (!pb_encode_tag(ostream, PB_WT_STRING, tag))
        return false;

    if (!func(ctx, &substream))
        return false;

    if (!pb_encode_varint(ostream, substream.bytes_written))
        return false;

    // Encode the submessage.
    if (!func(ctx, ostream))
        return false;

    return true;
}

bool Rq_callback(pb_istream_t *istream, pb_ostream_t *ostream, const pb_field_t *field)
{
    Sky_ctx_t *ctx = *(Sky_ctx_t **)field->pData;

    // Per the documentation here:
    // https://jpa.kapsi.fi/nanopb/docs/reference.html#pb-encode-delimited
    //
    switch (field->tag) {
    case Rq_aps_tag:
        if (get_num_aps(ctx))
            return encode_submessage(ctx, ostream, field->tag, encode_ap_fields);
        break;
    case Rq_vaps_tag:
        return encode_vap_data(ctx, ostream, Rq_vaps_tag, get_num_vaps(ctx), get_vap_data);

    case Rq_cdma_cells_tag:
        if (get_num_cdma(ctx))
            return encode_submessage(ctx, ostream, field->tag, encode_cdma_fields);
        break;
    case Rq_gsm_cells_tag:
        if (get_num_gsm(ctx))
            return encode_submessage(ctx, ostream, field->tag, encode_gsm_fields);
        break;
    case Rq_lte_cells_tag:
        if (get_num_lte(ctx))
            return encode_submessage(ctx, ostream, field->tag, encode_lte_fields);
        break;
    case Rq_nbiot_cells_tag:
        if (get_num_nbiot(ctx))
            return encode_submessage(ctx, ostream, field->tag, encode_nbiot_fields);
        break;
    case Rq_umts_cells_tag:
        if (get_num_umts(ctx))
            return encode_submessage(ctx, ostream, field->tag, encode_umts_fields);
        break;
    case Rq_gnss_tag:
        if (get_num_gnss(ctx))
            return encode_submessage(ctx, ostream, field->tag, encode_gnss_fields);
        break;
    default:
        printf("Rq_callback() ERROR: unknown tag.\n");
        break;
    }

    return true;
}

int32_t get_maximum_response_size(void)
{
    return RsHeader_size + CryptoInfo_size + 1 +
           AES_BLOCKLEN *
               ((Rs_size - ClientConfig_size + MAX_CLIENTCONFIG_SIZE + AES_BLOCKLEN - 1) /
                   AES_BLOCKLEN);
}

int32_t serialize_request(
    Sky_ctx_t *ctx, uint8_t *buf, uint32_t buf_len, uint32_t sw_version, bool request_config)
{
    size_t rq_size, aes_padding_length, crypto_info_size, hdr_size, total_length;
    int32_t bytes_written;
    struct AES_ctx aes_ctx;

    RqHeader rq_hdr = RqHeader_init_default;
    CryptoInfo rq_crypto_info = CryptoInfo_init_default;

    Rq rq;

    pb_ostream_t ostream;

    rq_hdr.partner_id = get_ctx_partner_id(ctx);

    // sky_new_request initializes rand_bytes if user does not
    if (ctx->rand_bytes != NULL)
        ctx->rand_bytes(rq_crypto_info.iv.bytes, AES_BLOCKLEN);

    // Initialize crypto_info
    rq_crypto_info.iv.size = AES_BLOCKLEN;

    memset(&rq, 0, sizeof(rq));

    rq.aps = rq.vaps = rq.gsm_cells = rq.nbiot_cells = rq.cdma_cells = rq.lte_cells =
        rq.umts_cells = rq.gnss = ctx;

    rq.timestamp = (int64_t)(*ctx->gettime)(NULL);

    memcpy(rq.device_id.bytes, get_ctx_device_id(ctx), get_ctx_id_length(ctx));
    rq.device_id.size = get_ctx_id_length(ctx);

    // Create and serialize the request.
    if (pb_get_encoded_size(&rq_size, Rq_fields, &rq) == false) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "pb_get_encoded_size");
        return -1;
    }

    // Account for necessary encryption padding.
    aes_padding_length = (AES_BLOCKLEN - rq_size % AES_BLOCKLEN) % AES_BLOCKLEN;

    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "rq_size %d + aes padding %d", rq_size, aes_padding_length);

    rq_size += aes_padding_length;

    rq_crypto_info.aes_padding_length = aes_padding_length;

    pb_get_encoded_size(&crypto_info_size, CryptoInfo_fields, &rq_crypto_info);

    // Initialize request header.
    rq_hdr.crypto_info_length = crypto_info_size;
    rq_hdr.rq_length = rq_size;
    rq_hdr.sw_version = sw_version;
    rq_hdr.request_client_conf = request_config ? 1 : 0;

    // First byte of message on wire is the length (in bytes) of the request
    // header.
    pb_get_encoded_size(&hdr_size, RqHeader_fields, &rq_hdr);

    total_length = 1 + hdr_size + crypto_info_size + rq_size;
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Total length %d. 1 + %d + %d + %d", total_length, hdr_size,
        crypto_info_size, rq_size);

    // Exit if we've been called just for the purpose of determining how much
    // buffer space is necessary.
    //
    if (buf == NULL)
        return total_length;

    // Return an error indication if the supplied buffer is too small.
    if (total_length > buf_len) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "total_length(%d) < buf_len(%d)", total_length, buf_len);
        return -1;
    }

    *buf = (uint8_t)hdr_size;

    bytes_written = 1;

    ostream = pb_ostream_from_buffer(buf + 1, hdr_size);

    if (pb_encode(&ostream, RqHeader_fields, &rq_hdr))
        bytes_written += ostream.bytes_written;
    else {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "pb_encode: RqHeader_fields");
        return -1;
    }

    // Serialize the crypto_info message.
    ostream = pb_ostream_from_buffer(buf + bytes_written, crypto_info_size);

    if (pb_encode(&ostream, CryptoInfo_fields, &rq_crypto_info))
        bytes_written += ostream.bytes_written;
    else {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "pb_encode: CryptoInfo_fields");
        return -1;
    }

    // Serialize the request body.
    //
    buf += bytes_written;

    ostream = pb_ostream_from_buffer(buf, rq_size);

    // Initialize request body.
    if (pb_encode(&ostream, Rq_fields, &rq))
        bytes_written += ostream.bytes_written;
    else {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "pb_encode: Rq_fields");
        return -1;
    }

#if SKY_DEBUG
    log_buffer(__FILE__, __FUNCTION__, ctx, SKY_LOG_LEVEL_DEBUG, buf, bytes_written);
#endif

    // Encrypt the (serialized) request body.
    //
    // TODO: value the padding bytes explicitly instead of just letting them be
    // whatever is in the buffer.
    //

    AES_init_ctx_iv(&aes_ctx, get_ctx_aes_key(ctx), rq_crypto_info.iv.bytes);

    AES_CBC_encrypt_buffer(&aes_ctx, buf, rq_size);

    return bytes_written + aes_padding_length;
}

int32_t deserialize_response(Sky_ctx_t *ctx, uint8_t *buf, uint32_t buf_len, Sky_location_t *loc)
{
    uint8_t hdr_size = *buf;
    struct AES_ctx aes_ctx;

    RsHeader header;

    Rs rs = Rs_init_default;

    CryptoInfo crypto_info;

    pb_istream_t istream;

    int a, i;

    // We assume that buf contains the response message in its entirety. (Since
    // the server closes the connection after sending the response, the client
    // doesn't need to know how many bytes to read - it just keeps reading
    // until the connection is closed by the server.)
    //
    // Deserialize the header. First byte of input buffer represents length of
    // header.
    //
    if (buf_len < 1) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "buf_len < 1");
        return -1;
    }

    buf += 1;

    if (buf_len < 1 + hdr_size) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "buf_len < 1 + hdr_size");
        return -1;
    }

    istream = pb_istream_from_buffer(buf, hdr_size);

    if (!pb_decode(&istream, RsHeader_fields, &header)) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "pb_decode for header");
        return -1;
    }

    memset(loc, 0, sizeof(*loc));
    loc->location_status = (Sky_loc_status_t)header.status;

    if (header.rs_length) {
        buf += hdr_size;

        // Deserialize the crypto_info.
        if (buf_len < 1 + hdr_size + header.crypto_info_length + header.rs_length) {
            LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "buf_len <...");
            return -1;
        }

        istream = pb_istream_from_buffer(buf, header.crypto_info_length);

        if (!pb_decode(&istream, CryptoInfo_fields, &crypto_info)) {
            LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "pb_decode for crypto");
            return -1;
        }

        buf += header.crypto_info_length;

        // Decrypt the response body.
        AES_init_ctx_iv(&aes_ctx, get_ctx_aes_key(ctx), crypto_info.iv.bytes);

        AES_CBC_decrypt_buffer(&aes_ctx, buf, header.rs_length);

        // Deserialize the response body.
        istream = pb_istream_from_buffer(buf, header.rs_length - crypto_info.aes_padding_length);

#if SKY_DEBUG
        log_buffer(__FILE__, __FUNCTION__, ctx, SKY_LOG_LEVEL_DEBUG, buf,
            header.rs_length - crypto_info.aes_padding_length);
#endif

        if (!pb_decode(&istream, Rs_fields, &rs)) {
            LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "pb_decode for body");
            return -1;
        }

        loc->lat = rs.lat;
        loc->lon = rs.lon;
        loc->hpe = (uint16_t)rs.hpe;
        loc->location_source = (Sky_loc_source_t)rs.source;
        // Extract Used info for each AP from the Used_aps bytes
        for (a = 0; a < ctx->ap_len; a++) {
            i = rs.used_aps.size - 1 - (a / 8);
            // LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Used: AP %d, Byte: 0x%02X idx: %d:%d", a, rs.used_aps.bytes[i], i, (a % 8));
            if (a < rs.used_aps.size * 8)
                ctx->beacon[a].ap.property.used = rs.used_aps.bytes[i] & (1 << (a % 8)) ? 1 : 0;
            else
                ctx->beacon[a].ap.property.used = 0;
        }

        if (apply_config_overrides(ctx->cache, &rs)) {
            if (ctx->logf && SKY_LOG_LEVEL_DEBUG <= ctx->min_level)
                (*ctx->logf)(SKY_LOG_LEVEL_DEBUG, "New config overrides received from server");
        }
        if (CONFIG(ctx->cache, last_config_time) == 0)
            CONFIG(ctx->cache, last_config_time) = (*ctx->gettime)(NULL);
    }

    return 0;
}

static int64_t get_gnss_lat_scaled(Sky_ctx_t *ctx, uint32_t idx)
{
    return get_gnss_lat(ctx, idx) * 1000000;
}

static int64_t get_gnss_lon_scaled(Sky_ctx_t *ctx, uint32_t idx)
{
    return get_gnss_lon(ctx, idx) * 1000000;
}

static int64_t get_gnss_alt_scaled(Sky_ctx_t *ctx, uint32_t idx)
{
    return get_gnss_alt(ctx, idx) * 10;
}

static int64_t get_gnss_speed_scaled(Sky_ctx_t *ctx, uint32_t idx)
{
    return get_gnss_speed(ctx, idx) * 10;
}

/*! \brief update dynamic config params with server overrides
 *
 *  @param c cache buffer
 *
 *  @return bool true if new override is recived from server
 */
static bool apply_config_overrides(Sky_cache_t *c, Rs *rs)
{
    bool override = false;

    config_defaults(c);
    if (rs->config.total_beacons != 0 && rs->config.total_beacons != CONFIG(c, total_beacons)) {
        if (rs->config.total_beacons < TOTAL_BEACONS && rs->config.total_beacons > 1) {
            CONFIG(c, total_beacons) = rs->config.total_beacons;
        }
    }
    if (rs->config.max_ap_beacons != 0 && rs->config.max_ap_beacons != CONFIG(c, max_ap_beacons)) {
        if (rs->config.max_ap_beacons < MAX_AP_BEACONS) {
            CONFIG(c, max_ap_beacons) = rs->config.max_ap_beacons;
        }
    }
    if (rs->config.cache_age_threshold != 0 &&
        rs->config.cache_age_threshold != CONFIG(c, cache_age_threshold)) {
        if (rs->config.cache_age_threshold < 9000) {
            CONFIG(c, cache_age_threshold) = rs->config.cache_age_threshold;
        }
    }
    if (rs->config.cache_beacon_threshold != 0 &&
        rs->config.cache_beacon_threshold != CONFIG(c, cache_beacon_threshold)) {
        if (rs->config.cache_beacon_threshold < CONFIG(c, total_beacons)) {
            CONFIG(c, cache_beacon_threshold) = rs->config.cache_beacon_threshold;
        }
    }
    if (rs->config.cache_neg_rssi_threshold != 0 &&
        rs->config.cache_neg_rssi_threshold != CONFIG(c, cache_neg_rssi_threshold)) {
        if (rs->config.cache_neg_rssi_threshold >= 10 &&
            rs->config.cache_neg_rssi_threshold < 128) {
            CONFIG(c, cache_neg_rssi_threshold) = rs->config.cache_neg_rssi_threshold;
        }
    }
    if (rs->config.cache_match_all_threshold != 0 &&
        rs->config.cache_match_all_threshold != CONFIG(c, cache_match_all_threshold)) {
        if (rs->config.cache_match_all_threshold > 0 &&
            rs->config.cache_match_all_threshold <= 100) {
            CONFIG(c, cache_match_all_threshold) = rs->config.cache_match_all_threshold;
        }
    }
    if (rs->config.cache_match_used_threshold != 0 &&
        rs->config.cache_match_used_threshold != CONFIG(c, cache_match_used_threshold)) {
        if (rs->config.cache_match_used_threshold > 0 &&
            rs->config.cache_match_used_threshold <= 100) {
            CONFIG(c, cache_match_used_threshold) = rs->config.cache_match_used_threshold;
        }
    }
    if (rs->config.max_vap_per_ap != 0 && rs->config.max_vap_per_ap != CONFIG(c, max_vap_per_ap)) {
        if (rs->config.max_vap_per_ap > 0 && rs->config.max_vap_per_ap <= 15) {
            CONFIG(c, max_vap_per_ap) = rs->config.max_vap_per_ap;
        } else {
            CONFIG(c, max_vap_per_ap) = 0; // disable vap compression
        }
    }
    if (rs->config.max_vap_per_rq != 0 && rs->config.max_vap_per_rq != CONFIG(c, max_vap_per_rq)) {
        if (rs->config.max_vap_per_rq > 0 && rs->config.max_vap_per_rq <= 255) {
            CONFIG(c, max_vap_per_rq) = rs->config.max_vap_per_rq;
        }
    }
    override =
        (rs->config.total_beacons != 0 || rs->config.max_ap_beacons != 0 ||
            rs->config.cache_match_threshold != 0 || rs->config.cache_age_threshold != 0 ||
            rs->config.cache_beacon_threshold != 0 || rs->config.cache_neg_rssi_threshold != 0 ||
            rs->config.max_vap_per_ap != 0 || rs->config.max_vap_per_rq);

    /* Add new config parameters here */

    return override;
}
