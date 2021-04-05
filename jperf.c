#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <math.h>
#include <time.h>

#define TEST_PORT  5555
#define MAX_PACKET 70000

char buffer[MAX_PACKET] = {0};

void usage(void)
{
    printf("\nUsage:\n");
    printf("  jperf --server [-v]\n");
    printf("  jperf --client <ip_address> [-v]\n\n");
}

void spinner(int *count)
{
    if(*count==400)
    {
	printf("\b-");
	fflush(stdout);
    }
    if(*count==800)
    {
	printf("\b\\");
	fflush(stdout);
    }
    if(*count==1200)
    {
	printf("\b|");
	fflush(stdout);
    }
    if(*count==1600)
    {
	*count=0;
	printf("\b/");
	fflush(stdout);
    }
}

void do_server(int verbose)
{
    int sock = 0;
    int ret = 0;
    int spinner_count=0;
    unsigned int sa_len = 0;
    struct sockaddr_in addr = {0};
    struct sockaddr_in client = {0};

    printf("\nServer listening on port %d \n", TEST_PORT);
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
        sendto(sock, buffer, ret, MSG_CONFIRM,
               (const struct sockaddr *) &client, sa_len);
	spinner_count++;
	if(verbose)
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

char *bandwidth(struct timespec start, struct timespec stop, int len)
{
    float duration = (float)(interval(start, stop) / 1000.0);
    int header = (round(len / 1500) + 1) * (22 + 42);
    float raw = (float)((header + len) * 80) / duration / 1000.0;
    float eff = (float)(len * 80) / duration / 1000.0;

    // raw frame contains 22 byte ethernet header + 42 byte udp header + payload
    snprintf(p_buf, sizeof(p_buf), " Raw: %-8.2fMbit/s, Effective: %-8.2fMbit/s", raw, eff);
    return p_buf;
}

void do_client(struct addrinfo *info, int verbose)
{
    int len = 0;
    int count = 0;
    int sock = 0;
    int ret = 0;
    int spinner_count=0;
    unsigned int sa_len = 0;
    struct sockaddr_in server = {0};
    struct timespec start, stop;

    printf("\nStarting test against %s on port %d \n",
           inet_ntoa(((struct sockaddr_in *)info->ai_addr)->sin_addr), TEST_PORT);
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
        if (len > 65507)
        {
            len = 65507;
        }
        printf("  Sending packets, length:%d ... \t", len);
        fflush(stdout);
        clock_gettime(CLOCK_REALTIME, &start);
        for (count = 0; count < 10000; count++)
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
	    if(verbose)
	    {
		spinner(&spinner_count);
	    }
	}
        clock_gettime(CLOCK_REALTIME, &stop);
        printf("\b %s\n", bandwidth(start, stop, len));
    }
}

int main(int argc, char **argv)
{
    int i = 0;
    int mode = 0;
    int verbose=0;
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
	    verbose=1;
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
    }

    if (mode == 0)
    {
        usage();
    }
    else if (mode == 1)
    {
        do_server(verbose);
    }
    else
    {
        do_client(info, verbose);
    }

    return 0;
}
