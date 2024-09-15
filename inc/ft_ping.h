#ifndef FT_PING_H
#define FT_PING_H

#include <bits/types/struct_timeval.h>
#include <inttypes.h>
#include <stdbool.h>
#include <sys/types.h>

#define PING_INTERVAL 1000000
#define HELP "\nUsage:\n./ft_ping [OPTIONS] <destination>\n\nOptions:\n\t-v: verbose\n\t-h: help\n"
struct stats {
    unsigned int   transmitted;
    unsigned int   received;
    double         rtt_min;
    double         rtt_avg;
    double         rtt_max;
    double         rtt_mdev;
    char*          dest_host;
    int            sockfd;
    struct timeval start_time;
};

struct icmp_h {
    u_int8_t  type;
    u_int8_t  code;
    u_int16_t cksum;
    u_int16_t id;
    u_int16_t seq;
};

struct args {
    bool  verbose;
    bool  help;
    char* dest;
};

enum ret {
    OK,
    FAILURE,
    ARG_ERR,
};

unsigned short checksum(void* buffer, int len);

#endif
