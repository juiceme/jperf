#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <math.h>
#include <time.h>

#define TEST_PORT    5555
#define MAX_PACKET   70000
#define UDP_MTULEN   1472
#define UDP_MAXLEN   65507
#define PACKET_COUNT 1000
#define PINGPONG     0xaa55a5a1
#define FLOOD_IN     0xaa55a5a2
#define FLOOD_OUT    0xaa55a5a3
#define CONFIRM      0x12345678

char buffer[MAX_PACKET] = {0};

struct command_header
{
    uint32_t mode;
    uint32_t count;
    uint32_t size;
    uint32_t confirm;
} __attribute__((packed));

void usage(void)
{
    printf("\nUsage:\n");
    printf("  jperf --server [-v]\n");
    printf("  jperf --client <ip_address> [--flood] [--over_mtu] [--burst <packets>] [-v]\n\n");
    printf("        Default max sweep size is ip/udp mtu, max sweep size is 65507 bytes\n");
    printf("        Default packet burst is 1000\n\n");
}

void spinner(uint32_t *count)
{
    if (*count == 100)
    {
        printf("\b-");
        fflush(stdout);
    }
    if (*count == 200)
    {
        printf("\b\\");
        fflush(stdout);
    }
    if (*count == 300)
    {
        printf("\b|");
        fflush(stdout);
    }
    if (*count == 400)
    {
        *count = 0;
        printf("\b/");
        fflush(stdout);
    }
}

void do_server(uint32_t verbose)
{
    int sock = 0;
    uint32_t ret = 0;
    uint32_t count = 0;
    uint32_t size = 0;
    uint32_t packet_count = 0;
    uint32_t spinner_count = 0;
    uint32_t mode = 0;
    uint32_t restart = 0;
    uint32_t sa_len = 0;
    struct sockaddr_in addr = {0};
    struct sockaddr_in client = {0};

    printf("Server listening on port %d \n", TEST_PORT);
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        printf("Cannot create socket, err: %s\n", strerror(errno));
        return;
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(TEST_PORT);

    if (bind(sock, (const struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        printf("Cannot bind socket, err: %s\n", strerror(errno));
        return;
    }
    printf(" ");
    while (1)
    {
        ret = recvfrom(sock, buffer, sizeof(buffer), MSG_WAITALL,
                       (struct sockaddr *) &client, &sa_len);
        if (ret == sizeof(struct command_header))
        {
            if (ntohl(((struct command_header *) buffer)->mode) == PINGPONG)
            {
                mode = PINGPONG;
                restart = 1;
            }
            if (ntohl(((struct command_header *) buffer)->mode) == FLOOD_IN)
            {
                mode = FLOOD_IN;
                count = ntohl(((struct command_header *) buffer)->count);
                packet_count = 0;
                restart = 1;
            }
            if (ntohl(((struct command_header *) buffer)->mode) == FLOOD_OUT)
            {
                mode = FLOOD_OUT;
                count = ntohl(((struct command_header *) buffer)->count);
                size = ntohl(((struct command_header *) buffer)->size);
                packet_count = 0;
                restart = 1;
            }
        }
        if (restart == 1)
        {
            printf("\b. ");
            fflush(stdout);
            if (mode != FLOOD_OUT)
            {
                ret = recvfrom(sock, buffer, sizeof(buffer), MSG_WAITALL,
                               (struct sockaddr *) &client, &sa_len);
            }
            restart = 0;
        }
        switch (mode)
        {
            case 0:
                break;
            case PINGPONG:
                sendto(sock, buffer, ret, MSG_CONFIRM,
                       (const struct sockaddr *) &client, sa_len);
                break;
            case FLOOD_IN:
                packet_count++;
                if (packet_count == count)
                {
                    memset(buffer, 0, sizeof(struct command_header));
                    ((struct command_header *) buffer)->mode = htonl(FLOOD_IN);
                    ((struct command_header *) buffer)->confirm = htonl(CONFIRM);
                    sendto(sock, buffer, sizeof(struct command_header), MSG_CONFIRM,
                           (const struct sockaddr *) &client, sa_len);
                    mode = 0;
                }
                break;
            case FLOOD_OUT:
                memset(buffer, 0, sizeof(struct command_header));
                for (packet_count = 0; packet_count < count; packet_count++)
                {
                    sendto(sock, buffer, size, MSG_CONFIRM,
                           (const struct sockaddr *) &client, sa_len);
                    spinner_count++;
                    if (verbose)
                    {
                        spinner(&spinner_count);
                    }
                }
                memset(buffer, 0, sizeof(struct command_header));
                ((struct command_header *) buffer)->mode = htonl(FLOOD_OUT);
                ((struct command_header *) buffer)->confirm = htonl(CONFIRM);
                sendto(sock, buffer, sizeof(struct command_header), MSG_CONFIRM,
                       (const struct sockaddr *) &client, sa_len);
                mode = 0;
                break;
            default:
                break;
        }
        spinner_count++;
        if (verbose)
        {
            spinner(&spinner_count);
        }
    }
}

int interval(struct timespec start, struct timespec stop)
{
    int s = stop.tv_sec - start.tv_sec;
    int ns = stop.tv_nsec - start.tv_nsec;
    if (ns < 0)
    {
        ns = 1000000000 + ns;
        s = s - 1;
    }
    return s * 1000 + round(ns / 1000000);
}

char p_buf[80];

char *bandwidth(struct timespec start, struct timespec stop, uint32_t len, uint32_t burst)
{
    float duration = (float)(interval(start, stop) / 1000.0);
    uint32_t header = (round(len / 1500) + 1) * (22 + 42);
    float bytes = (float)(len * burst);
    float raw_bw = (float)((header + bytes) * 8) / duration / 1000000.0;
    float eff_bw = (float)(bytes * 8) / duration / 1000000.0;

    // raw frame contains 22 byte ethernet header + 42 byte udp header + payload
    snprintf(p_buf, sizeof(p_buf), " %-8.2f       %-8.2f        %-3.2f", raw_bw, eff_bw, duration);
    return p_buf;
}

void print_header(void)
{
    printf("                                        Raw BW [Mb/s]  Eff. BW [Mb/s]  time [s]\n");
    printf("----------------------------------------------------------------------------------\n");
}

void do_client_pingpong(struct addrinfo *info, uint32_t max_packet, uint32_t burst, uint32_t verbose)
{
    int sock = 0;
    uint32_t len = 0;
    uint32_t count = 0;
    uint32_t ret = 0;
    uint32_t spinner_count = 0;
    uint32_t sa_len = 0;
    struct sockaddr_in server = {0};
    struct timespec start, stop;

    printf("\nStarting test against %s on port %d \n",
           inet_ntoa(((struct sockaddr_in *)info->ai_addr)->sin_addr), TEST_PORT);
    print_header();
    if ((sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol)) == -1)
    {
        printf("Cannot create socket, err: %s\n", strerror(errno));
        return;
    }
    server.sin_family = AF_INET;
    server.sin_port = htons(TEST_PORT);
    server.sin_addr.s_addr = INADDR_ANY;

    for (len = 1; len < MAX_PACKET; len = 2 * len)
    {
        if (len > max_packet)
        {
            len = max_packet;
        }
        printf("  Sending packets, length:%d ... \t", len);
        fflush(stdout);

        memset(buffer, 0, sizeof(struct command_header));
        ((struct command_header *) buffer)->mode = htonl(PINGPONG);
        if (sendto(sock, buffer, sizeof(struct command_header), 0,
                   info->ai_addr, info->ai_addrlen) == -1)
        {
            printf("Cannot send data, err: %s\n", strerror(errno));
            return;
        }

        memset(buffer, 0, sizeof(struct command_header));
        clock_gettime(CLOCK_REALTIME, &start);
        for (count = 0; count < burst; count++)
        {
            if (sendto(sock, buffer, len, 0, info->ai_addr, info->ai_addrlen) == -1)
            {
                printf("Cannot send data, err: %s\n", strerror(errno));
                return;
            }
            ret = recvfrom(sock, buffer, sizeof(buffer),
                           MSG_WAITALL, (struct sockaddr *) &server, &sa_len);
            if (ret != len)
            {
                printf("length mismatch, sent %d -> received %d\n", len, ret);
                return;
            }
            spinner_count++;
            if (verbose)
            {
                spinner(&spinner_count);
            }
        }
        clock_gettime(CLOCK_REALTIME, &stop);
        printf("\b %s\n", bandwidth(start, stop, len, burst));

        if (len == max_packet)
        {
            break;
        }
    }
}

void do_client_forward_flood(struct addrinfo *info, uint32_t max_packet, uint32_t burst, uint32_t verbose)
{
    int sock = 0;
    uint32_t len = 0;
    uint32_t count = 0;
    uint32_t ret = 0;
    uint32_t spinner_count = 0;
    uint32_t sa_len = 0;
    struct sockaddr_in server = {0};
    struct timespec start, stop;

    printf("\nStarting test against %s on port %d \n",
           inet_ntoa(((struct sockaddr_in *)info->ai_addr)->sin_addr), TEST_PORT);
    print_header();
    if ((sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol)) == -1)
    {
        printf("Cannot create socket, err: %s\n", strerror(errno));
        return;
    }
    server.sin_family = AF_INET;
    server.sin_port = htons(TEST_PORT);
    server.sin_addr.s_addr = INADDR_ANY;

    for (len = 1; len < MAX_PACKET; len = 2 * len)
    {
        if (len > max_packet)
        {
            len = max_packet;
        }
        printf("  Sending packets, length:%d ... \t", len);
        fflush(stdout);

        memset(buffer, 0, sizeof(struct command_header));
        ((struct command_header *) buffer)->mode = htonl(FLOOD_IN);
        ((struct command_header *) buffer)->count = htonl(PACKET_COUNT);
        if (sendto(sock, buffer, sizeof(struct command_header), 0,
                   info->ai_addr, info->ai_addrlen) == -1)
        {
            printf("Cannot send data, err: %s\n", strerror(errno));
            return;
        }

        memset(buffer, 0, sizeof(struct command_header));
        clock_gettime(CLOCK_REALTIME, &start);
        for (count = 0; count < burst; count++)
        {
            if (sendto(sock, buffer, len, 0, info->ai_addr, info->ai_addrlen) == -1)
            {
                printf("Cannot send data, err: %s\n", strerror(errno));
                return;
            }
            spinner_count++;
            if (verbose)
            {
                spinner(&spinner_count);
            }
        }
        ret = recvfrom(sock, buffer, sizeof(buffer),
                       MSG_WAITALL, (struct sockaddr *) &server, &sa_len);
        if (ret == sizeof(struct command_header))
        {
            if (ntohl(((struct command_header *) buffer)->mode) == FLOOD_IN &&
                ntohl(((struct command_header *) buffer)->confirm) == CONFIRM)
            {
                clock_gettime(CLOCK_REALTIME, &stop);
            }
        }
        printf("\b %s\n", bandwidth(start, stop, len, burst));

        if (len == max_packet)
        {
            break;
        }
    }
}

void do_client_reverse_flood(struct addrinfo *info, uint32_t max_packet, uint32_t burst, uint32_t verbose)
{
    int sock = 0;
    uint32_t len = 0;
    uint32_t ret = 0;
    uint32_t spinner_count = 0;
    uint32_t sa_len = 0;
    struct sockaddr_in server = {0};
    struct timespec start, stop;

    printf("\nStarting test against %s on port %d \n",
           inet_ntoa(((struct sockaddr_in *)info->ai_addr)->sin_addr), TEST_PORT);
    print_header();
    if ((sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol)) == -1)
    {
        printf("Cannot create socket, err: %s\n", strerror(errno));
        return;
    }
    server.sin_family = AF_INET;
    server.sin_port = htons(TEST_PORT);
    server.sin_addr.s_addr = INADDR_ANY;

    for (len = 1; len < MAX_PACKET; len = 2 * len)
    {
        if (len > max_packet)
        {
            len = max_packet;
        }
        printf("  Receiving packets, length:%d ... \t", len);
        fflush(stdout);

        memset(buffer, 0, sizeof(struct command_header));
        ((struct command_header *) buffer)->mode = htonl(FLOOD_OUT);
        ((struct command_header *) buffer)->count = htonl(burst);
        ((struct command_header *) buffer)->size = htonl(len);
        if (sendto(sock, buffer, sizeof(struct command_header), 0,
                   info->ai_addr, info->ai_addrlen) == -1)
        {
            printf("Cannot send data, err: %s\n", strerror(errno));
            return;
        }

        clock_gettime(CLOCK_REALTIME, &start);
        while (1)
        {
            ret = recvfrom(sock, buffer, sizeof(buffer),
                           MSG_WAITALL, (struct sockaddr *) &server, &sa_len);
            if (ret == sizeof(struct command_header))
            {
                if (ntohl(((struct command_header *) buffer)->mode) == FLOOD_OUT &&
                    ntohl(((struct command_header *) buffer)->confirm) == CONFIRM)
                {
                    break;
                }
            }
            spinner_count++;
            if (verbose)
            {
                spinner(&spinner_count);
            }
        }
        clock_gettime(CLOCK_REALTIME, &stop);
        printf("\b %s\n", bandwidth(start, stop, len, burst));

        if (len == max_packet)
        {
            break;
        }
    }
}

int main(int argc, char **argv)
{
    int i = 0;
    uint32_t mode = 0;
    uint32_t max_packet = UDP_MTULEN;
    uint32_t burst = PACKET_COUNT;
    uint32_t verbose = 0;
    char port[10];
    struct addrinfo hints = {0};
    struct addrinfo *info = NULL;

    snprintf(port, sizeof(port), "%d", TEST_PORT);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = 0;
    hints.ai_flags = AI_ADDRCONFIG;

    for (i = 0; i < argc; i++)
    {
        if (strncmp(argv[i], "-v", 2) == 0)
        {
            verbose = 1;
        }
        if (strncmp(argv[i], "--server", 8) == 0)
        {
            mode = 1;
        }
        if (strncmp(argv[i], "--client", 8) == 0)
        {
            mode = 2;
            if ((i + 1) < argc)
            {
                if (getaddrinfo(argv[i + 1], port, &hints, &info) != 0)
                {
                    mode = 0;
                }
            }
            else
            {
                mode = 0;
            }
        }
        if (strncmp(argv[i], "--flood", 7) == 0)
        {
            if (mode == 2)
            {
                mode = 3;
            }
        }
        if (strncmp(argv[i], "--over-mtu", 10) == 0)
        {
            max_packet = UDP_MAXLEN;
        }
        if (strncmp(argv[i], "--burst", 7) == 0)
        {
            if ((i + 1) < argc)
            {
                if ((burst = strtoul(argv[i + 1], NULL, 0)) == 0)
                {
                    mode = 0;
                    break;
                }
            }
            else
            {
                mode = 0;
                break;
            }
        }
    }

    if (mode == 0)
    {
        usage();
    }
    else if (mode == 1)
    {
        do_server(verbose);
    }
    else if (mode == 2)
    {
        do_client_pingpong(info, max_packet, burst, verbose);
    }
    else
    {
        do_client_forward_flood(info, max_packet, burst, verbose);
        do_client_reverse_flood(info, max_packet, burst, verbose);
    }
    return 0;
}
