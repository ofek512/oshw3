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

// Job structure for the request queue
typedef struct {
    int connfd;               // The socket descriptor
    struct timeval arrival;   // Time the request arrived (for Stat-Req-Arrival)
} job_t;

// Request queue structure (must be defined before global declaration)
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

// Global variables
request_queue_t *request_queue;     // global request queue
server_log server_log_inst;         // global server log (renamed to avoid conflict with math log())
threads_stats *thread_stats_array;  // array of per-thread statistics

// Parses command-line arguments
void getargs(int *port, int *threads, int *queue_size, int *debug_sleep_time, int argc, char *argv[])
{
    // Require at least port, threads, queue_size
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <port> <threads> <queue_size> [debug_sleep_time]\n", argv[0]);
        exit(1);
    }
    *port = atoi(argv[1]);
    *threads = atoi(argv[2]);
    *queue_size = atoi(argv[3]);
    
    // Validate port is in valid range (above privileged ports, below max)
    if (*port < 1024 || *port > 65535) {
        fprintf(stderr, "Error: port must be between 1024 and 65535\n");
        exit(1);
    }
    
    // Validate threads and queue_size are positive
    if (*threads <= 0) {
        fprintf(stderr, "Error: threads must be a positive number\n");
        exit(1);
    }
    if (*queue_size <= 0) {
        fprintf(stderr, "Error: queue_size must be a positive number\n");
        exit(1);
    }
    
    // debug_sleep_time is optional, default to 0 (no debug)
    if (argc >= 5) {
        *debug_sleep_time = atoi(argv[4]);
        // If negative, treat as 0 (no debug)
        if (*debug_sleep_time < 0) {
            *debug_sleep_time = 0;
        }
    } else {
        *debug_sleep_time = 0;  // Default: no debug sleep
    }
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

void* worker_thread(void* arg){
    threads_stats my_stats = (threads_stats)arg;
    while(1) {
        job_t job = dequeue(request_queue);
        // get dispatch time
        struct timeval dispatch_time;
        gettimeofday(&dispatch_time, NULL);

        time_stats tm_stats;
        tm_stats.task_dispatch = dispatch_time;
        tm_stats.task_arrival = job.arrival;

        requestHandle(job.connfd, tm_stats, my_stats, server_log_inst); //tm_stats important for statistics
        Close(job.connfd);
    }
}

int main(int argc, char *argv[])
{
    int listenfd, connfd, port, clientlen;
    int num_threads, queue_size, debug_sleep_time;
    struct sockaddr_in clientaddr;

    getargs(&port, &num_threads, &queue_size, &debug_sleep_time, argc, argv);

    // Create the global server log (pass debug_sleep_time for Person B to use)
    server_log_inst = create_log(debug_sleep_time);

    // Allocate array of pthread_t for worker threads (not creating them yet!)
    pthread_t *worker_threads = malloc(num_threads * sizeof(pthread_t));
    if (!worker_threads) {
        fprintf(stderr, "Failed to allocate worker threads array\n");
        exit(1);
    }

    // Create array of thread statistics (one per worker thread)
    thread_stats_array = malloc(num_threads * sizeof(threads_stats));
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
        thread_stats_array[i]->id = i + 1;       // Thread ID (1 to N per PDF)
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
    for (int i = 0; i < num_threads; i++) {
        int rc = pthread_create(&worker_threads[i], NULL, worker_thread, (void*)thread_stats_array[i]);
        if (rc != 0) {
            fprintf(stderr, "Failed to create worker thread %d\n", i);
            exit(1);
        }
    }

    listenfd = Open_listenfd(port);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t*) &clientlen);

        // record arrival time
        struct timeval arrival_time;
        gettimeofday(&arrival_time, NULL);

        // make job with connfd and arrival time
        job_t job;
        job.connfd = connfd;
        job.arrival = arrival_time;

        enqueue(request_queue, job);

    }
    // ------------------------ cleanup area ------------------------ //

    // Clean up the server log before exiting
    destroy_log(server_log_inst);

    // Cleanup: Free worker threads array
    //todo, check if i free the treads
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
