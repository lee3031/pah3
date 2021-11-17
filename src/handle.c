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

typedef struct save_buffer {
    ring_buffer_t* data;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
}save_buffer_t;

static save_buffer_t* save_buffer_list;

typedef struct seq_num_holder {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
}seq_num_holder_t;

static seq_num_holder_t seq_array[5] = {0};

save_buffer_t get_save_buffer(uint16_t src_port, uint16_t dst_port)
{
    for (int i = 0; i < tinytcp_conn_list_size; ++i) {
        fprintf(stderr, "getsave %d %d\n", i, save_buffer_list[i].src_port);
        if (save_buffer_list[i].src_port == src_port
        && save_buffer_list[i].dst_port == dst_port) {
            return save_buffer_list[i];
        }
    }
    return (save_buffer_t) { .data = NULL, .src_port = -1, .dst_port = -1, .seq_num = -1 };
}

seq_num_holder_t get_seq_holder(uint16_t src_port, uint16_t dst_port)
{
    for (int i = 0; i < tinytcp_conn_list_size; ++i) {
        if (seq_array[i].src_port == src_port
        && seq_array[i].dst_port == dst_port) {
            return seq_array[i];
        }
    }
    return (seq_num_holder_t) { .src_port = -1, .dst_port = -1, .seq_num = -1 };
}

uint32_t retransmit(tinytcp_conn_t* tinytcp_conn) {
    uint32_t total_bytes = 0;
    char dst_buff[MSS];
    uint32_t bytes = MSS;

    pthread_spin_lock(&tinytcp_conn->mtx);
    uint32_t occupied = occupied_space(tinytcp_conn->recv_buffer, NULL);
    tinytcp_conn->seq_num -= occupied;
    while(total_bytes < occupied) {
        if(total_bytes + bytes > occupied) {
            bytes = occupied - total_bytes;
        }

        ring_buffer_remove(tinytcp_conn->recv_buffer, dst_buff, bytes);

        char* tinytcp_pkt = create_tinytcp_pkt(tinytcp_conn->src_port,
        tinytcp_conn->dst_port, tinytcp_conn->seq_num,
        tinytcp_conn->ack_num, 1, 0, 0, dst_buff, bytes);
        send_to_network(tinytcp_pkt, TINYTCP_HDR_SIZE + bytes);
        fprintf(stderr, "retransmitting: %d\n", tinytcp_conn->seq_num);

        ring_buffer_add(tinytcp_conn->recv_buffer, dst_buff, bytes);
        tinytcp_conn->seq_num += bytes;
        total_bytes += bytes;
    }
    pthread_spin_unlock(&tinytcp_conn->mtx); 


}

void* handle_send_to_network(void* args)
{
    fprintf(stderr, "### started send thread\n");
    
    while (1) {

        int call_send_to_network = 0;

        for (int i = 0; i < tinytcp_conn_list_size; ++i) {
            tinytcp_conn_t* tinytcp_conn = tinytcp_conn_list[i];

            if (tinytcp_conn->curr_state == CONN_ESTABLISHED || tinytcp_conn->curr_state == READY_TO_TERMINATE) {
                if (tinytcp_conn->curr_state == READY_TO_TERMINATE) {
                    if (occupied_space(tinytcp_conn->send_buffer, NULL) == 0 && occupied_space(tinytcp_conn->recv_buffer, NULL) == 0 ) {
                        handle_close(tinytcp_conn);
                        num_of_closed_conn++;
                        continue;
                    }
                }
            
                if (timer_expired(tinytcp_conn->time_last_new_data_acked)) {
                    //TODO do someting
                             
                    // retransmit the packet
                    
                    if (tinytcp_conn->send_buffer != NULL){
                        fprintf(stderr, "\nTIMER EXPIRED\n");
                        retransmit(tinytcp_conn);
                    }
                    tinytcp_conn->time_last_new_data_acked = clock();
                }

                //TODO do something else
                // This is where the multiplexing works
                //TODO make call_send_to_network = 1 everytime you make a call to send_to_network()
                
                if (tinytcp_conn->send_buffer != NULL) {
                    pthread_spin_lock(&tinytcp_conn->mtx);
                    uint32_t send_buffer_occupied = occupied_space(tinytcp_conn->send_buffer, NULL);
                    uint32_t save_buffer_occupied = occupied_space(tinytcp_conn->recv_buffer, NULL);
                    if(send_buffer_occupied != 0 && ((save_buffer_occupied) < CAPACITY)) {
                        uint32_t numOfBytesToRead = send_buffer_occupied > MSS ? MSS : send_buffer_occupied;
                        numOfBytesToRead = (CAPACITY - save_buffer_occupied) > numOfBytesToRead ? 
                                            numOfBytesToRead : (CAPACITY - save_buffer_occupied);

                        if (numOfBytesToRead != 0){
                            char dst_buff[numOfBytesToRead];
                            
                            uint32_t bytes = ring_buffer_remove(tinytcp_conn->send_buffer, dst_buff, numOfBytesToRead);
                            uint32_t bots = ring_buffer_add(tinytcp_conn->recv_buffer, dst_buff, numOfBytesToRead);    

                            fprintf(stderr, "sending: %d, datasize: %d\n", tinytcp_conn->seq_num, numOfBytesToRead);

                            char* tinytcp_pkt = create_tinytcp_pkt(tinytcp_conn->src_port,
                            tinytcp_conn->dst_port, tinytcp_conn->seq_num,
                            tinytcp_conn->ack_num, 1, 0, 0, dst_buff, numOfBytesToRead);
                            send_to_network(tinytcp_pkt, TINYTCP_HDR_SIZE + numOfBytesToRead);
                            tinytcp_conn->seq_num += numOfBytesToRead;
                            call_send_to_network = 1;
                        }
                    }
                    pthread_spin_unlock(&tinytcp_conn->mtx);
                }
                
            }
        }

        if (call_send_to_network == 0) {
            usleep(100);
        }
    }
}


void handle_recv_from_network(char* tinytcp_pkt, uint16_t tinytcp_pkt_size)
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
        tinytcp_conn->seq_num = rand();
        tinytcp_conn->ack_num = seq_num + 1;
        tinytcp_conn->time_last_new_data_acked = clock();
        tinytcp_conn->num_of_dup_acks = 0;
        tinytcp_conn->send_buffer = NULL;
        tinytcp_conn->recv_buffer = create_ring_buffer(0);
        memcpy(tinytcp_conn->filename, data, data_size);
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
	    tinytcp_conn->curr_state = SYN_ACK_SENT;

        fprintf(stderr, "\nSYN-ACK sending "
                "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
                tinytcp_conn->src_port, tinytcp_conn->dst_port,
                tinytcp_conn->seq_num, tinytcp_conn->ack_num);

        //TODO send SYN-ACK
        char* tinytcp_pkt = create_tinytcp_pkt(tinytcp_conn->src_port,
            tinytcp_conn->dst_port, tinytcp_conn->seq_num,
            tinytcp_conn->ack_num, 1, 1, 0, data, data_size);
        send_to_network(tinytcp_pkt, TINYTCP_HDR_SIZE + data_size);

    } else if (syn == 1 && ack == 1) { //SYN-ACK recvd
        //get tinytcp connection
        tinytcp_conn_t* tinytcp_conn = tinytcp_get_conn(dst_port, src_port);
        assert(tinytcp_conn != NULL);

        if (tinytcp_conn->curr_state == SYN_SENT) {
            //TODO update tinytcp_conn attributes
            /**************************************/
	        tinytcp_conn->curr_state = SYN_ACK_RECVD;
            tinytcp_conn->ack_num = seq_num + 1;
            tinytcp_conn->seq_num += 1;
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
            tinytcp_conn->curr_state = FIN_ACK_SENT;
            tinytcp_conn->ack_num += 1;

            fprintf(stderr, "\nFIN-ACK sending "
                    "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
                    tinytcp_conn->src_port, tinytcp_conn->dst_port,
                    tinytcp_conn->seq_num, tinytcp_conn->ack_num);

            //TODO send FIN-ACK
            char* tinytcp_pkt = create_tinytcp_pkt(tinytcp_conn->src_port,
            tinytcp_conn->dst_port, tinytcp_conn->seq_num,
            tinytcp_conn->ack_num, 1, 0, 1, data, data_size);
            send_to_network(tinytcp_pkt, TINYTCP_HDR_SIZE + data_size);

        } else if (tinytcp_conn->curr_state == FIN_SENT) { //FIN_ACK recvd
            //TODO update tinytcp_conn attributes
            tinytcp_conn->curr_state = FIN_ACK_RECVD;
            
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
            
            tinytcp_conn->curr_state = CONN_ESTABLISHED;

            fprintf(stderr, "\nACK recvd "
                    "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
                    src_port, dst_port, seq_num, ack_num);

            fprintf(stderr, "\nconnection established...receiving file %s\n\n",
                    tinytcp_conn->filename);

        } else if (tinytcp_conn->curr_state == FIN_ACK_SENT) { //conn terminate ACK
            //TODO update tinytcp_conn attributes
            tinytcp_conn->curr_state = CONN_TERMINATED;
            tinytcp_conn->seq_num += 1;
            tinytcp_conn->ack_num += 1;

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
        
            if(tinytcp_conn->send_buffer != NULL) {
                tinytcp_conn->time_last_new_data_acked = clock();

                seq_num_holder_t my_seq_num_holder = get_seq_holder(tinytcp_conn->src_port, tinytcp_conn->dst_port);
                // fprintf(stderr, "Sending seqNum: %d, ackNum: %d\n", my_seq_num_holder.seq_num, ack_num);
                fprintf(stderr, "recieving: %d, previous seq: %d datasize: %d\n", ack_num, my_seq_num_holder.seq_num, data_size);
                if (my_seq_num_holder.seq_num != ack_num) {
                    uint32_t occupied = occupied_space(tinytcp_conn->recv_buffer, NULL);
                    uint32_t old_head = get_ring_buffer_head(tinytcp_conn->recv_buffer);
                    update_ring_buffer_head(tinytcp_conn->recv_buffer, old_head + data_size);

                    for (int i = 0; i < tinytcp_conn_list_size; ++i) {
                        if (seq_array[i].src_port == tinytcp_conn->src_port
                        && seq_array[i].dst_port == tinytcp_conn->dst_port) {
                            //fprintf(stderr, "Changing seqnum\n");
                            seq_array[i] = (seq_num_holder_t) { .src_port = tinytcp_conn->src_port, .dst_port = tinytcp_conn->dst_port, .seq_num = ack_num };
                        }
                    }
                    tinytcp_conn->num_of_dup_acks = 0;
                    // fprintf(stderr, "seqnum of savebuffer: %d\n", my_save_buffer.seq_num);
                    // occupied = occupied_space(tinytcp_conn->recv_buffer, NULL);
                    // fprintf(stderr, "used space in savebuffer: %d\n", occupied);
                    // tinytcp_conn->time_last_new_data_acked = clock();
                } else {
                    tinytcp_conn->num_of_dup_acks += 1;
                    if (tinytcp_conn->num_of_dup_acks >= 3){
                        fprintf(stderr, "\n3 DUP ACKS RECVD\n");

                        retransmit(tinytcp_conn);
                        tinytcp_conn->num_of_dup_acks = 0;
                    }
                }

            } else {
                // fprintf(stderr, "Sending seqNum: %d, ackNum: %d\n", seq_num, tinytcp_conn->ack_num);
                fprintf(stderr, "acknologing: %d, %d, datasize: %d\n", seq_num, tinytcp_conn->ack_num, data_size);

                if(seq_num >= tinytcp_conn->ack_num && !timer_expired(tinytcp_conn->time_last_new_data_acked)) {

                    pthread_spin_lock(&tinytcp_conn->mtx);

                    if(seq_num == tinytcp_conn->ack_num) {
                        tinytcp_conn->ack_num += data_size;
                        ring_buffer_add(tinytcp_conn->recv_buffer, data, data_size);
                        tinytcp_conn->num_of_dup_acks = 0;
                        
                    } else {
                        tinytcp_conn->num_of_dup_acks += 1;
                    }
                    pthread_spin_unlock(&tinytcp_conn->mtx);
                    
                    char* dst_buff = NULL;
                    char* tinytcp_pkt = create_tinytcp_pkt(tinytcp_conn->src_port,
                    tinytcp_conn->dst_port, tinytcp_conn->seq_num,
                    tinytcp_conn->ack_num, 1, 0, 0, data, data_size);

                    send_to_network(tinytcp_pkt, TINYTCP_HDR_SIZE + data_size);
                }
                tinytcp_conn->time_last_new_data_acked = clock();
            }

            //every time some *new* data has been ACKed

            //TODO send back an ACK (if needed).
            
        }
    }
}
                                                                                               
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
    tinytcp_conn->seq_num = rand();  //should be a random number
    tinytcp_conn->ack_num = 0;
    tinytcp_conn->time_last_new_data_acked = clock();
    tinytcp_conn->num_of_dup_acks = 0;
    tinytcp_conn->send_buffer = create_ring_buffer(0);
    tinytcp_conn->recv_buffer = create_ring_buffer(0);
    memcpy(tinytcp_conn->filename, data, data_size);
    /***************************************/
    // for (int i = 0; i < tinytcp_conn_list_size; i++) {
    //     if(save_buffer_list[i].data == NULL) {
    //         save_buffer_list[i].data = create_ring_buffer(0);
    //         save_buffer_list[i].src_port = cliport;
    //         save_buffer_list[i].dst_port = servport;
    //         save_buffer_list[i].seq_num = tinytcp_conn->seq_num;
    //         break;
    //     }
    // }
    for (int i = 0; i < tinytcp_conn_list_size; i++) {
        if(seq_array[i].src_port == 0) {
            seq_array[i].src_port = cliport;
            seq_array[i].dst_port = servport;
            seq_array[i].seq_num = tinytcp_conn->seq_num + 1;
            break;
        }
    }
    

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
	tinytcp_conn->curr_state = CONN_ESTABLISHED;
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
    // uint32_t save_buffer_occupied = occupied_space(tinytcp_conn->recv_buffer, NULL);
    // fprintf(stderr, "occupied space in save buffer: %d\n", save_buffer_occupied);

    // while(save_buffer_occupied != 0) {
    //     save_buffer_occupied = occupied_space(tinytcp_conn->recv_buffer, NULL);
    //     fprintf(stderr, "occupied space in save buffer: %d\n", save_buffer_occupied);

    //     usleep(1000);
    // }
    
    /***TODO update tinytcp_conn attributes***/
	tinytcp_conn->curr_state = FIN_SENT;
    tinytcp_conn->ack_num += 1;
	/*****************************************/

    fprintf(stderr, "\nFIN sending "
            "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
            tinytcp_conn->src_port, tinytcp_conn->dst_port,
            tinytcp_conn->seq_num, tinytcp_conn->ack_num);

    /***************************TODO send FIN*************************/
	char* tinytcp_pkt = create_tinytcp_pkt(tinytcp_conn->src_port,
            tinytcp_conn->dst_port, tinytcp_conn->seq_num,
            tinytcp_conn->ack_num, 1, 0, 1, tinytcp_conn->filename, sizeof(tinytcp_conn->filename));
    send_to_network(tinytcp_pkt, TINYTCP_HDR_SIZE + sizeof(tinytcp_conn->filename));
	/****************************************************************/

    //wait for FIN-ACK
    while (tinytcp_conn->curr_state != FIN_ACK_RECVD) {
        usleep(10);
    }

    /***********TODO update tinytcp_conn attributes********/
	tinytcp_conn->curr_state = CONN_TERMINATED;
	/******************************************************/

    fprintf(stderr, "\nACK sending "
            "(src_port:%u dst_port:%u seq_num:%u ack_num:%u)\n",
            tinytcp_conn->src_port, tinytcp_conn->dst_port,
            tinytcp_conn->seq_num, tinytcp_conn->ack_num);

    /***********************TODO send ACK*********************/
	tinytcp_pkt = create_tinytcp_pkt(tinytcp_conn->src_port,
            tinytcp_conn->dst_port, tinytcp_conn->seq_num,
            tinytcp_conn->ack_num, 1, 0, 0, tinytcp_conn->filename, sizeof(tinytcp_conn->filename));
    send_to_network(tinytcp_pkt, TINYTCP_HDR_SIZE + sizeof(tinytcp_conn->filename));
	/*********************************************************/
    tinytcp_free_conn(tinytcp_conn);

    fprintf(stderr, "\nfile %s sent...connection terminated\n\n",
            tinytcp_conn->filename);

    return;
}
