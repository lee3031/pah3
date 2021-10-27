/* The code is subject to Purdue University copyright policies.
 * DO NOT SHARE, DISTRIBUTE, OR POST ONLINE
 */

#ifndef __HANDLE_H__
#define __HANDLE_H__

void* handle_send_to_network(void* args);

void handle_recv_from_network(char* tinytcp_pkt,
                              uint16_t tinytcp_pkt_size);

int tinytcp_connect(tinytcp_conn_t* tinytcp_conn,
                    uint16_t cliport,
                    uint16_t servport,
                    char* data,
                    uint16_t data_size);

void handle_close(tinytcp_conn_t* tinytcp_conn);

#endif
