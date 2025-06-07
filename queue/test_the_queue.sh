#!/usr/bin/env bash
###############################################################################
# test_queue.sh — official grading suite for the Concurrent-Queue assignment
###############################################################################

set -Eeuo pipefail

CC="gcc"
CFLAGS="-O3 -D_POSIX_C_SOURCE=200809 -Wall -Wextra -Werror -std=c11 -pthread"
$CC $CFLAGS -c queue.c -o queue.o

cat > queue_tests.c <<'EOF'
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- queue.c prototypes ---- */
void   initQueue(void);
void   destroyQueue(void);
void   enqueue(void *item);
void  *dequeue(void);
size_t visited(void);

#define CHECK(cond,msg) do { if(!(cond)){fprintf(stderr,"ASSERT-FAIL: %s\n",msg); exit(1);} } while(0)

/* === Test 1 === */
static void test_basic(void) {
    initQueue();
    int x = 42;
    enqueue(&x);
    void *p = dequeue();
    CHECK(p == &x, "value mismatch");
    destroyQueue();
}

/* === Test 2 === */
typedef struct { atomic_size_t next; size_t total; } Pctx;
typedef struct { atomic_size_t done; uint8_t *seen; size_t total; } Cctx;

static void *prod(void *arg) {
    Pctx *ctx = arg;
    size_t i;
    while ((i = atomic_fetch_add_explicit(&ctx->next, 1, memory_order_relaxed)) < ctx->total)
        enqueue((void *)(i + 1));
    return NULL;
}

static void *cons(void *arg) {
    Cctx *ctx = arg;
    while (atomic_load_explicit(&ctx->done, memory_order_relaxed) < ctx->total) {
        size_t idx = (size_t)dequeue() - 1;
        CHECK(idx < ctx->total, "index out of range");
        CHECK(__atomic_test_and_set(&ctx->seen[idx], __ATOMIC_RELAXED) == 0,
              "duplicate dequeue");
        atomic_fetch_add_explicit(&ctx->done, 1, memory_order_relaxed);
    }
    return NULL;
}

static void test_multi(void) {
    const size_t N = 100000;
    const int P = 4, C = 4;
    uint8_t *seen = calloc(N, 1);
    CHECK(seen, "calloc failed");

    Pctx pc = { ATOMIC_VAR_INIT(0), N };
    Cctx cc = { ATOMIC_VAR_INIT(0), seen, N };

    initQueue();
    pthread_t pt[P], ct[C];
    for (int i = 0; i < P; ++i) pthread_create(&pt[i], NULL, prod, &pc);
    for (int i = 0; i < C; ++i) pthread_create(&ct[i], NULL, cons, &cc);
    for (int i = 0; i < P; ++i) pthread_join(pt[i], NULL);
    for (int i = 0; i < C; ++i) pthread_join(ct[i], NULL);
    for (size_t i = 0; i < N; ++i) CHECK(seen[i], "lost item");

    free(seen);
    destroyQueue();
}

/* === Helpers === */
typedef struct { pthread_mutex_t m; pthread_cond_t cv; int reached, target; } barrier_t;
static void barrier_init(barrier_t *b, int t) {
    pthread_mutex_init(&b->m, NULL);
    pthread_cond_init(&b->cv, NULL);
    b->reached = 0; b->target = t;
}
static void barrier_wait(barrier_t *b) {
    pthread_mutex_lock(&b->m);
    if (++b->reached == b->target) pthread_cond_broadcast(&b->cv);
    else while (b->reached < b->target) pthread_cond_wait(&b->cv, &b->m);
    pthread_mutex_unlock(&b->m);
}

/* === Test 3 === */
static barrier_t fb;
static atomic_int woke;
static int order[2] = { -1, -1 };

static void *fair_cons(void *arg) {
    intptr_t id = *((intptr_t *)arg);
    free(arg);
    barrier_wait(&fb);
    (void)dequeue();
    int slot = atomic_fetch_add(&woke, 1);
    order[id] = slot;
    return NULL;
}

static void test_fairness(void) {
    initQueue();
    woke = ATOMIC_VAR_INIT(0);
    barrier_init(&fb, 1);
    pthread_t a, b;

    intptr_t *id0 = malloc(sizeof(intptr_t));
    *id0 = 0;
    pthread_create(&a, NULL, fair_cons, id0);
    struct timespec ts = {0, 20 * 1000 * 1000};
    nanosleep(&ts, NULL);


    barrier_init(&fb, 1);
    intptr_t *id1 = malloc(sizeof(intptr_t));
    *id1 = 1;
    pthread_create(&b, NULL, fair_cons, id1);
    struct timespec ts1 = {0, 10 * 1000 * 1000};
    nanosleep(&ts1, NULL);


    int x = 1, y = 2;
    enqueue(&x);
    enqueue(&y);

    pthread_join(a, NULL);
    pthread_join(b, NULL);
    CHECK(order[0] == 0 && order[1] == 1, "non-FIFO wake-up");
    destroyQueue();
}

/* === Test 4 === */
static int got_by;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static void *ho_cons(void *arg) {
    intptr_t id = *((intptr_t *)arg);
    free(arg);
    void *p = dequeue();
    if (p == (void *)0xdeadbeef) {
        pthread_mutex_lock(&mtx);
        if (got_by == -1) got_by = (int)id;
        pthread_mutex_unlock(&mtx);
    }
    return NULL;
}

static void test_handoff(void) {
    const int K = 5;
    pthread_t t[K + 1];
    got_by = -1;
    initQueue();

    for (int i = 0; i < K; ++i) {
        intptr_t *id = malloc(sizeof(intptr_t));
        *id = i;
        pthread_create(&t[i], NULL, ho_cons, id);
    }

    struct timespec ts3 = {0, 30 * 1000 * 1000};
    nanosleep(&ts3, NULL);
    enqueue((void *)0xdeadbeef);
    struct timespec ts4 = {0, 10 * 1000 * 1000};
    nanosleep(&ts4, NULL);

    intptr_t *late = malloc(sizeof(intptr_t));
    *late = K;
    pthread_create(&t[K], NULL, ho_cons, late);

    for (int i = 0; i <= K; ++i) pthread_join(t[i], NULL);
    CHECK(got_by >= 0 && got_by < K, "item went to latecomer, not sleeper");
    destroyQueue();
}

/* === Test 5 === */
static void test_visited(void) {
    const size_t N = 10000;
    initQueue();
    for (size_t i = 0; i < N; ++i) enqueue((void *)(i + 1));
    for (size_t i = 0; i < N; ++i) (void)dequeue();
    CHECK(visited() == N, "visited() wrong");
    destroyQueue();
}

/* === Entry === */
int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "need sub-test name\n"); return 2; }
    if      (!strcmp(argv[1], "basic"))    test_basic();
    else if (!strcmp(argv[1], "multi"))    test_multi();
    else if (!strcmp(argv[1], "fairness")) test_fairness();
    else if (!strcmp(argv[1], "handoff"))  test_handoff();
    else if (!strcmp(argv[1], "visited"))  test_visited();
    else { fprintf(stderr, "unknown test\n"); return 2; }
    return 0;
}
EOF

### 3) Build test harness
gcc $CFLAGS queue_tests.c queue.o -o queue_tests

########################################################################
# 4) Run tests – keep our own status, no accidental early exit
########################################################################
declare -A DESC=(
  [basic]="single-thread sanity"
  [multi]="heavy MP/MC safety"
  [fairness]="FIFO wake-up fairness"
  [handoff]="sleeper hand-off rule"
  [visited]="visited() accuracy"
)

tests=(basic multi fairness handoff visited)
passed=0
failed=0

set +e   # <-- DISABLE 'exit on error' for the entire loop
for t in "${tests[@]}"; do
    printf "  %-8s – %-28s … " "$t" "${DESC[$t]}"

    output=$(timeout 10s ./queue_tests "$t" 2>&1)
    status=$?

    if [[ $status == 0 ]]; then
        echo "✅ PASS"
        ((passed++))
    else
        echo "❌ FAIL"
        echo "      ↳ ${DESC[$t]} test failed. Below is the harness output:"
        echo "------------------------------------------------------------------------"
        echo "$output"
        echo "------------------------------------------------------------------------"
        ((failed++))
    fi
done
set -e   # re-enable if you still want it for the rest of the script

echo "================================================================"
if (( failed == 0 )); then
    echo "🎉  All $passed / ${#tests[@]} tests passed!"
    exit 0
else
    echo "💥  $failed test(s) failed, $passed passed."
    exit 1
fi
