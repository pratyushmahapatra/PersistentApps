#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <x86intrin.h>

#define THRESHOLD 9

struct Node{
    int n;
    long key[THRESHOLD];
    void ptr[THRESHOLD + 1];
    bool leaf;
};

struct Data{
    char val[128];
};

void create(long key, void data);
void insert(long key, void data); 
void delete(long key);
void search(long key);


void reconstruct_list();
void flush(long addr);
void fence();

void *nodep;
void *datap;

int data_nodes;
int num_nodes;
struct Node* INIT;
struct Node* ROOT;
int flush_count;
int fence_count;

int leftNodeExists(void *path, int position) {
    struct Node* node = path[position];
    long lowestKey = node->key[0];
    struct Node* parent = path[position - 1];
    //search for position of lowest key
    int indexKey = 0;
    int flag = 0;
    for (int i = 0 ; i < parent->n; i++) {
        if ( lowestKey <= parent->key[i]) {
            indexKey = i;
            flag = 1;
            break;
        }
    }
    if (flag == 0)
        indexKey = parent->n;

    //Now look at left child
    if (indexKey == 0)
        return 0;

    struct Node* leftNode = parent->ptr[indexKey - 1];
    return leftNode->n;
} 

int rightNodeExists(void *path, int position) {
    struct Node* node = path[position];
    long lowestKey = node->key[0];
    struct Node* parent = path[position - 1];
    //search for position of lowest key
    int indexKey = 0;
    int flag = 0;
    for (int i = 0 ; i < parent->n; i++) {
        if ( lowestKey <= parent->key[i]) {
            indexKey = i;
            flag = 1;
            break;
        }
    }
    if (flag == 0)
        indexKey = parent->n;

    //Now look at left child
    if (indexKey == parent->n)
        return 0;

    struct Node* rightNode = parent->ptr[indexKey + 1];
    return rightNode->n;
} 

void transferLeftNode(void *path, int position) {
    struct Node* node = path[position];
    long lowestKey = node->key[0];
    struct Node* parent = path[position - 1];
    //search for position of lowest key
    int indexKey = 0;
    int flag = 0;
    for (int i = 0 ; i < parent->n; i++) {
        if ( lowestKey <= parent->key[i]) {
            indexKey = i;
            flag = 1;
            break;
        }
    }
    if (flag == 0)
        indexKey = parent->n;

    struct Node* leftNode = parent->ptr[indexKey - 1];
    long rightMostKey = leftNode->key[leftNode->n - 1];
    void rightPtr = leftNode->ptr[leftNode->n - 1];
    leftNode->ptr[leftNode->n - 1] = leftNode->ptr[leftNode->n];
    leftNode->n--;

    node->ptr[node->n + 1] = node->ptr[node->n];
    for (int i = node->n; i > 0; i--) {
        node->key[i] = node->key[i - 1];
        node->ptr[i] = node->ptr[i - 1];
    }
    node->key[0] = rightMostKey;
    node->ptr[0] = rightPtr;
    parent->key[indexKey] = rightMostKey;
}
void mergeLeftNode(void *path, int position) {
}
void transferRightNode(void *path, int position) {
    struct Node* node = path[position];
    long lowestKey = node->key[0];
    struct Node* parent = path[position - 1];
    //search for position of lowest key
    int indexKey = 0;
    int flag = 0;
    for (int i = 0 ; i < parent->n; i++) {
        if ( lowestKey <= parent->key[i]) {
            indexKey = i;
            flag = 1;
            break;
        }
    }
    if (flag == 0)
        indexKey = parent->n;

    struct Node* rightNode = parent->ptr[indexKey + 1];
    long leftMostKey = rightNode->key[0];
    void leftPtr = rightNode->ptr[0];
    //`leftNode->ptr[leftNode->n - 1] = leftNode->ptr[leftNode->n];
    //`leftNode->n--;

    //`node->ptr[node->n + 1] = node->ptr[node->n];
    //`for (int i = node->n; i > 0; i--) {
    //`    node->key[i] = node->key[i - 1];
    //`    node->ptr[i] = node->ptr[i - 1];
    //`}
    //`node->key[0] = leftMostKey;
    //`node->ptr[0] = leftPtr;
    //`parent->key[indexKey] = leftMostKey;
}
void mergeRightNode(void *path, int position) {
}

void flush(long addr) {
    _mm_clflushopt(addr);
    flush_count++;
}

void fence() {
    __asm__ __volatile__ ("mfence");
    fence_count++;
}

//We need a location for data to be stored
void create(long key, void data){
    INIT = (struct Node*)(segmentp);
    num_nodes = 1;
    INIT->n = 1;
    flush(&INIT->n);
    INIT->leaf = false;
    struct Node* new_node = (struct Node*)(segmentp) + num_nodes;
    num_nodes++;
    ROOT = new_node;
    ROOT->n = 1;
    ROOT->key[0] = key;
    new_node = (struct Node*)(ROOT) + 1;
    ROOT->ptr[0] = new_node;
    ROOT->leaf = false;
    new_node->leaf = true;
    new_node->n = 1;
    new_node->key[0] = key;
    struct Data* new_data = (struct Data*)(datap);
    data_nodes = 1; 
    new_node->ptr[0] = new_data;
    flush(&new_node->ptr); 
    new_data->val = (char *) data;
    flush(&new_data->val); 
}

void insert(long key, void data){
    struct Node* node = ROOT;
    int treeLen = 0;
    void pathTaken[1024];
    pathTaken[0] = ROOT;
    while (node->leaf != true) {
        int index;
        int flag = 0;
        for (int i = 0; i < node->n; i++) {
            if (node->key[i] >= key) {
                index = i;
                flag = 1;
                break;
            }
        }
        if (flag == 0)
            index = n + 1;
        node = ptr[index];
        treeLen++;
        pathTaken[treeLen] = node;
    }
    
    int pathIndex = treeLen;
    do{
        if (node->n != THRESHOLD) {   /*The way the insertion works, we can never add a key larger than the maximum in the node. Since our itermediate nodes have the max of the node as the key*/
            int index;
            for (int i = 0; i < n; i++){
                if (node->key[i] >= key){
                    index = i;
                    break;
                }
            }
            long tempkey[THRESHOLD];
            void tempptr[THRESHOLD + 1];
            for (int i = index; i < node->n; i++) {
                tempkey[i] = node->key[i]; 
                tempptr[i] = node->ptr[i]; 
            }
            for (int i = index; i < node->n; i++) {
                node->key[i+1] = tempkey[i]; 
                node->ptr[i+1] = tempptr[i]; 
            }
            node->n += 1;
            node->key[i] = key;
            if (node->leaf == true) {
                struct Data* new_data = (struct Data*)(datap) + data_nodes;
                node->ptr[i] = new_data;
                data_nodes++;
                new_data->val = (char)data;
            }
            else {
                node->ptr[i] = data;
            }
            break;
        }
        else {
            struct Node* new_node = (struct Node*)(segmentp) + num_nodes;
            num_nodes++;
            long tempkey[THRESHOLD + 1];
            void tempptr[THRESHOLD + 2];
            int index;
            for (int i = 0; i < n; i++){
                if (node->key[i] > key){
                    index = i;
                    break;
                }
            }
            int flag = 0;
            for (int i = 0; i < node->n; i++) {
                if ( i == index) {
                    tempkey[i] = key;
                    if (node->leaf == true) {
                        struct Data* new_data = (struct Data*)(datap) + data_nodes;
                        tempptr[i] = new_data;
                        data_nodes++;
                        new_data->val = (char)data;
                    }
                    else {
                        tempptr[i] = data;
                    }
                    flag = 1;
                    continue;
                }
                if (flag == 0) {
                    tempkey[i] = node->key[i]; 
                    tempptr[i] = node->ptr[i]; 
                }
                else if (flag == 1) {
                    tempkey[i+1] = node->key[i]; 
                    tempptr[i+1] = node->ptr[i]; 
                }
            }
            tempptr[THRESHOLD + 1] = node->ptr[THRESHOLD];

            //first half goes to node
            for (int = 0; i < (THRESHOLD + 1)/2; i++)
            {
                node->key[i] = tempkey[i];
                node->ptr[i] = tempptr[i];
            }
            node->n = (THRESHOLD+1)/2;
            node->ptr[node->n] = new_node;

            //second half to new node
            for (int = (THRESHOLD + 1)/2; i < THRESHOLD + 1; i++)
            {
                new_node->key[i] = tempkey[i];
                new_node->ptr[i] = tempptr[i];
            }
            new_node->n = (THRESHOLD+1)/2;
            new_node->ptr[node->n] = tempptr[THRESHOLD + 1];
            new_node->leaf = node->leaf;
        }    
        pathIndex--;
        node = pathTaken[pathIndex];
        key = new_node->key[0];
        data = new_node;
    } while(node!=ROOT);

    //One condition left - What if the ROOT is full?
    if (node == ROOT) {
        struct Node* new_node = (struct Node*)(segmentp) + num_nodes;
        num_nodes++;
        long tempkey[THRESHOLD + 1];
        void tempptr[THRESHOLD + 2];
        int index;
        for (int i = 0; i < n; i++){
            if (node->key[i] >= key){
                index = i;
                break;
            }
        }
        int flag = 0;
        for (int i = 0; i < node->n; i++) {
            if ( i == index) {
                tempkey[i] = key;
                tempptr[i] = data;
                flag = 1;
                continue;
            }
            if (flag == 0) {
                tempkey[i] = node->key[i]; 
                tempptr[i] = node->ptr[i]; 
            }
            else if (flag == 1) {
                tempkey[i+1] = node->key[i]; 
                tempptr[i+1] = node->ptr[i]; 
            }
        }
        tempptr[THRESHOLD + 1] = node->ptr[THRESHOLD];

        //first half goes to node
        for (int = 0; i < (THRESHOLD + 1)/2; i++)
        {
            node->key[i] = tempkey[i];
            node->ptr[i] = tempptr[i];
        }
        node->n = (THRESHOLD+1)/2;
        node->ptr[node->n] = new_node;

        //second half to new node
        for (int = (THRESHOLD + 1)/2; i < THRESHOLD + 1; i++)
        {
            new_node->key[i] = tempkey[i];
            new_node->ptr[i] = tempptr[i];
        }
        new_node->n = (THRESHOLD+1)/2;
        new_node->ptr[node->n] = tempptr[THRESHOLD + 1];
        new_node->leaf = node->leaf;

        struct Node* new_ROOT_node = (struct Node*)(segmentp) + num_nodes;
        num_nodes++;
        new_ROOT_node->n = 1;
        new_ROOT_node->leaf = false;
        new_ROOT_node->key[0] = new_node->key[0] - 1;
        new_ROOT_node->ptr[0] = node;
        new_ROOT_node->ptr[1] = new_node;
        ROOT = new_ROOT_node;
    }
}

void delete(long key){
    struct Node* node = ROOT;
    int treeLen = 0;
    void pathTaken[1024];
    pathTaken[0] = ROOT;
    while (node->leaf != true) {
        int index;
        int flag = 0;
        for (int i = 0; i < node->n; i++) {
            if (node->key[i] >= key) {
                index = i;
                flag = 1;
                break;
            }
        }
        if (flag == 0)
            index = n + 1;
        node = ptr[index];
        treeLen++;
        pathTaken[treeLen] = node;
    }
    int nKeys = node->n;
    int nodeFound = 0;
    int index = 0;
    for (int i = 0; i < nKeys; i++) {
        if (node->key[i] == key) {
            nodeFound = 1;
            index = i;
            break;
        } 
    }
    if (nodeFound == 0) {
        return;
    }
    else {
        long tempkey[THRESHOLD];
        void tempptr[THRESHOLD + 1];
        for (int i = index; i < node->n; i++) {
            tempkey[i] = node->key[i]; 
            tempptr[i] = node->ptr[i]; 
        }
        for (int i = index + 1; i < node->n; i++) {
            node->key[i - 1] = tempkey[i]; 
            node->ptr[i - 1] = tempptr[i]; 
        }
        node->ptr[node->n - 1] = tempptr[node->n]; 
        node->n -= 1;

        if ( node->n < (THRESHOLD + 1)/2) {
            if (leftNodeExists(pathTaken, treeLen) > (THRESHOLD + 1)/2) {
                //transfer element from left node
                transferLeftNode(pathTaken,treeLen);
            }
            else if (rightNodeExists(pathTaken, treeLen) > (THRESHOLD + 1)/2) {
                //transfer element from right node
                transferRightNode(pathTaken, treeLen);
            }
            else if (leftNodeExists(pathTaken, treeLen) <= (THRESHOLD + 1)/2) {
                //merge with left node
                mergeLeftNode(pathTaken, treeLen);
            }
            else if (rightNodeExists(pathTaken, treeLen) <= (THRESHOLD + 1)/2) {
                //merge with right node
                mergeRightNode(pathTaken, treeLen);
            }
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
    long size = 0x0000000001000000;
    int node_fd, data_fd, file_present;
    if  (access("/nobackup/pratyush/b+tree/b+tree_node.txt", F_OK) != -1) {
        printf("File exists\n");
        node_fd = open("/nobackup/pratyush/b+tree/b+tree_node.txt", O_CREAT | O_RDWR, S_IRWXU);
        data_fd = open("/nobackup/pratyush/b+tree/b+tree_data.txt", O_CREAT | O_RDWR, S_IRWXU);
        if (node_fd == -1) {
            perror("open");
        }
        if (data_fd == -1) {
            perror("open");
        }
        file_present = 1;
    }
    else {
        node_fd = open("/nobackup/pratyush/b+tree/b+tree_node.txt", O_CREAT | O_RDWR, S_IRWXU);
        data_fd = open("/nobackup/pratyush/b+tree/b+tree_data.txt", O_CREAT | O_RDWR, S_IRWXU);
        ftruncate(node_fd, size);
        ftruncate(data_fd, size);
        if (node_fd == -1) {
            perror("open");
        }
        if (data_fd == -1) {
            perror("open");
        }
        file_present = 0;
    }
    nodep = mmap( (void *) addr, size, PROT_READ| PROT_WRITE, 
                    MAP_SHARED, 
    	            node_fd,
    	            0);
    if (nodep == (void *) -1 ) {
        perror("mmap");
    }
    datap = mmap( (void *) addr, size, PROT_READ| PROT_WRITE, 
                    MAP_SHARED, 
    	            data_fd,
    	            0);
    if (datap == (void *) -1 ) {
        perror("mmap");
    }

    /*Store HEAD in persistent mem*/
    /*Normal testing first to see if the program works*/

    if (file_present) {
        reconstruct_list();   
        return 0; 
    }
    else {    
    }
    for (int i = 0; i < 100000; i++) {
    }
    print_list();

    munmap(segmentp, size);
    close(segment_fd);
    return 0;
}
