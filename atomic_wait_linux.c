
#include <sys/syscall.h>
#include <linux/futex.h>
#include <unistd.h>
#include <stdint.h>

typedef uint32_t u32;
typedef uint64_t u64;

#ifdef __x86_64__
static inline void futex_call(u32 *uaddr, int futex_op, u32 val) {
    register u64 rax asm("rax") = SYS_futex;
    register u64 r10 asm("r10") = 0;
    register u64 r8  asm( "r8") = 0;
    register u64 r9  asm( "r9") = 0;
    asm volatile (
        "syscall" : "=a"(rax) :
        "a"(rax), "D"(uaddr), "S"(futex_op),
        "d"(val), "r"(r10),   "r"(r8), "r"(r9)
        : "rcx", "r11" //, "memory" // Memory probably doesnt get clobbered.
    );
}
#else
static inline void futex_call(u32 *uaddr, int futex_op, u32 val) {
    syscall(SYS_futex, uaddr, futex_op, val, NULL, NULL, 0);
}
#endif




void atomic_wake_one(u32* futex) {
    futex_call(futex, FUTEX_WAKE_PRIVATE, 1);
}

void atomic_wake_all(u32* futex) {
    futex_call(futex, FUTEX_WAKE_PRIVATE, INT32_MAX);
}

void atomic_wait(u32* futex, u32 expected_value) {
    /*
    #if USE_SPINLOCK == 1
        for (int i = 0; i < SPINLOCK_SPIN_COUNT; i++) {
            if (*futex != expected_value) {
                return;
            }
        }
    #endif
    */

    futex_call(futex, FUTEX_WAIT_PRIVATE, expected_value);
}
