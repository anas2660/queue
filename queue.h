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
    union MaybeAtomicU32 pending, committed;
};

union QueueSingleSide {
    union MaybeAtomicU32 pending, committed;
};

typedef struct SPSCQueue {
    union QueueSingleSide head;
    union QueueSingleSide tail;
    unsigned int queue_size;    /* Should always be less than INT32_MAX */
    union {
        atomic_uint tail_waiters;
        atomic_uint head_waiters;
    };
} SPSCQueue;

typedef struct MPMCQueue {
    struct QueueMultiSide head;
    struct QueueMultiSide tail;
    unsigned int queue_size;    /* Should always be less than INT32_MAX */
    atomic_uint head_waiters;
    atomic_uint tail_waiters;
} MPMCQueue;

#define queue_init(queue, size) { memset((queue), 0, sizeof((queue)[0])); (queue)->queue_size = size; }
#define queue_get_used(queue) ((queue)->head.pending.atomic_value - (queue)->tail.committed.atomic_value)
#define queue_get_free_explicit(queue_size, head_pending, tail_committed) ((int)((queue_size) - ((head_pending) - (tail_committed))))
#define queue_get_free(queue) ((int)((queue)->queue_size - queue_get_used(queue)))
#define queue_get_committed(queue) ((queue)->head.committed.atomic_value - (queue)->tail.pending.atomic_value)
#define queue_get_committed_explicit(head_committed, tail_pending) ((head_committed) - (tail_pending))

/* Implementation */


#define QUEUE_ATOMIC_WAIT(atomic_x, v)
#define QUEUE_ATOMIC_WAIT_AND_READ(atomic_x, v) {QUEUE_ATOMIC_WAIT((atomix_x), (v)); (v) = atomic_load(atomic_x);}
#define QUEUE_ATOMIC_WAKE(atomic_x, n)
#define QUEUE_ATOMIC_WAKE_ONE(atomic_x)
#define QUEUE_ATOMIC_WAKE_ALL(atomic_x)

#define QUEUE_WAIT_FOR_TAIL_OLD(queue, v) {                                \
        atomic_fetch_add(&(queue)->tail_waiters, 1);                    \
        QUEUE_ATOMIC_WAIT_AND_READ(&(queue)->tail.committed.atomic_value, (v)); \
        atomic_fetch_sub(&(queue)->tail_waiters, 1);}

#define QUEUE_WAIT_FOR_TAIL(tail_waiters, tail_committed, v) {  \
        atomic_fetch_add(tail_waiters, 1);                      \
        QUEUE_ATOMIC_WAIT_AND_READ(tail_committed, v);          \
        atomic_fetch_sub(tail_waiters, 1);                      \
}

#define QUEUE_WAIT_FOR_HEAD_OLD(queue, v) {                                 \
        atomic_fetch_add(&(queue)->head_waiters, 1);                    \
        QUEUE_ATOMIC_WAIT_AND_READ(&(queue)->head.committed.atomic_value, (v)); \
        atomic_fetch_sub(&(queue)->head_waiters, 1);}


#define QUEUE_WAIT_FOR_HEAD(head_waiters, head_comitted, v) {           \
        atomic_fetch_add(head_waiters, 1);                              \
        QUEUE_ATOMIC_WAIT_AND_READ(head_committed, v);                  \
        atomic_fetch_sub(head_waiters, 1);                              \
}


#define QUEUE_WAKE_TAIL_WAITER(queue) QUEUE_ATOMIC_WAKE_ONE(&queue->tail.committed.atomic_value)
#define QUEUE_WAKE_HEAD_WAITER(queue) QUEUE_ATOMIC_WAKE_ONE(&queue->head.committed.atomic_value)
#define QUEUE_WAKE_ALL_TAIL_WAITERS(queue) QUEUE_ATOMIC_WAKE_ALL(&queue->tail.committed.atomic_value)
#define QUEUE_WAKE_ALL_HEAD_WAITERS(queue) QUEUE_ATOMIC_WAKE_ALL(&queue->head.committed.atomic_value)

#define uint unsigned int

/* --- Single producer implementation --- */

/* Returns an index to push or -1 on failure (Queue is full) */
static inline int sp_try_prepare_push(uint queue_size, uint head_pending, atomic_uint* tail_committed) {
    if (queue_get_free_explicit(queue_size, head_pending, atomic_load(tail_committed)) <= 0)
        return -1;

    /* As this is an SP queue the usage cannot increase from this point
     * onwards, so we can safely return the current working index. */
    return head_pending;
}

/* Returns an index to push or -1 on failure (Queue is full) */
static inline int sp_prepare_push(uint queue_size, uint head_pending, atomic_uint* tail_committed, atomic_uint* tail_waiters) {
    uint tail = atomic_load(tail_committed);

    while (queue_get_free_explicit(queue_size, head_pending, tail) <= 0)
        QUEUE_WAIT_FOR_TAIL(tail_waiters, tail_committed, tail);

    /* As this is an SP queue the usage cannot increase from this point
     * onwards, so we can safely return the current working index. */
    return head_pending;
}

static inline void sp_commit_push(uint prepared_index, atomic_uint* head_committed, atomic_uint* head_waiters) {
    atomic_fetch_add(head_committed, 1);

    if (atomic_load(head_waiters))
        QUEUE_WAKE_HEAD_WAITER(queue);
}

/* --- Single consumer implementation --- */

/* Returns an index to consume or -1 on failure (Queue is empty) */
static inline int sc_try_prepare_consume(atomic_uint* head_committed, uint tail_pending) {
    if (queue_get_committed_explicit(atomic_load(head_committed), tail_pending) == 0)
        return -1;

    /* As this is an SC queue the committed cannot decrease from this point
     * onwards, so we can safely return the current working index. */
    return tail_pending;
}

/* Returns an index to consume */
static inline int sc_prepare_consume(atomic_uint* head_committed, uint tail_pending, atomic_uint* head_waiters) {
    uint head = atomic_load(head_committed);

    while (queue_get_committed_explicit(head, tail_pending) == 0)
        QUEUE_WAIT_FOR_HEAD(head_waiters, head_committed, head);

    /* As this is an SC queue the committed cannot decrease from this point
     * onwards, so we can consume safely. */
    return tail_pending;
}

static inline void sc_commit_consume(unsigned int prepared_index, atomic_uint* tail_committed, atomic_uint* tail_waiters) {
    atomic_fetch_add(tail_committed, 1);

    /* As this is an SC queue any waiters would have to be the producer in this case */
    if (atomic_load(tail_waiters))
        QUEUE_WAKE_TAIL_WAITER(queue);
}

/* Multi producer implementation */

/* Returns an index to push or -1 on failure (Queue is full) */
static inline int mp_try_prepare_push(uint queue_size, atomic_uint* tail_committed, atomic_uint* head_pending) {

    /* Tail needs to be loaded first, otherwise we might overestimate the amount of free space */
    unsigned int tail = atomic_load(tail_committed);
    unsigned int head = atomic_load(head_pending);

    while (1) {
        if (queue_get_free_explicit(queue_size, head, tail) <= 0)
            return -1;

        /* As this is an MP queue another thread might have taken our index. */
        /* This will update `head` on failure */
        if (atomic_compare_exchange_strong(head_pending, &head, head+1))
            return head;
    }
}

/* Returns an index to push */
static inline int mp_prepare_push(uint queue_size, atomic_uint* tail_committed, atomic_uint* head_pending, atomic_uint* tail_waiters) {

    /* Tail needs to be loaded first, otherwise we might overestimate the amount
     * of free space */
    unsigned int tail = atomic_load(tail_committed);
    unsigned int head = atomic_load(head_pending);

    while (1) {
        while (queue_get_free_explicit(queue_size, head, tail) <= 0) {
            QUEUE_WAIT_FOR_TAIL(tail_waiters, tail_committed, tail);
            head = atomic_load(head_pending);
        }

        /* As this is an MP queue another thread might have taken our index. */
        /* This will update `head` on failure */
        if (atomic_compare_exchange_strong(head_pending, &head, head+1))
            return head;
    }
}

/* Returns 0 on success and -1 if it is too early to push. As we can only commit
 * sequentially it can be too early to push in some cases if it would be out of
 * order. */
static inline int mp_try_commit_push(unsigned int prepared_index, atomic_uint* head_committed, atomic_uint* head_waiters) {
    if (prepared_index != atomic_load(head_committed))
        return -1;

    atomic_fetch_add(head_committed, 1);

    if (atomic_load(head_waiters))
        QUEUE_WAKE_ALL_HEAD_WAITERS(queue);

    return 0;
}

static inline void mp_commit_push(unsigned int prepared_index, atomic_uint* head_committed, atomic_uint* head_waiters) {
    uint head = atomic_load(head_committed);

    /* Wait for sequential increment */
    while (prepared_index != head)
        QUEUE_WAIT_FOR_HEAD(head_waiters, head_committed, head);

    atomic_fetch_add(head_committed, 1);

    if (atomic_load(head_waiters))
        QUEUE_WAKE_ALL_HEAD_WAITERS(queue);
}

/* Multi consumer implementation */


/* Returns an index to consume or -1 on failure (Queue is empty) */
static inline int mc_try_prepare_consume(atomic_uint* head_committed, atomic_uint* tail_pending) {

    /* Head needs to be loaded first, otherwise we might overestimate the amount
     * of committed space */
    uint head = atomic_load(head_committed);
    uint tail = atomic_load(tail_pending);

    while (1) {
        if (queue_get_committed_explicit(head, tail) == 0)
            return -1;

        /* As this is an MC queue another thread might have taken our index. */
        /* This will update `tail` on failure */
        if (atomic_compare_exchange_strong(tail_pending, &tail, tail+1))
            return tail;
    }
}

/* Returns an index to consume */
static inline int mc_prepare_consume(atomic_uint* head_committed, atomic_uint* tail_pending, atomic_uint* head_waiters) {

    /* Head needs to be loaded first, otherwise we might overestimate the amount
     * of committed space */
    unsigned int head = atomic_load(head_committed);
    unsigned int tail = atomic_load(tail_pending);

    while (1) {
        while (queue_get_committed_explicit(head, tail) == 0) {
            QUEUE_WAIT_FOR_HEAD(head_waiters, head_comitted, head);
            tail = atomic_load(tail_pending);
        }

        /* As this is an MC queue another thread might have taken our index. */
        /* This will update `tail` on failure */
        if (atomic_compare_exchange_strong(tail_pending, &tail, tail+1))
            return tail;
    }
}


/* Returns 0 on success and -1 if it is too early to push*/
static inline int mc_try_commit_consume(unsigned int prepared_index, atomic_uint* tail_committed, atomic_uint* tail_waiters) {
    uint tail = atomic_load(tail_committed);

    /* Wait for sequential increment */
    if (prepared_index != tail)
        return -1;

    atomic_fetch_add(tail_committed, 1);

    if (atomic_load(tail_waiters))
        QUEUE_WAKE_ALL_TAIL_WAITERS(queue);

    return 0;
}

static inline void mc_commit_consume(unsigned int prepared_index, atomic_uint* tail_committed, atomic_uint* tail_waiters) {
    uint tail = atomic_load(tail_committed);

    /* Wait for sequential increment */
    while (prepared_index != tail)
        QUEUE_WAIT_FOR_TAIL(tail_waiters, tail_committed, tail);

    atomic_fetch_add(tail_committed, 1);

    if (atomic_load(tail_waiters))
        QUEUE_WAKE_ALL_TAIL_WAITERS(queue);
}



/* SPSC implementation */


/* Returns an index to push or -1 on failure (Queue is full) */
static inline int spsc_try_prepare_push(SPSCQueue* queue) {
    return sp_try_prepare_push(queue->queue_size, queue->head.pending.value, &queue->tail.committed.atomic_value);
}

/* Returns an index to push or -1 on failure (Queue is full) */
static inline int spsc_prepare_push(SPSCQueue* queue) {
    return sp_prepare_push(queue->queue_size, queue->head.pending.value,
                           &queue->tail.committed.atomic_value, &queue->tail_waiters);
}

static inline void spsc_commit_push(unsigned int prepared_index, SPSCQueue* queue) {
    sp_commit_push(prepared_index, &queue->head.committed.atomic_value, &queue->head_waiters);
}

/* Returns an index to consume or -1 on failure (Queue is empty) */
static inline int spsc_try_prepare_consume(SPSCQueue* queue) {
    return sc_try_prepare_consume(&queue->head.committed.atomic_value, queue->tail.pending.value);
}

/* Returns an index to consume */
static inline int spsc_prepare_consume(SPSCQueue* queue) {
    return sc_prepare_consume(&queue->head.committed.atomic_value, queue->tail.pending.value, &queue->head_waiters);
}

static inline void spsc_commit_consume(unsigned int prepared_index, SPSCQueue* queue) {
    sc_commit_consume(prepared_index, &queue->tail.committed.atomic_value, &queue->tail_waiters);
}








/* Returns an index to push or -1 on failure (Queue is full) */
static inline int mpmc_try_prepare_push(MPMCQueue* queue) {
    return mp_try_prepare_push(queue->queue_size,
                               &queue->tail.committed.atomic_value,
                               &queue->head.pending.atomic_value);
}

/* Returns an index to push */
static inline int mpmc_prepare_push(MPMCQueue* queue) {
    return mp_prepare_push(queue->queue_size,
                           &queue->tail.committed.atomic_value,
                           &queue->head.pending.atomic_value,
                           &queue->tail_waiters);
}

/* Returns 0 on success and -1 if it is too early to push. As we can only commit
 * sequentially it can be too early to push in some cases if it would be out of
 * order. */
static inline int mpmc_try_commit_push(unsigned int prepared_index, MPMCQueue* queue) {
    return mp_try_commit_push(prepared_index, &queue->head.committed.atomic_value, &queue->head_waiters);
}

static inline void mpmc_commit_push(unsigned int prepared_index, MPMCQueue* queue) {
    mp_commit_push(prepared_index, &queue->head.committed.atomic_value, &queue->head_waiters);
}


/* Returns an index to consume or -1 on failure (Queue is empty) */
static inline int mpmc_try_prepare_consume(MPMCQueue* queue) {
    return mc_try_prepare_consume(&queue->head.committed.atomic_value, &queue->tail.pending.atomic_value);
}


/* Returns an index to consume */
static inline int mpmc_prepare_consume(MPMCQueue* queue) {
    return mc_prepare_consume(&queue->head.committed.atomic_value,
                       &queue->tail.pending.atomic_value,
                       &queue->head_waiters);
}


/* Returns 0 on success and -1 if it is too early to push*/
static inline int mpmc_try_commit_consume(unsigned int prepared_index, MPMCQueue* queue) {
    return mc_try_commit_consume(prepared_index, &queue->tail.committed.atomic_value, &queue->tail_waiters);
}


static inline void mpmc_commit_consume(unsigned int prepared_index, MPMCQueue* queue) {
    mc_commit_consume(prepared_index, &queue->tail.committed.atomic_value, &queue->tail_waiters);
}

#undef uint
/* struct _queue_header { */
/*     atomic_uint position;  // Index of next task to be run. */
/*     atomic_uint committed; // Index of last task added to the queue. */
/*     atomic_uint pending;   // Index of last task currently being added. */
/*     atomic_uint queuers;   // Number of queuers waiting because of full queue. */

/*     uint32_t queue_size; */
/* }; */


/* #define Queue(T) T* */

/* static inline void* allocate_queue_with_size(int type_size, uint32_t size) { */
/*     void* q = malloc(type_size*size + sizeof(struct _queue_header)); */
/*     memset(q, 0, type_size*size + sizeof(struct _queue_header)); */
/*     ((struct _queue_header*)q)[-1].queue_size = size; */
/*     q = &(((struct _queue_header*)q)[1]); */
/*     return q; */
/* } */

/* /\* Returns a Queue(T) *\/ */
/* #define make_queue(T, size) allocate_queue_with_size(sizeof(T), size) */
/* #define free_queue(queue) free(&((struct _queue_header*)queue)[-1]) */

/* #define get_queue_header(queue) (&((struct _queue_header*)queue)[-1]) */


/* static inline int queue_free(void* queue) { */
/*     struct _queue_header* h = get_queue_header(queue); */

/*     //u32 committed, u32 position, u32 queue_size */
/*     return h->queue_size - (h->committed - position); */
/* } */




#endif /* QUEUE_H */
