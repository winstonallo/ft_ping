#include "ft_ping.h"
#include <bits/types/struct_iovec.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// Computes IP checksum (16 bit one's complement sum), ensuring packet integrity before accepting.
// .
// This ensures that the checksum result will not exceed the size of a 16 bit integer by
// wrapping around carry instead of making the number outgrow its bounds.
// .
// The sender calculates the checksum with '0' as a placeholder value in the checksum field. The receiver then
// calculates it including the checksum sent by the sender. Its one's complement sum is expected to be '0' -
// packets where this calculation fails are discarded.
// .
// https://web.archive.org/web/20020916085726/http://www.netfor2.com/checksum.html
static unsigned short
checksum(const void *const buffer, int len) {
    const unsigned short *buf = buffer;
    unsigned int sum = 0;

    for (sum = 0; len > 1; len -= 2) {
        sum += *buf++;
    }
    if (len == 1) {
        sum += *(unsigned char *)buf;
    }

    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);

    return ~sum;
}

void
init_icmp_header(Args *const args, const int seq) {
    args->icmp_h = (struct icmp *)args->packet;
    args->icmp_h->icmp_type = ICMP_ECHO;
    args->icmp_h->icmp_id = getpid();
    args->icmp_h->icmp_seq = seq;

    // `icmp_h` and `packet` point to the same memory address, they are just _cast to different types_.
    // Removing this line will result in the checksum not matching and all packets (except for the first
    // one) being lost!
    args->icmp_h->icmp_cksum = 0;
    args->icmp_h->icmp_cksum = checksum(args->packet, sizeof(args->packet));
}

Result
send_packet(const Args *const args, struct sockaddr_in *send_addr) {
    if (sendto(g_stats.sockfd, args->packet, sizeof(args->packet), 0, (struct sockaddr *)send_addr, sizeof(*send_addr)) <= 0) {
        return err_fmt(2, "sendto: ", strerror(errno));
    }
    g_stats.sent++;
    return ok(NULL);
}

static bool
packet_is_unexpected(struct icmp *icmp, struct icmp *icmp_header, const int seq) {
    const bool is_echo_reply = icmp->icmp_type == ICMP_ECHOREPLY;
    const bool id_matches = icmp->icmp_id == icmp_header->icmp_id;
    const bool seq_matches = icmp->icmp_seq == seq;

    return !is_echo_reply || !id_matches || !seq_matches;
}

Result
receive_packet(Args *const args, const int seq, const struct timeval *const trip_begin) {
    while (true) {
        ssize_t recv_len = recvfrom(g_stats.sockfd, args->buf, sizeof(args->buf), 0, (struct sockaddr *)&args->recv_addr, &args->recv_addr_len);

        struct iphdr *ip = (struct iphdr *)args->buf;
        size_t iphdr_len = ip->ihl << 2;
        struct icmp *icmp = (struct icmp *)(args->buf + iphdr_len);
        if (recv_len <= 0) {
            return recv_error(icmp, seq, recv_len);
        }

        if (packet_is_unexpected(icmp, args->icmp_h, seq)) {
            continue;
        }

        double rt_ms = update_stats(trip_begin);
        if ((int)rt_ms == -1) {
            return err(NULL);
        }

        display_rt_stats(args, icmp, ip, rt_ms);
    }
    return ok(NULL);
}
