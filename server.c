#include "segel.h"
#include "request.h"
#include "log.h"

//
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//
request_queue_t *request_queue; // global request queue
server_log log;                 // global server log

typedef struct {
    int connfd;               // The socket descriptor
    struct timeval arrival;   // Time the request arrived (for Stat-Req-Arrival)
} job_t;

typedef struct {
    job_t *jobs; // Circular array of jobs
    int head; // where we dequeue from
    int tail; // where we enqueue to
    int count;// number of jobs in the queue
    int size; // maximum size of the queue
    pthread_mutex_t lock; // mutex for synchronizing access
    pthread_cond_t not_empty; // worker threads wait on this when the queue is empty
    pthread_cond_t not_full; // main thread waits on this when the queue is full
} request_queue_t;

// Parses command-line arguments
void getargs(int *port, int *threads, int *queue_size, int *debug_sleep_time, int argc, char *argv[])
{
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <port> <threads> <queue_size> <debug_sleep_time>\n", argv[0]);
        exit(1);
    }
    *port = atoi(argv[1]);
    *threads = atoi(argv[2]);
    *queue_size = atoi(argv[3]);
    *debug_sleep_time = atoi(argv[4]);
}
// TODO: HW3 — Initialize thread pool and request queue
// This server currently handles all requests in the main thread.
// You must implement a thread pool (fixed number of worker threads)
// that process requests from a synchronized queue.
void enqueue(request_queue_t *queue, job_t job) {
    pthread_mutex_lock(&queue->lock);

    // main thread waits if the queue is full
    while(queue->count == queue->size) {
        pthread_cond_wait(&queue->not_full, &queue->lock);
    }

    //logic, we add the job to the tail of the queue and update tail to the next position cyclically
    queue->jobs[queue->tail] = job;
    queue->tail = (queue->tail + 1) % queue->size;
    queue->count++;

    // signal that queue is not enmpty anymore
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->lock);
}

job_t dequeue(request_queue_t *queue) {
    pthread_mutex_lock(&queue->lock);

    // worker threads will wait if the queue is empty
    while(queue->count == 0) {
        pthread_cond_wait(&queue->not_empty, &queue->lock);
    }

    //logic
    job_t job = queue->jobs[queue->head];
    queue->head = (queue->head + 1) % queue->size;
    queue->count--;

    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->lock);

    return job;
}

int main(int argc, char *argv[])
{
    int listenfd, connfd, port, clientlen;
    int num_threads, queue_size, debug_sleep_time;
    struct sockaddr_in clientaddr;

    getargs(&port, &num_threads, &queue_size, &debug_sleep_time, argc, argv);

    // Create the global server log (pass debug_sleep_time for Person B to use)
    log = create_log(debug_sleep_time);  // TODO: Person B will update create_log() to accept debug_sleep_time

    // Allocate array of pthread_t for worker threads (not creating them yet!)
    pthread_t *worker_threads = malloc(num_threads * sizeof(pthread_t));
    if (!worker_threads) {
        fprintf(stderr, "Failed to allocate worker threads array\n");
        exit(1);
    }

    // Create array of thread statistics (one per worker thread)
    threads_stats *thread_stats_array = malloc(num_threads * sizeof(threads_stats));
    if (!thread_stats_array) {
        fprintf(stderr, "Failed to allocate thread stats array\n");
        exit(1);
    }
    
    // Initialize each thread's statistics
    for (int i = 0; i < num_threads; i++) {
        thread_stats_array[i] = malloc(sizeof(struct Threads_stats));
        if (!thread_stats_array[i]) {
            fprintf(stderr, "Failed to allocate thread stats for thread %d\n", i);
            exit(1);
        }
        thread_stats_array[i]->id = i;           // Thread ID
        thread_stats_array[i]->stat_req = 0;     // Static request count
        thread_stats_array[i]->dynm_req = 0;     // Dynamic request count
        thread_stats_array[i]->post_req = 0;     // POST request count
        thread_stats_array[i]->total_req = 0;    // Total request count
    }

    // TODO: Mission A2 — Initialize the request queue here
    request_queue = malloc(sizeof(request_queue_t));
    if(!request_queue) {
        fprintf(stderr, "Failed to allocate request queue\n");
        exit(1);
    }
    // Initialize request queue fields
    request_queue->jobs = malloc(queue_size * sizeof(job_t));
    request_queue->size = queue_size;
    request_queue->head = 0;
    request_queue->tail = 0;
    request_queue->count = 0;

    //initialize mutex and condition variables
    pthread_mutex_init(&request_queue->lock, NULL);
    pthread_cond_init(&request_queue->not_empty, NULL);
    pthread_cond_init(&request_queue->not_full, NULL);
    // TODO: Mission A3 — Create the worker thread pool here

    listenfd = Open_listenfd(port);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t*) &clientlen);

        // TODO: HW3 — Record the request arrival time here

        // DEMO PURPOSE ONLY:
        // This is a dummy request handler that immediately processes
        // the request in the main thread without concurrency.
        // Replace this with logic to enqueue the connection and let
        // a worker thread process it from the queue.

        threads_stats t = malloc(sizeof(struct Threads_stats));
        t->id = 0;             // Thread ID (placeholder)
        t->stat_req = 0;       // Static request count
        t->dynm_req = 0;       // Dynamic request count
        t->total_req = 0;      // Total request count

        time_stats dum;

        // gettimeofday(&arrival, NULL);

        // Call the request handler (immediate in main thread — DEMO ONLY)
        requestHandle(connfd, dum, t, log);

        free(t); // Cleanup
        Close(connfd); // Close the connection
    }

    // Clean up the server log before exiting
    destroy_log(log);

    // Cleanup: Free worker threads array
    free(worker_threads);
    
    // Cleanup: Free thread stats array
    for (int i = 0; i < num_threads; i++) {
        free(thread_stats_array[i]);
    }
    free(thread_stats_array);

    // cleanup request queue and its resources
    pthread_mutex_destroy(&request_queue->lock);
    pthread_cond_destroy(&request_queue->not_empty);
    pthread_cond_destroy(&request_queue->not_full);
    free(request_queue->jobs);
    free(request_queue);
}
