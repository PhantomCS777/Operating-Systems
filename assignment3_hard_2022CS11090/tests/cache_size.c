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
#define CACHE_SIZE_PATH "/proc/smartbdev/cache_size"
#define FLUSH_ALL_PATH "/proc/smartbdev/flush"
#define BUFFER_SIZE 4096

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
    printf("helo\n");
    FILE *fp = fopen(FLUSH_ALL_PATH, "w");
    if (fp == NULL) {
        perror("Failed to open flush file");
        return;
    }
    
    fprintf(fp, "1");
    fclose(fp);
    printf("Sent flush all command\n");
}

int get_cache_size() {
    char buffer[BUFFER_SIZE];
    FILE *fp = fopen(CACHE_SIZE_PATH, "r");
    
    if (fp == NULL) {
        perror("Failed to open cache_size file");
        return -1;
    }
    
    if (fgets(buffer, BUFFER_SIZE, fp) == NULL) {
        perror("Failed to read cache size");
        fclose(fp);
        return -1;
    }
    
    fclose(fp);
    return atoi(buffer);
}

int set_cache_size(size_t new_size) {
    FILE *fp = fopen(CACHE_SIZE_PATH, "w");
    
    if (fp == NULL) {
        perror("Failed to open cache_size file for writing");
        return -1;
    }
    
    fprintf(fp, "%zu", new_size);
    fclose(fp);
    printf("Set cache size to %zu bytes\n", new_size);
    return 0;
}

void* write_data_thread(void* arg) {
    int thread_id = *((int*)arg);
    size_t data_size = 1024 * 1024;  // 1MB
    char* write_buffer = malloc(data_size);
    
    if (write_buffer == NULL) {
        perror("Failed to allocate memory");
        return NULL;
    }
    
    // Fill buffer with identifiable pattern
    for (size_t i = 0; i < data_size; i += 100) {
        size_t remaining = data_size - i < 100 ? data_size - i : 100;
        snprintf(write_buffer + i, remaining, 
                 "Thread-%d-Block-%zu", thread_id, i/100);
    }
    
    // Open device for writing
    int fd = open(DEVICE_PATH, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open device for writing");
        free(write_buffer);
        return NULL;
    }
    
    // Write data to device
    ssize_t bytes_written = write(fd, write_buffer, data_size);
    printf("Thread %d wrote %zd bytes\n", thread_id, bytes_written);
    
    close(fd);
    free(write_buffer);
    return NULL;
}

int main() {
    pthread_t threads[4];
    int thread_ids[4];
    
    printf("Testing dynamic cache size adjustment\n");
    
    // Get initial cache size
    int initial_cache_size = get_cache_size();
    printf("Initial cache size: %d bytes\n", initial_cache_size);
    print_device_stats();
    
    // Flush any existing cache data
    flush_all_cache();
    
    // Create thread to write data with initial cache size
    printf("\nWriting data with initial cache size...\n");
    thread_ids[0] = 0;
    if (pthread_create(&threads[0], NULL, write_data_thread, &thread_ids[0]) != 0) {
        perror("Failed to create thread");
        return 1;
    }
    pthread_join(threads[0], NULL);
    
    // Check cache status
    print_device_stats();
    
    // Decrease cache size
    size_t new_size = initial_cache_size / 2;
    printf("\nDecreasing cache size from %d to %zu bytes...\n", initial_cache_size, new_size);
    set_cache_size(new_size);
    print_device_stats();
    
    // Write data with smaller cache size
    printf("\nWriting data with smaller cache size...\n");
    thread_ids[1] = 1;
    if (pthread_create(&threads[1], NULL, write_data_thread, &thread_ids[1]) != 0) {
        perror("Failed to create thread");
        return 1;
    }
    pthread_join(threads[1], NULL);
    print_device_stats();
    
    // Increase cache size
    size_t large_size = initial_cache_size * 2;
    printf("\nIncreasing cache size from %zu to %zu bytes...\n", new_size, large_size);
    set_cache_size(large_size);
    print_device_stats();
    
    // Write data with larger cache size
    printf("\nWriting data with larger cache size...\n");
    thread_ids[2] = 2;
    if (pthread_create(&threads[2], NULL, write_data_thread, &thread_ids[2]) != 0) {
        perror("Failed to create thread");
        return 1;
    }
    pthread_join(threads[2], NULL);
    print_device_stats();
    
    // Test maximum capacity by writing more data than cache size
    printf("\nTesting maximum cache capacity...\n");
    
    // Create multiple threads to potentially overflow cache
    for (int i = 0; i < 3; i++) {
        thread_ids[i] = i + 10;
        if (pthread_create(&threads[i], NULL, write_data_thread, &thread_ids[i]) != 0) {
            perror("Failed to create thread");
            return 1;
        }
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < 3; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Check final cache status
    printf("\nFinal cache status after multiple writes:\n");
    print_device_stats();
    
    // Reset cache size to initial value
    printf("\nResetting cache size to initial value: %d bytes\n", initial_cache_size);
    set_cache_size(initial_cache_size);
    print_device_stats();
    
    printf("Dynamic cache size test completed\n");
    return 0;
}