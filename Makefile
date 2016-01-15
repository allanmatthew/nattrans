default: client
all: server client
client:
	gcc natclient.c -o natclient -Wall

server:
	gcc natserver.c -o natserver -Wall

clean:
	rm -f natserver natclient
