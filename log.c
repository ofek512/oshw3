#include <stdlib.h>
#include <string.h>
#include "segel.h"
#include "log.h"

typedef struct LogNode {
    char* data;
    int data_len;
    struct LogNode* next;
} LogNode;

// Opaque struct definition
struct Server_Log {
    // storage
    LogNode* head; // first entry
    LogNode* tail; // last entry
    int total_len; // sum of all entries

    // synchronization
    pthread_mutex_t mutex;
    pthread_cond_t cond_readers; //readers wait here
    pthread_cond_t cond_writers; //writers wait here

    int readers_count;
    int writers_waiting;
    int writer_active;

    int sleep_time; // debug sleep time in 
};

// Creates a new server log instance (stub)
server_log create_log(int sleep_time) {
    server_log log = malloc(sizeof(struct Server_Log));
    if (log == NULL) {
        return NULL;
    }

    log->head = NULL;
    log->tail = NULL;
    log->total_len = 0;

    pthread_mutex_init(&log->mutex, NULL);
    pthread_cond_init(&log->cond_readers, NULL);
    pthread_cond_init(&log->cond_writers, NULL);

    log->readers_count = 0;
    log->writers_waiting = 0;
    log->writer_active = 0;

    log->sleep_time = sleep_time;

    return log;

}

// Destroys and frees the log (stub)
void destroy_log(server_log log) {
    if (log == NULL) {
        return;
    }

    LogNode* current = log->head;
    while (current != NULL) {
        LogNode *next = current->next;
        free(current->data);
        free(current);
        current = next;
    }

    pthread_mutex_destroy(&log->mutex);
    pthread_cond_destroy(&log->cond_readers);
    pthread_cond_destroy(&log->cond_writers);

    free(log);
}

// Returns dummy log content as string (stub)
int get_log(server_log log, char** dst) {
    // check if there is a writer active or waiting writers
    pthread_mutex_lock(&log->mutex);

    while (log->writer_active || log->writers_waiting > 0) {
        pthread_cond_wait(&log->cond_readers, &log->mutex);
    }

    log->readers_count++;

    pthread_mutex_unlock(&log->mutex);

    int len = log->total_len;
    *dst = malloc(len + 1); // +1 for null terminator, i guess

    // if allocation failed
    if(*dst == NULL) {
        pthread_mutex_lock(&log->mutex);
        log->readers_count--;
        if (log->readers_count == 0) 
            pthread_cond_signal(&log->cond_writers);
        pthread_mutex_unlock(&log->mutex);
        return 0;
    }

    // walking on linked list and copying data
    char* ptr = *dst;
    LogNode* current = log->head;
    while(current != NULL) {
        memcpy(ptr, current->data, current->data_len);
        ptr += current->data_len;
        current = current->next;
    }
    *ptr = '\0'; // null terminate

    pthread_mutex_lock(&log->mutex);
    log->readers_count--;
    if (log->readers_count == 0) 
        pthread_cond_signal(&log->cond_writers);
    pthread_mutex_unlock(&log->mutex);

    return len;
}

// Appends a new entry to the log (no-op stub)
void add_to_log(server_log log, const char* data, int data_len) {
    
    pthread_mutex_lock(&log->mutex);
    
    log->writers_waiting++;
    
    while (log->readers_count > 0 || log->writer_active) {
        pthread_cond_wait(&log->cond_writers, &log->mutex);
    }
    
    log->writers_waiting--;
    
    log->writer_active = 1;
    
    // Debug sleep INSIDE critical section while holding lock (per PDF requirement)
    if (log->sleep_time > 0) {
        sleep(log->sleep_time);
    }
    
    LogNode *new_node = malloc(sizeof(LogNode));
    if (new_node != NULL) {
        new_node->data = malloc(data_len + 1);
        if (new_node->data != NULL) {
            memcpy(new_node->data, data, data_len);
            new_node->data[data_len] = '\0';  // Null terminate
            new_node->data_len = data_len;
            new_node->next = NULL;
            
            // Append to linked list
            if (log->tail == NULL) {
                // List is empty
                log->head = new_node;
                log->tail = new_node;
            } else {
                // Append to end
                log->tail->next = new_node;
                log->tail = new_node;
            }
            
            // Update total length
            log->total_len += data_len;
        } else {
            // data malloc failed, free the node
            free(new_node);
        }
    }
    
    log->writer_active = 0;
    
    // Signal writer FIRST to ensure writer priority
    pthread_cond_signal(&log->cond_writers);
    pthread_cond_broadcast(&log->cond_readers);
    pthread_mutex_unlock(&log->mutex);
}
