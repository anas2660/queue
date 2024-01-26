#ifndef QUEUE_H
#define QUEUE_H

#include <stdint.h>
#include <stdlib.h>

#include <stdatomic.h>
#include <string.h>

/*

                      Queue Description

  Queues work in relative index space.
   - You provide the buffer.
   - You mod the index by the buffer size.
     - This allows for arbitrary queue sizes while preserving
       power of two optimizations.
     - Use queue sizes of powers of two for optimal performance.


  In the diagram:
   'w' indicates entries that are being consumed.
   'x' indicates entries in the queue that are yet to be consumed.
   'p' indicates entries that are being pushed.
   ' ' indicates free space.

                  tail side
                   \     /
           committed|   |pending
                    |   |
          | | | | | |w|w|x|x|x|x|x|x|p|p| | | | | |
                                    |   |
                           committed|   |pending
                                    /   \
                                  head side

  The `committed` and `pending` values will only increment
  sequentially for both the head and tail sides.

  The diagram describes an MPMC queue. If the queue is single
  producer the `committed` and `pending` of the head will always
  be equal. If the queue is single consumer the same applies for
  the tail.

*/

struct QueueMultiSide {
    atomic_uint pending, committed;
};

union QueueSingleSide {
    atomic_uint pending, committed;
};

typedef struct SPSCQueue {
    union QueueSingleSide head;
    union QueueSingleSide tail;
    unsigned int queue_size;
} SPSCQueue;

#define queue_init(queue, size) { memset((queue), 0, sizeof((queue)[0])); (queue)->queue_size = size; }
#define queue_get_free_space(queue) ((queue)-> ())

static inline int spsc_try_push() {
    return 0;
}











struct _queue_header {
    atomic_uint position;  // Index of next task to be run.
    atomic_uint committed; // Index of last task added to the queue.
    atomic_uint pending;   // Index of last task currently being added.
    atomic_uint queuers;   // Number of queuers waiting because of full queue.

    uint32_t queue_size;
};


#define Queue(T) T*

static inline void* allocate_queue_with_size(int type_size, uint32_t size) {
    void* q = malloc(type_size*size + sizeof(struct _queue_header));
    memset(q, 0, type_size*size + sizeof(struct _queue_header));
    ((struct _queue_header*)q)[-1].queue_size = size;
    q = &(((struct _queue_header*)q)[1]);
    return q;
}

/* Returns a Queue(T) */
#define make_queue(T, size) allocate_queue_with_size(sizeof(T), size)
#define free_queue(queue) free(&((struct _queue_header*)queue)[-1])

#define get_queue_header(queue) (&((struct _queue_header*)queue)[-1])


static inline int queue_free_space(void* queue) {
    struct _queue_header* h = get_queue_header(queue);

    //u32 committed, u32 position, u32 queue_size
    return h->queue_size - (h->committed - position);
}




#endif /* QUEUE_H */
