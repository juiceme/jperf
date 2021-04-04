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
#define MAX_PACKET 5000

char buffer[MAX_PACKET] = {0};

void usage(void)
{
    printf("\nUsage:\n");
    printf("  jperf --server\n");
    printf("  jperf --client <ip_address>\n\n");
}

void do_server(void)
{
    int sock=0;
    int ret =0;
    unsigned int sa_len = 0;
    struct sockaddr_in addr = {0};
    struct sockaddr_in client = {0};

    printf("\nServer listening on port %d \n", TEST_PORT);
    if((sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
	printf("Cannot create socket, err: %s\n", strerror(errno));
	return;
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(TEST_PORT);
    if(bind(sock, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
	printf("Cannot bind socket, err: %s\n", strerror(errno));
	return;
    }
    while(1) {
	ret=recvfrom(sock, buffer, sizeof(buffer), MSG_WAITALL, (struct sockaddr *) &client, &sa_len);
	sendto(sock, buffer, ret, MSG_CONFIRM, (const struct sockaddr *) &client, sa_len);
    }
}

int interval(struct timespec start, struct timespec stop)
{
    int s = stop.tv_sec - start.tv_sec;
    int ns = stop.tv_nsec - start.tv_nsec;
    if(ns < 0) {
	ns = 1000000000 + ns;
	s = s - 1;
    }
    return s*1000 + round(ns/1000000);
}

void do_client(struct addrinfo *info)
{
    int len=0;
    int count=0;
    int sock =0;
    int ret=0;
    unsigned int sa_len=0;
    struct sockaddr_in server = {0};
    struct timespec start, stop;

    printf("\nStarting test against %s on port %d \n",
	   inet_ntoa(((struct sockaddr_in *)info->ai_addr)->sin_addr), TEST_PORT);
    if((sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol)) == -1) {
	printf("Cannot create socket, err: %s\n", strerror(errno));
	return;
    }
    server.sin_family = AF_INET;
    server.sin_port = htons(TEST_PORT);
    server.sin_addr.s_addr = INADDR_ANY;
    for(len=1; len<MAX_PACKET; len=2*len) {
	printf("  Sending packets, len:%d ... ", len);
	fflush(stdout);
	clock_gettime(CLOCK_REALTIME, &start);
	for(count=0; count<10000; count++) {
	    if(sendto(sock, buffer, len, 0, info->ai_addr, info->ai_addrlen) == -1) {
		printf("Cannot send data, err: %s\n", strerror(errno));
		return;
	    }
	    ret=recvfrom(sock, buffer, sizeof(buffer), MSG_WAITALL, (struct sockaddr *) &server, &sa_len);
	    if(ret != len) {
		printf("length mismatch, sent %d -> received %d\n", len, ret);
		return;
	    }
	}
	clock_gettime(CLOCK_REALTIME, &stop);
	printf(" time: %dms\n", interval(start, stop));
    }
}

int main(int argc, char **argv)
{
    int i=0;
    int mode=0;
    char port[10];
    struct addrinfo hints = {0};
    struct addrinfo *info = NULL;

    snprintf(port, sizeof(port), "%d", TEST_PORT);
    hints.ai_family=AF_INET;
    hints.ai_socktype=SOCK_DGRAM;
    hints.ai_protocol=0;
    hints.ai_flags=AI_ADDRCONFIG;

    for(i=0; i<argc; i++) {
	if(strncmp(argv[i], "--server", 8) == 0) {
	    mode = 1;
	    break;
	}
	if(strncmp(argv[i], "--client", 8) == 0) {
	    mode = 2;
	    if((i+1)<argc) {
		if(getaddrinfo(argv[i+1],port,&hints,&info)!=0) {
		    mode=0;
		}
	    } else {
		mode = 0;
	    }
	}
    }

    if(mode==0) {
	usage();
    } else if(mode==1) {
	do_server();
    } else {
	do_client(info);
    }

    return 0;
}
