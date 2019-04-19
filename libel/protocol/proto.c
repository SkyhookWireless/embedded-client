#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include <pb_encode.h>
#include <pb_decode.h>

#include "el.pb.h"
#include "proto.h"
#include "aes.h"

typedef int64_t (*DataGetter)(Sky_ctx_t *, uint32_t);
typedef int64_t (*DataWrapper)(int64_t);
typedef bool (*EncodeSubmsgCallback)(Sky_ctx_t *, pb_ostream_t *);

static int64_t mac_to_int(Sky_ctx_t *ctx, uint32_t idx)
{
    // This is a wrapper function around get_ap_mac(). It converts the 8-byte
    // mac array to an uint64_t.
    //
    uint8_t *mac = get_ap_mac(ctx, idx);

    uint64_t ret_val = 0;

    for (size_t i = 0; i < 6; i++)
        ret_val = ret_val * 256 + mac[i];

    return ret_val;
}

static int64_t flip_sign(int64_t value)
{
    return -value;
}

static bool encode_repeated_int_field(Sky_ctx_t *ctx, pb_ostream_t *ostream,
    uint32_t tag, uint32_t num_elems, DataGetter getter, DataWrapper wrapper)
{
    // Encode field tag.
    if (!pb_encode_tag(ostream, PB_WT_STRING, tag))
        return false;

    // Get and encode the field size.
    pb_ostream_t substream = PB_OSTREAM_SIZING;

    for (size_t i = 0; i < num_elems; i++) {
        int64_t data = getter(ctx, i);

        if (wrapper != NULL)
            data = wrapper(data);

        if (!pb_encode_varint(&substream, data))
            return false;
    }

    if (!pb_encode_varint(ostream, substream.bytes_written))
        return false;

    // Now encode the field for real.
    for (size_t i = 0; i < num_elems; i++) {
        int64_t data = getter(ctx, i);

        if (wrapper != NULL)
            data = wrapper(data);

        if (!pb_encode_varint(ostream, data))
            return false;
    }

    return true;
}

static bool encode_connected_field(Sky_ctx_t *ctx, pb_ostream_t *ostream,
    uint32_t num_beacons, uint32_t tag,
    bool (*callback)(Sky_ctx_t *, uint32_t idx))
{
    bool retval = true;

    for (size_t i = 0; i < num_beacons; i++) {
        if (callback(ctx, i)) {
            retval = pb_encode_tag(ostream, PB_WT_VARINT, tag) &&
                     pb_encode_varint(ostream, i + 1);

            break;
        }
    }

    return retval;
}

static bool encode_age_field(Sky_ctx_t *ctx, pb_ostream_t *ostream,
    uint32_t num_beacons, uint32_t tag1, uint32_t tag2, DataGetter getter)
{
    // Encode age fields. Optimization: send only a single common age value if
    // all ages are the same.
    int64_t age = getter(ctx, 0);
    bool ages_all_same = true;

    for (size_t i = 1; ages_all_same && i < num_beacons; i++) {
        if (getter(ctx, i) != age)
            ages_all_same = false;
    }

    if (num_beacons > 1 && ages_all_same) {
        return pb_encode_tag(ostream, PB_WT_VARINT, tag1) &&
               pb_encode_varint(ostream, age + 1);
    } else {
        return encode_repeated_int_field(
            ctx, ostream, tag2, num_beacons, getter, NULL);
    }
}

static bool encode_ap_fields(Sky_ctx_t *ctx, pb_ostream_t *ostream)
{
    uint32_t num_beacons = get_num_aps(ctx);

    return encode_connected_field(ctx, ostream, num_beacons,
               Aps_connected_idx_plus_1_tag, get_ap_is_connected) &&
           encode_repeated_int_field(
               ctx, ostream, Aps_mac_tag, num_beacons, mac_to_int, NULL) &&
           encode_repeated_int_field(ctx, ostream, Aps_channel_number_tag,
               num_beacons, get_ap_freq, NULL) &&
           encode_repeated_int_field(ctx, ostream, Aps_neg_rssi_tag,
               num_beacons, get_ap_rssi, flip_sign) &&
           encode_age_field(ctx, ostream, num_beacons,
               Aps_common_age_plus_1_tag, Aps_age_tag, get_ap_age);
}

static bool encode_gsm_fields(Sky_ctx_t *ctx, pb_ostream_t *ostream)
{
    uint32_t num_beacons = get_num_gsm(ctx);

    return encode_connected_field(ctx, ostream, num_beacons,
               GsmCells_connected_idx_plus_1_tag, get_gsm_is_connected) &&
           encode_repeated_int_field(ctx, ostream, GsmCells_mcc_tag,
               num_beacons, get_gsm_mcc, NULL) &&
           encode_repeated_int_field(ctx, ostream, GsmCells_mnc_tag,
               num_beacons, get_gsm_mnc, NULL) &&
           encode_repeated_int_field(ctx, ostream, GsmCells_lac_tag,
               num_beacons, get_gsm_lac, NULL) &&
           encode_repeated_int_field(
               ctx, ostream, GsmCells_ci_tag, num_beacons, get_gsm_ci, NULL) &&
           encode_repeated_int_field(ctx, ostream, GsmCells_neg_rssi_tag,
               num_beacons, get_gsm_rssi, flip_sign) &&
           encode_age_field(ctx, ostream, num_beacons,
               GsmCells_common_age_plus_1_tag, GsmCells_age_tag, get_gsm_age);
}

static bool encode_nbiot_fields(Sky_ctx_t *ctx, pb_ostream_t *ostream)
{
    uint32_t num_beacons = get_num_nbiot(ctx);

    return encode_connected_field(ctx, ostream, num_beacons,
               NbiotCells_connected_idx_plus_1_tag, get_nbiot_is_connected) &&
           encode_repeated_int_field(ctx, ostream, NbiotCells_mcc_tag,
               num_beacons, get_nbiot_mcc, NULL) &&
           encode_repeated_int_field(ctx, ostream, NbiotCells_mnc_tag,
               num_beacons, get_nbiot_mnc, NULL) &&
           encode_repeated_int_field(ctx, ostream, NbiotCells_tac_tag,
               num_beacons, get_nbiot_tac, NULL) &&
           encode_repeated_int_field(ctx, ostream, NbiotCells_e_cellid_tag,
               num_beacons, get_nbiot_ecellid, NULL) &&
           encode_repeated_int_field(ctx, ostream, NbiotCells_neg_rssi_tag,
               num_beacons, get_nbiot_rssi, flip_sign) &&
           encode_age_field(ctx, ostream, num_beacons,
               NbiotCells_common_age_plus_1_tag, NbiotCells_age_tag,
               get_nbiot_age);
}

static bool encode_lte_fields(Sky_ctx_t *ctx, pb_ostream_t *ostream)
{
    uint32_t num_beacons = get_num_lte(ctx);

    return encode_connected_field(ctx, ostream, num_beacons,
               LteCells_connected_idx_plus_1_tag, get_lte_is_connected) &&
           encode_repeated_int_field(ctx, ostream, LteCells_mcc_tag,
               num_beacons, get_lte_mcc, NULL) &&
           encode_repeated_int_field(ctx, ostream, LteCells_mnc_tag,
               num_beacons, get_lte_mnc, NULL) &&
           encode_repeated_int_field(ctx, ostream, LteCells_tac_tag,
               num_beacons, get_lte_tac, NULL) &&
           encode_repeated_int_field(ctx, ostream, LteCells_eucid_tag,
               num_beacons, get_lte_e_cellid, NULL) &&
           encode_repeated_int_field(ctx, ostream, LteCells_neg_rssi_tag,
               num_beacons, get_lte_rssi, flip_sign) &&
           encode_age_field(ctx, ostream, num_beacons,
               LteCells_common_age_plus_1_tag, LteCells_age_tag, get_lte_age);
}

static bool encode_submessage(Sky_ctx_t *ctx, pb_ostream_t *ostream,
    uint32_t tag, EncodeSubmsgCallback func)
{
    // Encode the submessage tag.
    if (!pb_encode_tag(ostream, PB_WT_STRING, tag))
        return false;

    // Get and encode the submessage size.
    pb_ostream_t substream = PB_OSTREAM_SIZING;

    if (!func(ctx, &substream))
        return false;

    if (!pb_encode_varint(ostream, substream.bytes_written))
        return false;

    // Encode the submessage.
    if (!func(ctx, ostream))
        return false;

    return true;
}

bool Rq_callback(
    pb_istream_t *istream, pb_ostream_t *ostream, const pb_field_t *field)
{
    Sky_ctx_t *ctx = *(Sky_ctx_t **)field->pData;

    // Per the documentation here:
    // https://jpa.kapsi.fi/nanopb/docs/reference.html#pb-encode-delimited
    //
    switch (field->tag) {
    case Rq_aps_tag:
        if (get_num_aps(ctx))
            return encode_submessage(
                ctx, ostream, field->tag, encode_ap_fields);
        break;
    case Rq_gsm_cells_tag:
        if (get_num_gsm(ctx))
            return encode_submessage(
                ctx, ostream, field->tag, encode_gsm_fields);
        break;
    case Rq_nbiot_cells_tag:
        if (get_num_nbiot(ctx))
            return encode_submessage(
                ctx, ostream, field->tag, encode_nbiot_fields);
        break;
    case Rq_lte_cells_tag:
        if (get_num_lte(ctx))
            return encode_submessage(
                ctx, ostream, field->tag, encode_lte_fields);
        break;
    default:
        break;
    }

    return true;
}

int32_t serialize_request(Sky_ctx_t *ctx, uint8_t *buf, uint32_t buf_len)
{
    // Initialize request header.
    RqHeader rq_hdr;

    rq_hdr.partner_id = get_ctx_partner_id(ctx);

    // Initialize crypto_info.
    CryptoInfo rq_crypto_info;

    // FIXME: Properly value IV if the user has not provided a rand_bytes()
    // function. Otherwise, and until then, it will get filled with
    // quasi-random stack scheisse.
    //
    if (ctx->rand_bytes != NULL)
        ctx->rand_bytes(rq_crypto_info.iv.bytes, 16);

    rq_crypto_info.iv.size = 16;

    // Initialize request body.
    Rq rq;

    memset(&rq, 0, sizeof(rq));

    rq.aps = rq.gsm_cells = rq.nbiot_cells = rq.cdma_cells = rq.lte_cells =
        rq.umts_cells = ctx;

    rq.timestamp = (int64_t)time(NULL);

    memcpy(rq.device_id.bytes, get_ctx_device_id(ctx), get_ctx_id_length(ctx));
    rq.device_id.size = get_ctx_id_length(ctx);

    // Create and serialize the request header message.
    size_t rq_size;
    pb_get_encoded_size(&rq_size, Rq_fields, &rq);

    // Account for necessary encryption padding.
    size_t aes_padding_length = (16 - rq_size % 16) % 16;

    rq_crypto_info.aes_padding_length = aes_padding_length;

    size_t crypto_info_size;

    pb_get_encoded_size(&crypto_info_size, CryptoInfo_fields, &rq_crypto_info);

    rq_hdr.crypto_info_length = crypto_info_size;
    rq_hdr.rq_length = rq_size + aes_padding_length;

    // First byte of message on wire is the length (in bytes) of the request
    // header.
    size_t hdr_size;

    pb_get_encoded_size(&hdr_size, RqHeader_fields, &rq_hdr);

    size_t total_length =
        1 + hdr_size + rq_hdr.crypto_info_length + rq_hdr.rq_length;

    // Exit if we've been called just for the purpose of determining how much
    // buffer space is necessary.
    //
    if (buf == NULL)
        return total_length;

    // Return an error indication if the supplied buffer is too small.
    if (total_length > buf_len)
        return -1;

    *buf = (uint8_t)hdr_size;

    int32_t bytes_written = 1;

    pb_ostream_t hdr_ostream = pb_ostream_from_buffer(buf + 1, buf_len);

    if (pb_encode(&hdr_ostream, RqHeader_fields, &rq_hdr))
        bytes_written += hdr_ostream.bytes_written;
    else
        return -1;

    // Serialize the crypto_info message.
    pb_ostream_t crypto_info_ostream =
        pb_ostream_from_buffer(buf + bytes_written, buf_len - bytes_written);

    if (pb_encode(&crypto_info_ostream, CryptoInfo_fields, &rq_crypto_info))
        bytes_written += crypto_info_ostream.bytes_written;
    else
        return -1;

    // Serialize the request body.
    //
    buf += bytes_written;

    pb_ostream_t rq_ostream =
        pb_ostream_from_buffer(buf, buf_len - bytes_written);

    if (pb_encode(&rq_ostream, Rq_fields, &rq))
        bytes_written += rq_ostream.bytes_written;
    else
        return -1;

    // Encrypt the (serialized) request body.
    //
    // TODO: value the padding bytes explicitly instead of just letting them be
    // whatever is in the buffer.
    //
    struct AES_ctx aes_ctx;

    AES_init_ctx_iv(&aes_ctx, get_ctx_aes_key(ctx), rq_crypto_info.iv.bytes);

    AES_CBC_encrypt_buffer(&aes_ctx, buf, rq_size + aes_padding_length);

    return bytes_written + aes_padding_length;
}

int32_t deserialize_response(
    Sky_ctx_t *ctx, uint8_t *buf, uint32_t buf_len, Sky_location_t *loc)
{
    // We assume that buf contains the response message in its entirety. (Since
    // the server closes the connection after sending the response, the client
    // doesn't need to know how many bytes to read - it just keeps reading
    // until the connection is closed by the server.)
    //
    // Deserialize the header. First byte of input buffer represents length of
    // header.
    //
    if (buf_len < 1)
        return -1;

    uint8_t hdr_size = *buf;

    buf += 1;

    RsHeader header;

    if (buf_len < 1 + hdr_size)
        return -1;

    pb_istream_t hdr_istream = pb_istream_from_buffer(buf, hdr_size);

    if (!pb_decode(&hdr_istream, RsHeader_fields, &header)) {
        return -1;
    }

    buf += hdr_size;

    // Deserialize the crypto_info.
    CryptoInfo crypto_info;

    if (buf_len < 1 + hdr_size + header.crypto_info_length + header.rs_length)
        return -1;

    pb_istream_t crypto_info_istream =
        pb_istream_from_buffer(buf, header.crypto_info_length);

    if (!pb_decode(&crypto_info_istream, CryptoInfo_fields, &crypto_info)) {
        return -1;
    }

    buf += header.crypto_info_length;

    // Decrypt the response body.
    struct AES_ctx aes_ctx;

    AES_init_ctx_iv(&aes_ctx, get_ctx_aes_key(ctx), crypto_info.iv.bytes);

    AES_CBC_decrypt_buffer(&aes_ctx, buf, header.rs_length);

    // Deserialize the response body.
    pb_istream_t body_info_istream = pb_istream_from_buffer(
        buf, header.rs_length - crypto_info.aes_padding_length);

    Rs rs = Rs_init_default;

    if (!pb_decode(&body_info_istream, Rs_fields, &rs)) {
        return -1;
    } else {
        loc->lat = rs.lat;
        loc->lon = rs.lon;
        loc->hpe = (uint16_t)rs.hpe;
        loc->location_source = (Sky_loc_source_t)rs.source;

        return 0;
    }
}