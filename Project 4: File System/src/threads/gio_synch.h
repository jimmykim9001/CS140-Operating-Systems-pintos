#ifndef PROJECT5_GIO_SYNCH_H
#define PROJECT5_GIO_SYNCH_H
#include "thread.h"
#include "../lib/stdint.h"
#include "../lib/kernel/list.h"

#define __sync_fetch(a) __sync_fetch_and_add(a, 0)
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

struct rw_lock{
    struct list waiters;
    struct thread *w_holder;
    uint16_t level;
};

void rw_lock_init(struct rw_lock *l);
void r_lock_acquire(struct rw_lock *l);
void r_lock_release(struct rw_lock *l);
void w_lock_acquire(struct rw_lock *l);
void w_lock_release(struct rw_lock *l);
void r_lock_upgrade_to_w(struct rw_lock *l);
bool r_try_lock_acquire(struct rw_lock *l);
bool w_try_lock_acquire(struct rw_lock *l);

#define exchange(a, b) { \
                            enum intr_level old_level = intr_disable ();  \
                            a ^= b;                                        \
                            b ^= a;                                         \
                            intr_set_level (old_level);                      \
                        }



#endif //PROJECT5_GIO_SYNCH_H
