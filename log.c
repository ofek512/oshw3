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
    int writers_active;

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
    log->writers_active = 0;

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
    // TODO: Return the full contents of the log as a dynamically allocated string
    // This function should handle concurrent access

    const char* dummy = "Log is not implemented.\n";
    int len = strlen(dummy);
    *dst = (char*)malloc(len + 1); // Allocate for caller
    if (*dst != NULL) {
        strcpy(*dst, dummy);
    }
    return len;
}

// Appends a new entry to the log (no-op stub)
void add_to_log(server_log log, const char* data, int data_len) {
    // TODO: Append the provided data to the log
    // This function should handle concurrent access
}
