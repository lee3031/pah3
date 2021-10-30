/* The code is subject to Purdue University copyright policies.
 * DO NOT SHARE, DISTRIBUTE, OR POST ONLINE
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include "ring_buffer.h"
#include "tinytcp.h"
#include "handle.h"


#define SERVADDR "127.0.0.1"
#define SERVPORT 53005

int is_serv = 0;
int clifd, servfd;
struct sockaddr_in cliaddr, servaddr;

int loss_prob = 0;

uint16_t servport[MAX_CONNS];
uint16_t cliport[MAX_CONNS];

tinytcp_conn_t* tinytcp_conn_list[MAX_CONNS];
int tinytcp_conn_list_size;

pthread_spinlock_t send_to_network_mtx, tinytcp_conn_list_mtx;

uint64_t total_bytes_sent;

int num_of_closed_conn = 0;

FILE *fp1, *fp2;

typedef struct delay_buffer {
    uint16_t src_port;
    char* pkt;
    uint16_t size;
    uint8_t delay;
    uint8_t donot_drop_nxt_pkt;
} delay_buffer_t;

delay_buffer_t delay_buffers[MAX_CONNS];

typedef struct link_delay_buff {
    clock_t time;
    char pkt[MAX_TINYTCP_PKT_SIZE];
    uint16_t pkt_size;
} link_delay_buffer_t;

link_delay_buffer_t link_delay_buffer[LINK_DELAY_BUF_SIZE];
uint64_t head = 0, tail = 0;


static void init_delay_buffers()
{
    for (int i = 0; i < MAX_CONNS; ++i) {
        delay_buffers[i].src_port = (is_serv) ? servport[i] : cliport[i];
        delay_buffers[i].pkt = NULL;
        delay_buffers[i].size = 0;
        delay_buffers[i].delay = 0;
        delay_buffers[i].donot_drop_nxt_pkt = 0;
    }
}


static delay_buffer_t* get_delay_buffer(uint16_t src_port)
{
    for (int i = 0; i < MAX_CONNS; ++i) {
        if (delay_buffers[i].src_port == src_port) {
            return &delay_buffers[i];
        }
    }

    return NULL;
}


static uint16_t checksum(unsigned char* data,
                         uint16_t len)
{
    uint32_t sum = 0;
    int i = 0;

    while (len > 1) {
        sum += *((uint16_t *)data + i);
        if (sum & 0x80000000) {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
        i += 1;
        len -= 2;
    }

    if (len) {
        sum += (uint16_t) *(unsigned char *)data;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)~sum;
}


int timer_expired(clock_t lasttime_msec)
{
    clock_t time_elapsed_msec = ((clock() - lasttime_msec) * 1000) / CLOCKS_PER_SEC;
    if (time_elapsed_msec >= RTO) {
        return 1;
    } else {
        return 0;
    }
}


char* create_tinytcp_pkt(uint16_t src_port,
                         uint16_t dst_port,
                         uint32_t seq_num,
                         uint32_t ack_num,
                         uint8_t ack_flag,
                         uint8_t syn_flag,
                         uint8_t fin_flag,
                         char* data,
                         uint16_t data_size)
{
    if (data == NULL) {
        data_size = 0;
    }

    char* tinytcp_pkt
        = (char *) malloc((TINYTCP_HDR_SIZE + data_size) * sizeof(char));

    if (data != NULL) {
        memcpy(tinytcp_pkt + TINYTCP_HDR_SIZE, data, data_size);
    }

    tinytcp_hdr_t* tinytcp_hdr = (tinytcp_hdr_t *) tinytcp_pkt;

    tinytcp_hdr->src_port = htons(src_port);

    tinytcp_hdr->dst_port = htons(dst_port);

    tinytcp_hdr->seq_num = htonl(seq_num);

    tinytcp_hdr->ack_num = htonl(ack_num);

    uint16_t data_offset = 0x0050;

    uint8_t flags = 0;
    flags = ack_flag << 4 | syn_flag << 1 | fin_flag;

    tinytcp_hdr->data_offset_and_flags = htons(data_offset << 8 | flags);

    tinytcp_hdr->window_size = 0;

    tinytcp_hdr->checksum = 0;

    tinytcp_hdr->urg_ptr = 0;

    tinytcp_hdr->checksum = htons(checksum((unsigned char *) tinytcp_pkt,
                                       (TINYTCP_HDR_SIZE + data_size)));

    return tinytcp_pkt;
}


void send_to_network(char* tinytcp_pkt,
                     uint16_t tinytcp_pkt_size)
{
    usleep(100);
    
    pthread_spin_lock(&send_to_network_mtx);

    if (strlen(tinytcp_pkt) > MAX_TINYTCP_PKT_SIZE) {
        fprintf(stderr, "error send_to_network: tinytcp packet too big\n");
        exit(1);
    }

    char* pkt = tinytcp_pkt;
    uint16_t pkt_size = tinytcp_pkt_size;

    tinytcp_hdr_t* tinytcp_hdr = (tinytcp_hdr_t *) pkt;

    delay_buffer_t* delay_buff = get_delay_buffer(ntohs(tinytcp_hdr->src_port));

    int donot_drop = 0;

    for (int k = 0; k < 2; ++k) {
        tinytcp_hdr = (tinytcp_hdr_t *) pkt;

        uint16_t src_port = ntohs(tinytcp_hdr->src_port);
        uint16_t dst_port = ntohs(tinytcp_hdr->dst_port);
        uint32_t seq_num = ntohl(tinytcp_hdr->seq_num);
        uint32_t ack_num = ntohl(tinytcp_hdr->ack_num);
        uint16_t data_offset_and_flags = ntohs(tinytcp_hdr->data_offset_and_flags);
        uint8_t tinytcp_hdr_size = ((data_offset_and_flags & 0xF000) >> 12) * 4;
        uint8_t ack = (data_offset_and_flags & 0x0010) >> 4;
        uint8_t syn = (data_offset_and_flags & 0x0002) >> 1;
        uint8_t fin = data_offset_and_flags & 0x0001;
        char* data = pkt + TINYTCP_HDR_SIZE;
        uint16_t data_size = pkt_size - TINYTCP_HDR_SIZE;

        if (donot_drop == 0 && syn == 1 || fin == 1) {
            donot_drop = 1;
            if (delay_buff != NULL) delay_buff->donot_drop_nxt_pkt = 1;
        } else if (donot_drop == 0 && ack == 1 && delay_buff != NULL
          && delay_buff->donot_drop_nxt_pkt == 1) {
            donot_drop = 1;
            delay_buff->donot_drop_nxt_pkt = 0;
        }

        if (!donot_drop) {
            int x = rand() % 100;
            if (x < loss_prob) { //drop or reorder
                if (delay_buff != NULL && delay_buff->pkt == NULL) {
                    int y = rand() % 100;
                    if (y < 30) { //reorder
                        delay_buff->pkt = pkt;
                        delay_buff->size = pkt_size;
                        delay_buff->delay = 0;
                        fprintf(fp1,"[debug] Delayed Packet (%d bytes): ",pkt_size);
                    } else { //drop
                        fprintf(fp1,"[debug] Dropped Packet (%d bytes): ",pkt_size);
                        free(pkt);
                    }
                } else { //drop
                    fprintf(fp1, "[debug] Dropped Packet (%d bytes): ", pkt_size);
                    free(pkt);
                }
                fprintf(fp1, "sport:%u dport:%u snum:%u "
                        "anum:%u ack:%u syn:%u fin:%u ",
                        src_port, dst_port, seq_num, ack_num, ack, syn, fin);
                fprintf(fp1, "data:");
                for (uint16_t i = 0; i < data_size; ++i) {
                    fprintf(fp1, "%02X ", *((uint8_t *) data + i));
                }
                fprintf(fp1, "\n\n");
                fflush(fp1);

                pthread_spin_unlock(&send_to_network_mtx);

                return;
            }
        }

        uint16_t bytes_sent;

        if (is_serv) {
            bytes_sent = sendto(servfd, pkt, pkt_size, MSG_CONFIRM,
                    (const struct sockaddr *) &cliaddr, sizeof(cliaddr));

        } else {
            bytes_sent = sendto(clifd, pkt, pkt_size, MSG_CONFIRM,
                    (const struct sockaddr *) &servaddr, sizeof(servaddr));
        }

        total_bytes_sent += bytes_sent;

        if (k == 0) {
            fprintf(fp1, "[debug] Sent Packet (%d bytes): ", tinytcp_pkt_size);
        } else {
            fprintf(fp1, "[debug] Sent Delayed Packet (%d bytes): ",
                    tinytcp_pkt_size);
        }
        fprintf(fp1, "sport:%u dport:%u snum:%u anum:%u ack:%u syn:%u fin:%u ",
                src_port, dst_port, seq_num, ack_num, ack, syn, fin);
        fprintf(fp1, "data:");
        for (uint16_t i = 0; i < data_size; ++i) {
            fprintf(fp1, "%02X ", *((uint8_t *) data + i));
        }
        fprintf(fp1, "\n\n");
        fflush(fp1);

        free(pkt);

        if (delay_buff != NULL && delay_buff->pkt != NULL) {
            if (delay_buff->delay == 2) {
                pkt = delay_buff->pkt;
                pkt_size = delay_buff->size;
                delay_buff->pkt = NULL;
                donot_drop = 1;
                continue;
            } else if (delay_buff->delay < 2) {
                delay_buff->delay += 1;
                break;
            } else {
                fprintf(stderr, "error send_to_network: packet delayed too long\n");
                exit(1);
            }
        }

        break;
    }

    pthread_spin_unlock(&send_to_network_mtx);

    if (!is_serv) {
        fprintf(stderr, "=");
    }

    return;
}


void* recv_from_network(void* args)
{
    fprintf(stderr, "### started recv thread\n");

    while (1) {
        char tinytcp_pkt[MAX_TINYTCP_PKT_SIZE];
        uint16_t tinytcp_pkt_size;

        if (is_serv) {
            int size = sizeof(cliaddr);
            tinytcp_pkt_size = recvfrom(servfd, tinytcp_pkt, MAX_TINYTCP_PKT_SIZE,
                    MSG_WAITALL, (struct sockaddr *) &cliaddr, &size);

        } else {
            int size = sizeof(servaddr);
            tinytcp_pkt_size = recvfrom(clifd, tinytcp_pkt, MAX_TINYTCP_PKT_SIZE,
                    MSG_WAITALL, (struct sockaddr *) &servaddr, &size);
        }

        if (tail - head < LINK_DELAY_BUF_SIZE) {
            link_delay_buffer[tail%LINK_DELAY_BUF_SIZE].time = clock();
            memcpy(link_delay_buffer[tail%LINK_DELAY_BUF_SIZE].pkt,
                    tinytcp_pkt, MAX_TINYTCP_PKT_SIZE);
            link_delay_buffer[tail%LINK_DELAY_BUF_SIZE].pkt_size = tinytcp_pkt_size;
            tail++;

        } else {
            if (is_serv) {
                fprintf(stderr, "ALERT!!! recv link buffer overflow at server!! "
                        "Packet from client dropped!! "
                        "Possible reason: Your handle_recv_from_network() "
                        "implementation is too slow!\n");
            } else {
                fprintf(stderr, "ALERT!! recv link buffer overflow at client!! "
                        "Packet from server dropped!! "
                        "Possible reason: Your handle_recv_from_network() "
                        "implementation is too slow!\n");
            }
        }

        usleep(100);
    }
}


void* simulate_link_delay(void* args)
{
    fprintf(stderr, "### started link delay thread\n");

    while(1) {
        usleep(100);

        char tinytcp_pkt[MAX_TINYTCP_PKT_SIZE];
        uint16_t tinytcp_pkt_size;

        if (tail != head) {
            clock_t time_elapsed_msec
                = ((clock() - link_delay_buffer[head%LINK_DELAY_BUF_SIZE].time)
                  * 1000) / CLOCKS_PER_SEC;
            if (time_elapsed_msec >= DELAY) {
                memcpy(tinytcp_pkt, link_delay_buffer[head%LINK_DELAY_BUF_SIZE].pkt,
                        MAX_TINYTCP_PKT_SIZE);
                tinytcp_pkt_size
                    = link_delay_buffer[head%LINK_DELAY_BUF_SIZE].pkt_size;
                head++;

            } else {
                continue;
            }

        } else {
            continue;
        }

        tinytcp_hdr_t* tinytcp_hdr = (tinytcp_hdr_t *) tinytcp_pkt;

        uint16_t src_port = ntohs(tinytcp_hdr->src_port);
        uint16_t dst_port = ntohs(tinytcp_hdr->dst_port);
        uint32_t seq_num = ntohl(tinytcp_hdr->seq_num);
        uint32_t ack_num = ntohl(tinytcp_hdr->ack_num);
        uint16_t data_offset_and_flags = ntohs(tinytcp_hdr->data_offset_and_flags);
        uint8_t tinytcp_hdr_size = ((data_offset_and_flags & 0xF000) >> 12) * 4;
        uint8_t ack = (data_offset_and_flags & 0x0010) >> 4;
        uint8_t syn = (data_offset_and_flags & 0x0002) >> 1;
        uint8_t fin = data_offset_and_flags & 0x0001;
        char* data = tinytcp_pkt + TINYTCP_HDR_SIZE;
        uint16_t data_size = tinytcp_pkt_size - TINYTCP_HDR_SIZE;

        if (tinytcp_pkt_size <= MAX_TINYTCP_PKT_SIZE
        && tinytcp_hdr_size == TINYTCP_HDR_SIZE) {
            fprintf(fp2, "[debug] Recvd Packet (%d bytes): ", tinytcp_pkt_size);
            fprintf(fp2, "sport:%u dport:%u snum:%u anum:%u ack:%u syn:%u fin:%u ",
                    src_port, dst_port, seq_num, ack_num, ack, syn, fin);
            fprintf(fp2, "data:");
            for (uint16_t i = 0; i < data_size; ++i) {
                fprintf(fp2, "%02X ", *((uint8_t *) data + i));
            }
            fprintf(fp2, "\n\n");
            fflush(fp2);

            //verify checksum
            uint16_t recvd_checksum = ntohs(tinytcp_hdr->checksum);
            tinytcp_hdr->checksum = 0;
            uint16_t calculated_checksum
                = checksum((unsigned char *) tinytcp_pkt, tinytcp_pkt_size);
            if (recvd_checksum != calculated_checksum) {
                fprintf(stderr, "error recv_from_network: checksum mismatch; "
                        "packet dropped!!\n");
                continue;
            }

            handle_recv_from_network(tinytcp_pkt, tinytcp_pkt_size);
        }
    }
}


tinytcp_conn_t* tinytcp_create_conn()
{
    tinytcp_conn_t* tinytcp_conn
        = (tinytcp_conn_t *) malloc(sizeof(struct tinytcp_conn));

    pthread_spin_init(&tinytcp_conn->mtx, 0);
    tinytcp_conn->eof = 0;

    pthread_spin_lock(&tinytcp_conn_list_mtx);
    tinytcp_conn_list[tinytcp_conn_list_size++] = tinytcp_conn;
    pthread_spin_unlock(&tinytcp_conn_list_mtx);

    return tinytcp_conn;
}


tinytcp_conn_t* tinytcp_get_conn(uint16_t src_port,
                                 uint16_t dst_port)
{
    pthread_spin_lock(&tinytcp_conn_list_mtx);
    for (int i = 0; i < tinytcp_conn_list_size; ++i) {
        if (tinytcp_conn_list[i]->src_port == src_port
        && tinytcp_conn_list[i]->dst_port == dst_port) {
            pthread_spin_unlock(&tinytcp_conn_list_mtx);
            return tinytcp_conn_list[i];
        }
    }
    pthread_spin_unlock(&tinytcp_conn_list_mtx);

    return NULL;
}


void tinytcp_close_conn(tinytcp_conn_t* tinytcp_conn)
{
    pthread_spin_lock(&tinytcp_conn->mtx);
    assert(tinytcp_conn->curr_state == CONN_ESTABLISHED);
    tinytcp_conn->curr_state = READY_TO_TERMINATE;
    pthread_spin_unlock(&tinytcp_conn->mtx);
}


void tinytcp_free_conn(tinytcp_conn_t* tinytcp_conn)
{
    return;
}


void check_file_integrity(char* filename)
{
    char sfilepath[500];
    strcpy(sfilepath, "sendfiles/");
    strcat(sfilepath, filename);
    strcat(sfilepath, "\0");

    char rfilepath[500];
    strcpy(rfilepath, "recvfiles/");
    strcat(rfilepath, filename);
    strcat(rfilepath, "\0");

    if (access(rfilepath, R_OK) != 0) {
        fprintf(stderr, "FAILURE: file %s cannot be read!\n", rfilepath);
        return;
    }

    char command[2000];
    sprintf(command, "cmp --silent %s %s "
            "&& echo 'SUCCESS: Files %s and %s are identical!' "
            "|| echo 'FAILURE: Files %s and %s are different!'",
            sfilepath, rfilepath, sfilepath, rfilepath, sfilepath, rfilepath);
    int ret = system(command);
}


int main(int argc, char* argv[])
{
    srand((unsigned) time(NULL));

    if (argc < 3) {
        fprintf(stderr, "usage: ./tinytcp [client|server] "
                "loss_probability_between_0_and_100 [file1 file2 ....]\n");
        exit(1);
    }

    if (argc > 8) {
        fprintf(stderr, "usage: can only send at most 5 files at a time\n");
        exit(1);
    }

    loss_prob = atoi(argv[2]);

    if (loss_prob < 0 || loss_prob > 100) {
        fprintf(stderr, "usage: loss_probability must be >= 0 and <= 100");
        exit(1);
    }

    if (!strcmp(argv[1], "client")) {
        fp1 = fopen("dumps/cli_send.dump", "w");
        fp2 = fopen("dumps/cli_recv.dump", "w");
    } else if (!strcmp(argv[1], "server")) {
        fp1 = fopen("dumps/serv_send.dump", "w");
        fp2 = fopen("dumps/serv_recv.dump", "w");
    }

    for (int i = 0; i < MAX_CONNS; ++i) {
        servport[i] = 5001 + i;
        cliport[i] = 3001 + i;
    }

    tinytcp_conn_list_size = 0;

    total_bytes_sent = 0;

    pthread_spin_init(&send_to_network_mtx, 0);
    pthread_spin_init(&tinytcp_conn_list_mtx, 0);

    struct hostent *he;
    if ((he = gethostbyname(SERVADDR)) == NULL) {
		perror("gethostbyname");
		exit(1);
	}
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr = *((struct in_addr *)he->h_addr_list[0]);
    servaddr.sin_port = htons(SERVPORT);

    pthread_t tid1, tid2, tid3;
    pthread_create(&tid1, NULL, recv_from_network, NULL);
    usleep(100);
    pthread_create(&tid2, NULL, handle_send_to_network, NULL);
    usleep(100);
    pthread_create(&tid3, NULL, simulate_link_delay, NULL);
    usleep(100);

    if (!strcmp(argv[1], "client")) {
        if (argc > 3) {
            for (int i = 0; i < argc-3; ++i) {
                char filepath[500];
                strcpy(filepath, "sendfiles/");
                strcat(filepath, argv[i+3]);
                strcat(filepath, "\0");
                if (access(filepath, R_OK) != 0) {
                    fprintf(stderr, "error: cannot read file %s\n", filepath);
                    exit(1);
                }
            }
        }

        is_serv = 0;

        if ((clifd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            perror("socket creation failed");
            exit(1);
        }

        init_delay_buffers();

        tinytcp_conn_t* tinytcp_conn[MAX_CONNS];

        int s_fd[MAX_CONNS];

        time_t starttime_in_sec = time(NULL);

        for (int i = 0; i < argc-3; ++i) {
            //create tinytcp connection
            if ((tinytcp_conn[i] = tinytcp_create_conn()) < 0) {
                fprintf(stderr, "error tinytcp_create_conn failed\n");
                exit(1);
            }

            char filename[500];
            strcpy(filename, "\0");
            strcat(filename, argv[i+3]);

            char filepath[500];
            strcpy(filepath, "sendfiles/");
            strcat(filepath, filename);
            strcat(filepath, "\0");

            s_fd[i] = open(filepath, O_RDONLY);
            assert(s_fd[i] >= 0);

            //connect with server
            if (tinytcp_connect(tinytcp_conn[i], cliport[i], servport[i],
                        filename, strlen(filename)) < 0) {
                fprintf(stderr, "error tinytcp_connect failed\n");
                exit(1);
            }
        }

        while (1) {

            usleep(10);

            if (num_of_closed_conn == tinytcp_conn_list_size) {
                break;
            }

            for (int i = 0; i < tinytcp_conn_list_size; ++i) {
                tinytcp_conn_t* tinytcp_conn = tinytcp_conn_list[i];
                assert(tinytcp_conn != NULL);

                if (tinytcp_conn->eof == 0 && tinytcp_conn->send_buffer != NULL) {
                    char dst_buff[CAPACITY];

                    uint32_t space = empty_space(tinytcp_conn->send_buffer);
                    uint32_t rbytes = rand() % ((space < MSS) ? space : MSS);
                    assert(rbytes <= CAPACITY); //rbytes less than dst_buff size

                    if (rbytes == 0) continue;

                    int bytes = read(s_fd[i], dst_buff, rbytes);

                    if (bytes == 0) { //EOF
                        tinytcp_conn->eof = 1;
                        tinytcp_close_conn(tinytcp_conn);
                        close(s_fd[i]);
                    } else {
                        assert(ring_buffer_add(tinytcp_conn->send_buffer,
                                    dst_buff, bytes) == bytes);
                    }
                }
            }
        }

        time_t endtime_in_sec = time(NULL);

        fprintf(stderr, "total time: %ld sec\n", endtime_in_sec - starttime_in_sec);
        fprintf(stderr, "total bytes sent: %ld\n", total_bytes_sent);
        for (int i = 0; i < argc-3; ++i) {
            char filename[500];
            strcpy(filename, "\0");
            strcat(filename, argv[i+3]);
            check_file_integrity(filename);
        }

    } else if (!strcmp(argv[1], "server")) {
        is_serv = 1;

        if ((servfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            perror("socket creation failed");
            exit(1);
        }

        if (bind(servfd, (const struct sockaddr *) &servaddr,
                sizeof(servaddr)) < 0) {
            perror("bind failed");
            exit(1);
        }

        init_delay_buffers();

        while (1) {

            usleep(10);

            for (int i = 0; i < tinytcp_conn_list_size; ++i) {
                tinytcp_conn_t* tinytcp_conn = tinytcp_conn_list[i];
                assert(tinytcp_conn != NULL);

                if (tinytcp_conn->recv_buffer != NULL) {
                    char dst_buff[CAPACITY];
                    uint32_t bytes = ring_buffer_remove(tinytcp_conn->recv_buffer,
                                                       dst_buff, CAPACITY);
                    if (bytes > 0) {
                        int wbytes = write(tinytcp_conn->r_fd, dst_buff, bytes);
                        if (wbytes > 0) {
                            fprintf(stderr, "=");
                        }
                    }
                }
            }
        }

    } else {
        fprintf(stderr, "usage: ./tinytcp [client|server] "
                "loss_probability_between_0_and_100 [file1 file2 ....]\n");
        exit(1);
    }

    pthread_spin_destroy(&send_to_network_mtx);
    pthread_spin_destroy(&tinytcp_conn_list_mtx);

    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);
    pthread_join(tid3, NULL);

    return 0;
}
