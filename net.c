#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;

/* attempts to read n (len) bytes from fd; returns true on success and false on failure. 
It may need to call the system call "read" multiple times to reach the given size len. 
*/
static bool nread(int fd, int len, uint8_t *buf) {
	//variable to keep track of total bytes read from fd
	int total_bytes_read = 0;
	
	//while total_bytes_read is less than the n bytes we need to read from fd
	while (total_bytes_read < len) {
		//bytes read is calculated by reading len - total_bytes_read bytes to buf + total_ bytes_read from fd
		int bytes_read = read(fd, buf + total_bytes_read, len - total_bytes_read);
		
		//increment total_bytes_read by bytes_read
		total_bytes_read += bytes_read;
		
		//if total_bytes_read is GEQ to 0, return true for success since all bytes have been read
		if (total_bytes_read >= 0) {
			return true;
		}
	}
	
	//return true for success in reading n bytes from fd
        return true;
}

/* attempts to write n bytes to fd; returns true on success and false on failure 
It may need to call the system call "write" multiple times to reach the size len.
*/
static bool nwrite(int fd, int len, uint8_t *buf) {
	//variable to keep track of total bytes written to fd
	int total_bytes_written = 0;
	
	//while total_bytes_written is less than the n bytes we need to write from fd
	while (total_bytes_written < len) {
		//bytes written is calculated by writing len - totaly_bytes_written bytes from buf + total_bytes_read into fd
		int bytes_written = write(fd, buf + total_bytes_written, len - total_bytes_written);
		
		//total_bytes_written incremented by bytes_written
		total_bytes_written += bytes_written;
		
		//if bytes_written is GEQ to 0, return true for success since all bytes have been written
		if (bytes_written >= 0) {
			return true;
		}
	}
	
	//return true for success in writing n bytes to fd
  	return true;
}

/* Through this function call the client attempts to receive a packet from sd 
(i.e., receiving a response from the server.). It happens after the client previously 
forwarded a jbod operation call via a request message to the server.  
It returns true on success and false on failure. 
The values of the parameters (including op, ret, block) will be returned to the caller of this function: 

op - the address to store the jbod "opcode"  
ret - the address to store the info code (lowest bit represents the return value of the server side calling the corresponding jbod_operation function. 2nd lowest bit represent whether data block exists after HEADER_LEN.)
block - holds the received block content if existing (e.g., when the op command is JBOD_READ_BLOCK)

In your implementation, you can read the packet header first (i.e., read HEADER_LEN bytes first), 
and then use the length field in the header to determine whether it is needed to read 
a block of data from the server. You may use the above nread function here.  
*/
static bool recv_packet(int sd, uint32_t *op, uint8_t *ret, uint8_t *block) {
	//header allocated memory with size HEADER_LEN
	uint8_t* header = malloc(HEADER_LEN);
	
	//if reading HEADER_LEN bytes from sd into header is not true, free header and return false for failure to receive the packet
	if (!nread(sd, HEADER_LEN, header)) {
		free(header);
		return false;
	}
	
	//copy op into header, with size 4, representing the size of the opcode field
	memcpy(op, header, 4);
	
	//sets op to little-endian order (network to host byte ordering) to read bytes in correct order
	*op = ntohl(*op);
	
	//copy the address of element 4 in header to ret, with size of 1 byte
	memcpy(ret, &header[4], 1);
	
	//header[0] = 1;
	//header[1] = 2;
	//header[2] = 3;
	//header[3] = 4;
	//for(int i = 0; i < 5; i++) {
		//printf("This is header %d: %u\n", i, header[i]);
	//}
	
	//shift ret 1 bit to the right to access the 2nd-lowest bit
	//if this operation results in 0, there is no payload to access
	//free the header and return true for success since there is no data to read from the server
	if ((*ret >> 1) == 0) {
		free(header);
		return true;
	}
	
	//read 256 bytes from sd to the block
	nread(sd, JBOD_BLOCK_SIZE, block);
	
	//free the header allocated memory
	free(header);
	
	//return true for success in receiving the packet	
	return true;

}



/* The client attempts to send a jbod request packet to sd (i.e., the server socket here); 
returns true on success and false on failure. 

op - the opcode. 
block- when the command is JBOD_WRITE_BLOCK, the block will contain data to write to the server jbod system;
otherwise it is NULL.

The above information (when applicable) has to be wrapped into a jbod request packet (format specified in readme).
You may call the above nwrite function to do the actual sending.  
*/
static bool send_packet(int sd, uint32_t op, uint8_t *block) {
	//printf("this is 'sd': %d\n", sd);
	
	//sets op to big-endian order (host to network byte ordering)
	op = htonl(op);
	
	//allocate memory for the packet with size of HEADER_LEN + 256
	uint8_t* packet = malloc(HEADER_LEN + JBOD_BLOCK_SIZE);
	
	//copy address of op into op, with size of 4, representing the opcode field size
	memcpy(packet, &op, 4);
	
	//initialize variable to represent the infocode in the protocol
	uint8_t info_code = 0;
	
	//variable to indicate if a payload is present
	bool payload_present = false;
	
	//if the block is not NULL (if there is a payload)
	//set info_code to 2 (0x02), set payload_present to true
	if (block != NULL) {
		info_code = 0x02;
		payload_present = true;
	}
	
	//packet[0] = 1;
	//packet[1] = 2;
	//packet[2] = 3;
	//packet[3] = 4;
		
	//copy the info_code into the address of element 4 in the packet
	memcpy(&packet[4], &info_code, 1);
	
	//if payload_present results in true (there is a payload)
	if (payload_present) {
		//copy the block into the address of element 5 in the packet with size of 256 bytes
		memcpy(&packet[5], block, JBOD_BLOCK_SIZE);
		//write HEADER_LEN + 256 bytes from the packet into sd
		nwrite(sd, HEADER_LEN + JBOD_BLOCK_SIZE, packet);
	//if payload_present results in false (no payload)
	} else {
		//write HEADER_LEN bytes from packet to sd
		nwrite(sd, HEADER_LEN, packet);
	}
	
	//free the packet allocated memory
	free(packet);
	
	//write length bits from packet to sd
	return true;
}



/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. 
 * this function will be invoked by tester to connect to the server at given ip and port.
 * you will not call it in mdadm.c
*/
bool jbod_connect(const char *ip, uint16_t port) {
	//struct for socket address
	struct sockaddr_in caddr;

	//set client_sd by creating the socket
	cli_sd = socket(AF_INET, SOCK_STREAM, 0);
	
	//if cli_sd is -1, return false for failure to connect to server
	if (cli_sd == -1) {
		return false;
	}
	
	//set caddr protocol family
	caddr.sin_family = AF_INET;
	//set caddr port
	caddr.sin_port = htons(port);
	
	//if doing this function results in value less than or equal to 0, close the cli_sd and return false for failure to connect to server
	if (inet_pton(AF_INET, ip, &caddr.sin_addr) <= 0) {
		close(cli_sd);
		return false;
	}
	
	//if connecting the socket equals -1, close cli_sd and return false for failure to connect to server
	if (connect(cli_sd, (const struct sockaddr *)&caddr, sizeof(caddr)) == -1) {
		close(cli_sd);
		return false;
	}
	
	//return true for successful connection to server
	return true;
}




/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void) {
	//close cli_sd and set it to -1 to disconnect from server
	close(cli_sd);
	cli_sd = -1;
}



/* sends the JBOD operation to the server (use the send_packet function) and receives 
(use the recv_packet function) and processes the response. 

The meaning of each parameter is the same as in the original jbod_operation function. 
return: 0 means success, -1 means failure.
*/
int jbod_client_operation(uint32_t op, uint8_t *block) {
	//if send_packet was not successful (returned false), return -1 for failure since we could not send packet to server
	if (!send_packet(cli_sd, op, block)) {
		return -1;
	}
	
	//variable for return code
	uint8_t ret;
	
	//if recv_packet was not successful (returned false), return -1 for failure since we could not receive packet from server
	if (!recv_packet(cli_sd, &op, &ret, block)) {
		return -1;
	}
	
	//masking ret and 0x01. return -1 for failure, 0 for success 
	return (ret & 0x01) ? -1 : 0;
}
