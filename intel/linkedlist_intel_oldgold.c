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


struct Node{
    int data;
    struct Node *next;
    struct Node *prev;
};

typedef uint64_t hrtime_t;

void create(int data);
void append(int data); //append to tail
void delete(int data); //deletes the first element with matching data
void deleteNode(); //delete random node
struct Node* find_first(int data);
void findfirst_and_update(int old_data, int new_data);
void findall_and_update(int old_data, int new_data);
void reconstruct_list();
void flush(long addr);
void fence();
hrtime_t rdtsc();


void *segmentp;
struct Node* INIT;
struct Node* HEAD;
struct Node* TAIL;
int flush_count;
int fence_count;
int del_count;
int append_count;
//bool *bitmap;
hrtime_t flush_time;
hrtime_t flush_time_s;
hrtime_t del_time;
hrtime_t append_time;
hrtime_t flush_begin;

hrtime_t rdtsc() {
    unsigned long int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return (hrtime_t)hi << 32 | lo; 
} 

void flush(long addr) {
    if (flush_begin == 0)
        flush_begin = rdtsc();
     hrtime_t flush_begin_s = rdtsc();
    _mm_clflushopt(addr);
    //flush_count++;
    hrtime_t flush_end = rdtsc(); 
    flush_time_s += (flush_end - flush_begin_s);
}

void fence() {
    hrtime_t fence_begin = rdtsc(); 
    __asm__ __volatile__ ("mfence");
    //fence_count++;
    hrtime_t fence_end = rdtsc(); 
    flush_time += (fence_end - flush_begin);
    flush_time_s += (fence_end - fence_begin);
    flush_begin = 0;
}


void create(int data){
    INIT = (struct Node*)(segmentp);
    INIT->data = 1;
    flush(&INIT->data);
    INIT->prev = NULL;
    struct Node* new_node = (struct Node*)(segmentp) + 1;
    new_node->data = data;
    flush(&new_node->data);
    new_node->prev = NULL;
    new_node->next = NULL;
    flush(&new_node->next);
    HEAD = new_node;
    TAIL = new_node;
    INIT->next = HEAD;
    flush(&INIT->next);
    fence();
}

void append(int data){
    //hrtime_t append_begin = rdtsc(); 
    /*Traverse linked list to find TAIL*/
    append_count += 1;
    struct Node* new_node = TAIL;
    new_node = new_node + 1;
    new_node->data = data;
    flush(&new_node->data);
    new_node->prev = TAIL;
    new_node->next = NULL;
    flush(&new_node->next);
    TAIL->next = new_node;
    if (rand() % 100 == 0) {
        TAIL->next = TAIL;
        printf("About to sleep\n");
        sleep(20);
    }
    flush(&TAIL->next);
    TAIL = new_node;
    fence();
    //hrtime_t append_end = rdtsc(); 
	//append_time += (append_end - append_begin);  
}

void delete(int data){
    struct Node *node = HEAD;
    
    while (node->next != NULL){
        if (node->data == data) {
            if (node == HEAD) {
                struct Node *newHEAD = node->next;
                if (newHEAD != NULL) {
                    newHEAD->prev = NULL;
                    HEAD = newHEAD;
                    INIT->next = HEAD;
                    flush(&INIT->next);
                    fence();
                    break;
                }
                else {
                    INIT->data = 0; // No data in persistent memory
                    flush(&INIT->data);
                    fence();
                    break;
                }
            }
            else if (node == TAIL) {
                struct Node* previous_node = node->prev;
                struct Node* next_node = node->next;
                previous_node->next = next_node;
                flush(&previous_node->next);
                fence();
                TAIL = previous_node;
                break;
            }
            else {
                struct Node* previous_node = node->prev;
                struct Node* next_node = node->next;
                previous_node->next = next_node;
                flush(&previous_node->next);
                next_node->prev = previous_node;
                fence();
                break;
            }
        }
        else {
            node = node->next;
        }
    }
}

void deleteNode() {
    del_count += 1;
    //hrtime_t del_begin = rdtsc(); 
    struct Node* node = HEAD;
    struct Node *newHEAD = node->next;
    if (newHEAD != NULL) {
        newHEAD->prev = NULL;
        HEAD = newHEAD;
        INIT->next = HEAD;
        flush(&INIT->next);
        fence();
    }
    else {
        INIT->data = 0; // No data in persistent memory
        flush(&INIT->data);
        fence();
    }
    //hrtime_t del_end = rdtsc();
	//del_time += (del_end - del_begin);  
}

struct Node* find_first(int data){
    struct Node* node = HEAD;
    while(node->data != data){
        if(node->next != NULL){
            node = node->next;
        }
        else {
            return NULL;
        }
    }
    return node;     
}

void findfirst_and_update(int old_data, int new_data){
    struct Node* node = HEAD;
    while(node->next != NULL){
        if (node->data == old_data){
            node->data = new_data;
            flush(&node->data);
            fence();
            return;
        }
        else{
            node = node->next;
        }
    }
}


void findall_and_update(int old_data, int new_data){
    struct Node* node = HEAD;
    while(node->next != NULL){
        if (node->data == old_data){
            node->data = new_data;
            flush(&node->data);
            fence();
        }
        else{
            node = node->next;
        }
    }
}

void print_list(){
    struct Node* node = HEAD;
    while(node->next != NULL) {
        printf("%d->", node->data);
        node = node->next;
    }
    printf("%d", node->data);
    printf("\n");
}

void reconstruct_list(){
    struct Node* node = segmentp;
    if (node->data == 1) {
        printf("Valid linked list\n");
        INIT = node;
        INIT->prev = NULL;
        INIT->data = 1;
        HEAD = node->next;
        HEAD->prev = NULL;
        node = HEAD;
        while (node->next != NULL) {
            struct Node* prev_node = node;
            struct Node* next_node = node->next;
            next_node->prev = prev_node;
            node = node->next;
        }
        printf("Linked list reconstructed\n");
        print_list();
    }
    else {
        printf("Invalid linked list node->data=%d\n", node->data);
        create(1);
    }
}

int main(int argc, char *argv[]){
    /*Define persistent region - memory map*/
    hrtime_t program_start;
    flush_count = 0;
    fence_count = 0;
	del_count = 0;
	append_count = 0;
    long addr = 0x0000010000000000;
    long size = 0x0000000100000000;
	int ratio = atoi(argv[1]);

    int segment_fd, file_present;
    if  (access("/mnt/ext4-pmem22/persistent_apps/linkedlist_p.txt", F_OK) != -1) {
        printf("File exists\n");
        segment_fd = open("/mnt/ext4-pmem22/persistent_apps/linkedlist_p.txt", O_CREAT | O_RDWR, S_IRWXU);
        file_present = 1;
    }
    else {
        segment_fd = open("/mnt/ext4-pmem22/persistent_apps/linkedlist_p.txt", O_CREAT | O_RDWR, S_IRWXU);
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

    //bitmap = (bool *) calloc(size,sizeof(bool));

    /*Store HEAD in persistent mem*/
    /*Normal testing first to see if the program works*/
    int SSiter = (100000000)/(ratio + 1);
    if (file_present) {
        reconstruct_list();   
    }
    else {    
        create(1);
        for (int i = 0; i < 10000000; i++) {
            append(rand());
        }
        program_start = rdtsc();
        flush_count = 0;
        fence_count = 0;
        flush_time = 0;
        flush_time_s = 0;
        del_time = 0;
        append_time = 0;
        for (int i = 0; i < SSiter; i++) {
            for (int j = 0; j < ratio; j++)
                append(rand());
            deleteNode();
        }
    }

    hrtime_t program_end = rdtsc();
    //printf("Program time: %f msec Flush time: %f msec Non overlapping Flush time : %f msec \n", ((double)(program_end - program_start)/(3.4*1000*1000)), ((double)flush_time)/(3.4*1000*1000), ((double)flush_time_s)/(3.4*1000*1000));
    //printf("Number of flushes: %ld, Number of fences: %ld Num deletes : %ld Num appends : %ld\n", flush_count, fence_count, del_count, append_count);
    munmap(segmentp, size);
    close(segment_fd);
    return 0;
}