/* The code is subject to Purdue University copyright policies.
 * DO NOT SHARE, DISTRIBUTE, OR POST ONLINE
 */

#ifndef __RING_BUFFER_H__
#define __RING_BUFFER_H__

#define CAPACITY 4096 //bytes

typedef struct ring_buffer ring_buffer_t;

ring_buffer_t* create_ring_buffer(uint32_t head);

char* get_ring_buffer_data(ring_buffer_t* buffer);

uint32_t get_ring_buffer_head(ring_buffer_t* buffer);

uint32_t get_ring_buffer_tail(ring_buffer_t* buffer);

uint32_t get_ring_buffer_capcity(ring_buffer_t* buffer);

void update_ring_buffer_head(ring_buffer_t* buffer,
                             uint32_t new_head);

void update_ring_buffer_tail(ring_buffer_t* buffer,
                             uint32_t new_tail);

uint32_t ring_buffer_add(ring_buffer_t* buffer,
                         char* src_buff,
                         uint32_t bytes);

uint32_t ring_buffer_remove(ring_buffer_t* buffer,
                            char* dst_buff,
                            uint32_t bytes);

uint32_t empty_space(ring_buffer_t* buffer);

uint32_t occupied_space(ring_buffer_t* buffer,
                        uint32_t* idx);

void free_ring_buffer(ring_buffer_t* buffer);

#endif
