/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "hashmap.h"
#include <assert.h>
#include <errno.h>
#include "threads.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <stdio.h>
#include <x86intrin.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef MAP_SYNC
#define MAP_SYNC 0x80000
#endif

#ifndef MAP_SHARED_VALIDATE
#define MAP_SHARED_VALIDATE 0x03
#endif

#define KEY 0
#define VALUE 1
#define NEXT 2
#define BUCKETS 3
#define BUCKET_COUNT 4
#define DEL 5
#define SIZE 6
#define HASH 7

typedef uint64_t hrtime_t;

int flush_count;
int fence_count;
int del_count;
int append_count;
hrtime_t flush_begin;
hrtime_t flush_time;
hrtime_t flush_time_s;
hrtime_t rdtsc();

void *hashmapp;
void* entryp;
int offset;

typedef struct list{
      int data;
      struct list *next;
      struct list *prev;
} list;

list *head;
list *tail;


hrtime_t rdtsc() {
    unsigned long int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return (hrtime_t)hi << 32 | lo; 
}

typedef struct Entry Entry;
struct Entry {
    int key;
    long long value;
    int hash;
    Entry* next;
    int offset;
};
struct Hashmap {
    Entry** buckets;
    size_t bucketCount;
    int (*hash)(int key);
    bool (*equals)(int keyA, int keyB);
    mutex_t lock; 
    size_t size;
};

void flush(int op, int offset, long long value) {
    long addr;
    struct Hashmap* map = (struct Hashmap*) hashmapp;
    map->buckets = hashmapp + sizeof(struct Hashmap);
    Entry* entry = (Entry*) entryp + offset; 
    if (op == SIZE) {
    	map->size = (size_t)value;
    } else if (op == BUCKETS) {
        map->buckets[value] = entry;
        addr = &map->buckets[value];
	} else if (op == BUCKET_COUNT) {
        map->bucketCount = (int) value;
        addr = &map->bucketCount;
    } else if(op == KEY) {
    	entry->key = (int) value;
        addr = &entry->key;
    } else if(op == VALUE) {
    	entry->value = value;
    	addr = &entry->value;
    } else if (op == DEL) {
    	entry->key = NULL;
    	addr = &entry->key;
    } else if (op == HASH) {
        entry->hash = (int) value;
        addr = &entry->hash;
    }   else if (op == NEXT) {
        if (value == NULL)
            entry->next = NULL;
        else {
            Entry* next_entry = (Entry*) entryp + value;
            entry->next = next_entry;
        }
        addr = &entry->next;
    }
    if (flush_begin == 0)
        flush_begin = rdtsc();
     hrtime_t flush_begin_s = rdtsc();
    _mm_clflushopt(addr);
    flush_count++;
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

Hashmap* hashmapCreate(size_t initialCapacity, int (*hash)(int key), bool (*equals)(int keyA, int keyB)) {
    assert(hash != NULL);
    assert(equals != NULL);
    
    Hashmap* map = malloc(sizeof(Hashmap));
    if (map == NULL) {
        return NULL;
    }
    
    // 0.75 load factor.
    size_t minimumBucketCount = initialCapacity * 4 / 3;
    map->bucketCount = 1;
    flush(BUCKET_COUNT, NULL, (long long)map->bucketCount);
    while (map->bucketCount <= minimumBucketCount) {
        // Bucket count must be power of 2.
        map->bucketCount <<= 1; 
        flush(BUCKET_COUNT, NULL, (long long)map->bucketCount);
    }
    map->buckets = calloc(map->bucketCount, sizeof(Entry*));
    if (map->buckets == NULL) {
        free(map);
        return NULL;
    }
    
    map->size = 0;
    flush(SIZE, NULL, (long long)map->size);
    fence();
    map->hash = hash;
    map->equals = equals;
    
    mutex_init(&map->lock);
    
    return map;
}
/**
 * Hashes the given key.
 */
#ifdef __clang__
__attribute__((no_sanitize("integer")))
#endif
static inline int hashKey(Hashmap* map, int key) {
    int h = map->hash(key);
    // We apply this secondary hashing discovered by Doug Lea to defend
    // against bad hashes.
    h += ~(h << 9);
    h ^= (((unsigned int) h) >> 14);
    h += (h << 4);
    h ^= (((unsigned int) h) >> 10);
       
    return h;
}
size_t hashmapSize(Hashmap* map) {
    return map->size;
}
static inline size_t calculateIndex(size_t bucketCount, int hash) {
    return ((size_t) hash) & (bucketCount - 1);
}
static void expandIfNecessary(Hashmap* map) {
    // If the load factor exceeds 0.75...
    if (map->size > (map->bucketCount * 3 / 4)) {
        // Start off with a 0.33 load factor.
        size_t newBucketCount = map->bucketCount << 1;
        Entry** newBuckets = calloc(newBucketCount, sizeof(Entry*));
        if (newBuckets == NULL) {
            // Abort expansion.
            return;
        }
        
        // Move over existing entries.
        size_t i;
        for (i = 0; i < map->bucketCount; i++) {
            Entry* entry = map->buckets[i];
            while (entry != NULL) {
                Entry* next = entry->next;
                size_t index = calculateIndex(newBucketCount, entry->hash);
                entry->next = newBuckets[index];
                if (entry->next == NULL)
                    flush(NEXT, entry->offset, NULL);
                else {
                    Entry* new_next = entry->next;
                    flush(NEXT, entry->offset, new_next->offset);
                }
                newBuckets[index] = entry;
                flush(BUCKETS, entry->offset, index);
                entry = next;
            }
        }
        // Copy over internals.
        free(map->buckets);
        map->buckets = newBuckets;
        map->bucketCount = newBucketCount;
        flush(BUCKET_COUNT, NULL, map->bucketCount);
        fence();
    }
}
void hashmapLock(Hashmap* map) {
    mutex_lock(&map->lock);
}
void hashmapUnlock(Hashmap* map) {
    mutex_unlock(&map->lock);
}
void hashmapFree(Hashmap* map) {
    size_t i;
    for (i = 0; i < map->bucketCount; i++) {
        Entry* entry = map->buckets[i];
        while (entry != NULL) {
            Entry* next = entry->next;
            flush(DEL, entry->offset, NULL);
			free(entry);
            entry = next;
        }
    }
    fence();
    free(map->buckets);
    mutex_destroy(&map->lock);
    free(map);
}
#ifdef __clang__
__attribute__((no_sanitize("integer")))
#endif
/* FIXME: relies on signed integer overflow, which is undefined behavior */
int hashmapHash(int key, size_t keySize) {
    int h = keySize;
    char* data = (char*) key;
    size_t i;
    for (i = 0; i < keySize; i++) {
        h = h * 31 + *data;
        data++;
    }
    return h;
}
static Entry* createEntry(int key, int hash, long long value) {
    Entry* entry = malloc(sizeof(Entry));
    if (entry == NULL) {
        return NULL;
    }
	offset++;
	entry->offset = offset;
    entry->key = key;
	flush(KEY, entry->offset, (long long)entry->key);
    entry->hash = hash;
    flush(HASH, entry->offset, (long long)entry->hash);
    entry->value = (long long)value;
	flush(VALUE, entry->offset, entry->value);
    entry->next = NULL;
    fence();
	return entry;
}
static inline bool equalKeys(int keyA, int hashA, int keyB, int hashB,
        bool (*equals)(int, int)) {
    if (keyA == keyB) {
        return true;
    }
    if (hashA != hashB) {
        return false;
    }
    return equals(keyA, keyB);
}
void* hashmapPut(Hashmap* map, int key, long long value) {
    int hash = hashKey(map, key);
    int bucketEmpty = 0;
    size_t index = calculateIndex(map->bucketCount, hash);
    Entry** p = &(map->buckets[index]);
    Entry* first = *p;
    if (first == NULL)
        bucketEmpty = 1;
    while (true) {
        Entry* current = *p;
        // Add a new entry.
        if (current == NULL) {
            *p = createEntry(key, hash, value);
            if (*p == NULL) {
                errno = ENOMEM;
                return NULL;
            }
            map->size++;
            flush(SIZE, NULL, map->size);
            if (bucketEmpty == 1) {
                flush(BUCKETS, (*p)->offset, index);
            }
            fence();
            expandIfNecessary(map);
            return NULL;
        }
        // Replace existing entry.
        if (equalKeys(current->key, current->hash, key, hash, map->equals)) {
            long long oldValue = current->value;
            current->value = value;
            flush(VALUE, current->offset, current->value);
            fence();
            return oldValue;
        }
        // Move to next entry.
        p = &current->next;
    }
}
void* hashmapGet(Hashmap* map, int key) {
    int hash = hashKey(map, key);
    size_t index = calculateIndex(map->bucketCount, hash);
    Entry* entry = map->buckets[index];
    while (entry != NULL) {
        if (equalKeys(entry->key, entry->hash, key, hash, map->equals)) {
            return entry->value;
        }
        entry = entry->next;
    }
    return NULL;
}
bool hashmapContainsKey(Hashmap* map, int key) {
    int hash = hashKey(map, key);
    size_t index = calculateIndex(map->bucketCount, hash);
    Entry* entry = map->buckets[index];
    while (entry != NULL) {
        if (equalKeys(entry->key, entry->hash, key, hash, map->equals)) {
            return true;
        }
        entry = entry->next;
    }
    return false;
}
void* hashmapMemoize(Hashmap* map, int key, 
        long long (*initialValue)(int key, void* context), void* context) {
    int hash = hashKey(map, key);
    int bucketEmpty = 0;
    size_t index = calculateIndex(map->bucketCount, hash);
    Entry** p = &(map->buckets[index]);
    Entry* first = *p;
    if (first == NULL)
        bucketEmpty = 1;
    while (true) {
        Entry* current = *p;
        // Add a new entry.
        if (current == NULL) {
            *p = createEntry(key, hash, NULL);
            if (*p == NULL) {
                errno = ENOMEM;
                return NULL;
            }
            long long value;
            value = (initialValue(key, context));
            (*p)->value = value;
            flush(VALUE, (*p)->offset, (*p)->value);
            map->size++;
            flush(SIZE, NULL, map->size);
            if (bucketEmpty == 1) {
                flush(BUCKETS, (*p)->offset, index);
            }
            fence();
            expandIfNecessary(map);
            return value;
        }
        // Return existing value.
        if (equalKeys(current->key, current->hash, key, hash, map->equals)) {
            return current->value;
        }
        // Move to next entry.
        p = &current->next;
    }
}
void* hashmapRemove(Hashmap* map, int key) {
    int hash = hashKey(map, key);
    size_t index = calculateIndex(map->bucketCount, hash);
    // Pointer to the current entry.
    Entry** p = &(map->buckets[index]);
    Entry* current;
    while ((current = *p) != NULL) {
        if (equalKeys(current->key, current->hash, key, hash, map->equals)) {
            long long value = current->value;
            *p = current->next;
            flush(DEL, current->offset, NULL);
            free(current);
            map->size--;
            flush(SIZE, NULL, map->size);
            fence();
            return value;
        }
        p = &current->next;
    }
    return NULL;
}
void hashmapForEach(Hashmap* map, 
        bool (*callback)(int key, long long value, void* context),
        void* context) {
    size_t i;
    for (i = 0; i < map->bucketCount; i++) {
        Entry* entry = map->buckets[i];
        while (entry != NULL) {
            Entry *next = entry->next;
            if (!callback(entry->key, entry->value, context)) {
                return;
            }
            entry = next;
        }
    }
}
size_t hashmapCurrentCapacity(Hashmap* map) {
    size_t bucketCount = map->bucketCount;
    return bucketCount * 3 / 4;
}
size_t hashmapCountCollisions(Hashmap* map) {
    size_t collisions = 0;
    size_t i;
    for (i = 0; i < map->bucketCount; i++) {
        Entry* entry = map->buckets[i];
        while (entry != NULL) {
            if (entry->next != NULL) {
                collisions++;
            }
            entry = entry->next;
        }
    }
    return collisions;
}
int hashmapIntHash(int key) {
    // Return the key value itself.
    return key;
}
bool hashmapIntEquals(int keyA, int keyB) {
    return keyA == keyB;
}

void recover_hashmap(struct Hashmap* map_p, Hashmap* map){

    map->size = map_p->size;
    // 0.75 load factor.
    size_t minimumBucketCount = map->size * 4 / 3;
    map->bucketCount = 1;
    while (map->bucketCount <= minimumBucketCount) {
        // Bucket count must be power of 2.
        map->bucketCount <<= 1; 
    }
    map->buckets = calloc(map->bucketCount, sizeof(Entry*));
	if (map->buckets == NULL) {
	    // Abort expansion.
	    return;
	}

    Entry *entry_p = entryp;
    int i = 0;
    int offset = 0;
    while (i < map->size) {
    	if (entry_p->key != NULL) {
    		i++;
    		Entry* entry = malloc(sizeof(Entry));
    		if (entry == NULL) {
        		return NULL;
    		}
    		entry->offset = offset;
    		entry->key = entry_p->key;
    		entry->value = entry_p->value;
    		int hash = hashKey(map, entry->key);
    		entry->hash = hash;
    		size_t index = calculateIndex(map->bucketCount, entry->hash);
    		Entry* current = map->buckets[index];
    		if (current == NULL) {
    			map->buckets[index] = entry;
    			entry->next = NULL;
    		} else {
    			Entry *previous;
    		    while (current != NULL) {
    				previous = current;
    				current = current->next;
    			}
    			previous->next = entry;
    			entry->next = NULL;
    		}
    	}
    	offset++;
    	entry_p = entryp + sizeof(Entry)*offset;
    }
}

void print_hashmap(Hashmap *map){
	Entry* entry;
	for (int i = 0; i < map->bucketCount; i++) {
		entry = map->buckets[i];
		while (entry != NULL) {
			printf("Bucket : %d Key : %d Hash: %d Value: %lld\n", i, entry->key, entry->hash, entry->value);
    		entry= entry->next;
		}
	}
}

//Append to list
void append_val(int data) {
	tail->data = data;
	list * new_tail = (list *) malloc(sizeof(list*));
	new_tail->prev = tail;
	tail->next = new_tail;
	tail = new_tail;
}

int select_val() {
	list *element = head->next;
	if (element == NULL)
		return -1;
	list *next_element = element->next;
	int data = element->data;
	head->next = next_element;
	next_element->prev = head;
	free(element);
	return data;
}

int main(int argc, char * argv[]) {

    offset = 0;
    hrtime_t program_start;
    hrtime_t program_end;
    flush_count = 0;
    fence_count = 0;
    long addr = 0x0000010000000000;
    long sizeentry = 200000000*sizeof(Entry);
    int sizehashmap = sizeof(struct Hashmap) + 200000000*sizeof(Entry*);
    int ratio = atoi(argv[1]);

    int hashmap_fd, entry_fd, file_present;
    if  (access("/mnt/ext4-pmem22/persistent_apps/hashmap.txt", F_OK) != -1) {
        printf("File exists\n");
        hashmap_fd = open("/mnt/ext4-pmem22/persistent_apps/hashmap.txt", O_CREAT | O_RDWR, S_IRWXU);
        entry_fd = open("/mnt/ext4-pmem22/persistent_apps/mapentry.txt", O_CREAT | O_RDWR, S_IRWXU);
        //hashmap_fd = open("/nobackup/pratyush/persistent_apps/hashmap/hashmap.txt", O_CREAT | O_RDWR, S_IRWXU);
        //entry_fd = open("/nobackup/pratyush/persistent_apps/hashmap/mapentry.txt", O_CREAT | O_RDWR, S_IRWXU);
        file_present = 1;
    }
    else {
        hashmap_fd = open("/mnt/ext4-pmem22/persistent_apps/hashmap.txt", O_CREAT | O_RDWR, S_IRWXU);
        entry_fd = open("/mnt/ext4-pmem22/persistent_apps/mapentry.txt", O_CREAT | O_RDWR, S_IRWXU);
        //hashmap_fd = open("/nobackup/pratyush/persistent_apps/hashmap/hashmap.txt", O_CREAT | O_RDWR, S_IRWXU);
        //entry_fd = open("/nobackup/pratyush/persistent_apps/hashmap/mapentry.txt", O_CREAT | O_RDWR, S_IRWXU);
        ftruncate(hashmap_fd, sizehashmap);
        ftruncate(entry_fd, sizeentry);
        if (hashmap_fd == -1) {
            perror("open");
        }
        if (entry_fd == -1) {
            perror("open");
        }
        file_present = 0;
    }
    hashmapp = mmap( (void *) addr, sizehashmap, PROT_READ| PROT_WRITE, 
                    MAP_SYNC|MAP_SHARED_VALIDATE, 
                    hashmap_fd,
                    0);
    if (hashmapp == (void *) -1 ) {
        perror("mmap");
    }
    entryp = mmap( (void *) addr, sizeentry, PROT_READ| PROT_WRITE, 
                    MAP_SYNC|MAP_SHARED_VALIDATE, 
                    entry_fd,
                    0);
    if (entryp == (void *) -1 ) {
        perror("mmap");
    }

    if (file_present == 1) {
    	Hashmap* map = malloc(sizeof(Hashmap));
	    if (map == NULL) {
	        return NULL;
	    }
	    map->hash = hashmapIntHash;
    	map->equals = hashmapIntEquals;
    	mutex_init(&map->lock);

    	struct Hashmap *mapp = (struct Hashmap*) hashmapp;
    	recover_hashmap(mapp, map);
    	print_hashmap(map);
    } else {
    	struct Hashmap *map = hashmapCreate(4, hashmapIntHash, hashmapIntEquals);
        long long value;
        int key;
        int initIterations = 100000;
	    int ssIterations = (100000000)/(ratio + 1);
	    head = (list *) malloc(sizeof(list*));
	    tail = (list *) malloc(sizeof(list*));
	    head->next = tail;
    	tail->prev = head; 
    	
	    for (int i = 0; i < initIterations; i++)
		{
			key = rand();
			append_val(key);
			value = rand();
			hashmapPut(map, key, value);
		}

        flush_count = 0;
        fence_count = 0;
        program_start = rdtsc();
		for (int i = 0; i < ssIterations; i++)
		{
    		for (int j = 0; j < ratio; j++) {
    			key = rand();
				append_val(key);
				value = rand();
				hashmapPut(map, key, value);
        	}    
			key = select_val(); 	
			hashmapRemove(map, key);
		}
        program_end = rdtsc();
        printf("Program time: %f msec Flush time: %f msec Non overlapping Flush time : %f msec \n", ((double)(program_end - program_start)/(3.4*1000*1000)), ((double)flush_time)/(3.4*1000*1000), ((double)flush_time_s)/(3.4*1000*1000));
        printf("Number of flushes: %ld, Number of fences: %ld\n", flush_count, fence_count);
        //print_hashmap(map);
    }
}
