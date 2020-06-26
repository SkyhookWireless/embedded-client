#ifndef SEND_H
#define SEND_H

int XPS_send_request(
    char *request, int req_size, uint8_t *response, int resp_size, char *server, uint16_t port);

#endif
