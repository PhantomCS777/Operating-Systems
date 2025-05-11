#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>

#define DEVICE_PATH "/dev/smartbdev"
#define STATS_PATH "/proc/smartbdev/stats"
#define FLUSH_ALL_PATH "/proc/smartbdev/flush"
#define BUFFER_SIZE 4096
#define DATA_SIZE (16* 1024)  // 16 kb 

typedef struct {
    int thread_id;
    size_t data_size;
} thread_args_t;

void print_device_stats() {
    char buffer[BUFFER_SIZE];
    FILE *fp = fopen(STATS_PATH, "r");
    
    if (fp == NULL) {
        perror("Failed to open stats file");
        return;
    }
    
    printf("\n----- Device Statistics -----\n");
    while (fgets(buffer, BUFFER_SIZE, fp) != NULL) {
        printf("%s", buffer);
    }
    printf("----------------------------\n\n");
    
    fclose(fp);
}

void flush_all_cache() {
    FILE *fp = fopen(FLUSH_ALL_PATH, "w");
    if (fp == NULL) {
        perror("Failed to open flush file");
        return;
    }
    
    fprintf(fp, "1");
    fclose(fp);
    printf("Sent flush all command\n");
}

void* cache_write_thread(void* arg) {
    thread_args_t* args = (thread_args_t*)arg;
    char* write_buffer = malloc(args->data_size);
    
    if (write_buffer == NULL) {
        perror("Failed to allocate memory");
        return NULL;
    }
    
    // Fill buffer with identifiable pattern
    for (size_t i = 0; i < args->data_size; i += 100) {
        size_t remaining = args->data_size - i < 100 ? args->data_size - i : 100;
        snprintf(write_buffer + i, remaining, 
                 "Thread-%d-Block-%zu", args->thread_id, i/100);
    }
    
    // Open device for writing
    int fd = open(DEVICE_PATH, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open device for writing");
        free(write_buffer);
        return NULL;
    }
    
    // Write data to device
    ssize_t bytes_written = write(fd, write_buffer, args->data_size);
    printf("Thread %d wrote %zd bytes\n", args->thread_id, bytes_written);
    
    close(fd);
    free(write_buffer);
    return NULL;
}

int main() {
    pthread_t threads[4];
    thread_args_t thread_args[4];
    
    printf("Testing write cache functionality\n");
    
    // Try to flush the cache at start
    flush_all_cache();
    
    // Check initial cache status
    print_device_stats();
    
    // Create threads to write data
    for (int i = 0; i < 4; i++) {
        thread_args[i].thread_id = i;
        thread_args[i].data_size = DATA_SIZE;
        
        if (pthread_create(&threads[i], NULL, cache_write_thread, &thread_args[i]) != 0) {
            perror("Failed to create thread");
            return 1;
        }
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Check cache status after writes
    printf("\nCache status after writes:\n");
    print_device_stats();
    
    // Flush all cached data
    flush_all_cache();
    
    // Check cache status after flush
    printf("\nCache status after flush:\n");
    print_device_stats();
    
    printf("Write cache test completed\n");
    return 0;
}