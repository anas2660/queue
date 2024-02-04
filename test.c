#include "queue.h"
#include <stdio.h>
#include <assert.h>
#include <threads.h>

#define DATA_SIZE 1024
#define QUEUE_SIZE 1024

int input_buffer[DATA_SIZE];
int output_buffer[DATA_SIZE];
int queue_buffer[QUEUE_SIZE];

int consumer(SPSCQueue* q) {
    for (int i = 0; i < DATA_SIZE; i++) {
        int index = spsc_prepare_consume(q);
        output_buffer[i] = queue_buffer[index % QUEUE_SIZE];
        spsc_commit_consume(index, q);
    }
    return 0;
}



int main(int argc, char *argv[]) {

    for (int i = 0; i < DATA_SIZE; i++)
        input_buffer[i] = i + 100;

    SPSCQueue q;
    queue_init(&q, QUEUE_SIZE);


    thrd_t thread;
    thrd_create(&thread, (void*)consumer, &q);

    for (int i = 0; i < DATA_SIZE; i++) {
        int index = spsc_prepare_push(&q);
        queue_buffer[index % QUEUE_SIZE] = input_buffer[i];
        spsc_commit_push(index, &q);
    }

    thrd_join(thread, NULL);


    for (int i = 0; i < DATA_SIZE; i++) {
        printf(" [%d]: in:%d  out:%d\n", i, input_buffer[i], output_buffer[i]);
        assert(input_buffer[i] == output_buffer[i]);
    }


    return 0;
}
