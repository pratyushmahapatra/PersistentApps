#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

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
void flush(long addr);
void fence();
hrtime_t _rdtsc();

void *segmentp;
struct Node* HEAD;
int flush_count;
int fence_count;


hrtime_t _rdtsc() {
    unsigned long int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return (hrtime_t)hi << 32 | lo; 
} 


void create(int data){
    struct Node* new_node = (struct Node*)(segmentp);
    new_node->data = data;
    new_node->prev = NULL;
    new_node->next = NULL;
    HEAD = new_node;
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
    struct Node* TAIL = NULL;
    struct Node* node = HEAD;
    while (node->next != NULL){
        node = node->next;
    }
    TAIL = node;
    struct Node* new_node = TAIL;
    new_node = new_node + 1;
    new_node->data = data;
    new_node->prev = TAIL;
    new_node->next = NULL;
    TAIL->next = new_node;
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
                    break;
                }
            }
            else {
                struct Node* previous_node = node->prev;
                struct Node* next_node = node->next;
                previous_node->next = next_node;
                next_node->prev = previous_node;
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

int main(){
    /*Define persistent region - memory map*/
    hrtime_t program_start = _rdtsc();
    long addr = 0x0000010000000000;
    long size = 0x0000000001000000;
    int segment_fd;
    
    segment_fd = open("/nobackup/pratyush/linkedlist/linkedlist_volatile.txt", O_CREAT | O_RDWR, S_IRWXU);
    ftruncate(segment_fd, size);
    if (segment_fd == -1) {
        perror("open");
    }

    segmentp = mmap( (void *) addr, size, PROT_READ| PROT_WRITE, 
                    MAP_SHARED, 
    	            segment_fd,
    	            0);
    if (segmentp == (void *) -1 ) {
        perror("mmap");
    }

    /*Store HEAD in persistent mem*/
    /*Normal testing first to see if the program works*/

    create(1);
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
    close(segment_fd);
    hrtime_t program_end = _rdtsc();
    printf("Program time: %f\n", ((double)(program_end - program_start)/(3.4*1000*1000)));
    return 0;
}
