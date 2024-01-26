#include "queue.h"



int main(int argc, char *argv[]) {

    {
        SPSCQueue q = { .queue_size = 10 };
    }

    SPSCQueue q;
    QUEUE_INIT(&q, 10);








    return 0;
}
