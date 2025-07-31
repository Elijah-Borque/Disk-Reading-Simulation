#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "cache.h"
#include "jbod.h"

static cache_entry_t *cache = NULL; //initializes the struct for the cache
static int cache_size = 0; //intializes cache size as 0
static int num_queries = 0; //initialize number of queries as 0
static int num_hits = 0; //intialize number of hits as 0

static int *cache_time_inserted = NULL;
static int num_inserted = 0;

//function to create the cache
int cache_create(int num_entries) {
	//if cache is already created
	if (cache != NULL) {
		return -1; //return -1 for failure
	}
	
	//if cache size is not between 2 minimum and 4096 maximum
	if ((num_entries < 2) || (num_entries > 4096)) {
		return -1; //return -1 for failure
	}
	
	cache_size = num_entries; // cache size is equal to number of entries
	
	cache = malloc(sizeof(cache_entry_t) * num_entries); //dynamically allocate memory for the cache
	cache_time_inserted = malloc(sizeof(int) * num_entries);
	
	//for every element entry in the cache, make the entry valid
	for (int i = 0; i < cache_size; i++) {
		cache[i].valid = true;
		cache_time_inserted[i] = 0;
	}
	
	num_queries = 0; //reset num_queries back to 0
	num_hits = 0; //reset num_hits back to 0
	num_inserted = 0;
	
	return 1; //return 1 for success
}

//function to destroy the cache
int cache_destroy(void) {
	//if cache is already destroyed or nonexistent
	if (cache == NULL) {
		return -1; //return -1 for failure
	}
	
	cache = NULL; //set the cache to NULL
	cache_time_inserted = NULL;
	cache_size = 0; //reset the cache size back to 0
	free(cache); //free the cache memory
	free(cache_time_inserted);
	
	return 1; //return 1 for success
}

//function to lookup data in the cache
int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
	//if buf or cache is NULL or cache size is 0
	if ((buf == NULL) || (cache == NULL) || (cache_size == 0)) {
		return -1; //return -1 for failure
	}
	
	num_queries++; //increment the number of queries
	
	//for every entry in the cache
	for (int i = 0; i < cache_size; i++) {
		//if the disk_num and block_num of entry is equal to given disk_num and block_num respectively, and if num_accesses of that entry is greater than 0
		if ((cache[i].disk_num == disk_num) && (cache[i].block_num == block_num) && (cache[i].num_accesses > 0)) {
			memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE); //copy entry into the buffer with size of 256
			cache[i].num_accesses++; //increment number of times entry was accessed
			num_hits++; //increment number hits since lookup successful
			return 1; //return 1 for success
		}
	}

	return -1; //return -1 for failure
}

//function to update an entry in the cache
void cache_update(int disk_num, int block_num, const uint8_t *buf) {
	//if cache or buf is NULL
	if ((cache == NULL) || (buf == NULL)) {
		return; //no need to update since uninitialized cache and buffer
	}
	
	//for every entry in cache
	for (int i = 0; i < cache_size; i++) {
		//if the disk_num and block_num of entry is equal to given disk_num and block_num respectively, and if num_accesses of that entry is greater than 0
		if ((cache[i].disk_num == disk_num) && (cache[i].block_num == block_num) && (cache[i].num_accesses > 0)) {
			memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE); //copy buf into the entry with size of 256
			cache[i].num_accesses++; //increment number of times entry was accessed
			cache_time_inserted[i] = ++num_inserted;
			
			return; //return when finished updating
		}
	}
	
	return; //exit update function
}

//function to insert data into the cache
int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
	//if cache or buffer NULL, or disk_num negative or greater than 16, or block_num negative or greater than 256
	if ((cache == NULL) || (buf == NULL) || (disk_num < 0) || (disk_num > JBOD_NUM_DISKS) || (block_num < 0) || (block_num > JBOD_NUM_BLOCKS_PER_DISK)) {
		return -1; //return -1 for failure
	}
	//printf("disk_num = %d, block_num = %d\n", disk_num, block_num);
	
	
	
	int lowest_num_accesses = cache[0].num_accesses; //initialize variable to keep track of LFU entry value
	int lowest_num_accesses_index = 0; //intialize variable to keep track of location of LFU entry
	
	//for each entry in cache
	for (int i = 0; i < cache_size; i++) {
		//entry already in cache
		if ((cache[i].disk_num == disk_num) && (cache[i].block_num == block_num) && (cache[i].num_accesses > 0)) {

			return -1; //return -1 for failure
		
		//check if there's an empty space within the cache
		} else if (cache[i].num_accesses == 0) {
			cache[i].disk_num = disk_num; //set entry disk_num to given disk_num
			cache[i].block_num = block_num; //set entry block_num to given block_num
			memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE); //copy buffer into entry with size of 256
			cache[i].num_accesses = 1; //set number of access of newly inserted data to 1
			cache_time_inserted[i] = ++num_inserted;
			
			return 1; //return 1 for success
		//if cache is full and we need to remove LFU item in the cache and insert new item there
		} else {
			//if entry num_accesses <= to lowest_num accesses AND if entry num_accesses not equal to num_accesses of first entry
			if ((cache[i].num_accesses <= lowest_num_accesses)) { 
				if (cache[i].num_accesses < lowest_num_accesses || (cache_time_inserted[i] < cache_time_inserted[lowest_num_accesses_index]) ) {
					lowest_num_accesses = cache[i].num_accesses; //lowest_num_accesses is entry i num_accesses
					lowest_num_accesses_index = i; //lowest_num_accesses_index is entry i
				} 
			}
		
		}
			
	}
	
	cache[lowest_num_accesses_index].disk_num = disk_num; //entry with lowest_num_accesses disk_num is now given disk_num
	cache[lowest_num_accesses_index].block_num = block_num; //entry with lowest_num_accesses block_num is now given block_num
	cache[lowest_num_accesses_index].valid = true; //entry is now valid in cache
	memcpy(cache[lowest_num_accesses_index].block, buf, JBOD_BLOCK_SIZE); //copy buffer into cache
	cache[lowest_num_accesses_index].num_accesses = 1; // num_accesses of this entry now equal to 1
	cache_time_inserted[lowest_num_accesses_index] = ++num_inserted;
	
	return 1; //return 1 for success
}

//function to determine if the cache is enabled
bool cache_enabled(void) {
	return cache != NULL && cache_size > 2; //return value depending if cache is not NULL AND cache size > 2
}

//function to print the hit rate
void cache_print_hit_rate(void) {
	fprintf(stderr, "num_hits: %d, num_queries: %d\n", num_hits, num_queries);
	fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}
