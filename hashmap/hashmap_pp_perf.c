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

typedef struct Value Value;
struct Value{
    long long value1; //8B
    long long value2; //8B
    long long value3; //8B
    long long value4; //8B
    long long value5; //8B
    long long value6; //8B
    long long value7; //8B
}; //56B


typedef struct Entry Entry;
struct Entry {
    long long key;
    Value value;
    int hash;
    Entry* next;
};
struct Hashmap {
    Entry** buckets;
    size_t bucketCount;
    int (*hash)(int key);
    bool (*equals)(int keyA, int keyB);
    mutex_t lock; 
    size_t size;
};

void flush(long addr, int size) {
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

Hashmap* hashmapCreate(size_t initialCapacity, int (*hash)(int key), bool (*equals)(int keyA, int keyB)) {
    assert(hash != NULL);
    assert(equals != NULL);
    
    Hashmap* map = (Hashmap*) hashmapp;
    if (map == NULL) {
        return NULL;
    }
    
    // 0.75 load factor.
    size_t minimumBucketCount = initialCapacity * 4 / 3;
    map->bucketCount = 1;
    while (map->bucketCount <= minimumBucketCount) {
        // Bucket count must be power of 2.
        map->bucketCount <<= 1; 
    }
    map->buckets = hashmapp + sizeof(Hashmap);
    map->size = 0;
    flush(&map->size, sizeof(&map->size));
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
                newBuckets[index] = entry;
                entry = next;
            }
        }
        map->bucketCount = newBucketCount;
        for (i = 0; i < map->bucketCount; i++) {
            map->buckets[i] = NULL;
        }
        // Copy over internals.
        for (i = 0; i < map->bucketCount; i++) {
            Entry* entry = newBuckets[i];
            while (entry != NULL) {
                Entry* next = entry->next;
                entry->next = map->buckets[i];
                map->buckets[i] = entry;
                entry = next;
            }
        }
        map->buckets = newBuckets;
        free(newBuckets);
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
			free(entry);
            entry = next;
        }
    }
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
    Entry* entry = (Entry*)entryp + offset;
    if (entry == NULL) {
        return NULL;
    }
	offset++;
    entry->key = key;
    entry->hash = hash;
    entry->value.value1 = (long long)value;
    entry->next = NULL;
    flush(entry, sizeof(&entry->value) + sizeof(&entry->key));
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
    size_t index = calculateIndex(map->bucketCount, hash);
    Entry** p = &(map->buckets[index]);
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
            flush(&map->size, sizeof(&map->size));
            fence();
            expandIfNecessary(map);
            return NULL;
        }
        // Replace existing entry.
        if (equalKeys(current->key, current->hash, key, hash, map->equals)) {
            long long oldValue = current->value.value1;
            current->value.value1 = value;
            flush(&current->value, sizeof(&current->value));
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
            return entry->value.value1;
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
    size_t index = calculateIndex(map->bucketCount, hash);
    Entry** p = &(map->buckets[index]);
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
            (*p)->value.value1 = value;
            flush(&((*p)->value), sizeof(&((*p)->value)));
            map->size++;
            flush(&map->size, sizeof(&map->size));
            fence();
            expandIfNecessary(map);
            return value;
        }
        // Return existing value.
        if (equalKeys(current->key, current->hash, key, hash, map->equals)) {
            return current->value.value1;
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
            long long value = current->value.value1;
            *p = current->next;
            current->key = NULL;
            flush(&current->key, sizeof(&current->key));
            map->size--;
            flush(&map->size, sizeof(&map->size));
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
            if (!callback(entry->key, entry->value.value1, context)) {
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

void recover_hashmap(Hashmap* map){

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
    		int hash = hashKey(map, entry_p->key);
    		entry_p->hash = hash;
    		size_t index = calculateIndex(map->bucketCount, entry_p->hash);
    		Entry* current = map->buckets[index];
    		entry_p->next = current;
    		map->buckets[index] = entry_p;
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
			printf("Bucket : %d Key : %d Hash: %d Value: %lld\n", i, entry->key, entry->hash, entry->value.value1);
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
    del_count = 0;
    append_count = 0;
    long addr = 0x0000010000000000;
    long sizeentry = 100000000*sizeof(Entry);
    int sizehashmap = sizeof(struct Hashmap) + 100000000*sizeof(Entry*) ;
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
    	struct Hashmap *map = (struct Hashmap*) hashmapp;
    	recover_hashmap(map);
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
        print_hashmap(map);
    }
}
