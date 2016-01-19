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
#include <gst/gst.h>

int main(int argc, char *argv[]) {
    int sock_fd;
    struct sockaddr_in peerAddr;
    int port = 9000;
    char if_name[] = "eth1";
    int i;

    if(argc < 3){
        printf("./vid_tx -d IFNAME\n");
        return -1;
    }

    for(i=1; i<argc; ++i){
        if(!strcmp(argv[i], "-d")) {
            strcpy(if_name, argv[i+1]);
        }
    }

    memset(&peerAddr, 0, sizeof(peerAddr));

    sock_fd = punch_udp(port, if_name, &peerAddr);

    if(sock_fd < 0)
        return -1;

    printf("Successfully punched hole to %s:%i\n", 
            inet_ntoa(peerAddr.sin_addr),
            ntohs(peerAddr.sin_port));

    GstElement *pipeline;
    GstBus *bus;
    GstMessage *msg;
    gst_init(NULL, NULL);

    printf("Running gstreamer pipeline\n");

    close(sock_fd);

    char gst_pipeline_string[2048];
    sprintf(gst_pipeline_string,"udpsrc port=%i caps=\"application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)JPEG, payload=(int)96, ssrc=(uint)1499883257, clock-base=(uint)3193355557, seqnum-base=(uint)26772\"! rtpjpegdepay ! jpegdec ! ffmpegcolorspace ! autovideosink", 
            port);

    pipeline = gst_parse_launch(gst_pipeline_string, NULL);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    bus = gst_element_get_bus (pipeline);
    msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    printf("Pipeline done\n");
    if (msg != NULL)
        gst_message_unref (msg);
    gst_object_unref (bus);
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);

    return 0;
}

