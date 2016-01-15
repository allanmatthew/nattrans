#ifndef NATSERVER_H
#define NATSERVER_H

/* A struct that holds public and private
   ip information */
typedef struct __attribute__((__packed__)){ 
    uint64_t public_addr;
    uint64_t private_addr;
    uint16_t public_port;
    uint16_t private_port;
}ip_data_t;

typedef struct __attribute__((__packed__)){
        uint64_t pair_id;       //Identify the pair
        uint64_t uid;           //Identify the unit
        ip_data_t ip_data;
}client_t;

enum pkt_types {
    REQUEST = 0,  //Request from client to server
    SERVER_ACK,   //Server's ack of request
    CLIENT_INFO,  //Client info for partner
    CLIENT_ACK,   //Client's ack of client info
    PUBLIC_DATA,  //Data sent to public IP tuple
    PRIVATE_DATA, //Data sent to private IP tuple
};

typedef struct __attribute__((__packed__)){
	uint8_t pkt_type;
	client_t client_data;
}packet_t;

#endif //NATSERVER_H
