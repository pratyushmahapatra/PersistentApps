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
#define DATASIZE 128

struct Node{
    int n;
    long key[THRESHOLD];
    long ptr[THRESHOLD + 1];
    bool leaf;
};

struct Data{
    char val[DATASIZE];
};

void create(long key, char* data);
void insert(long key, char* data); 
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

int leftNodeExists(long *path, int position) {
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

int rightNodeExists(long *path, int position) {
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

void transferLeftNode(long *path, int position) {
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
    long rightPtr = leftNode->ptr[leftNode->n - 1];
    leftNode->ptr[leftNode->n - 1] = leftNode->ptr[leftNode->n];
    leftNode->n--;

    node->ptr[node->n + 1] = node->ptr[node->n];
    for (int i = node->n; i > 0; i--) {
        node->key[i] = node->key[i - 1];
        node->ptr[i] = node->ptr[i - 1];
    }
    node->key[0] = rightMostKey;
    node->ptr[0] = rightPtr;
    node->n++;
    parent->key[indexKey] = leftNode->key[node->n-1];
}

void mergeLeftNode(long *path, int position) {
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
    //merge values from right node into left node
    for (int i = leftNode->n; i < leftNode->n + node->n; i++)
    {
        leftNode->key[i] = node->key[i - leftNode->n];
        leftNode->ptr[i] = node->ptr[i - leftNode->n];
    }
    leftNode->n += node->n; 
    leftNode->ptr[leftNode->n] = node->key[node->n];
    node->n = 0;
    //change key in the parent Node
    for (int i = indexKey; i < parent->n; i++) {
        parent->key[i] = parent->key[i + 1];
        parent->ptr[i] = parent->ptr[i + 1];
    }
    parent->ptr[parent->n - 1] = parent->ptr[parent->n];
    parent->n--;
}

void transferRightNode(long *path, int position) {
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
    long leftPtr = rightNode->ptr[0];

    for (int i = 0; i < rightNode->n - 1; i++) {
        rightNode->key[i] = rightNode->key[i + 1];
        rightNode->ptr[i] = rightNode->ptr[i + 1];
    }
    rightNode->ptr[rightNode->n - 1] = rightNode->ptr[rightNode->n];
    rightNode->n--;

    node->key[node->n] = leftMostKey;
    node->ptr[node->n + 1] = node->ptr[node->n + 1];
    node->ptr[node->n] = leftPtr;
    node->n++;
    parent->key[indexKey] = leftMostKey;
}

void mergeRightNode(long *path, int position) {
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
    //merge values from right node into left node
    for (int i = rightNode->n; i < rightNode->n + node->n; i++)
    {
        node->key[i] = rightNode->key[i - node->n];
        node->ptr[i] = rightNode->ptr[i - node->n];
    }
    node->n += rightNode->n; 
    node->ptr[node->n] = rightNode->key[rightNode->n];
    rightNode->n = 0;
    //change key in the parent Node
    for (int i = indexKey + 1; i < parent->n; i++) {
        parent->key[i] = parent->key[i + 1];
        parent->ptr[i] = parent->ptr[i + 1];
    }
    parent->ptr[parent->n - 1] = parent->ptr[parent->n];
    parent->n--;
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
void create(long key, char* data){
    INIT = (struct Node*)(nodep);
    num_nodes = 1;
    INIT->n = 1;
    flush(&INIT->n);
    INIT->leaf = false;
    struct Node* new_node = (struct Node*)(nodep) + num_nodes;
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
    memcpy(new_data->val, data, DATASIZE); 
    //new_data->val = (char *) data;
    flush(&new_data->val); 
}

void insert(long key, char* data){
    struct Node* node = ROOT;
    int treeLen = 0;
    long pathTaken[1024*1024];
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
            index = node->n;
        node = node->ptr[index];
        if (node == NULL) {
        //havent yet been initialized
            node = (struct Node*)(nodep) + num_nodes;
            num_nodes++;
        }
        treeLen++;
        pathTaken[treeLen] = node;
    }
    
    int pathIndex = treeLen;
    do{
        if (node->n != THRESHOLD) {   /*The way the insertion works, we can never add a key larger than the maximum in the node. Since our itermediate nodes have the max of the node as the key*/
            int index;
            int flag = 0;
            for (int i = 0; i < node->n; i++){
                if (node->key[i] >= key){
                    index = i;
                    flag = 1;
                    break;
                }
            }
            if (flag == 0)
                index = node->n;
            long tempkey[THRESHOLD];
            long tempptr[THRESHOLD + 1];
            for (int i = index; i < node->n; i++) {
                tempkey[i] = node->key[i]; 
                tempptr[i] = node->ptr[i]; 
            }
            for (int i = index; i < node->n; i++) {
                node->key[i+1] = tempkey[i]; 
                node->ptr[i+1] = tempptr[i]; 
            }
            node->n += 1;
            node->key[index] = key;
            if (node->leaf == true) {
                struct Data* new_data = (struct Data*)(datap) + data_nodes;
                node->ptr[index + 1] = new_data;
                data_nodes++;
                memcpy(new_data->val, data, DATASIZE); 
                //new_data->val = (char)data;
            }
            else {
                node->ptr[index] = data;
            }
            break;
        }
        else {
            struct Node* new_node = (struct Node*)(nodep) + num_nodes;
            num_nodes++;
            long tempkey[THRESHOLD + 1];
            long tempptr[THRESHOLD + 2];
            int index;
            int flag;
            for (int i = 0; i < node->n; i++){
                if (node->key[i] > key){
                    index = i;
                    flag = 1;
                    break;
                }
            }
            if (flag == 0)
                index = node->n;

            flag = 0;
            for (int i = 0; i < node->n; i++) {
                if ( i == index) {
                    tempkey[i] = key;
                    if (node->leaf == true) {
                        struct Data* new_data = (struct Data*)(datap) + data_nodes;
                        tempptr[i] = new_data;
                        data_nodes++;
                        //new_data->val = (char)data;
                        memcpy(new_data->val, data, DATASIZE); 
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
            if (index == node->n) {
                tempkey[index] = key;
                if (node->leaf == true) {
                    struct Data* new_data = (struct Data*)(datap) + data_nodes;
                    tempptr[index] = new_data;
                    data_nodes++;
                    memcpy(new_data->val, data, DATASIZE); 
                    //new_data->val = (char)data;
                }
                else {
                    tempptr[index] = data;
                }
            }
            tempptr[THRESHOLD + 1] = node->ptr[THRESHOLD];

            //first half goes to node
            for (int i = 0; i < (THRESHOLD + 1)/2; i++)
            {
                node->key[i] = tempkey[i];
                node->ptr[i] = tempptr[i];
            }
            node->n = (THRESHOLD+1)/2;
            node->ptr[node->n] = new_node;

            //second half to new node
            for (int i = (THRESHOLD + 1)/2; i < THRESHOLD + 1; i++)
            {
                new_node->key[i] = tempkey[i];
                new_node->ptr[i] = tempptr[i];
            }
            new_node->n = (THRESHOLD+1)/2;
            new_node->ptr[node->n] = tempptr[THRESHOLD + 1];
            new_node->leaf = node->leaf;
            //update old parent key
            struct Node* parentNode = pathTaken[pathIndex - 1];
            //use the inserted key to figure out index in parent key
            index = 0;
            flag = 0;
            for (int i = 0; i < parentNode->n; i++) {
                if (parentNode->key[i] >= key) {
                    index = i;
                    flag = 1;
                    break;
                }
            }
            if (flag == 0)
                index = parentNode->n;
            parentNode->key[index] = node->key[node->n - 1];
            key = new_node->key[new_node->n - 1];
            data = new_node;
        }    
        pathIndex--;
        node = pathTaken[pathIndex];
    } while(node!=ROOT);

    //One condition left - What if the ROOT is full?
    if (node == ROOT) {
        struct Node* new_node = (struct Node*)(nodep) + num_nodes;
        num_nodes++;
        long tempkey[THRESHOLD + 1];
        long tempptr[THRESHOLD + 2];
        int index;
        for (int i = 0; i < node->n; i++){
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
        for (int i = 0; i < (THRESHOLD + 1)/2; i++)
        {
            node->key[i] = tempkey[i];
            node->ptr[i] = tempptr[i];
        }
        node->n = (THRESHOLD+1)/2;
        node->ptr[node->n] = new_node;

        //second half to new node
        for (int i = (THRESHOLD + 1)/2; i < THRESHOLD + 1; i++)
        {
            new_node->key[i] = tempkey[i];
            new_node->ptr[i] = tempptr[i];
        }
        new_node->n = (THRESHOLD+1)/2;
        new_node->ptr[node->n] = tempptr[THRESHOLD + 1];
        new_node->leaf = node->leaf;

        struct Node* new_ROOT_node = (struct Node*)(nodep) + num_nodes;
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
    long pathTaken[1024];
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
            index = node->n;
        node = node->ptr[index];

        if (node == NULL)   //if key is not present
            return;

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
    int pathIndex = treeLen;
    if (nodeFound == 0) {
        return;
    }
    else {
        do {
            node = pathTaken[pathIndex];
            if (node->leaf == true) {
                long tempkey[THRESHOLD];
                long tempptr[THRESHOLD + 1];
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
            }

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
            pathIndex--;
            //need to figure out the key combination - No need to send the key up again
        } while(node != ROOT);
    }
}


void printLeaf()
{
    struct Node* node = ROOT;
    //The b+tree has the same number of levels in all directions
    while (node->leaf != true) {
        node = node->ptr[0];
    }
    //node points to the left most node
    while (node->ptr[node->n] != NULL) {
        for (int i = 0; i < node->n; i++) {
            printf("%d-", node->key[i]);
        }
        node = node->ptr[node->n];
        printf("||");
    }
    for (int i = 0; i < node->n; i++) {
        printf("%d-", node->key[i]);
    }
}

void reconstruct_list(){
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
        node_fd = open("/nobackup/pratyush/persistent_apps/b+tree/b+tree_node.txt", O_CREAT | O_RDWR, S_IRWXU);
        data_fd = open("/nobackup/pratyush/persistent_apps/b+tree/b+tree_data.txt", O_CREAT | O_RDWR, S_IRWXU);
        if (node_fd == -1) {
            perror("open");
        }
        if (data_fd == -1) {
            perror("open");
        }
        file_present = 1;
    }
    else {
        node_fd = open("/nobackup/pratyush/persistent_apps/b+tree/b+tree_node.txt", O_CREAT | O_RDWR, S_IRWXU);
        data_fd = open("/nobackup/pratyush/persistent_apps/b+tree/b+tree_data.txt", O_CREAT | O_RDWR, S_IRWXU);
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

    //if (file_present) {
    //    reconstruct_list();   
    //    return 0; 
    //}
    //else {    
    //}
    char *data = "pratyush"; 
    create(10, data);
    for (int i = 0; i < 100000; i++) {
        insert(rand()%1000, data); 
        delete(rand()%1000);
    }
    printLeaf();

    munmap(nodep, size);
    munmap(datap, size);
    close(node_fd);
    close(data_fd);
    return 0;
}
