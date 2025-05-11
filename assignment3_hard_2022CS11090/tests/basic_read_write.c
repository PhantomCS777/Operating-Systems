#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>

#define DEVICE_PATH "/dev/smartbdev"
#define BUFFER_SIZE 128*4 // 512 bytes 

typedef struct {
    int thread_id;
    int success;  // 1 if read matched write, 0 otherwise
} thread_result_t;



void* basic_write_read_thread(void* arg) {
    thread_result_t* result = (thread_result_t*)arg;
    int thread_id = result->thread_id;
    char write_buffer[BUFFER_SIZE];
    char read_buffer[BUFFER_SIZE];
    
    // Initialize result to failure
    result->success = 0;
    
    // Create unique test data
    snprintf(write_buffer, BUFFER_SIZE, "Test data from thread %d with timestamp %ld", thread_id, (long)time(NULL));
    
    // Open device for writing
    int fd_write = open(DEVICE_PATH, O_WRONLY);
    if (fd_write < 0) {
        perror("Failed to open device for writing");
        return NULL;
    }

    // Calculate a unique offset per thread
    off_t offset = thread_id * BUFFER_SIZE;
    printf("Thread %d writing at offset %ld\n", thread_id, (long)offset);

    // Seek to the desired position
    if (lseek(fd_write, offset, SEEK_SET) == (off_t)-1) {
        perror("Failed to seek in write");
        close(fd_write);
        return NULL;
    }

    // Write data
    ssize_t bytes_written = write(fd_write, write_buffer, sizeof(write_buffer));
    // printf("Thread %d wrote %zd bytes\n", thread_id, bytes_written);
    close(fd_write);
    
    // Small delay
    usleep(100000);

    // Open device for reading
    int fd_read = open(DEVICE_PATH, O_RDONLY);
    if (fd_read < 0) {
        perror("Failed to open device for reading");
        return NULL;
    }

    // Seek to the same offset before reading
    if (lseek(fd_read, offset, SEEK_SET) == (off_t)-1) {
        perror("Failed to seek in read");
        close(fd_read);
        return NULL;
    }

    // Read data
    memset(read_buffer, 0, BUFFER_SIZE);
    ssize_t bytes_read = read(fd_read, read_buffer, BUFFER_SIZE);
    printf("Thread %d read %zd bytes\n", thread_id, bytes_read);
    close(fd_read);
    
    // Verify
    if (bytes_read == bytes_written && strcmp(write_buffer, read_buffer) == 0) {
        printf("Thread %d: PASS - Read data matches written data\n", thread_id);
        result->success = 1;
    } else {
        printf("Thread %d: FAIL - Read data does not match written data\n", thread_id);
        printf("  - Bytes written: %zd, Bytes read: %zd\n", bytes_written, bytes_read);
        if (bytes_read > 0) {
            printf("  - Read data begins with: %.200s...\n", read_buffer);
            printf("  - Expected data begins with: %.200s...\n", write_buffer);
        }
    }

    return NULL;
}

#define NUM_THREADS 4000
int main() {
    pthread_t threads[NUM_THREADS];
    thread_result_t thread_results[NUM_THREADS];
    int success_count = 0;
    
    printf("Testing basic read/write functionality with multiple threads\n");
    
    // Create multiple threads to read and write
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_results[i].thread_id = i;
        thread_results[i].success = 0;
        
        if (pthread_create(&threads[i], NULL, basic_write_read_thread, &thread_results[i]) != 0) {
            perror("Failed to create thread");
            return 1;
        }
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
        if (thread_results[i].success) {
            success_count++;
        }
    }
    
    // Print summary
    printf("\nTest Summary:\n");
    printf("  - Threads: %d\n",NUM_THREADS);
    printf("  - Successful read/write matches: %d\n", success_count);
    printf("  - Test result: %s\n", success_count == NUM_THREADS ? "PASSED" : "FAILED");
    
    return success_count == NUM_THREADS ? 0 : 1;  // Return 0 on success, 1 on failure
}