#ifndef EZUTIL_H
#define EZUTIL_H

char *XPS_perror(XPS_StatusCode_t err);
int XPS_log_error(char *fmt, ...);
int XPS_log_debug(char *fmt, ...);
int XPS_log_warning(char *fmt, ...);
uint32_t XPS_hex2bin(char *hexstr, uint32_t hexlen, uint8_t *result, uint32_t reslen);
int32_t XPS_bin2hex(char *buff, int32_t buff_len, uint8_t *data, int32_t data_len);
int XPS_skylogger(Sky_log_level_t level, char *s);
XPS_LocationSource_t XPS_determine_source(Sky_loc_source_t s);
const char *XPS_determine_source_str(Sky_loc_source_t s);

#endif
