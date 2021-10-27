/* The code is subject to Purdue University copyright policies.
 * DO NOT SHARE, DISTRIBUTE, OR POST ONLINE
 */

/* An implementation of a fixed-capacity char ring buffer with FIFO semantics.
 * It allows *one* producer thread and *one* consumer thread to operate in parallel
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "ring_buffer.h"


typedef struct ring_buffer {
    char* data;
    uint32_t head;
    uint32_t tail;
    uint32_t capacity;
} ring_buffer_t;


/* creates a char ring buffer of fixed capacity */
ring_buffer_t* create_ring_buffer(uint32_t head)
{
    ring_buffer_t* buffer = (ring_buffer_t *) malloc(sizeof(ring_buffer_t));
    buffer->data = (char *) malloc(sizeof(char) * CAPACITY);
    buffer->head = head;
    buffer->tail = head;
    buffer->capacity = CAPACITY;
    return buffer;
}


/* returns pointer to ring buffer data */
char* get_ring_buffer_data(ring_buffer_t* buffer)
{
    return buffer->data;
}


/* returns head of ring buffer */
uint32_t get_ring_buffer_head(ring_buffer_t* buffer)
{
    return buffer->head;
}


/* returns tail of ring buffer */
uint32_t get_ring_buffer_tail(ring_buffer_t* buffer)
{
    return buffer->tail;
}


/* returns capacity of ring buffer */
uint32_t get_ring_buffer_capcity(ring_buffer_t* buffer)
{
    return buffer->capacity;
}


/* update ring buffer head */
void update_ring_buffer_head(ring_buffer_t* buffer,
                             uint32_t new_head)
{
    buffer->head = new_head;
}


/* update ring buffer tail */
void update_ring_buffer_tail(ring_buffer_t* buffer,
                             uint32_t new_tail)
{
    buffer->tail = new_tail;
}


/* copies bytes amount of data from src_buff to tail of ring buffer.
 * returns bytes added. NOTE: no partial additions, i.e.,
 * either fully succeds (return bytes) or fully fails (returns 0)
 * */
uint32_t ring_buffer_add(ring_buffer_t* buffer,
                         char* src_buff,
                         uint32_t bytes)
{
    if (buffer == NULL) {
        fprintf(stderr, "error ring_buffer_add: buffer is NULL\n");
        exit(1);
    }

    uint32_t empty = empty_space(buffer);
    if (bytes == 0 || bytes > empty) {
        return 0;
    }

    uint32_t start_idx = buffer->tail % buffer->capacity;

    if (src_buff != NULL) {
        if (start_idx + bytes <= buffer->capacity) {
            memcpy((buffer->data + start_idx), src_buff, bytes);
        } else {
            uint32_t diff = bytes - (buffer->capacity - start_idx);
            memcpy((buffer->data + start_idx), src_buff,
                    (buffer->capacity - start_idx));
            memcpy(buffer->data, src_buff + (buffer->capacity - start_idx), diff);
        }

        buffer->tail += bytes;
        assert(buffer->head <= buffer->tail);
        assert(buffer->tail - buffer->head <= buffer->capacity);

        return bytes;

    } else {
        return 0;
    }
}


/* removes a maximum of bytes amount of data from head of ring buffer
 * and puts into dst_buff.
 * if bytes > occupied data, removes occupied amount of data.
 * returns bytes removed
 * */
uint32_t ring_buffer_remove(ring_buffer_t* buffer,
                            char* dst_buff,
                            uint32_t bytes)
{
    if (buffer == NULL) {
        fprintf(stderr, "error ring_buffer_remove: buffer is NULL\n");
        exit(1);
    }

    uint32_t occupied = occupied_space(buffer, NULL);
    if (bytes > occupied) {
        bytes = occupied;
    }

    uint32_t start_idx = buffer->head % buffer->capacity;

    if (dst_buff != NULL && bytes > 0) {
        if (start_idx + bytes <= buffer->capacity) {
            memcpy(dst_buff, (buffer->data + start_idx), bytes);
        } else {
            uint32_t diff = bytes - (buffer->capacity - start_idx);
            memcpy(dst_buff, (buffer->data + start_idx),
                    (buffer->capacity - start_idx));
            memcpy(dst_buff + (buffer->capacity - start_idx), buffer->data, diff);
        }
    }

    buffer->head += bytes;
    assert(buffer->head <= buffer->tail);
    assert(buffer->tail - buffer->head <= buffer->capacity);

    return bytes;
}


/* returns the amount of empty space (in bytes) in ring buffer */
uint32_t empty_space(ring_buffer_t* buffer)
{
    if (buffer == NULL) {
        fprintf(stderr, "error ring_buffer empty_space: buffer is NULL\n");
        exit(1);
    }

    assert(buffer->head <= buffer->tail);
    uint32_t occupied = buffer->tail - buffer->head;
    assert(occupied <= buffer->capacity);

    return (buffer->capacity - occupied);
}


/* returns the amount of occupied space between index *idx and tail of ring buffer.
 * if idx == NULL, uses head of ring buffer as idx instead
 * */
uint32_t occupied_space(ring_buffer_t* buffer,
                        uint32_t* idx)
{
    if (buffer == NULL) {
        fprintf(stderr, "error ring_buffer occupied_space: buffer is NULL\n");
        exit(1);
    }

    uint32_t head = (idx == NULL) ? buffer->head : *idx;

    assert(head <= buffer->tail);
    uint32_t occupied = buffer->tail - head;

    return occupied;
}


/* destroys the ring buffer */
void free_ring_buffer(ring_buffer_t* buffer)
{
    if (buffer != NULL) {
        free(buffer->data);
        free(buffer);
    }
}
