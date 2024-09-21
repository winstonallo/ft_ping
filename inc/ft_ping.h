#ifndef FT_PING_H
#define FT_PING_H

#include <bits/types/struct_timeval.h>
#include <inttypes.h>
#include <stdbool.h>
#include <sys/types.h>

#define USAGE_ERROR "ft_ping: usage error: Destination address required\n"

#define PING_INTERVAL 1000000
#define PACKET_SIZE 64
#define PAYLOAD_SIZE 56
#define MAX_PINGS 1024

typedef struct {
    unsigned int   transmitted;
    unsigned int   received;
    double         rtt_min;
    double         rtt_avg;
    double         rtt_max;
    double         rtt_mdev;
    double         rtts[MAX_PINGS];
    char           dest_host[256];
    int            sockfd;
    struct timeval start_time;
} Stats;

typedef struct {
    bool  verbose;
    bool  help;
    char* dest;
} Args;

typedef enum {
    OK,
    FAILURE,
    ARG_ERR,
} Ret;

typedef enum {
    ICMP_SEND_OK,
    ICMP_SEND_FAILURE,
    ICMP_SEND_MAX_RETRIES_REACHED,
} ICMPSendRes;

#endif
