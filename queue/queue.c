#include <threads.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <assert.h>

typedef struct Node {
    void *data;
    struct Node *next;
} Node;

// Each consumer that has to block creates an instance on its own stack, chains it into the global waiter list and then
// sleeps on its private condition variable.
typedef struct Waiter {
    cnd_t cv; // private cond var for precise wake-ups
    struct Waiter *next;
    void *item; // item handed over by the producer
    int  assigned; // 0 until producer sets item
} Waiter;

// Global queue state – protected by q_mtx except for visited_cnt.
static mtx_t q_mtx;
static Node *q_head = NULL;
static Node *q_tail = NULL;

static Waiter *w_head = NULL;
static Waiter *w_tail = NULL;

static atomic_size_t visited_cnt; // total items that traversed the queue
static int mtx_created = 0; // guard against double mtx_init

// Helper routines

// Fully initialise the mutex exactly once. Safe because the assignment promises initQueue() is not called concurrently.
static void ensure_mutex_created(void) {
    if (!mtx_created) {
        mtx_init(&q_mtx, mtx_plain);
        mtx_created = 1;
    }
}

/* Add a waiter to the FIFO waiter list */
static void waiter_enqueue(Waiter *w) {
    w->next = NULL;
    if (w_tail) {
        w_tail->next = w;
    } else {
        w_head = w;
    }
    w_tail = w;
}

/* Pop the oldest waiter from the list (expects q_mtx held). */
static Waiter *waiter_dequeue(void) {
    Waiter *w = w_head;
    if (w_head) {
        w_head = w_head->next;
        if (!w_head) w_tail = NULL;
    }
    return w;
}


void initQueue(void) {
    ensure_mutex_created();

    mtx_lock(&q_mtx);

    // Clear data structures so a fresh run can reuse the module.
    q_head = q_tail = NULL;
    w_head = w_tail = NULL;
    atomic_store_explicit(&visited_cnt, 0, memory_order_relaxed);

    mtx_unlock(&q_mtx);
}

void destroyQueue(void) {
    if (!mtx_created) return;
    mtx_lock(&q_mtx);

    // Free any remaining queue nodes (legal because no consumers run now)
    Node *n = q_head;
    while (n) {
        Node *next = n->next;
        free(n);
        n = next;
    }
    q_head = q_tail = NULL;
    // There must be no sleepers at this point. Sanity check:
    assert(w_head == NULL && "destroyQueue() called while threads are waiting");

    mtx_unlock(&q_mtx);

    mtx_destroy(&q_mtx);
    mtx_created = 0;  // allow a subsequent initQueue()
}

void enqueue(void *item) {
    // Pre‑allocate node. Assuming malloc never fails
    Node *n = malloc(sizeof(Node));
    n->data = item;
    n->next = NULL;

    mtx_lock(&q_mtx);

    // If there are sleeping consumers, hand the item directly to the oldest
    // sleeper (FIFO fairness). Otherwise append to the item list.
    if (w_head) {
        Waiter *w = waiter_dequeue();
        w->item = item;
        w->assigned = 1;
        // signals (wakes up) a sleeping consumer thread that is waiting on its private condition variable
        cnd_signal(&w->cv);
        free(n); // node is unnecessary – item skipped the list
    } else {
        // Normal push to tail of linked list
        if (q_tail) q_tail->next = n; else q_head = n;
        q_tail = n;
    }

    mtx_unlock(&q_mtx);
}


void *dequeue(void) {
    Waiter self; /* stack‑allocated waiter descriptor */
    cnd_init(&self.cv);
    self.next = NULL;
    self.item = NULL;
    self.assigned = 0;

    mtx_lock(&q_mtx);
    // item already waiting
    if (q_head) {
        Node *n = q_head;
        q_head = n->next;
        if (!q_head) q_tail = NULL;

        void *ret = n->data;
        free(n);
        atomic_fetch_add_explicit(&visited_cnt, 1, memory_order_relaxed);
        mtx_unlock(&q_mtx);
        cnd_destroy(&self.cv);
        return ret;
    }
    // No item – join the sleepers list
    waiter_enqueue(&self);
    // Sleep until a producer assigns us an item. No spurious wake‑ups per assignment spec, so we don't loop defensively
    cnd_wait(&self.cv, &q_mtx);
    void *ret = self.item;
    atomic_fetch_add_explicit(&visited_cnt, 1, memory_order_relaxed);

    mtx_unlock(&q_mtx);
    cnd_destroy(&self.cv);
    return ret;
}

size_t visited(void) {
    // Lock‑free so relaxed read is sufficient
    return atomic_load_explicit(&visited_cnt, memory_order_relaxed);
}
