#ifndef NATSERVER_H
#define NATSERVER_H

/* A struct that holds public and private
   ip information */
typedef struct { 
        struct sockaddr_in public_ip;
        struct sockaddr_in private_ip;
}ip_data_t;

typedef struct {
        uint64_t pair_id;       //Identify the pair
        uint64_t uid;           //Identify the unit
        ip_data_t ip_data;
}client_t;

enum pkt_types {
    REQUEST = 0, //Request from client to server
    SERVER_ACK,  //Server's ack of request
    CLIENT_INFO, //Client info for partner
    CLIENT_ACK,  //Client's ack of client info
    DATA,        //Generic data
};

typedef struct {
	enum pkt_types pkt_type;
	client_t client_data;
}packet_t;

#endif //NATSERVER_H
