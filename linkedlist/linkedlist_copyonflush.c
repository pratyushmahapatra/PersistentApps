#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <x86intrin.h>
#include <stdbool.h>

struct Node{
    int data;
    struct Node *next;
    struct Node *prev;
};

typedef uint64_t hrtime_t;

void create(int data);
struct Node* add(struct Node *after, int data);
void append(int data); //append to tail
void delete(int data); //deletes the first element with matching data
struct Node* find_first(int data);
void findfirst_and_update(int old_data, int new_data);
void findall_and_update(int old_data, int new_data);
void reconstruct_list();
void flush(long data, long addr);
void fence();
hrtime_t rdtsc();

void *segmentp;
void *segmentp_cpy;
struct Node* INIT;
struct Node* HEAD;
struct Node* TAIL;
int flush_count;
int fence_count;
hrtime_t flush_time;
hrtime_t fence_time;


hrtime_t rdtsc() {
    unsigned long int lo, hi;
    asm volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return (hrtime_t)hi << 32 | lo; 
} 

void flush(long data, long addr) {
    hrtime_t flush_begin = rdtsc(); 
    long *addr_new = addr + (segmentp_cpy - segmentp);
    *addr_new = data;
    _mm_clflushopt(addr_new);
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
    flush(1, &INIT->data);
    INIT->prev = NULL;
    struct Node* new_node = (struct Node*)(segmentp) + 1;
    new_node->data = data;
    flush(data, &new_node->data);
    new_node->prev = NULL;
    new_node->next = NULL;
    flush(NULL, &new_node->next);
    HEAD = new_node;
    TAIL = new_node;
    INIT->next = HEAD;
    flush(HEAD, &INIT->next);
    fence();
}

struct Node* add(struct Node *after, int data){
    struct Node* node = HEAD;
    while (node->next != NULL){
        node = node->next;
    }
    struct Node* new_node = node;
    new_node = new_node + 1;
    new_node->data = data;
    new_node->next = after->next;
    struct Node* next_node = new_node->next;
    next_node->prev = new_node;
    after->next = new_node;
    new_node->prev = after;
    return new_node;
}

void append(int data){
    /*Traverse linked list to find TAIL*/
    struct Node* new_node = TAIL;
    new_node = new_node + 1;
    new_node->data = data;
    flush(data, &new_node->data);
    new_node->prev = TAIL;
    new_node->next = NULL;
    flush(NULL, &new_node->next);
    TAIL->next = new_node;
    flush(new_node, &TAIL->next);
    fence();
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
                    flush(newHEAD, HEAD);
                    INIT->next = HEAD;
                    flush(HEAD, &INIT->next);
                    fence();
                    break;
                }
                else {
                    INIT->data = 0; // No data in persistent memory
                    flush(0, &INIT->data);
                    fence();
                    break;
                }
            }
            else if(node == TAIL) {
                struct Node* previous_node = node->prev;
                previous_node->next = NULL;
                TAIL = previous_node;
                flush(previous_node->next, &previous_node->next);
                fence();
                break;
            }
            else {
                struct Node* previous_node = node->prev;
                struct Node* next_node = node->next;
                previous_node->next = next_node;
                flush(next_node, &previous_node->next);
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
            flush(new_data, &node->data);
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
            flush(new_data, &node->data);
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
    struct Node* node = segmentp_cpy;
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
    hrtime_t program_start = rdtsc();
    flush_count = 0;
    fence_count = 0;
    long addr = 0x0000010000000000;
    long size = 0x0000000010000000;
    int segment_fd, file_present, segment_fd_cpy;
    if  (access("/nobackup/pratyush/linkedlist/linkedlist.txt", F_OK) != -1) {
        printf("File exists\n");
        segment_fd = open("/nobackup/pratyush/linkedlist/linkedlist.txt", O_CREAT | O_RDWR, S_IRWXU);
        segment_fd_cpy = open("/nobackup/pratyush/linkedlist/linkedlistcopy.txt", O_CREAT | O_RDWR, S_IRWXU);
        file_present = 1;
    }
    else {
        segment_fd = open("/nobackup/pratyush/linkedlist/linkedlist.txt", O_CREAT | O_RDWR, S_IRWXU);
        ftruncate(segment_fd, size);
        if (segment_fd == -1) {
            perror("open");
        }
        segment_fd_cpy = open("/nobackup/pratyush/linkedlist/linkedlistcopy.txt", O_CREAT | O_RDWR, S_IRWXU);
        ftruncate(segment_fd_cpy, size);
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

    segmentp_cpy = mmap( (void *) addr + 2*size, size, PROT_READ| PROT_WRITE, 
                    MAP_SHARED, 
    	            segment_fd_cpy,
    	            0);
    if (segmentp_cpy == (void *) -1 ) {
        perror("mmap");
    }

    bool* bitmap = calloc(size/sizeof(struct Node*),sizeof(bool));

    /*Store HEAD in persistent mem*/
    /*Normal testing first to see if the program works*/

    if (file_present) {
        reconstruct_list();   
        return 0;
    }
    else {    
        create(1);
    }

    for (int i = 0; i < 100000; i++) {
        append(2);
        append(20);
        append(10);
        delete(20);
        delete(1);
        append(1);
    }
    print_list();

    munmap(segmentp, size);
    munmap(segmentp_cpy, size);
    close(segment_fd);
    close(segment_fd_cpy);
    hrtime_t program_end = rdtsc();
    printf("Program time: %f , Fence time: %f Flush time: %f\n", ((double)(program_end - program_start)/(3.4*1000*1000)), ((double)fence_time)/(3.4*1000*1000), ((double)flush_time)/(3.4*1000*1000));
    printf("Number of flushes: %ld, Number of fences: %ld\n", flush_count, fence_count);
    return 0;
}
