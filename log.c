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

// Acquire reader lock
void reader_lock(server_log log) {
    pthread_mutex_lock(&log->mutex);

    while (log->writer_active || log->writers_waiting > 0) {
        pthread_cond_wait(&log->cond_readers, &log->mutex);
    }

    log->readers_count++;

    // Debug sleep INSIDE critical section (per updated PDF requirement)
    if (log->sleep_time > 0) {
        sleep(log->sleep_time);
    }

    pthread_mutex_unlock(&log->mutex);
}

// Release reader lock
void reader_unlock(server_log log) {
    pthread_mutex_lock(&log->mutex);
    log->readers_count--;
    if (log->readers_count == 0) {
        pthread_cond_signal(&log->cond_writers);
    }
    pthread_mutex_unlock(&log->mutex);
}

// Acquire writer lock
void writer_lock(server_log log) {
    pthread_mutex_lock(&log->mutex);
    
    log->writers_waiting++;
    
    while (log->readers_count > 0 || log->writer_active) {
        pthread_cond_wait(&log->cond_writers, &log->mutex);
    }
    
    log->writers_waiting--;
    log->writer_active = 1;
    
    // Debug sleep INSIDE critical section (per updated PDF requirement)
    if (log->sleep_time > 0) {
        sleep(log->sleep_time);
    }
    
    pthread_mutex_unlock(&log->mutex);
}

// Release writer lock
void writer_unlock(server_log log) {
    pthread_mutex_lock(&log->mutex);
    log->writer_active = 0;
    
    // Signal writer FIRST to ensure writer priority
    pthread_cond_signal(&log->cond_writers);
    pthread_cond_broadcast(&log->cond_readers);
    pthread_mutex_unlock(&log->mutex);
}

// Returns dummy log content as string (stub)
// Must be called between reader_lock/reader_unlock
// Must be called between reader_lock/reader_unlock
int get_log(server_log log, char** dst) {
    int len = log->total_len;
    *dst = malloc(len + 1);

    if(*dst == NULL) {
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
    *ptr = '\0';

    return len;
}

// Appends a new entry to the log
// Must be called between writer_lock/writer_unlock
void add_to_log(server_log log, const char* data, int data_len) {
    LogNode *new_node = malloc(sizeof(LogNode));
    if (new_node != NULL) {
        new_node->data = malloc(data_len + 1);
        if (new_node->data != NULL) {
            memcpy(new_node->data, data, data_len);
            new_node->data[data_len] = '\0';
            new_node->data_len = data_len;
            new_node->next = NULL;
            
            // Append to linked list
            if (log->tail == NULL) {
                log->head = new_node;
                log->tail = new_node;
            } else {
                log->tail->next = new_node;
                log->tail = new_node;
            }
            
            log->total_len += data_len;
        } else {
            free(new_node);
        }
    }
}
