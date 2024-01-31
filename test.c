#include "queue.h"



int main(int argc, char *argv[]) {

    {
        SPSCQueue q = { .queue_size = 10 };
    }

    SPSCQueue q;
    queue_init(&q, 10);






    return 0;
}
