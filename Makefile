default: client
all: server client
client:
	gcc natclient.c -o natclient -Wall

server:
	gcc natserver.c -o natserver -Wall

video:
	gcc punch_udp.c video_tx.c -o vid_tx -Wall `pkg-config --cflags --libs gstreamer-0.10`
	gcc punch_udp.c video_rx.c -o vid_rx -Wall `pkg-config --cflags --libs gstreamer-0.10`

clean:
	rm -f natserver natclient vid_tx
