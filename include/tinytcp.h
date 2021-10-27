/* The code is subject to Purdue University copyright policies.
 * DO NOT SHARE, DISTRIBUTE, OR POST ONLINE
 */

#ifndef __TINYTCP_H__
#define __TINYTCP_H__

#define MSS 256 //bytes
#define TINYTCP_HDR_SIZE 20 //bytes
#define MAX_TINYTCP_PKT_SIZE (TINYTCP_HDR_SIZE + MSS) //bytes

#define LINK_DELAY_BUF_SIZE 10000

#define DELAY 10 //100 msec

#define RTO 100 //1 sec

#define MAX_CONNS 5

typedef struct __attribute__((__packed__)) tinytcp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t data_offset_and_flags;
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urg_ptr;
} tinytcp_hdr_t;

typedef enum {
    SYN_SENT,
    SYN_ACK_RECVD,
    SYN_RECVD,
    SYN_ACK_SENT,
    CONN_ESTABLISHED,
    READY_TO_TERMINATE,
    FIN_SENT,
    FIN_ACK_RECVD,
    FIN_RECVD,
    FIN_ACK_SENT,
    CONN_TERMINATED
} tinytcp_conn_state_t;

typedef struct tinytcp_conn {
    pthread_spinlock_t mtx;
    uint16_t src_port;
    uint16_t dst_port;
    tinytcp_conn_state_t curr_state;
    uint32_t seq_num;
    uint32_t ack_num;
    clock_t time_last_new_data_acked;
    uint8_t num_of_dup_acks;
    ring_buffer_t* send_buffer;
    ring_buffer_t* recv_buffer;
    char filename[500];
    uint8_t eof; //do not touch
    int r_fd; //do not touch
} tinytcp_conn_t;

int timer_expired(clock_t lasttime_msec);

char* create_tinytcp_pkt(uint16_t src_port,
                         uint16_t dst_port,
                         uint32_t seq_num,
                         uint32_t ack_num,
                         uint8_t ack_flag,
                         uint8_t syn_flag,
                         uint8_t fin_flag,
                         char* data,
                         uint16_t data_size);

void send_to_network(char* tinytcp_pkt,
                     uint16_t tinytcp_pkt_size);

tinytcp_conn_t* tinytcp_create_conn();

tinytcp_conn_t* tinytcp_get_conn(uint16_t src_port,
                                 uint16_t dst_port);

void tinytcp_close_conn(tinytcp_conn_t* tinytcp_conn);

void tinytcp_free_conn(tinytcp_conn_t* tinytcp_conn);

extern tinytcp_conn_t* tinytcp_conn_list[MAX_CONNS];

extern int tinytcp_conn_list_size;

extern int num_of_closed_conn;

#endif
