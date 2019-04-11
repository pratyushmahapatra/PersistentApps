#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <x86intrin.h>

typedef uint64_t hrtime_t;

struct Node{
    int data;
    struct Node *next;
    struct Node *prev;
};


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

void *segmentp;
struct Node* INIT;
struct Node* HEAD;
struct Node* TAIL;
int flush_count;
int fence_count;
bool *bitmap;
hrtime_t flush_time;
hrtime_t fence_time;

hrtime_t rdtsc() {
    unsigned long int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return (hrtime_t)hi << 32 | lo; 
} 

void flush(long addr) {
    hrtime_t flush_begin = rdtsc(); 
    //__asm__ __volatile__ ("clflushopt %0" : : "m"(addr));
    _mm_clflushopt(addr);
    flush_count++;
    hrtime_t flush_end = rdtsc(); 
    flush_time += (flush_end - flush_begin);
}

void fence() {
    hrtime_t fence_begin = rdtsc(); 
    __asm__ __volatile__ ("mfence");
    fence_count++;
    hrtime_t fence_end = rdtsc(); 
    fence_time += (fence_end - fence_begin);
}


void create(int data){
    INIT = (struct Node*)(segmentp);
    INIT->data = 1;
    flush(&INIT->data);
    INIT->prev = NULL;
    bitmap[0] = true;
    struct Node* new_node = (struct Node*)(segmentp) + 1;
    new_node->data = data;
    flush(&new_node->data);
    new_node->prev = NULL;
    new_node->next = NULL;
    bitmap[1] = true;
    flush(&new_node->next);
    HEAD = new_node;
    TAIL = new_node;
    INIT->next = HEAD;
    flush(&INIT->next);
    fence();
}

void append(int data){
    /*Traverse linked list to find TAIL*/
    struct Node* new_node = TAIL;
    new_node = new_node + 1;
    new_node->data = data;
    flush(&new_node->data);
    new_node->prev = TAIL;
    new_node->next = NULL;
    flush(&new_node->next);
    TAIL->next = new_node;
    flush(&TAIL->next);
    TAIL = new_node;
    int offset = (int)((struct Node*)new_node - (struct Node*)segmentp);
    bitmap[offset] = true;
    fence();
}

void delete(int data){
    struct Node *node = HEAD;
    
    while (node->next != NULL){
        if (node->data == data) {
            int offset = (int)((struct Node*)node - (struct Node*)segmentp);
            bitmap[offset] = false;
            if (node == HEAD) {
                struct Node *newHEAD = node->next;
                if (newHEAD != NULL) {
                    newHEAD->prev = NULL;
                    HEAD = newHEAD;
                    flush(HEAD);
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

void deleteNode(){
    int findNode = rand()%((int)((struct Node*)TAIL - (struct Node*)segmentp));
    if (findNode == 0)
        findNode = 1;
    struct Node* node;
    if (bitmap[findNode] == true) {
        node = (struct Node*) segmentp + findNode;
    }
    else {
        for (int i = findNode; i < ((int)((struct Node*)TAIL - (struct Node*)segmentp)); i++) {
            if (bitmap[i] == true) {
                findNode = i;
                node = (struct Node*) segmentp + findNode;
                break;
            }
        }
    }
    bitmap[findNode] = false; 
    if (node == HEAD) {
        struct Node *newHEAD = node->next;
        if (newHEAD != NULL) {
            newHEAD->prev = NULL;
            HEAD = newHEAD;
            flush(HEAD);
            INIT->next = HEAD;
            flush(&INIT->next);
            fence();
        }
        else {
            INIT->data = 0; // No data in persistent memory
            flush(&INIT->data);
            fence();
        }
    }
    else if (node == TAIL) {
        struct Node* previous_node = node->prev;
        struct Node* next_node = node->next;
        previous_node->next = next_node;
        flush(&previous_node->next);
        fence();
        TAIL = previous_node;
    }
    else {
        struct Node* previous_node = node->prev;
        struct Node* next_node = node->next;
        previous_node->next = next_node;
        flush(&previous_node->next);
        next_node->prev = previous_node;
        fence();
    }
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
    while(node->next != NULL){
        printf("%d->", node->data);
        node = node->next;
    }
    printf("%d", node->data);
    printf("\n");
}

void reconstruct_list(){
    struct Node* node = segmentp;
    if (node->data == 1){
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

int main(){
    /*Define persistent region - memory map*/
    flush_count = 0;
    fence_count = 0;
    long addr = 0x0000010000000000;
    long size = 0x0000000010000000;
    int segment_fd, file_present;
    if  (access("/nobackup/pratyush/persistent_apps/linkedlist.txt", F_OK) != -1) {
        printf("File exists\n");
        segment_fd = open("/nobackup/pratyush/persistent_apps/linkedlist.txt", O_CREAT | O_RDWR, S_IRWXU);
        file_present = 1;
    }
    else {
        segment_fd = open("/nobackup/pratyush/persistent_apps/linkedlist.txt", O_CREAT | O_RDWR, S_IRWXU);
        ftruncate(segment_fd, size);
        if (segment_fd == -1) {
            perror("open");
        }
        file_present = 0;
    }
    segmentp = mmap( (void *) addr, size, PROT_READ| PROT_WRITE, 
                    MAP_SHARED, 
    	            segment_fd,
    	            0);
    if (segmentp == (void *) -1 ) {
        perror("mmap");
    }

    bitmap = (bool *) calloc(size,sizeof(bool));
    hrtime_t program_start = rdtsc();

    /*Store HEAD in persistent mem*/
    /*Normal testing first to see if the program works*/

    if (file_present) {
        reconstruct_list();   
        return 0; 
    }
    else {    
        create(1);
    }
   for (int i = 0; i < 250000; i++) {
        append(rand());
        append(rand());
        append(rand());
        deleteNode();
        deleteNode();
    }
    //print_list();

    hrtime_t program_end = rdtsc();
    printf("Program time: %f msec , Fence time: %f msec Flush time: %f msec\n", ((double)(program_end - program_start)/(3.4*1000*1000)), ((double)fence_time)/(3.4*1000*1000), ((double)flush_time)/(3.4*1000*1000));
    printf("Number of flushes: %ld, Number of fences: %ld\n", flush_count, fence_count);
    munmap(segmentp, size);
    close(segment_fd);
    return 0;
}
