#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <time.h>
#include "queue.h"

#define N_THREADS 4

void* test_data[N_THREADS] = {(void*)1, (void*)2, (void*)3, (void*)4};
void* results[N_THREADS];
atomic_int ready_threads = 0;

// Test 1: Single-threaded enqueue and dequeue
void test_single_thread() {
    initQueue();
    enqueue(test_data[0]);
    void* result = dequeue();
    printf("Test 1: %s\n", result == test_data[0] ? "passed" : "failed");
    destroyQueue();
}

// Test 2: FIFO order
void test_fifo_order() {
    initQueue();
    enqueue(test_data[0]);
    enqueue(test_data[1]);
    int passed = (dequeue() == test_data[0] && dequeue() == test_data[1]);
    printf("Test 2: %s\n", passed ? "passed" : "failed");
    destroyQueue();
}

// Test 3: Blocking dequeue (consumer waits)
int consumer_blocking(void* arg) {
    void* data = dequeue();
    printf("Test 3: %s\n", data == arg ? "passed" : "failed");
    return 0;
}

void test_blocking_dequeue() {
    initQueue();
    thrd_t t;
    thrd_create(&t, consumer_blocking, test_data[0]);
    thrd_sleep(&(struct timespec){.tv_sec = 1}, NULL);
    enqueue(test_data[0]);
    thrd_join(t, NULL);
    destroyQueue();
}

// Test 4: visited() check
void test_visited_counter() {
    initQueue();
    enqueue(test_data[0]);
    dequeue();
    size_t count = visited();
    printf("Test 4: %s (visited = %zu)\n", count >= 1 ? "passed" : "failed", count);
    destroyQueue();
}

// Test 5: Sleeping threads resume FIFO
int fifo_consumer(void* arg) {
    intptr_t idx = (intptr_t)arg;
    // stagger the threads to ensure they block in order
    // HAS A CHANCE OF NOT WORKING
    thrd_sleep(&(struct timespec){.tv_nsec = 100000000*idx}, NULL); 
    results[idx] = dequeue();
    return 0;
}

void test_sleep_fifo_order() {
    ready_threads = 0; // reset for safety
    initQueue();

    thrd_t threads[N_THREADS];

    // Start all threads that will block on dequeue()
    for (intptr_t i = 0; i < N_THREADS; ++i) {
        thrd_create(&threads[i], fifo_consumer, (void*)i);
    }

    // Give them time to block
    thrd_sleep(&(struct timespec){.tv_nsec = 500000000}, NULL);

    for (int i = 0; i < N_THREADS; ++i) {
        enqueue(test_data[i]);
    }

    thrd_sleep(&(struct timespec){.tv_nsec = 500000000}, NULL);

    for (int i = 0; i < N_THREADS; ++i) {
        thrd_join(threads[i], NULL);
    }

    int passed = 1;
    for (int i = 0; i < N_THREADS; ++i) {
        if (results[i] != test_data[i]) {
            printf("Test 5 failed: Thread %d got wrong value\n", i);
            passed = 0;
        }
    }

    if (passed){
        if (visited() != N_THREADS) {
            printf("Test 5 failed: visited() count is %zu, expected %d\n", visited(), N_THREADS);
            passed = 0;
        }
    }

    if (passed) {
        printf("Test 5: passed (FIFO wake-up order maintained)\n");
    }

    destroyQueue();
}

// Test 7: dequeue blocks until item arrives (no deadlock or crash)
int blocking_consumer_edge(void* arg) {
    (void)arg; // explicitly ignore
    void* item = dequeue();
    results[0] = item;
    return 0;
}

void test_dequeue_blocks_and_returns_correct_item() {
    initQueue();
    thrd_t t;
    thrd_create(&t, blocking_consumer_edge, NULL);

    // Give the consumer time to block
    thrd_sleep(&(struct timespec){.tv_sec = 1}, NULL);

    enqueue(test_data[0]);
    thrd_join(t, NULL);

    if (results[0] == test_data[0]) {
        printf("Test 7: passed (dequeue blocked and returned correct item)\n");
    } else {
        printf("Test 7: failed\n");
    }
    destroyQueue();
}

// Test 8: initQueue after destroyQueue works properly
void test_reinit_queue() {
    initQueue();
    enqueue(test_data[0]);
    dequeue();
    destroyQueue();

    // Reinitialize and reuse
    initQueue();
    enqueue(test_data[1]);
    void* item = dequeue();

    if (item == test_data[1]) {
        printf("Test 8: passed (reinit after destroy works)\n");
    } else {
        printf("Test 8: failed\n");
    }
    destroyQueue();
}


int main() {
    test_single_thread();
    test_fifo_order();
    test_blocking_dequeue();
    test_visited_counter();
    test_sleep_fifo_order();
    test_dequeue_blocks_and_returns_correct_item();
    test_reinit_queue();
    return 0;
}