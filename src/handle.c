/* The code is subject to Purdue University copyright policies.
 * DO NOT SHARE, DISTRIBUTE, OR POST ONLINE
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <arpa/inet.h>

#include "ring_buffer.h"
#include "tinytcp.h"
#include "handle.h"


void* handle_send_to_network(void* args)
{
    fprintf(stderr, "### started send thread\n");

    while (1) {

        int call_send_to_network = 0;

        for (int i = 0; i < tinytcp_conn_list_size; ++i) {
            tinytcp_conn_t* tinytcp_conn = tinytcp_conn_list[i];

            if (tinytcp_conn->curr_state == CONN_ESTABLISHED
            || tinytcp_conn->curr_state == READY_TO_TERMINATE) {

                if (tinytcp_conn->curr_state == READY_TO_TERMINATE) {
                    if (occupied_space(tinytcp_conn->send_buffer, NULL) == 0) {
                        handle_close(tinytcp_conn);
                        num_of_closed_conn++;
                        continue;
                    }
                }

                if (timer_expired(tinytcp_conn->time_last_new_data_acked)) {
                    //TODO do someting

                }

                //TODO do something else

                //TODO make call_send_to_network = 1 everytime you make a call to send_to_network()
                call_send_to_network = 1;

            }
        }

        if (call_send_to_network == 0) {
            usleep(100);
        }
    }
}


void handle_recv_from_network(char* tinytcp_pkt,
                              uint16_t tinytcp_pkt_size)
{
    //parse received tinytcp packet
    tinytcp_hdr_t* tinytcp_hdr = (tinytcp_hdr_t *) tinytcp_pkt;

    uint16_t src_port = ntohs(tinytcp_hdr->src_port);
    uint16_t dst_port = ntohs(tinytcp_hdr->dst_port);
    uint32_t seq_num = ntohl(tinytcp_hdr->seq_num);
    uint32_t ack_num = ntohl(tinytcp_hdr->ack_num);
    uint16_t data_offset_and_flags = ntohs(tinytcp_hdr->data_offset_and_flags);
    uint8_t tinytcp_hdr_size = ((data_offset_and_flags & 0xF000) >> 12) * 4; //bytes
    uint8_t ack = (data_offset_and_flags & 0x0010) >> 4;
    uint8_t syn = (data_offset_and_flags & 0x0002) >> 1;
    uint8_t fin = data_offset_and_flags & 0x0001;
    char* data = tinytcp_pkt + TINYTCP_HDR_SIZE;
    uint16_t data_size = tinytcp_pkt_size - TINYTCP_HDR_SIZE;

    if (syn == 1 && ack == 0) { //SYN recvd
        //create tinytcp connection
        tinytcp_conn_t* tinytcp_conn = tinytcp_create_conn();

        //TODO initialize tinytcp_conn attributes. filename is contained in data
        /**************************************/
        tinytcp_conn->src_port = dst_port;
        tinytcp_conn->dst_port = src_port;
        tinytcp_conn->curr_state = SYN_RECVD;
        tinytcp_conn->seq_num = data_size;  
        tinytcp_conn->ack_num = seq_num + 1;
        //tinytcp_conn->time_last_new_data_acked;
        tinytcp_conn->num_of_dup_acks = 0;
        tinytcp_conn->send_buffer = create_ring_buffer(0);
        tinytcp_conn->recv_buffer = create_ring_buffer(0);
        tinytcp_conn->filename = data;
        /***************************************/

        char filepath[500];
        strcpy(filepath, "recvfiles/");
        strncat(filepath, data, data_size);
        strcat(filepath, "\0");

        tinytcp_conn->r_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        assert(tinytcp_conn->r_fd >= 0);

        fprintf(stderr, "\nSYN recvd "
                "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
                src_port, dst_port, seq_num, ack_num);

        //TODO update tinytcp_conn attributes

        fprintf(stderr, "\nSYN-ACK sending "
                "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
                tinytcp_conn->src_port, tinytcp_conn->dst_port,
                tinytcp_conn->seq_num, tinytcp_conn->ack_num);

        //TODO send SYN-ACK

    } else if (syn == 1 && ack == 1) { //SYN-ACK recvd
        //get tinytcp connection
        tinytcp_conn_t* tinytcp_conn = tinytcp_get_conn(dst_port, src_port);
        assert(tinytcp_conn != NULL);

        if (tinytcp_conn->curr_state == SYN_SENT) {
            //TODO update tinytcp_conn attributes
            /**************************************/
            // tinytcp_conn->src_port = cliport;
            // tinytcp_conn->dst_port = servport;
            // tinytcp_conn->curr_state = SYN_SENT;
            // tinytcp_conn->seq_num = data_size;  //should be a random number
            // tinytcp_conn->ack_num = 0;
            // //tinytcp_conn->time_last_new_data_acked;
            // tinytcp_conn->num_of_dup_acks = 0;
            // tinytcp_conn->send_buffer = create_ring_buffer(0);
            // tinytcp_conn->recv_buffer = create_ring_buffer(0);
            // tinytcp_conn->filename = data;
            /***************************************/
            fprintf(stderr, "\nSYN-ACK recvd "
                    "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
                    src_port, dst_port, seq_num, ack_num);

        }

    } else if (fin == 1 && ack == 1) {
        //get tinytcp connection
        tinytcp_conn_t* tinytcp_conn = tinytcp_get_conn(dst_port, src_port);
        assert(tinytcp_conn != NULL);

        if (tinytcp_conn->curr_state == CONN_ESTABLISHED) { //FIN recvd
            fprintf(stderr, "\nFIN recvd "
                    "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
                    src_port, dst_port, seq_num, ack_num);

            //flush the recv_buffer
            while (occupied_space(tinytcp_conn->recv_buffer, NULL) != 0) {
                usleep(10);
            }

            //TODO update tinytcp_conn attributes

            fprintf(stderr, "\nFIN-ACK sending "
                    "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
                    tinytcp_conn->src_port, tinytcp_conn->dst_port,
                    tinytcp_conn->seq_num, tinytcp_conn->ack_num);

            //TODO send FIN-ACK

        } else if (tinytcp_conn->curr_state == FIN_SENT) { //FIN_ACK recvd
            //TODO update tinytcp_conn attributes

            fprintf(stderr, "\nFIN-ACK recvd "
                    "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
                    src_port, dst_port, seq_num, ack_num);

        }

    } else if (ack == 1) {
        //get tinytcp connection
        tinytcp_conn_t* tinytcp_conn = tinytcp_get_conn(dst_port, src_port);
        assert(tinytcp_conn != NULL);

        if (tinytcp_conn->curr_state == SYN_ACK_SENT) { //conn set up ACK
            //TODO update tinytcp_conn attributes

            fprintf(stderr, "\nACK recvd "
                    "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
                    src_port, dst_port, seq_num, ack_num);

            fprintf(stderr, "\nconnection established...receiving file %s\n\n",
                    tinytcp_conn->filename);

        } else if (tinytcp_conn->curr_state == FIN_ACK_SENT) { //conn terminate ACK
            //TODO update tinytcp_conn attributes

            fprintf(stderr, "\nACK recvd "
                    "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
                    src_port, dst_port, seq_num, ack_num);

            tinytcp_free_conn(tinytcp_conn);

            fprintf(stderr, "\nfile %s received...connection terminated\n\n",
                    tinytcp_conn->filename);

        } else if (tinytcp_conn->curr_state == CONN_ESTABLISHED
            || tinytcp_conn->curr_state == READY_TO_TERMINATE) { //data ACK
            //implement this only if you are sending any data.. not necessary for
            //initial parts of the assignment!

            //TODO handle received data packets

            //TODO reset timer (i.e., set time_last_new_data_acked to clock())
            //every time some *new* data has been ACKed

            //TODO send back an ACK (if needed).
        }
    }
}
/*
         _  _             _                          _ 
  /\/\  (_)| |  ___  ___ | |_  ___   _ __    ___    / |
 /    \ | || | / _ \/ __|| __|/ _ \ | '_ \  / _ \   | |
/ /\/\ \| || ||  __/\__ \| |_| (_) || | | ||  __/   | |
\/    \/|_||_| \___||___/ \__|\___/ |_| |_| \___|   |_|
                                                     
*/                                                   
int tinytcp_connect(tinytcp_conn_t* tinytcp_conn,
                    uint16_t cliport, //use this to initialize src port
                    uint16_t servport, //use this to initialize dst port
                    char* data, //filename is contained in the data
                    uint16_t data_size)
{
    //TODO initialize tinytcp_conn attributes. filename is contained in data
    /**************************************/
    tinytcp_conn->src_port = cliport;
    tinytcp_conn->dst_port = servport;
    tinytcp_conn->curr_state = SYN_SENT;
    tinytcp_conn->seq_num = data_size;  //should be a random number
    tinytcp_conn->ack_num = 0;
    //tinytcp_conn->time_last_new_data_acked;
    tinytcp_conn->num_of_dup_acks = 0;
    tinytcp_conn->send_buffer = create_ring_buffer(0);
    tinytcp_conn->recv_buffer = create_ring_buffer(0);
    tinytcp_conn->filename = data;
    /***************************************/
    fprintf(stderr, "\nSYN sending "
            "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
            tinytcp_conn->src_port, tinytcp_conn->dst_port,
            tinytcp_conn->seq_num, tinytcp_conn->ack_num);

    //send SYN, put data (filename) into the packet
    char* tinytcp_pkt = create_tinytcp_pkt(tinytcp_conn->src_port,
            tinytcp_conn->dst_port, tinytcp_conn->seq_num,
            tinytcp_conn->ack_num, 0, 1, 0, data, data_size);
    send_to_network(tinytcp_pkt, TINYTCP_HDR_SIZE + data_size);

    //wait for SYN-ACK
    while (tinytcp_conn->curr_state != SYN_ACK_RECVD) {
        usleep(10);
    }

    /*********TODO update tinytcp_conn attributes*********/
	int recvd_seq_num = tinytcp_conn->seq_num;
	tinytcp_conn->curr_state = CONN_ESTABLISHED;
	tinytcp_conn->seq_num = tinytcp_conn->ack_num;
	tinytcp_conn->ack_num = recvd_seq_num + 1;
	/*****************************************************/

    fprintf(stderr, "\nACK sending "
            "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
            tinytcp_conn->src_port, tinytcp_conn->dst_port,
            tinytcp_conn->seq_num, tinytcp_conn->ack_num);

    /*****************TODO send ACK**********************/
	tinytcp_pkt = create_tinytcp_pkt(tinytcp_conn->src_port,
            tinytcp_conn->dst_port, tinytcp_conn->seq_num,
            tinytcp_conn->ack_num, 1, 0, 0, data, data_size);
    send_to_network(tinytcp_pkt, TINYTCP_HDR_SIZE + data_size);
	/****************************************************/
	
    fprintf(stderr, "\nconnection established...sending file %s\n\n",
            tinytcp_conn->filename);

    return 0;
}


void handle_close(tinytcp_conn_t* tinytcp_conn)
{
    /***TODO update tinytcp_conn attributes***/
	tinytcp_conn->curr_state = FIN_SENT;
	/*****************************************/

    fprintf(stderr, "\nFIN sending "
            "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
            tinytcp_conn->src_port, tinytcp_conn->dst_port,
            tinytcp_conn->seq_num, tinytcp_conn->ack_num);

    /***************************TODO send FIN*************************/
	char* tinytcp_pkt = create_tinytcp_pkt(tinytcp_conn->src_port,
            tinytcp_conn->dst_port, tinytcp_conn->seq_num,
            tinytcp_conn->ack_num, 1, 0, 1, tinytcp_conn->data, tinytcp_conn->data_size);
    send_to_network(tinytcp_pkt, TINYTCP_HDR_SIZE + tinytcp_conn->data_size);
	/****************************************************************/

    //wait for FIN-ACK
    while (tinytcp_conn->curr_state != FIN_ACK_RECVD) {
        usleep(10);
    }

    /***********TODO update tinytcp_conn attributes********/
	int recvd_seq_num = tinytcp_conn->seq_num;
	tinytcp_conn->curr_state = CONN_TERMINATED;
	tinytcp_conn->seq_num = tinytcp_conn->ack_num;
	tinytcp_conn->ack_num = recvd_seq_num + 1;
	/******************************************************/

    fprintf(stderr, "\nACK sending "
            "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
            tinytcp_conn->src_port, tinytcp_conn->dst_port,
            tinytcp_conn->seq_num, tinytcp_conn->ack_num);

    /***********************TODO send ACK*********************/
	tinytcp_pkt = create_tinytcp_pkt(tinytcp_conn->src_port,
            tinytcp_conn->dst_port, tinytcp_conn->seq_num,
            tinytcp_conn->ack_num, 1, 0, 0, tinytcp_conn->data, tinytcp_conn->data_size);
    send_to_network(tinytcp_pkt, TINYTCP_HDR_SIZE + tinytcp_conn->data_size);
	/*********************************************************/
    tinytcp_free_conn(tinytcp_conn);

    fprintf(stderr, "\nfile %s sent...connection terminated\n\n",
            tinytcp_conn->filename);

    return;
}
