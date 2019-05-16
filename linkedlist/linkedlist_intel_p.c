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

#define DATA 0
#define NEXT 1
#define PREV 2

struct Node{
    int data;
    struct Node *next;
    struct Node *prev;
    long offset;
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
void flush(int op, long offset, long value);
void fence();
hrtime_t rdtsc();

void *segmentp;
struct Node* INIT;
struct Node* HEAD;
struct Node* TAIL;
long offset;
int flush_count;
int fence_count;
int del_count;
int append_count;
//bool *bitmap;
hrtime_t flush_time;
hrtime_t flush_time_s;
//hrtime_t fence_time;
hrtime_t del_time;
hrtime_t append_time;
hrtime_t flush_begin;

hrtime_t rdtsc() {
    unsigned long int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return (hrtime_t)hi << 32 | lo; 
} 

void flush(int op, long offset, long value) {
    struct Node* node = (struct Node*)(segmentp) + offset;
    long addr;
    if (op == DATA) {
        node->data = value;
        addr = &node->data;
    } else if(op == NEXT) {
        if (value == NULL)
            node->next == NULL;
        else {
        struct Node* next_node = (struct Node*)(segmentp) + value;
        node->next = next_node;
        }
        addr = &node->next;
    } else if(op == PREV) {
        if (value == NULL)
            node->prev == NULL;
        else {
            struct Node* prev_node = (struct Node*)(segmentp) + value;
            node->prev = prev_node;
        }
        addr = &node->prev;
    }
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
    INIT->offset = offset;
    flush(DATA, INIT->offset, INIT->data);
    INIT->prev = NULL;
    flush(PREV, INIT->offset, NULL);
    struct Node* new_node = (struct Node*)(malloc(sizeof(struct Node)));
    offset++;
    new_node->offset = offset;
    new_node->data = data;
    flush(DATA, new_node->offset, new_node->data);
    new_node->next = NULL;
    flush(NEXT, new_node->offset, new_node->next);
    new_node->prev = NULL;
    flush(PREV, new_node->offset, NULL);
    HEAD = new_node;
    TAIL = new_node;
    INIT->next = HEAD;
    flush(NEXT, INIT->offset, HEAD->offset);
    fence();
}

void append(int data){
    //hrtime_t append_begin = rdtsc(); 
    /*Traverse linked list to find TAIL*/
    append_count += 1;
    struct Node* new_node = (struct Node*)(malloc(sizeof(struct Node)));
    offset++;
    new_node->offset = offset;
    new_node->data = data;
    flush(DATA, new_node->offset, new_node->data);
    new_node->prev = TAIL;
    new_node->next = NULL;
    flush(PREV, new_node->offset, TAIL->offset);
    flush(NEXT, new_node->offset, NULL);
    TAIL->next = new_node;
    flush(NEXT, TAIL->offset, new_node->offset);
    TAIL = new_node;
    fence();
    //hrtime_t append_end = rdtsc(); 
	//append_time += (append_end - append_begin);  
}

void delete(int data){
    struct Node *node = HEAD;
    while (node->next != NULL) {
        if (node->data == data) {
            if (node == HEAD) {
                struct Node *newHEAD = node->next;
                if (newHEAD != NULL) {
                    newHEAD->prev = NULL;
                    flush(PREV, newHEAD->offset, NULL);
                    HEAD = newHEAD;
                    INIT->next = HEAD;
                    flush(NEXT, INIT->offset, HEAD->offset);
                    fence();
                    free(node);
                    break;
                }
                else {
                    INIT->data = 0; // No data in persistent memory
                    flush(DATA, INIT->offset, INIT->data);
                    fence();
                    free(node);
                    break;
                }
            }
            else if (node == TAIL) {
                struct Node* previous_node = node->prev;
                struct Node* next_node = node->next;
                previous_node->next = next_node;
                flush(NEXT, previous_node->offset, next_node->offset);
                fence();
                TAIL = previous_node;
                free(node);
                break;
            }
            else {
                struct Node* previous_node = node->prev;
                struct Node* next_node = node->next;
                previous_node->next = next_node;
                flush(NEXT, previous_node->offset, next_node->offset);
                next_node->prev = previous_node;
                flush(PREV, next_node->offset, previous_node->offset);
                fence();
                free(node);
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
        flush(PREV, newHEAD->offset, NULL);
        HEAD = newHEAD;
        INIT->next = HEAD;
        flush(NEXT, INIT->offset, HEAD->offset);
        fence();
    }
    else {
        INIT->data = 0; // No data in persistent memory
        flush(DATA, INIT->offset, INIT->data);
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
            //flush(&node->data);
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
            //flush(&node->data);
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
    offset = 0;
    hrtime_t program_start;
    flush_count = 0;
    fence_count = 0;
    del_count = 0;
    append_count = 0;
    long addr = 0x0000010000000000;
    long size = 0x0000000100000000;
	int ratio = atoi(argv[1]);

    int segment_fd, file_present;
    if  (access("/mnt/ext4-pmem22/persistent_apps/linkedlist.txt", F_OK) != -1) {
        printf("File exists\n");
        segment_fd = open("/mnt/ext4-pmem22/persistent_apps/linkedlist.txt", O_CREAT | O_RDWR, S_IRWXU);
        file_present = 1;
    }
    else {
        segment_fd = open("/mnt/ext4-pmem22/persistent_apps/linkedlist.txt", O_CREAT | O_RDWR, S_IRWXU);
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
        return 0;
    }
    else {    
        create(1);

        for (int i = 0; i < 10000000; i++) {
            append(rand());
        }
        program_start = rdtsc();
        flush_count = 0;
        fence_count = 0;
        //fence_time = 0;
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
    //print_list();

    hrtime_t program_end = rdtsc();
    printf("Program time: %f msec Flush time: %f msec Non overlapping Flush time : %f msec \n", ((double)(program_end - program_start)/(3.4*1000*1000)), ((double)flush_time)/(3.4*1000*1000), ((double)flush_time_s)/(3.4*1000*1000));
    printf("Number of flushes: %ld, Number of fences: %ld Num deletes : %ld Num appends : %ld\n", flush_count, fence_count, del_count, append_count);
    munmap(segmentp, size);
    close(segment_fd);
    return 0;
}
