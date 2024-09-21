#include "ft_ping.h"
#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <bits/types/struct_iovec.h>
#include <bits/types/struct_timeval.h>
#include <errno.h>
#include <limits.h>
#include <linux/stddef.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

Stats stats = {0};

/*
 * Computes IP checksum (16 bit one's complement sum), ensuring packet integrity before accepting.
 * .
 * This ensures that the checksum result will not exceed the size of a 16 bit integer by
 * wrapping around carry instead of making the number outgrow its bounds.
 * .
 * https://web.archive.org/web/20020916085726/http://www.netfor2.com/checksum.html
 */
unsigned short checksum(const void* const buffer, int len) {
    const unsigned short* buf = buffer;
    unsigned int          sum = 0;

    for (sum = 0; len > 1; len -= 2) {
        sum += *buf++;
    }
    if (len == 1) {
        sum += *(unsigned char*)buf;
    }

    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);

    return ~sum;
}
/**
 * Initializes icmp header at each ping iteration.
 */
void init_icmp_header(struct icmp* const icmp_header, const int seq, const char* const packet, const int packet_len) {
    icmp_header->icmp_type = ICMP_ECHO;
    icmp_header->icmp_id   = getpid();
    icmp_header->icmp_seq  = seq;

    /*
     * `icmp_header` and `packet` point to the same memory address, they are just _cast to different types_.
     * Removing this line will result in the checksum not matching and all packets (except for the first
     * one) being lost!
     */
    icmp_header->icmp_cksum = 0;
    icmp_header->icmp_cksum = checksum(packet, packet_len);
}

/*
 * Handles `SIGINT`.
 * .
 * In the case of `ping`, this means calculating the total ping time (from first ping to
 * signal receive time) and printing the ping statistics before exiting.
 */
void sigint(const int sig) {
    if (sig != SIGINT) {
        return;
    }
    if (stats.transmitted < 1) {
        stats.transmitted = 1;
    }
    int            loss = 100 - (stats.received * 100) / stats.transmitted;
    struct timeval end_time;
    gettimeofday(&end_time, NULL);
    double total_ms = (end_time.tv_sec - stats.start_time.tv_sec) * 1000.0 + (end_time.tv_usec - stats.start_time.tv_usec) / 1000.0;

    printf("\n--- %s ping statistics ---\n%u packets transmitted, %u received, %d%% ", stats.dest_host, stats.transmitted, stats.received, loss);
    printf("packet loss time %dms\nrtt min/avg/max/mdev = %.3f/%.3f/%.3f/%.3f ms\n", (int)total_ms, stats.rtt_min, stats.rtt_avg, stats.rtt_max, stats.rtt_mdev);
    close(stats.sockfd);
    exit(EXIT_SUCCESS);
}

/*
Parses the arguments from the command line.
* .
* Supported options:
*   - `-v`: verbose
*   - `-(h|?)`: help
* .
* Anything without leading `-` is parsed as destination address.
* Expects exactly `1` destination address.
* .
* Returns `0` on success, `-1` on missing destination address, `2` on unexpected input.
*/
int parse_args(const int ac, const char** const av, Args* const args) {
    int i = 0;

    while (++i < ac) {
        if (av[i][0] == '-') {
            if (!strcmp(av[i], "-v")) {
                args->v = true;
            } else if (!strcmp(av[i], "-h") || !strcmp(av[i], "-?")) {
                args->h = true;
                return ARG_ERR;
            } else {
                fprintf(stderr, "Unknown option: %s\n", av[i]);
                args->h = true;
                return ARG_ERR;
            }
        } else {
            if (args->dest == NULL) {
                args->dest = av[i];
            } else {
                fprintf(stderr, "Unexpected argument: %s\n", av[i]);
                args->h = true;
                return ARG_ERR;
            }
        }
    }
    if (!args->dest && !args->h) {
        fprintf(stderr, "Destination address required\n");
        return -1;
    }
    return 0;
}

/*
 * Prints help message.
 * .
 * Returns `2`
 */
int help() {
    printf("\nUsage:\n./ft_ping [OPTIONS] <destination>\n\nOptions:\n\t-v: verbose\n\t-(h | ?): help\n");
    return 2;
}

/*
 * Fills `send_addr` with the destination host's metadata from `getaddrinfo`.
 * .
 * Notes:
 * - Uses raw sockets, needs sudo access
 * - Assumes IPv4, as IPv6 is not required for this projects
 * .
 * Returns `0` on success, `-1` on failure.
 */
int get_send_addr(const Args* const args, struct sockaddr_in* const send_addr) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_RAW;

    int err = getaddrinfo(args->dest, NULL, &hints, &res);
    if (err != 0) {
        if (args->v) {
            printf("ft_ping: sockfd: %d (socktype SOCK_RAW), hints.ai_family: AF_INET\n\n", stats.sockfd);
        }
        fprintf(stderr, "ft_ping: %s: %s\n", args->dest, gai_strerror(err));
        return -1;
    }
    struct sockaddr_in* addr = (struct sockaddr_in*)res->ai_addr;
    send_addr->sin_family    = addr->sin_family;
    send_addr->sin_addr      = addr->sin_addr;
    freeaddrinfo(res);
    return 0;
}

/*
 * Updates statistics to be printed on `SIGINT`:
 * .
 * `rtt_min` (minimum round trip time)
 * `rtt_max` (maximum round trip time)
 * `rtt_avg` (average round trip time)
 * `rtt_mdev` (mean round trip time deviation)
 */
void update_stats(const double ttl_ms) {
    stats.received++;
    stats.rtt_min = fmin(stats.rtt_min, ttl_ms);
    stats.rtt_max = fmax(stats.rtt_max, ttl_ms);
    stats.rtt_avg = ((stats.rtt_avg * (stats.received - 1)) + ttl_ms) / stats.received;

    double sum_deviation = 0.0;
    /*
     * MD = \frac{1}{N} \sum\limits_{i=1}^{N} \left| RTT_i - \overline{RTT} \right|
     * Where:
     * - MD = mean deviation
     * - RTT = round trip times vector
     * - N = number of requests
     */
    for (size_t i = 0; i < stats.received; ++i) {
        sum_deviation += fabs(stats.rtts[i] - stats.rtt_avg);
    }
    stats.rtt_mdev = sum_deviation / stats.received;
}

/*
 * Initializes stats struct, I think you figured that out.
 */
void init_stats() {
    stats.rtt_min = INFINITY;
    stats.rtt_max = 0.0;
    stats.rtt_avg = 0.0;
    gettimeofday(&stats.start_time, NULL);
}

ICMPSendRes send_icmp_packet(const char* const packet, const size_t packet_size, const struct sockaddr_in* const send_addr, int* const failed_attempts) {
    if (sendto(stats.sockfd, packet, packet_size, 0, (struct sockaddr*)send_addr, sizeof(*send_addr)) <= 0) {
        perror("sendto");

        (*failed_attempts)++;
        if (*failed_attempts >= 5) {
            fprintf(stderr, "Too many consecutive failures, exiting.\n");
            return ICMP_SEND_MAX_RETRIES_REACHED;
        }
        return ICMP_SEND_FAILURE;
    }
    stats.transmitted++;
    return ICMP_SEND_OK;
}

ssize_t recv_icmp_packet(char* const buf, const size_t buflen, const struct sockaddr_in* const recv_addr, socklen_t* const addr_len, const int seq, const bool v) {
    ssize_t recv_len = recvfrom(stats.sockfd, buf, buflen, 0, (struct sockaddr*)recv_addr, addr_len);
    if (recv_len <= 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (v) {
                printf("Request timeout for icmp_seq %d\n", seq);
            }
        } else {
            perror("recvfrom");
        }
    }
    return recv_len;
}

void display_rt_stats(const bool v, const char* const ip_str, const struct icmp* const icmp, const struct iphdr* const ip, const double rt_ms) {
    printf("%d bytes from %s: imcp_seq=%u ", PACKET_SIZE, ip_str, icmp->icmp_seq);
    if (v) {
        printf("ident=%d ", icmp->icmp_id);
    }
    /**
     * ttl: Time to live -> how many roundtrips a packet can go in the network before being discarded.
     * Each time a router receives a packet, it subtracts one from ttl before passing it on.
     */
    printf("ttl=%u time=%.3f ms\n", ip->ttl, rt_ms);
}

int set_socket_options() {
    struct timeval timeout;
    timeout.tv_sec  = 1;
    timeout.tv_usec = 0;
    if (setsockopt(stats.sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt");
        return -1;
    }
    return 0;
}

int main(int ac, char** av) {
    stats.sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (stats.sockfd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    Args args = {0};
    if (parse_args(ac, (const char**)av, &args) == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    if (args.h) {
        return help();
    }

    struct sockaddr_in send_addr = {0};
    if (get_send_addr(&args, &send_addr) != 0) {
        return EXIT_FAILURE;
    }

    if (args.v) {
        printf("ft_ping: sockfd: %d (socktype SOCK_RAW), hints.ai_family: AF_INET\n\n", stats.sockfd);
        printf("ai-ai_family: AF_INET, ai->ai_canonname: '%s'\n", args.dest);
    }

    strncpy(stats.dest_host, args.dest, sizeof(stats.dest_host));
    stats.dest_host[sizeof(stats.dest_host) - 1] = '\0';

    char               buffer[1024];
    struct sockaddr_in recv_addr = {0};
    socklen_t          addr_len  = sizeof(recv_addr);
    char               ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(send_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
    printf("PING %s (%s) %d(%zu) data bytes\n", args.dest, ip_str, PAYLOAD_SIZE, sizeof(struct icmp) + PAYLOAD_SIZE);

    /*
     * Fill the packet with easily recognizable default value. Apparently this helps with debugging
     * fragmenation/reassembly issues in bigger networks, I'll give it a try and come back here to change this if
     * it turns out to be bullshit.
     */
    char         packet[sizeof(struct icmp) + PAYLOAD_SIZE] = {0};
    struct icmp* icmp_header                                = (struct icmp*)packet;
    memset(packet + sizeof(struct icmp), 0x42, PAYLOAD_SIZE);
    init_icmp_header(icmp_header, 0, packet, sizeof(packet));

    init_stats();
    if (set_socket_options() < 0) {
        return EXIT_FAILURE;
    }

    int failed_attempts = 0;
    signal(SIGINT, sigint);

    for (int count = 1; count; count++) {
        init_icmp_header(icmp_header, count, packet, sizeof(packet));

        ICMPSendRes icmp_res = send_icmp_packet(packet, sizeof(packet), &send_addr, &failed_attempts);
        if (icmp_res == ICMP_SEND_MAX_RETRIES_REACHED) {
            break;
        } else if (icmp_res == ICMP_SEND_FAILURE) {
            continue;
        }

        struct timeval trip_begin, trip_end;
        gettimeofday(&trip_begin, NULL);

        bool received_reply = false;
        while (true) {
            ssize_t recv_len = recv_icmp_packet(buffer, sizeof(buffer), &recv_addr, &addr_len, count, args.v);
            if (recv_len <= 0) {
                return EXIT_FAILURE;
            }

            /*
             * The Internet Header Length (IHL) field in the IP header is represented in 32-bit
             * words. Since 32 / 8 == 4, each word in this contet is 4 bytes, meaning that we
             * need to multiply the IHL by 4 to get the actual header length in bytes.
             */
            struct iphdr* ip            = (struct iphdr*)buffer;
            size_t        ip_header_len = ip->ihl << 2;

            struct icmp* icmp = (struct icmp*)(buffer + ip_header_len);

            if (icmp->icmp_type == ICMP_ECHOREPLY && icmp->icmp_id == icmp_header->icmp_id && icmp->icmp_seq == count) {
                gettimeofday(&trip_end, NULL);
                double rt_ms = (trip_end.tv_sec - trip_begin.tv_sec) * 1000.0 + (trip_end.tv_usec - trip_begin.tv_usec) / 1000.0;

                display_rt_stats(args.v, ip_str, icmp, ip, rt_ms);

                if (stats.received < MAX_PINGS) {
                    stats.rtts[stats.received] = rt_ms;
                }

                update_stats(rt_ms);

                received_reply = true;
                break;
            }
        }

        if (!received_reply) {
            failed_attempts++;
            if (failed_attempts >= 5) {
                fprintf(stderr, "Too many consecutive failures, exiting.\n");
                break;
            }
        } else {
            failed_attempts = 0;
        }

        usleep(PING_INTERVAL);
    }

    close(stats.sockfd);
    return 0;
}
