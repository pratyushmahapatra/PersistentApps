#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <x86intrin.h>
#include <stdint.h>

#ifndef MAP_SYNC
#define MAP_SYNC 0x80000
#endif

#ifndef MAP_SHARED_VALIDATE
#define MAP_SHARED_VALIDATE 0x03
#endif

typedef uint64_t hrtime_t;
void *segmentp;
struct Node* INIT;
struct Node* HEAD;
struct Node* TAIL;
int flush_count = 0;
int fence_count = 0;
hrtime_t flush_time = 0;
hrtime_t flush_time_s = 0;
hrtime_t flush_begin = 0;
int offset = 0;

hrtime_t rdtsc() {
    unsigned long int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return (hrtime_t)hi << 32 | lo; 
} 

void flush(long long addr , int size) {
    if (flush_begin == 0)
        flush_begin = rdtsc();
    hrtime_t flush_begin_s = rdtsc();
    for (int i=0; i <size; i += 64) {
    	_mm_clflushopt(addr + i);
    	flush_count++;
	}
    hrtime_t flush_end = rdtsc(); 
    flush_time_s += (flush_end - flush_begin_s);
}

void fence() {
    hrtime_t fence_begin = rdtsc(); 
    __asm__ __volatile__ ("mfence");
    fence_count++;
    hrtime_t fence_end = rdtsc(); 
    flush_time += (fence_end - flush_begin);
    flush_time_s += (fence_end - fence_begin);
    flush_begin = 0;
}


typedef struct Value Value;
struct Value{
    long long value; //8B
    //long long padding1; //8B
    //long long padding2; //8B
    //long long padding3; //8B
    //long long padding4; //8B
    //long long padding5; //8B
    //long long padding6; //8B
    //long long padding7; //8B
}; 

void append (long long value) {
    struct Value *element = segmentp + sizeof(struct Value)*offset;
    offset++;
    element->value = value;
    flush(&element, sizeof(struct Value));
    fence();
}

int main(int argc, char *argv[]){
    /*Define persistent region - memory map*/
    hrtime_t program_start;
    flush_count = 0;
    fence_count = 0;
    long addr = 0x0000010000000000;
    long size = 0x0000001000000000;

    int segment_fd, file_present;
    if  (access("/mnt/ext4-pmem22/persistent_apps/trial.txt", F_OK) != -1) {
        printf("File exists\n");
        segment_fd = open("/mnt/ext4-pmem22/persistent_apps/trial.txt", O_CREAT | O_RDWR, S_IRWXU);
        file_present = 1;
    }
    else {
        segment_fd = open("/mnt/ext4-pmem22/persistent_apps/trial.txt", O_CREAT | O_RDWR, S_IRWXU);
        ftruncate(segment_fd, size);
        if (segment_fd == -1) {
            perror("open");
        }
        file_present = 0;
    }
    segmentp = mmap( (void *) addr, size, PROT_READ| PROT_WRITE, 
                    MAP_SYNC|MAP_SHARED_VALIDATE, 
    	            segment_fd,
    	            0);
    if (segmentp == (void *) -1 ) {
        perror("mmap");
    }

    int ssIterations = 500000000;

    program_start = rdtsc();
    flush_begin = 0;
    flush_count = 0;
    fence_count = 0;
    flush_time = 0;
    flush_time_s = 0;
    for (int i = 0; i < ssIterations; i++) {
        append(rand());
    }
    

    hrtime_t program_end = rdtsc();
    printf("Program time: %f msec Flush time: %f msec Non overlapping Flush time : %f msec \n", ((double)(program_end - program_start)/(3.4*1000*1000)), ((double)flush_time)/(3.4*1000*1000), ((double)flush_time_s)/(3.4*1000*1000));
    printf("Number of flushes: %ld, Number of fences: %ld\n", flush_count, fence_count);
    munmap(segmentp, size);
    close(segment_fd);
    return 0;
}
