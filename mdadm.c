#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "jbod.h"
#include "mdadm.h"
#include "net.h"

int is_mounted = 0; //variable to determine if the linear device is mounted
int is_written = 0; //variable to signify the write permission of the user

//function to take in commands from jbod.h and executes them on the specified disk and block
uint32_t mdadm_operation (int command, int disk_id, int block_id) {
  return (command) | (disk_id << 6) | (block_id << 10);
}

//function to mount the linear device
int mdadm_mount(void) {
  if (is_mounted == 1) {
    return -1; //returns -1 for failure if the device is already mounted
  }

  uint32_t op = mdadm_operation(JBOD_MOUNT, 0, 0); //mounts the linear device
  jbod_client_operation(op, NULL);
  
  is_mounted = 1; //sets is_mounted to 1 to indicate device is mounted

  return 1; //return 1 for successfully mounting the linear device
}

//function to unmount the linear device
int mdadm_unmount(void) {
  if (is_mounted == 0) {
    return -1; //returns -1 for failure if the device is already unmounted
  }

  uint32_t op = mdadm_operation(JBOD_UNMOUNT, 0, 0); //mountes the linear device
  jbod_client_operation(op, NULL);
  
  is_mounted = 0; //sets is_mounted to 0 to indicated that the device is unmounted

  return 1; //return 1 for successfully unmounting the linear device
}

//function to give write permission to use
int mdadm_write_permission(void){
  if(is_written != 1){ //if write permission wasn't already granted
    uint32_t op = mdadm_operation(JBOD_WRITE_PERMISSION, 0, 0); //do jbod operation to give user the write permission
    jbod_client_operation(op, NULL);
		 
    is_written = 1; //set is_written to 1 to signify the user has write permission
  }	   		 
  
  return 1; //return 1 for successful granting of write permission to user		 
}


//function to revoke the user's write permission
int mdadm_revoke_write_permission(void){
  if(is_written != -1){ //if write permission was not already revoked
    uint32_t op = mdadm_operation(JBOD_REVOKE_WRITE_PERMISSION, 0, 0); //do jbod operation to revoke the write permission from the user
    jbod_client_operation(op, NULL);

    is_written = -1; //set is_written to -1 to signify the user's write permission has been revoked
  }
  
  return 1; //return 1 for successful revoking of write permission of user
}

//function to read bytes into a buffer starting at a given address
int mdadm_read(uint32_t start_addr, uint32_t read_len, uint8_t *read_buf)  {
  //check if cache is enabled
  //printf("checking if cache is enabled\n");
  
  if (cache_enabled()) {
  	int mdadm_cache = cache_lookup((start_addr / JBOD_DISK_SIZE), (start_addr / JBOD_BLOCK_SIZE % JBOD_NUM_BLOCKS_PER_DISK), read_buf); // if cache enabled, mdadm_cache is return result of cache_lookup function
  	
  	//if entry was found in cache
  	if (mdadm_cache == 1) {
  		return read_len; //return read_len to signify that the data was already in the cache
  	}
  }
  //printf("after enable check\n");
  
  //printf("checking if we can read\n");
  
  //if not in the cache, continue by getting data from main memory
  if ((read_len > 1024) || (is_mounted == 0) || ((read_len > 0) && (read_buf == NULL)) || ((read_len + start_addr) >= (JBOD_NUM_DISKS * JBOD_BLOCK_SIZE * JBOD_NUM_BLOCKS_PER_DISK))) {
    return -1; //returns -1 for failure since the read is out of bounds or the length of the read is larger than 1024 bytes
  }
  
  //printf("checking if there is anything to read\n");
  
  if ((start_addr == 0) && (read_len == 0) && (read_buf == NULL)) {
    return 0; //returns 0 if there is nothing to read
  }
  
  int current_disk; //variable for location of disk being read
  int current_block; //vairable for location of block being read
  int block_offset; //variable for offset of block within a disk

  uint32_t bytes_read = 0; //variable to keep track of bytes read
  uint32_t remaining_bytes = read_len; //variable to keep track of remaining bytes to read
  uint32_t bytes_to_read; //variable to keep track of bytes to read
  uint8_t temp_buf[JBOD_BLOCK_SIZE]; //temporary buffer with block size of 256

  //while there are values to be read, this will loop until all bytes are read
  while (remaining_bytes > 0) {
    current_disk = start_addr / JBOD_DISK_SIZE; //get location of current disk being read by performing mod of start_addr by the size of the disk
    current_block = (start_addr / JBOD_BLOCK_SIZE) % JBOD_NUM_BLOCKS_PER_DISK; //get location of the current block being read by performing mod of start_addr divided by 256 by 256 
    block_offset = start_addr % JBOD_BLOCK_SIZE; // get block offset by performing mod of starting address by the block size

    if (bytes_read == 0) { //bytes haven't been read yet
      if (remaining_bytes + block_offset > JBOD_BLOCK_SIZE) { //if number of bytes to be read + block offest is greater than 256
        bytes_to_read = JBOD_BLOCK_SIZE - block_offset; //bytes to be read is 256 - block offset
      } else {
        bytes_to_read = remaining_bytes; //else bytes to be read is the number of remaining bytes to be read
      }
    } else { //if bytes have been read already
      if (remaining_bytes > JBOD_BLOCK_SIZE) { //if remaining bytes to be read is greater than 256
        bytes_to_read = JBOD_BLOCK_SIZE; //bytes to be read is 256
      } else {
        bytes_to_read = remaining_bytes; //else bytes to be read is the number of remaining bytes to be read
      }
    }
    
    uint32_t op = mdadm_operation(JBOD_SEEK_TO_DISK, current_disk, 0); //seek to a specific disk 
    jbod_client_operation(op, NULL);

    op =  mdadm_operation(JBOD_SEEK_TO_BLOCK, 0, current_block); //seek to a specific block within a disk
    jbod_client_operation(op, NULL);

    op = mdadm_operation(JBOD_READ_BLOCK, 0, 0); //read specified block within the specified disk
    jbod_client_operation(op, temp_buf); //read the block into the temporary buffer

    memcpy((read_buf + bytes_read), (temp_buf + block_offset), bytes_to_read); //copies the bytes read to the buffer

    bytes_read += bytes_to_read; //updates the bytes read by incrementing it by the number of bytes already read
    remaining_bytes -= bytes_to_read; //updates the remaining bytes left to be read (length of the read) by decrementing it by the number of bytes already read
    start_addr += bytes_to_read; //updates the start address by incremeting it by the number of bytes already read
    
    //check if cache enabled
    //printf("inserting read data into cache\n");
    if (cache_enabled()) {
    	cache_insert(current_disk, current_block, temp_buf); //if enabled insert data that was read into cache for later use
    }
    //printf("after data inserted\n");
  }
  //printf("read complete\n");
  
  return bytes_read; //return the number of bytes read
}

int mdadm_write(uint32_t start_addr, uint32_t write_len, const uint8_t *write_buf) {
  if ((write_len > 1024) || (is_mounted == 0) || ((write_len > 0) && (write_buf == NULL)) || ((write_len + start_addr) > (JBOD_NUM_DISKS * JBOD_BLOCK_SIZE * JBOD_NUM_BLOCKS_PER_DISK))) {
    return -1; //returns -1 for failure since the write is out of bounds or the length of the read is larger than 1024 bytes      
  }
  
  if ((start_addr == 0) && (write_len == 0) && (write_buf == NULL)) {
    return 0; //returns 0 if there is nothing to write                                                                           
  }
 
  int current_disk = start_addr / JBOD_DISK_SIZE; //calculate current disk being written at by doing starting address divided by 65536
  int current_block = (start_addr / JBOD_BLOCK_SIZE) % JBOD_NUM_BLOCKS_PER_DISK; //calculate current block being written at by doing starting address divided by 256 modded by 256
  int block_offset = start_addr % JBOD_BLOCK_SIZE; // get block offset by performing mod of starting address by the block size

  uint32_t bytes_written = 0; //variable for bytes written
  uint32_t remaining_bytes = write_len; //variable for remaining bytes to be written initialized as the length of write
  uint32_t bytes_to_write; //variable for bytes to be written

  while (remaining_bytes > 0) { //while there are remaining bytes to be written. loops until all bytes in write_buf have been written
    uint8_t temp_buf[JBOD_BLOCK_SIZE]; //initializes a temporary buffer with size of 256
    
    uint32_t op = mdadm_operation(JBOD_SEEK_TO_DISK, current_disk, 0); //seek to current disk to be written to
    jbod_client_operation(op, NULL);
    
    op = mdadm_operation(JBOD_SEEK_TO_BLOCK, 0, current_block); //seek to current block to be written to
    jbod_client_operation(op, NULL);
    
    op = mdadm_operation(JBOD_READ_BLOCK, 0, 0); //read contents of block into temp_buf
    jbod_client_operation(op, temp_buf);
    
    if (bytes_written == 0) { //if there haven't been bytes written yet
      if (remaining_bytes + block_offset > JBOD_BLOCK_SIZE) { //if remaining bytes to be read + block offset is greater than 256
	bytes_to_write = JBOD_BLOCK_SIZE - block_offset; //bytes to write is 256 - block offset
      } else {
	bytes_to_write = remaining_bytes; //else bytes to write is the remaining bytes to be written
      }
      
      memcpy(temp_buf + block_offset, write_buf, bytes_to_write); //copy bytes_to_write bytes from write_buf into temp_buf + block_offset
    } else {
      if (remaining_bytes > JBOD_BLOCK_SIZE) { //if remaining bytes to be read is greater than 256
	bytes_to_write = JBOD_BLOCK_SIZE; //bytes to write is 256
      } else {
	bytes_to_write = remaining_bytes; //else bytes to write is remaining bytes left to write
      }
      
      memcpy(temp_buf, write_buf + bytes_written, bytes_to_write); //copy bytes_to_write bytes from write_buf + bytes_already written into temp_buf
    }
    
    //char *test_buf = stringify2(temp_buf, JBOD_BLOCK_SIZE); //turns contents of temp_buf into a string and puts it into pointer test_buf

    //printf("temp_buf contents:\n  got:\n%s\n", test_buf); //prints contents of temp_buf 

    //free(test_buf); //frees memory of test_buf
    
    op = mdadm_operation(JBOD_SEEK_TO_DISK, current_disk, 0); //seek back to current disk be written to
    jbod_client_operation(op, NULL);

    op = mdadm_operation(JBOD_SEEK_TO_BLOCK, 0, current_block); //seek back to current block to be written to
    jbod_client_operation(op, NULL);
    
    op = mdadm_operation(JBOD_WRITE_BLOCK, 0, 0); //write contents of temp_buf into storage system
    jbod_client_operation(op, temp_buf);
    //printf("     jbod_op value:%d\n", jbod_operation(op, temp_buf)); //show value of jbod_operation to see if success or fail

    bytes_written += bytes_to_write; //updates value of bytes written by incrementing it by bytes_to_write
    remaining_bytes -= bytes_to_write; //updates value of remaining bytes to be written by decrementing by bytes_to_write
    start_addr += bytes_to_write; //updates start_addr by incrementing by bytes_to_write
    
    current_disk = start_addr / JBOD_DISK_SIZE; //updates current_disk for next iteration by doing start address divided by 65536
    current_block = (start_addr / JBOD_BLOCK_SIZE) % JBOD_NUM_BLOCKS_PER_DISK; //updates current_block for next iteration by doing start address divided by 256 modded by 256
    block_offset = start_addr % JBOD_BLOCK_SIZE; //updates block offset for next iteration by doing start address modded by 256
  }   
  
  //printf("    %d\n", bytes_written); //shows number of bytes written at end of write
  return bytes_written; //returns number of bytes written at end of write
}
