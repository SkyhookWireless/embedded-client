#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include "sys/time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "proto.h"

//#define SERVER_HOST "elg.skyhook.com"
#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT 9756
#define PARTNER_ID 2
#define AES_KEY "000102030405060708090a0b0c0d0e0f"
#define CLIENT_MAC "deadbeefdead"

bool hostname_to_ip(char* hostname, char* ip, uint16_t ip_len)
{
    struct hostent* he = gethostbyname(hostname);

    if (he == NULL)
    {
        printf("gethostbyname failed");
        return false;
    }

    struct in_addr** addr_list = (struct in_addr **) he->h_addr_list;

    for (size_t i = 0; addr_list[i] != NULL; i++)
    {
        //Return the first one;
        strncpy(ip, inet_ntoa(*addr_list[i]), ip_len);
        return true;
    }

    return false;
}

int main(int argc, char** argv)
{
    (void)argc;	/* suppress warning */
    (void)argv;	/* suppress warning */
    // Initialize request message.
    init_rq(PARTNER_ID,
            AES_KEY,
            CLIENT_MAC);

    // Populate request with dummy data.
    for (size_t i = 0; i < 2; i++)
    {
        add_ap("aabbcc112233", -10, false, Aps_ApBand_UNKNOWN);
        add_ap("aabbcc112244", -20, false, Aps_ApBand_UNKNOWN);
        add_ap("aabbcc112255", -30, false, Aps_ApBand_UNKNOWN);
        add_ap("aabbcc112266", -40, false, Aps_ApBand_UNKNOWN);
    }

    add_lte_cell(300, 400, 32462, -20, 400001);

    // Serialize request.
    unsigned char buf[1024];

    size_t len = serialize_request(buf, sizeof(buf));

    // Write request to a file.
    FILE* fp = fopen("rq.bin", "wb");

    fwrite(buf, len, 1, fp);
    fclose(fp);

    // Send request to server.
    //
    struct sockaddr_in serv_addr;

    memset(&serv_addr, 0, sizeof(serv_addr));

    char ipaddr[16]; // the char representation of an ipv4 address

    // lookup server ip address
    if (!hostname_to_ip(SERVER_HOST, ipaddr, sizeof(ipaddr)))
    {
        printf("Could not resolve host %s\n", SERVER_HOST);
        exit(-1);
    }

    // init server address struct and set ip and port
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    serv_addr.sin_addr.s_addr = inet_addr(ipaddr);

    // open socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd == -1)
    {
        printf("cannot open socket\n");
        exit(-1);
    }

    // Set connection timeout 10 seconds
    struct timeval tv = {10, 0};

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *) &tv, sizeof(struct timeval)))
    {
        printf("setsockopt failed\n");
        exit(-1);
    }

    /* start connection */
    int32_t rc = connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));

    if (rc < 0)
    {
        close(sockfd);
        printf("connect to server failed\n");
        exit(-1);
    }

    /* send data to the server */
    rc = send(sockfd, buf, (size_t) len, 0);

    if (rc != (int32_t)len)
    {
        close(sockfd);
        printf("send() sent a different number of bytes (%d) from expected\n", rc);
        exit(-1);
    }

    // TODO: read the response.
}
