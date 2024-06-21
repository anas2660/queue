# Queue

Public domain (bring your own memory) queue sync implementation.
Implements SPSC, MPSC, SPMC, and MPMC queues.

## Usage

Queues work in index space.
 - You provide the buffer.
 - You mod the index by the buffer size.
   - This allows for arbitrary queue sizes while preserving power of two optimizations.
   - Use queue sizes of powers of two for optimal performance.

Producer side:
 - Use `prepare_push` functions to get an indices to work on.
 - Use `commit_push` functions to commit to the queue

Consumer side:
 - Use `prepare_consume` functions to get an indices to work on.
 - Use `commit_consume` functions to actually consume from the queue (frees indices)

## Implementation

```
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
```
The `committed` and `pending` values will only increment
sequentially for both the head and tail sides.

The diagram describes an MPMC queue. If the queue is single
producer the `committed` and `pending` of the head will always
be equal. If the queue is single consumer the same applies for
the tail.
