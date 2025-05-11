#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <stdint.h>
#define DEVICE_PATH "/dev/smartbdev"
#define WRITE_COUNT 10      // Number of sequential writes per thread
#define BUFFER_SIZE 512     // Size of each write
#define NUM_THREADS 4       // Number of concurrent threads
#define TEST_ITERATIONS 3   // How many times to read after each write

typedef struct {
    int thread_id;
    int success;           
} thread_result_t;

// Get current time in microseconds for timestamps
uint64_t get_timestamp_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec) * 1000000 + (uint64_t)(tv.tv_usec);
}

void* fifo_write_verify_thread(void* arg) {
    thread_result_t* result = (thread_result_t*)arg;
    int thread_id = result->thread_id;
    char write_buffer[BUFFER_SIZE];
    char read_buffer[BUFFER_SIZE];
    
    // Default to success until a verification fails
    result->success = 1;
    
    // Each thread gets its own offset to avoid interference between threads
    // But all writes from the same thread go to this same location
    off_t thread_offset = thread_id * BUFFER_SIZE;
    
    printf("Thread %d starting FIFO write test at offset %ld\n", thread_id, (long)thread_offset);
    
    // Open device for both reading and writing
    int fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        result->success = 0;
        return NULL;
    }
    
    // Perform a series of writes to the same location and verify each time
    for (int write_num = 0; write_num < WRITE_COUNT; write_num++) {
        // Create unique, identifiable test data for this write
        uint64_t timestamp = get_timestamp_us();
        snprintf(write_buffer, BUFFER_SIZE, "T%d-W%d: Thread %d Write #%d with timestamp %lu", thread_id, write_num, thread_id, write_num, timestamp);
        
        // Fill the rest of the buffer with a pattern including the write number
        for (int j = strlen(write_buffer); j < BUFFER_SIZE - 1; j++) {
            write_buffer[j] = 'A' + (write_num % 26);
        }
        write_buffer[BUFFER_SIZE - 1] = '\0';
        
        // Seek to thread's position and write
        if (lseek(fd, thread_offset, SEEK_SET) == (off_t)-1) {
            perror("Failed to seek for write");
            result->success = 0;
            break;
        }
        
        printf("Thread %d: Writing #%d: %.30s...\n", thread_id, write_num, write_buffer);
        
        ssize_t bytes_written = write(fd, write_buffer, BUFFER_SIZE);
        if (bytes_written != BUFFER_SIZE) {
            printf("Thread %d: Write #%d failed - wrote %zd bytes instead of %d (error: %s)\n", 
                   thread_id, write_num, bytes_written, BUFFER_SIZE, strerror(errno));
            result->success = 0;
            break;
        }
        
     
        // fsync(fd);
        
        
        for (int read_attempt = 0; read_attempt < TEST_ITERATIONS; read_attempt++) {
           
            // usleep(10000); 
            
            // Seek back to beginning of thread's region
            if (lseek(fd, thread_offset, SEEK_SET) == (off_t)-1) {
                perror("Failed to seek for read");
                result->success = 0;
                break;
            }
            
            // Read data
            memset(read_buffer, 0, BUFFER_SIZE);
            ssize_t bytes_read = read(fd, read_buffer, BUFFER_SIZE);
            
            if (bytes_read != BUFFER_SIZE) {
                printf("Thread %d: Read after write #%d failed - read %zd bytes instead of %d\n", 
                       thread_id, write_num, bytes_read, BUFFER_SIZE);
                result->success = 0;
                continue;
            }
            
            // Compare with what we just wrote - should match exactly
            if (strcmp(write_buffer, read_buffer) != 0) {
                printf("Thread %d: FAIL - Read after write #%d does not match!\n", thread_id, write_num);
                printf("  Expected: %.50s...\n", write_buffer);
                printf("  Actual:   %.50s...\n", read_buffer);
                result->success = 0;
            } else {
                printf("Thread %d: PASS - Read #%d correctly returned latest write\n", 
                       thread_id, read_attempt);
            }
        }
        
        if (!result->success) {
            break;
        }
    }
    
    close(fd);
    
    printf("Thread %d: Overall result: %s\n", thread_id, result->success ? "PASS - All writes verified in FIFO order" : "FAIL - Some writes did not verify correctly");
    
    return NULL;
}

int main() {
    pthread_t threads[NUM_THREADS];
    thread_result_t thread_results[NUM_THREADS];
    int success_count = 0;
    
    printf("=== Testing FIFO write ordering with %d threads, %d sequential writes each ===\n", 
           NUM_THREADS, WRITE_COUNT);
    printf("=== Each thread writes to the same location multiple times ===\n");
    printf("=== Verifying that each thread always sees its most recent write ===\n");
    
    // Create threads
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_results[i].thread_id = i;
        thread_results[i].success = 0;
        
        if (pthread_create(&threads[i], NULL, fifo_write_verify_thread, &thread_results[i]) != 0) {
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
    printf("\n=== Test Summary ===\n");
    printf("Threads: %d\n", NUM_THREADS);
    printf("Writes per thread: %d\n", WRITE_COUNT);
    printf("Read attempts after each write: %d\n", TEST_ITERATIONS);
    printf("Successful FIFO verifications: %d/%d\n", success_count, NUM_THREADS);
    printf("Test result: %s\n", success_count == NUM_THREADS ? "PASSED" : "FAILED");
    
    return success_count == NUM_THREADS ? 0 : 1;
}