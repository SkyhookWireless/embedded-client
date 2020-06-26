#ifndef _SKYHOOK_LITE_XPS_H
#define _SKYHOOK_LITE_XPS_H

#include <inttypes.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    XPS_CELL_TYPE_GSM = 1,
    XPS_CELL_TYPE_UMTS,
    XPS_CELL_TYPE_CDMA,
    XPS_CELL_TYPE_NB_IOT,
    XPS_CELL_TYPE_LTE
} XPS_CellType_t;

typedef struct {
    XPS_CellType_t type;
    uint32_t id1; // mcc (gsm, umts, lte, nb_iot) or sid (cdma). 0 if unknown.
    uint32_t id2; // mnc (gsm, umts, lte, nb_iot) or nid (cdma). 0 if unknown.
    uint32_t id3; // cell id (gsm, umts, lte), bsid (cdma). 0 if unknown.
    uint32_t id4; // lac (gsm, umts) or tac (lte). 0 if unknown.
    uint32_t id5; // bsic (gsm), psc (umts) or pci (lte). 0 if unknown.
    int16_t ss; // rssi (gsm, cdma), rscp (umts), rsrp (lte) or nrsrp (nb-iot)
    time_t timestamp;
    bool is_connected;
} XPS_ScannedCell_t;

typedef struct {
    uint8_t mac[6];
    int16_t rssi; // Signal Strength
    uint16_t freq; // channel frequency. 0 if unknown
    int16_t ta; // timing advance (optional). -1 if unknown
    time_t timestamp;
    bool is_connected;
} XPS_ScannedAP_t;

typedef struct {
    double lat;
    double lon;
    float altitude;
    float speed;
    float bearing;
    uint16_t nsat;
    uint16_t hpe;
    uint16_t vpe;
    time_t timestamp;
} XPS_GNSS_t;

typedef enum {
    XPS_LOCATION_SOURCE_UNKNOWN = 0,
    XPS_LOCATION_SOURCE_HYBRID,
    XPS_LOCATION_SOURCE_CELL,
    XPS_LOCATION_SOURCE_WIFI,
    XPS_LOCATION_SOURCE_GNSS
} XPS_LocationSource_t;

typedef struct {
    double lat, lon;
    uint16_t hpe;
    uint32_t timestamp;
    XPS_LocationSource_t location_source;
} XPS_Location_t;

typedef enum {
    XPS_STATUS_OK = 0,
    XPS_STATUS_ERROR_NOT_CONFIGURED,
    XPS_STATUS_ERROR_BAD_BEACON_DATA,
    XPS_STATUS_ERROR_NOT_AUTHORIZED,
    XPS_STATUS_ERROR_SERVER_UNAVAILABLE,
    XPS_STATUS_ERROR_NETWORK_ERROR,
    XPS_STATUS_ERROR_LOCATION_CANNOT_BE_DETERMINED,
    XPS_STATUS_ERROR_INTERNAL = 99
} XPS_StatusCode_t;

XPS_StatusCode_t XPS_set_option(char *key, char *value);

XPS_StatusCode_t XPS_locate(uint16_t num_aps, XPS_ScannedAP_t *aps, uint16_t num_cells,
    XPS_ScannedCell_t *cells, XPS_GNSS_t *gnss, XPS_Location_t *location_result);

#include "ezutil.h"
#include "ezoption.h"
#include "ezsend.h"

#ifdef __cplusplus
}
#endif

#endif
