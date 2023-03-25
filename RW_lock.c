#include "RW_lock.h"
#include "err.h"

#include <pthread.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

struct RW_lock {
    pthread_mutex_t lock;
    pthread_cond_t readers;
    pthread_cond_t writers;
    int rcount, wcount, rwait, wwait;
    int change;
};

RW_lock* rw_lock_init() {
    RW_lock* rw_lock = malloc(sizeof(RW_lock));
    CHECK_PTR(rw_lock);

    CHECK(pthread_mutex_init(&rw_lock->lock, 0));
    CHECK(pthread_cond_init(&rw_lock->readers, 0));
    CHECK(pthread_cond_init(&rw_lock->writers, 0));

    rw_lock->rcount = 0;
    rw_lock->wcount = 0;
    rw_lock->rwait = 0;
    rw_lock->wwait = 0;
    rw_lock->change = 0;
    return rw_lock;
}

void rw_lock_destroy(RW_lock* rw_lock) {
    CHECK(pthread_cond_destroy(&rw_lock->readers));
    CHECK(pthread_cond_destroy(&rw_lock->writers));
    CHECK(pthread_mutex_destroy (&rw_lock->lock));
    free(rw_lock);
}

void reader_preprotocol(RW_lock* rw_lock) {
    CHECK(pthread_mutex_lock(&rw_lock->lock));

    rw_lock->rwait++;

    if ((rw_lock->wcount > 0 || rw_lock->wwait > 0) && rw_lock->change == 0) {
        do {
            CHECK(pthread_cond_wait(&rw_lock->readers, &rw_lock->lock));
        } while (rw_lock->wcount > 0 && rw_lock->change == 0);
    }

    rw_lock->rwait--;
    rw_lock->rcount++;
    rw_lock->change = 0;

    if (rw_lock->rwait > 0) {
        CHECK(pthread_cond_signal(&rw_lock->readers));
    }

    CHECK(pthread_mutex_unlock(&rw_lock->lock));
}

void reader_postprotocol(RW_lock* rw_lock) {
    CHECK(pthread_mutex_lock(&rw_lock->lock));

    rw_lock->rcount--;

    if (rw_lock->rcount == 0 && rw_lock->wwait > 0) {
        CHECK(pthread_cond_signal(&rw_lock->writers));
    }

    CHECK(pthread_mutex_unlock(&rw_lock->lock));
}

void writer_preprotocol(RW_lock* rw_lock) {
    CHECK(pthread_mutex_lock(&rw_lock->lock));

    rw_lock->wwait++;

    while (rw_lock->wcount > 0 || rw_lock->rcount > 0 || rw_lock->change == 1) {
        CHECK(pthread_cond_wait(&rw_lock->writers, &rw_lock->lock));
    }

    rw_lock->wwait--;
    rw_lock->wcount++;

    CHECK(pthread_mutex_unlock(&rw_lock->lock));
}

void writer_postprotocol(RW_lock* rw_lock) {
    CHECK(pthread_mutex_lock(&rw_lock->lock));

    rw_lock->wcount--;

    if (rw_lock->rwait > 0) {
        rw_lock->change = 1;
        CHECK(pthread_cond_signal(&rw_lock->readers));
    } else if (rw_lock->wwait > 0) {
        CHECK(pthread_cond_signal(&rw_lock->writers));
    }

    CHECK(pthread_mutex_unlock(&rw_lock->lock));
}