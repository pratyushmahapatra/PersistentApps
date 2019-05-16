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

typedef struct Value Value;
struct Value{
    long long value; //8B
    long long padding1; //8B
    long long padding2; //8B
    long long padding3; //8B
    long long padding4; //8B
    long long padding5; //8B
    long long padding6; //8B
}; //56B

struct Node{
    Value data;
    struct Node *next; //should be 64B boundary
    struct Node *prev;
}__attribute__((__aligned__(64)));

typedef uint64_t hrtime_t;

void create(int data);
void append(int data); //append to tail
void delete(int data); //deletes the first element with matching data
void deleteNode(); //delete random node
struct Node* find_first(int data);
void findfirst_and_update(int old_data, int new_data);
void findall_and_update(int old_data, int new_data);
void reconstruct_list();
void flush(long long addr, int size);
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
long size_ll;

hrtime_t rdtsc() {
    unsigned long int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return (hrtime_t)hi << 32 | lo; 
} 

void flush(long long addr , int size) {
    
    if ((addr < segmentp) || (addr > segmentp + size_ll)) {
        printf("Invalid flush address : %u. Segmentp: %u Segmentp+size : %u\n", addr, segmentp, segmentp + size_ll);
        exit(0);
    }

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


void create(int data){
    INIT = (struct Node*)(segmentp);
    INIT->data.value = 1;
    INIT->prev = NULL;
    struct Node* new_node = (struct Node*)(segmentp) + 1;
    new_node->data.value = data;
    new_node->prev = NULL;
    new_node->next = NULL;
    HEAD = new_node;
    TAIL = new_node;
    INIT->next = HEAD;
    flush(INIT, sizeof(INIT->data) + sizeof(&INIT->next));
    flush(new_node, sizeof(new_node->data) + sizeof(&new_node->next));
    fence();
}

void append(int data){
    //hrtime_t append_begin = rdtsc(); 
    /*Traverse linked list to find TAIL*/
    append_count += 1;
    struct Node* new_node = TAIL;
    new_node = new_node + 1;
    new_node->data.value = data;
    new_node->prev = TAIL;
    new_node->next = NULL;
    flush(new_node , sizeof(new_node->data) + sizeof(&new_node->next));
    TAIL->next = new_node;
    flush(&TAIL->next, sizeof(&TAIL->next));
    TAIL = new_node;
    fence();
    //hrtime_t append_end = rdtsc(); 
	//append_time += (append_end - append_begin);  
}

void delete(int data){
    struct Node *node = HEAD;
    
    while (node->next != NULL){
        if (node->data.value == data) {
            if (node == HEAD) {
                struct Node *newHEAD = node->next;
                if (newHEAD != NULL) {
                    newHEAD->prev = NULL;
                    HEAD = newHEAD;
                    INIT->next = HEAD;
                    flush(&INIT->next, sizeof(&INIT->next));
                    fence();
                    break;
                }
                else {
                    INIT->data.value = 0; // No data in persistent memory
                    flush(&INIT->data, sizeof(INIT->data));
                    fence();
                    break;
                }
            }
            else if (node == TAIL) {
                struct Node* previous_node = node->prev;
                struct Node* next_node = node->next;
                previous_node->next = next_node;
                flush(&previous_node->next, sizeof(&previous_node->next));
                fence();
                TAIL = previous_node;
                break;
            }
            else {
                struct Node* previous_node = node->prev;
                struct Node* next_node = node->next;
                previous_node->next = next_node;
                flush(&previous_node->next, sizeof(&previous_node->next));
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
        flush(&INIT->next, sizeof(&INIT->next));
        fence();
    }
    else {
        INIT->data.value = 0; // No data in persistent memory
        flush(&INIT->data, sizeof(INIT->data));
        fence();
    }
    //hrtime_t del_end = rdtsc();
	//del_time += (del_end - del_begin);  
}

struct Node* find_first(int data){
    struct Node* node = HEAD;
    while(node->data.value != data){
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
        if (node->data.value == old_data){
            node->data.value = new_data;
            flush(&node->data, sizeof(node->data));
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
        if (node->data.value == old_data){
            node->data.value = new_data;
            flush(&node->data, sizeof(node->data));
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
        printf("%d->", node->data.value);
        node = node->next;
    }
    printf("%d", node->data.value);
    printf("\n");
}

void reconstruct_list(){
    struct Node* node = segmentp;
    if (node->data.value == 1) {
        printf("Valid linked list\n");
        INIT = node;
        INIT->prev = NULL;
        INIT->data.value = 1;
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
        printf("Invalid linked list node->data=%d\n", node->data.value);
        create(1);
    }
}

int main(int argc, char *argv[]){
    /*Define persistent region - memory map*/
    printf("Size of struct node : %d\n", sizeof(struct Node));
    hrtime_t program_start;
    flush_count = 0;
    fence_count = 0;
	del_count = 0;
	append_count = 0;
    long addr = 0x0000010000000000;
    size_ll =   0x0000004000000000;
	int ratio = atoi(argv[1]);


    int segment_fd, file_present;
    if  (access("/mnt/ext4-pmem22/persistent_apps/linkedlist_p.txt", F_OK) != -1) {
        printf("File exists\n");
        segment_fd = open("/mnt/ext4-pmem22/persistent_apps/linkedlist_p.txt", O_CREAT | O_RDWR, S_IRWXU);
        file_present = 1;
    }
    else {
        segment_fd = open("/mnt/ext4-pmem22/persistent_apps/linkedlist_p.txt", O_CREAT | O_RDWR, S_IRWXU);
        ftruncate(segment_fd, size_ll);
        if (segment_fd == -1) {
            perror("open");
        }
        file_present = 0;
    }
    segmentp = mmap( (void *) addr, size_ll, PROT_READ| PROT_WRITE, 
                    MAP_SYNC|MAP_SHARED_VALIDATE, 
    	            segment_fd,
    	            0);
    if (segmentp == (void *) -1 ) {
        perror("mmap");
    }

    
    //bitmap = (bool *) calloc(size,sizeof(bool));

    /*Store HEAD in persistent mem*/
    /*Normal testing first to see if the program works*/
    int initIterations = 200000000;
    int ssIterations = (100000000)/(ratio + 1);

    for (int i = 0; i < initIterations + ssIterations; i++) {
        struct Node* node = segmentp + sizeof(struct Node)*i;
        node->data.value = 0;
    }

    if (file_present) {
        reconstruct_list();   
    }
    else {    
        create(1);
        for (int i = 0; i < initIterations; i++) {
            append(rand());
        }
        program_start = rdtsc();
        flush_begin = 0;
        flush_count = 0;
        fence_count = 0;
        flush_time = 0;
        flush_time_s = 0;
        del_time = 0;
        append_time = 0;
        for (int i = 0; i < ssIterations; i++) {
            for (int j = 0; j < ratio; j++)
                append(rand());
            deleteNode();
        }
    }

    hrtime_t program_end = rdtsc();
    printf("Program time: %f msec Flush time: %f msec Non overlapping Flush time : %f msec \n", ((double)(program_end - program_start)/(3.4*1000*1000)), ((double)flush_time)/(3.4*1000*1000), ((double)flush_time_s)/(3.4*1000*1000));
    printf("Number of flushes: %ld, Number of fences: %ld\n", flush_count, fence_count);
    munmap(segmentp, size_ll);
    close(segment_fd);
    return 0;
}
