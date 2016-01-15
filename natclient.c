#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <stdbool.h>
#include <sys/time.h>
#include <time.h>
#include "natserver.h"

#define SOURCEPORT 9000
#define PAIR_ID 12345
#define UID 1112

#define ACK_TIMEOUT_US 100000
#define MAX_RETRIES 5

# define timersub(a, b, result)                                               \
  do {                                                                        \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;                             \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;                          \
    if ((result)->tv_usec < 0) {                                              \
      --(result)->tv_sec;                                                     \
      (result)->tv_usec += 1000000;                                           \
    }                                                                         \
  } while (0)

/* Get current wall-clock time and return it in microseconds since the Unix
 * epoch.
 *
 * CLOCK_REALTIME should be used to get the system's best guess at real time.
 * CLOCK_MONOTONIC should be used when jumps in time would cause trouble.
 */
uint64_t clock_gettime_us(clockid_t clock_id)
{
#if (! defined __MACH__ || ! defined __APPLE__)
    struct timespec ts;
    clock_gettime(clock_id, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (ts.tv_nsec + 500) / 1000;
#else
    static struct mach_timebase_info tb_info = { 0, 0 };
    uint64_t now_us = -1;
    if (tb_info.denom == 0)
        mach_timebase_info(&tb_info);
    if (clock_id == CLOCK_MONOTONIC)
    {
        now_us = (mach_absolute_time() * tb_info.numer) / (1000 * tb_info.denom);
    }
    else if (clock_id == CLOCK_REALTIME)
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        now_us = (tv.tv_sec * 1000000ULL) + tv.tv_usec;
    }
    return now_us;
#endif
}


int open_port(int *sock_fd, int port) {
	struct sockaddr_in myaddr;
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 50000;

	/* create a UDP socket */
	if ((*sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		printf("udp: can't create socket\n");
		return -1;
	}

#if 0
	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
        snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "wwan0");
        if (setsockopt(sock_fd, SOL_SOCKET, SO_BINDTODEVICE, (void *)&ifr, sizeof(ifr)) < 0) {
                printf("Unable to bind to interface\n");
                return 0;
        }
#endif

    setsockopt(*sock_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));

	/* Bind the socket to any IP, but we'll check the source later */
    memset((char *)&myaddr, 0, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    myaddr.sin_port = htons(port);

    if (bind(*sock_fd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0)
    {
        printf("udp: bind: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}


int main(int argc, char *argv[]) {
    client_t me;
    client_t partner;
	int sock_fd;
    struct sockaddr_in sock;
	int bytesSent;
	char buf[1500];
	int recvlen;
	socklen_t slen;
	struct timeval t_now;
	struct timeval t_from_peer;
    uint64_t pair_id = PAIR_ID;
    uint64_t last_tx_t;
    int n_retry;
    bool first_client = true;
    int i;

    char server_addr[64];
    char if_name[64];
    int server_port;

    if(argc < 6) {
        printf("netclient -d IFNAME -s IP_OF_SERVER -p SERVER_PORT -A/B\n");
        return -1;
    }

    memset(server_addr, 0, sizeof(server_addr));
    memset(if_name, 0, sizeof(server_addr));

    for(i=1; i<argc; ++i){
        if(!strcmp(argv[i], "-d")) {
            strcpy(if_name, argv[i+1]);
        }
        else if(!strcmp(argv[i], "-s")) {
            strcpy(server_addr, argv[i+1]);
        }
        else if(!strcmp(argv[i], "-p")) {
            server_port = atoi(argv[i+1]);
        }
        else if(!strcmp(argv[i], "-A")) {
            first_client = true;
        }
        else if(!strcmp(argv[i], "-B")) {
            first_client = false;
        }
    }

    printf("Attempting to communicate with server %s:%i as client %s on interface %s\n",
            server_addr, server_port, (first_client ? "A" : "B"), if_name);

    if(open_port(&sock_fd, SOURCEPORT) != 0)
        return -1;
	
    memset((char *)&sock, 0, sizeof(sock));
	sock.sin_family = AF_INET;
	inet_aton(server_addr, &sock.sin_addr);
	sock.sin_port = htons(server_port);
    
    memset(&me, 0, sizeof(client_t));
    memset(&partner, 0, sizeof(client_t));

    /* Get my private IP info */
    struct ifreq ifr;
    strncpy(ifr.ifr_name, if_name, IFNAMSIZ-1);
    if(ioctl(sock_fd, SIOCGIFADDR, &ifr) != 0) {
        printf("Error: could not get IP of device %s\n", if_name);
        return -1;
    }
    me.ip_data.private_ip.sin_addr = ((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr;
    me.ip_data.private_ip.sin_port = htons(SOURCEPORT);
    me.ip_data.private_ip.sin_family = AF_INET;

    /* Set the pair and UID values */
    me.pair_id = pair_id;
    srand(time(NULL));
    me.uid = rand();

    /* Send a REQUEST packet to the server */
    printf("Sending REQUEST packet\n");
	memset(buf,0,sizeof(buf));
    ((packet_t*)buf)->pkt_type = REQUEST;
    memcpy(&((packet_t*)buf)->client_data, &me, sizeof(client_t));

	bytesSent = sendto(sock_fd, buf, sizeof(packet_t), 0, 
            (struct sockaddr *)&sock, sizeof(sock));

    last_tx_t = clock_gettime_us(CLOCK_MONOTONIC); 

	printf("Sent %i bytes to %s:%i\n", bytesSent, 
            inet_ntoa(sock.sin_addr), ntohs(sock.sin_port));

	/* Wait for data back from the server */
	memset(buf, 0, sizeof(buf));
	slen = sizeof(sock);
	printf("Waiting for SERVER_ACK...\n");
    n_retry = 0;
    while(1) {
        recvlen = recvfrom(sock_fd, buf, sizeof(buf), 0, 
                (struct sockaddr*)&sock, &slen);

        if(recvlen < 0) {
            if(clock_gettime_us(CLOCK_MONOTONIC) - last_tx_t > ACK_TIMEOUT_US) {
                if(++n_retry >= MAX_RETRIES) {
                    printf("Couldn't connect to server, giving up\n");
                    return -1;
                }

                /* Send a REQUEST packet to the server */
                printf("Retrying REQUEST packet\n");
                memset(buf,0,sizeof(buf));
                ((packet_t*)buf)->pkt_type = REQUEST;
                memcpy(&((packet_t*)buf)->client_data, &me, sizeof(client_t));

                bytesSent = sendto(sock_fd, buf, sizeof(packet_t), 0, 
                        (struct sockaddr *)&sock, sizeof(sock));

                last_tx_t = clock_gettime_us(CLOCK_MONOTONIC); 

                printf("Sent %i bytes to %s:%i\n", bytesSent, 
                        inet_ntoa(sock.sin_addr), ntohs(sock.sin_port));
            }
            continue;
        }

        printf("Received %i bytes from %s:%i\n", recvlen, 
                inet_ntoa(sock.sin_addr), ntohs(sock.sin_port));

        if(recvlen != sizeof(packet_t)) {
            printf("Got the wrong packet length\n");
            continue;
        }

        /* Make sure this is the right type of data */
        if(((packet_t*)buf)->pkt_type != SERVER_ACK) {
            printf("Got the wrong type of response from server\n");
            continue;
        }

        /* Overwrite our current me */
        memcpy(&me, &((packet_t*)buf)->client_data, sizeof(client_t));
        printf("Apparently public/private IP is: %s:%i / %s:%i\n",
                inet_ntoa(me.ip_data.public_ip.sin_addr),
                ntohs(me.ip_data.public_ip.sin_port),
                inet_ntoa(me.ip_data.private_ip.sin_addr),
                ntohs(me.ip_data.private_ip.sin_port));

        break;

    }

    printf("Waiting for partner data...\n");
    memset(buf, 0, sizeof(buf));

    while(1) {
        recvlen = recvfrom(sock_fd, buf, sizeof(buf), 0, 
                (struct sockaddr*)&sock, &slen);

        /* Wait forever */
        if(recvlen < 0)
            continue;
        
        if(recvlen != sizeof(packet_t)) {
            printf("Got the wrong packet length\n");
            continue;
        }

        /* Make sure this is the right type of data */
        if(((packet_t*)buf)->pkt_type != CLIENT_INFO) {
            printf("Got the wrong type of response from server\n");
            continue;
        }

        /* Set our partner data */
        memcpy(&partner, &((packet_t*)buf)->client_data, sizeof(client_t));
        printf("My partner exists at:  %s:%i / %s:%i \n",
                inet_ntoa(partner.ip_data.public_ip.sin_addr),
                ntohs(partner.ip_data.public_ip.sin_port),
                inet_ntoa(partner.ip_data.private_ip.sin_addr),
                ntohs(partner.ip_data.private_ip.sin_port));

        /* Send the server a CLIENT_ACK */
        memset(buf,0,sizeof(buf));
        ((packet_t*)buf)->pkt_type = CLIENT_ACK;
        memcpy(&((packet_t*)buf)->client_data, &me, sizeof(client_t));
        bytesSent = sendto(sock_fd, buf, sizeof(packet_t), 0, 
                (struct sockaddr *)&sock, sizeof(sock));
        printf("Sent %i bytes to %s:%i\n", bytesSent, 
                inet_ntoa(sock.sin_addr), ntohs(sock.sin_port));
        break;
    }

    /* We might get another CLIENT_INFO if the server didn't get our ACK;
     * we ignore it and let the server timeout. Otherwise we try to send 
     * data to the other client */
        
    if(first_client) {
        /* Send a timestamp to the other client */
        printf("Sending first timestamp to partner\n");
        memset(buf, 0, sizeof(buf));
        gettimeofday(&t_now, NULL);
        ((packet_t*)buf)->pkt_type = DATA;
        memcpy((void*)&((packet_t*)buf)->client_data, &t_now, sizeof(struct timeval));
        bytesSent = sendto(sock_fd, buf, sizeof(struct timeval), 0, 
                (struct sockaddr *)&partner.ip_data.public_ip, sizeof(partner.ip_data.public_ip));
        printf("Sent %i bytes to %s:%i\n", bytesSent, 
                inet_ntoa(partner.ip_data.public_ip.sin_addr), 
                ntohs(partner.ip_data.public_ip.sin_port));
    }

    while(1) {

        /* Wait for data back from the other client */
        memset(buf, 0, sizeof(buf));
        recvlen = recvfrom(sock_fd, buf, sizeof(buf), 0, 
                (struct sockaddr*)&sock, &slen);

        if(recvlen < 0)
            continue;

        /* Make sure to tell the server we got the data */
        if(((packet_t*)buf)->pkt_type != DATA) {
            printf("Unrecognized data\n");
            continue;
        }

        printf("Received %i bytes from %s:%i\n", recvlen, 
                inet_ntoa(sock.sin_addr), ntohs(sock.sin_port));

        memcpy(&t_from_peer, (void*)&((packet_t*)buf)->client_data, sizeof(struct timeval));
        gettimeofday(&t_now, NULL);
        struct timeval t_delta;
        timersub(&t_now, &t_from_peer, &t_delta);
        printf("Packet latency: %fms\n", (float)t_delta.tv_usec/1.e3);

        if(first_client)
            sleep(1);
        
        /* Send a timestamp to the other client */
        printf("Sending new timestamp to partner\n");
        memset(buf, 0, sizeof(buf));
        gettimeofday(&t_now, NULL);
        ((packet_t*)buf)->pkt_type = DATA;
        memcpy((void*)&((packet_t*)buf)->client_data, &t_now, sizeof(struct timeval));
        bytesSent = sendto(sock_fd, buf, sizeof(struct timeval), 0, 
                (struct sockaddr *)&partner.ip_data.public_ip, sizeof(partner.ip_data.public_ip));
        printf("Sent %i bytes to %s:%i\n", bytesSent, 
                inet_ntoa(partner.ip_data.public_ip.sin_addr), 
                ntohs(partner.ip_data.public_ip.sin_port));

        //and continue ping-ponging.
    }

	close(sock_fd);

	return 0;
}
