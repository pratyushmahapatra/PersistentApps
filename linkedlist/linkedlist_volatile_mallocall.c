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
static inline hrtime_t _rdtsc();

long node_number;

struct Node** node;
struct Node* HEAD;


static inline hrtime_t _rdtsc() {
    unsigned long int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return (hrtime_t)hi << 32 | lo; 
} 


void create(int data){
    HEAD = node[node_number];
    HEAD->data = data;
    HEAD->prev = NULL;
    HEAD->next = NULL;
    node_number++;
}

void append(int data){
    struct Node* TAIL = NULL;
    struct Node* node_t = HEAD;
    while (node_t->next != NULL){
        node_t = node_t->next;
    }
    TAIL = node_t;
    struct Node* new_node = node[node_number];
    new_node->data = data;
    new_node->prev = TAIL;
    new_node->next = NULL;
    TAIL->next = new_node;
    node_number++;
}

void delete(int data){
    struct Node *node_t = HEAD;
    
    while (node_t->next != NULL){
        if (node_t->data == data) {
            if (node_t == HEAD) {
                struct Node *newHEAD = node_t->next;
                if (newHEAD != NULL) {
                    newHEAD->prev = NULL;
                    HEAD = newHEAD;
                    break;
                }
            }
            else {
                struct Node* previous_node = node_t->prev;
                struct Node* next_node = node_t->next;
                previous_node->next = next_node;
                next_node->prev = previous_node;
                break;
            }
        }
        else {
            node_t = node_t->next;
        }
    }
}

struct Node* find_first(int data){
    struct Node* node_t = HEAD;
    while(node_t->data != data){
        if(node_t->next != NULL){
            node_t = node_t->next;
        }
        else {
            return NULL;
        }
    }
    return node_t;     
}

void findfirst_and_update(int old_data, int new_data){
    struct Node* node_t = HEAD;
    while(node_t->next != NULL){
        if (node_t->data == old_data){
            node_t->data = new_data;
            return;
        }
        else{
            node_t = node_t->next;
        }
    }
}


void findall_and_update(int old_data, int new_data){
    struct Node* node_t = HEAD;
    while(node_t->next != NULL){
        if (node_t->data == old_data){
            node_t->data = new_data;
        }
        else{
            node_t = node_t->next;
        }
    }
}

void print_list(){
    struct Node* node_t = HEAD;
    while(node_t->next != NULL){
        printf("%d->", node_t->data);
        node_t = node_t->next;
    }
    printf("%d", node_t->data);
    printf("\n");
}


int main() {
    hrtime_t program_start = _rdtsc();
    node = (struct Node**)malloc(1000000*sizeof(struct Node*));
    for (int i = 0; i < 1000000; i++) {
        node[i] = (struct Node*)malloc(sizeof(struct Node*));
    }
    hrtime_t malloc_end = _rdtsc();
    node_number = 0;
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
    hrtime_t program_end = _rdtsc();
    
    printf("Time taken to malloc: %f, Program time: %f\n", ((double)(malloc_end - program_start)/(3.4*1000*1000)), ((double)(program_end - program_start)/(3.4*1000*1000)) );

    return 0;
}
