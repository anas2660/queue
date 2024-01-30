#ifndef QUEUE_H
#define QUEUE_H

#include <stdint.h>
#include <stdlib.h>

#include <stdatomic.h>
#include <string.h>

/*

                      Queue Description

  Queues work in index space.
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

  Usage:
    Producer side:
        Use `prepare_push` functions to get an indices to work on.
        Use `commit_push` functions to commit to the queue

    Consumer side:
        Use `prepare_consume` functions to get an indices to work on.
        Use `commit_consume` functions to actually consume from the queue (frees indices)

*/

union MaybeAtomicU32 {
    atomic_uint atomic_value;
    unsigned int value;
};

struct QueueMultiSide {
    MaybeAtomicU32 pending, committed;
};

union QueueSingleSide {
    MaybeAtomicU32 pending, committed;
};

typedef struct SPSCQueue {
    union QueueSingleSide head;
    union QueueSingleSide tail;
    unsigned int queue_size;    /* Should always be less than INT32_MAX */
    atomic_uint waiters;
} SPSCQueue;

typedef struct MPMCQueue {
    struct QueueMultiSide head;
    struct QueueMultiSide tail;
    unsigned int queue_size;    /* Should always be less than INT32_MAX */
    atomic_uint head_waiters;
    atomic_uint tail_waiters;
} MPMCQueue;

#define QUEUE_ATOMIC_WAIT(x)
#define QUEUE_ATOMIC_WAKE(x, n)
#define QUEUE_ATOMIC_WAKE_ONE(x)
#define QUEUE_ATOMIC_WAKE_ALL(x)

#define QUEUE_WAIT_FOR_TAIL(queue) QUEUE_ATOMIC_WAIT(&(queue)->tail.committed.atomic_value)
#define QUEUE_WAIT_FOR_HEAD(queue) QUEUE_ATOMIC_WAIT(&(queue)->head.committed.atomic_value)

#define QUEUE_WAKE_TAIL_WAITER(queue) QUEUE_ATOMIC_WAKE_ONE(&queue->tail.committed.atomic_value)
#define QUEUE_WAKE_HEAD_WAITER(queue) QUEUE_ATOMIC_WAKE_ONE(&queue->head.committed.atomic_value)

#define queue_init(queue, size) { memset((queue), 0, sizeof((queue)[0])); (queue)->queue_size = size; }
#define queue_get_used_space(queue) ((queue)->head.pending.atomic_value - (queue)->tail.committed.atomic_value)
#define queue_get_free_space_explicit(queue, head_pending, tail_committed) ((queue)->queue_size - (head_pending - tail_committed))
#define queue_get_free_space(queue) ((queue)->queue_size - queue_get_used_space(queue))
#define queue_get_committed_space(queue) ((queue)->head.committed.atomic_value - (queue)->tail.pending.atomic_value)

/* Returns an index to push or -1 on failure (Queue is full) */
static inline int spsc_try_prepare_push(SPSCQueue* queue) {
    if (queue_get_free_space(queue) == 0)
        return -1;

    /* As this is an SP queue the usage cannot increase from this point
     * onwards, so we can add safely. */
    return queue->head.pending.value++;
}

/* Returns an index to push or -1 on failure (Queue is full) */
static inline int spsc_prepare_push(SPSCQueue* queue) {
    while (queue_get_free_space(queue) == 0)
        QUEUE_WAIT_FOR_TAIL(queue);

    /* As this is an SP queue the usage cannot increase from this point
     * onwards, so we can add safely. */
    return queue->head.pending.value++;
}

static inline void spsc_commit_push(SPSCQueue* queue, unsigned int prepared_index) {
    queue->head.committed.atomic_value++;

    /* As this is an SP queue any waiters would have to be the consumer in this case */
    if (queue->waiters)
        QUEUE_WAKE_HEAD_WAITER(queue);
}

/* Returns an index to consume or -1 on failure (Queue is empty) */
static inline int spsc_try_prepare_consume(SPSCQueue* queue) {
    if (queue_get_committed_space(queue) == 0)
        return -1;

    /* As this is an SC queue the committed cannot decrease from this point
     * onwards, so we can add safely. */
    return queue->tail.pending.value++;
}

static inline int spsc_prepare_consume(SPSCQueue* queue) {
    while (queue_get_committed_space(queue) == 0)
        QUEUE_WAIT_FOR_HEAD(queue);

    /* As this is an SC queue the committed cannot decrease from this point
     * onwards, so we can consume safely. */
    return queue->tail.pending.value++;
}

static inline void spsc_commit_consume(SPSCQueue* queue, unsigned int prepared_index) {
    queue->tail.committed.atomic_value++;

    /* As this is an SC queue any waiters would have to be the producer in this case */
    if (queue->waiters)
        QUEUE_WAKE_TAIL_WAITER(queue);
}

/* Returns an index to push or -1 on failure (Queue is full) */
static inline int mpmc_try_prepare_push(MPMCQueue* queue) {
    unsigned int tail = atomic_load(&queue->tail.committed.atomic_value);
    unsigned int head = atomic_load(&queue->head.pending.atomic_value);

    while (1) {
        if (queue_get_free_space_explicit(queue, head, tail) == 0)
            return -1;

        /* As this is an MP queue another thread might have taken our index. */
        /* This will update `head` on failure */
        if (atomic_compare_exchange_strong(&queue->head.pending.atomic_value, &head, head+1))
            return head;
    }
}

/* Returns an index to push */
static inline int mpmc_prepare_push(MPMCQueue* queue) {
    unsigned int tail = atomic_load(&queue->tail.committed.atomic_value);
    unsigned int head = atomic_load(&queue->head.pending.atomic_value);

    while (1) {
        if (queue_get_free_space_explicit(queue, head, tail) == 0) {
            QUEUE_WAIT_FOR_TAIL(queue);
            tail = atomic_load(&queue->tail.committed.atomic_value);
            head = atomic_load(&queue->head.pending.atomic_value);
        }

        /* As this is an MP queue another thread might have taken our index. */
        /* This will update `head` on failure */
        if (atomic_compare_exchange_strong(&queue->head.pending.atomic_value, &head, head+1))
            return head;
    }
}

/* Returns 0 on success and -1 if it is too early to push */
static inline int mpmc_try_commit_push(MPMCQueue* queue) {

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
