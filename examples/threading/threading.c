#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void delay_ms(int ms) {
    usleep(ms * 1000);
}

void* threadfunc(void* thread_param){
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    thread_func_args->thread_complete_success = false;
    delay_ms(thread_func_args->wait_to_obtain_ms);
    int rc = pthread_mutex_lock(thread_func_args->mutex);
    if (rc != 0) {
        perror("Failed to lock mutex");
        return thread_param; 
    }
    delay_ms(thread_func_args->wait_to_release_ms);
    rc = pthread_mutex_unlock(thread_func_args->mutex);
    if (rc != 0) {
        perror("Failed to unlock mutex");
        return thread_param;
    }
    thread_func_args->thread_complete_success = true;

    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex, int wait_to_obtain_ms, int wait_to_release_ms){
    struct thread_data* data = (struct thread_data*) malloc(sizeof(struct thread_data));
    if (data == NULL) {
        perror("Failed to allocate memory for thread_data");
        return false;
    }
    data->mutex = mutex;
    data->wait_to_obtain_ms = wait_to_obtain_ms;
    data->wait_to_release_ms = wait_to_release_ms;
    data->thread_complete_success = false;
    int rc = pthread_create(thread, NULL, threadfunc, (void*)data);
    if (rc != 0) {
        perror("Failed to create thread");
        free(data);
        return false;
    }
    return true;
}
