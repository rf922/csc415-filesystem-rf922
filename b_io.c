/**************************************************************
* Class:  CSC-415-01 Summer 2023
* Names: Tyler Fulinara, Rafael Sant Ana Leitao, Anthony Silva , Vinh Ngo Rafael Fabiani
* Student IDs: 922002234, 920984945,
922907645, 921919541,
922965105
* GitHub Name: rf922
* Group Name: MKFS
* Project: Basic File System
* File: b_io.c
*
* Description: Basic File System - Key File I/O Operations
*
**************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h> // for malloc
#include <string.h> // for memcpy
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "b_io.h"
#include "mfs.h"
#include "FAT.h"

// Maximum number of file descriptors that can be open at the same time in the system.
#define MAXFCBS 20

// Size of the chunks that the system will use to read from or write to the files.
#define B_CHUNK_SIZE 512

// Global variable.
extern int bytes_per_block;
// Function declaration to find the last block in a chain of blocks.
int get_last_block(int location);

// Structure to store file information.
typedef struct file_info
{
	char file_name[NAME_MAX_LENGTH]; // file name
	int file_size;					 // file size in bytes
	int location;					 // starting logical block in disk
	int blocks;						 // total blocks of file in disk
	Directory_Entry *de;			 // used for correctly updating directory infomation
} file_info;

/**
 * The function retrieves the information of a file given its name.
 *
 * @param fname - A pointer to a string containing the name of the file.
 *                This string is expected to include the file's complete path in the file system.
 *
 * @return - On success, this function returns a pointer to a `file_info` struct.
 * 		   - If fname is a directory, it returns NULL.
 */
file_info *get_file_info(char *fname)
{
	// Check if the given path is a directory.
	if (fs_isDir(fname) == 1)
	{
		// If path is a directory, exits get_file_info returning NULL.
		return NULL;
	}

	// Declare an instance of parsed_entry struct to hold the parsed
	// information of the directory path.
	parsed_entry entry;

	// Statement to indicate the directory path does not exist.
	// If parse function returns -1, exits get_file_info returning NULL.
	if (parse_directory_path(fname, &entry) == -1)
	{
		printf(" get fileinffo %s does not exists", entry.name);
		return NULL;
	}

	// Checks if the parent directory of file exists.
	// If parent directory does not exist, exits get_file_info returning NULL.
	if (entry.parent == NULL)
	{
		printf(" get fileinfo invalid file\n");
		return NULL;
	}

	// Allocate memory on the heap to create a new file_info object.
	file_info *finfo = malloc(sizeof(file_info));

	// Checks if malloc was successful in allocating memory.
	// exits get_file_info returning NULL.
	if (finfo == NULL)
	{
		printf("Memory allocation failed\n");
		return NULL;
	}

	// If entry has a valid index, file or directory exists.
	if (entry.index != -1)
	{

		// Copy the file or directory name from the entry to the finfo.
		strcpy(finfo->file_name, entry.parent[entry.index].dir_name);

		// Set the file size in finfo to the size in entry.
		finfo->file_size = entry.parent[entry.index].dir_file_size;

		// Set the location of the file or directory in finfo to the first cluster number in entry.
		finfo->location = entry.parent[entry.index].dir_first_cluster;

		// Number of blocks occupied by the file or directory.
		int blocks = (finfo->file_size + bytes_per_block - 1) / bytes_per_block;

		// Set the number of blocks in finfo.
		finfo->blocks = blocks;

		finfo -> de = &entry.parent[entry.index];
	}
	else
	{

		// If entry index is -1, it's not a valid file or directory,
		// so it sets the name in finfo to an empty string.
		strcpy(finfo->file_name, "");
	}

	// If the parent of the parsed entry is not the root directory or the current directory,
	// free the memory allocated for the parent.
	if (entry.parent != root_directory && entry.parent != current_directory)
	{
		free(entry.parent);
		// After freeing memory set the pointer to NULL to avoid dangling pointers.
		entry.parent = NULL;
	}
	// Returns file_info structure that contains information about the file or directory.
	return finfo;
}

// Definition of the File Control Block structure.
typedef struct b_fcb
{
	/** TODO add al the information you need in the file control block **/
	file_info *fi;		  // low level system file info
	char *buf;			  // holds the open file buffer
	int index;			  // holds the current position in the buffer
	int buflen;			  // holds how many valid bytes are in the buffer
	int current_location; // current block location
	int blocks_read;	  // blocks read so far
	int file_size_index;  // file offset
	int flags;			  // mark the purpose when open the file
} b_fcb;

// Declaration of the File Control Block array.
b_fcb fcbArray[MAXFCBS];

int startup = 0; // Indicates that this has not been initialized

/**
 * The function initializes the file system by setting up the File Control Block array.
 */
void b_init()
{
	// init fcbArray to all free
	for (int i = 0; i < MAXFCBS; i++)
	{
		fcbArray[i].buf = NULL; // indicates a free fcbArray
	}

	startup = 1;
}

/**
 * The function returns an available File Control Block (FCB) element index.
 *
 * @return - If there is a free FCB element, it returns the index of that element.
 *         - If all FCB elements are in use, it returns -1 to indicate that all FCBs are occupied.
 */
b_io_fd b_getFCB()
{
	for (int i = 0; i < MAXFCBS; i++)
	{
		if (fcbArray[i].buf == NULL)
		{
			return i; // Not thread safe (But do not worry about it for this assignment)
		}
	}
	return (-1); // all in use
}

/**
 * The function opens a buffered file given its filename and flags.
 *
 * @param filename - A pointer to a string containing the name of the file to be opened.
 * @param flags - An integer representing the access mode and file status flags for the file.
 *
 * @return - On success, this function returns the file descriptor associated with the opened file.
 *         - If an error occurs during opening the file, it returns -1.
 */
b_io_fd b_open(char *filename, int flags)
{
	// Stores the file descriptor of the opened file.
	b_io_fd returnFd;

	// Return -1 if the filename is NULL, indicating an error.
	if (filename == NULL)
		return -1;

	// Checks if the file is being opened in read-only mode.
	if ((flags & O_RDONLY))
	{
		// If any of the write-related flags are set, it means there is an attempt
		// to combine read-only with write-related operations, which is not allowed.
		if ((flags & O_TRUNC) || (flags & O_CREAT) || (flags & O_APPEND) || (flags & O_RDWR))
		{
			return -1;
		}
	}

	// If both flags are set, it indicates an attempt to open the file for both write-only
	// and read-write, which is not allowed due to conflicting access modes.
	if ((flags & O_WRONLY) && (flags & O_RDWR))
		return -1;

	// TODO
	if ((flags & O_TRUNC) && (flags & O_APPEND))
		return 1;

	// Check if the file system has been initialized.
	// If the system is not initialized, call 'b_init()' to initialize it.
	if (startup == 0)
		b_init(); // Initialize our system

	// get our own file descriptor
	returnFd = b_getFCB();

	// check for error - all used FCB's.
	if (returnFd == -1)
		return -1;

	// Gets file information using get_file_info() for the specified filename.
	fcbArray[returnFd].fi = (file_info *)get_file_info(filename);
	
	// Check if the file information retrieval was successful.
	// If fi is NULL, it means the get_file_info() returned NULL,
	// indicating an error in retrieving file information.
	if (fcbArray[returnFd].fi == NULL)
	{
		printf("[OPEN] invalid filename\n");
		return -1;
	}

	// Check if file name is an empty string "". That indicates file does not exist.
	if (strcmp(fcbArray[returnFd].fi->file_name, "") == 0)
	{
		// If the 'O_CREAT' flag is not set, file should not be created.
		// It then print an error message, free the memory allocated for fi,
		// set it to NULL, and returns -1.
		if (!(flags & O_CREAT))
		{ // don't want to make new if not exists
			printf("[OPEN] file does not exists\n");
			free(fcbArray[returnFd].fi);
			fcbArray[returnFd].fi = NULL;
			return -1;
		}

		// Try to create the file using the fs_mkfile().
		// If the creation fails prints an error message,
		// free the memory allocated for fi, set it to NULL, and return -1.
		if (fs_mkfile(filename) == -1)
		{
			free(fcbArray[returnFd].fi);
			fcbArray[returnFd].fi = NULL;
			return -1;
		}

		// Gets the file information fi for the file using get_file_info().
		fcbArray[returnFd].fi = get_file_info(filename);
	}

	// If O_TRUNC flag is set, it means the file should be truncated and
	// its content should be cleared.
	// This step is to make sure that the file information is updated.
	else if ((flags & O_TRUNC))
	{
		// Delete the existing file from the file system to clear its content.
		fs_delete(filename);
		// Create a new empty file with the same name as the original file.
		fs_mkfile(filename);
		// Retrieve the updated file information for the empty file.
		fcbArray[returnFd].fi = get_file_info(filename);
	}

	// Allocates memory for the buffer used to hold the content of the file for
	// the file descriptor returnFd.
	fcbArray[returnFd].buf = (char *)malloc(B_CHUNK_SIZE);

	// Sets the index in the buffer to 0 to indicate that the buffer is initially empty.
	fcbArray[returnFd].index = 0;

	// Sets the buffer length to 0, to indicate that the buffer doesn't contain any
	// valid data yet.
	fcbArray[returnFd].buflen = 0;

	// Sets the current_location to the starting logical block of the file
	// This keeps track of the current location of the file's content.
	fcbArray[returnFd].current_location = fcbArray[returnFd].fi->location;

	// Initializes blocks_read to 0, to indicate that no blocks have been read from the file yet.
	fcbArray[returnFd].blocks_read = 0;

	// Initializes file_size_index to 0, which is the current offset of the file.
	// It is updated when reading or writing to the file to keep track of the current position.
	fcbArray[returnFd].file_size_index = 0;

	// Stores the flags in fcbArray to keep track of the intend of opening the file.
	// The flags indicate the access mode for the file.
	fcbArray[returnFd].flags = flags;

	// Check if O_APPEND flag is set. That indicates the file is opened in append mode.
	if ((flags & O_APPEND))
	{
		// Moves the pointer to the end of the file by getting
		// the last block's logical block number.
		fcbArray[returnFd].current_location = get_last_block(fcbArray[returnFd].fi->location);

		// Updates the blocks_read field with the total number of blocks in the file.
		// It keeps track of how many blocks have been read.
		fcbArray[returnFd].blocks_read = fcbArray[returnFd].fi->blocks;

		// Updates the file_size_index field to the end of the file.
		// This is the current offset of file and is used for read and write operations.
		fcbArray[returnFd].file_size_index = fcbArray[returnFd].fi->file_size;
	}

	printf("The file location: %d \n", fcbArray[returnFd].current_location);
	printf("The file_size_index: %d \n", fcbArray[returnFd].file_size_index);
	// Returns the file descriptor, that indicates the file opened successfully.
	return (returnFd); // all set
}

/**
 * The function writes data from the buffer to the buffered file associated
 * with the given file descriptor.
 *
 * @param fd - The file descriptor of the buffered file where data will be written.
 * @param buffer - A pointer to the buffer containing the data to be written.
 * @param count - The number of bytes to be written from the buffer.
 *
 * @return - On success, the function returns the number of bytes written to the file.
 *         - If an error occurs during writing, it returns -1.
 */

// Function to write data to a file
int b_write(b_io_fd fd, char *buffer, int count) //600
{


	// Check if the system is initialized
    if (startup == 0)
        b_init(); // Initialize our system

    // Check that fd is between 0 and (MAXFCBS-1)
    if ((fd < 0) || (fd >= MAXFCBS))
    {
        return -1; // Invalid file descriptor
    }

	//gives us the current position in the file
	fcbArray[fd].index = fcbArray[fd].fi->de->dir_file_size % B_CHUNK_SIZE;

	int remainingBytesInBuffer = B_CHUNK_SIZE - fcbArray[fd].index; //remaining bytes from index of fd
	int part1,part2,part3;
	int blocksToCopy; //blocks to copy for part2
	int userBufferPosition = 0; //current positon of the user buffer that we need to start memcpy from 

	//first block -> count < remaining bytes in buffer
	if(count <= remainingBytesInBuffer) {

		printf("[WRITE] goes into first if \n");

		//whats written to current block because it fits inside
		part1 = count;

		//dont need anything else to write
		part2 = 0;
		part3 = 0;
	}
	else{ //count > block //1200 is what they are asking aka count

		printf("[WRITE] goes into first else \n");

		part1 = remainingBytesInBuffer; //should hold a full block

		part3 = count - remainingBytesInBuffer; //how much more after a full block

		blocksToCopy = part3 / B_CHUNK_SIZE; // Checks how many blocks you need for part 2

		part2 = blocksToCopy * B_CHUNK_SIZE; //convert blocks to amount of bytes you need per block

		part3 = part3 - part2; //if negative part3 won't go 
	}

	if(part1 > 0){

		//grab the current block, store it in our buffer
		LBAread(fcbArray[fd].buf, 1,fcbArray[fd].current_location );

		printf("[WRITE] goes into part1 \n");

		printf("[WRITE] index part1 %d\n", fcbArray[fd].index);
		printf("[WRITE] location part1  %d\n", fcbArray[fd].current_location);

		//buffer always reset to 0 after LBAwrite and the src is buffer up to the count of part1
		//write to our buffer from their buffer
		memcpy(fcbArray[fd].buf + fcbArray[fd].index, buffer, part1); 

		printf("[WRITE] buffer  %s\n", fcbArray[fd].buf);

		printf("[WRITE] file writes this  %s\n", fcbArray[fd].buf + fcbArray[fd].index);

		//do a lbawrite first
		if (LBAwrite(fcbArray[fd].buf, 1, fcbArray[fd].current_location) == 0) {
				printf("[WRITE] failed at part1 \n");	
	 			return -1;
	 	}

		//updating positon you are at in the file NOT THE BUFFER
		fcbArray[fd].index += part1;

		//update file size due to write
		fcbArray[fd].fi->de->dir_file_size += part1;

		//because we have memcpy the part1 of the buffer
		userBufferPosition += part1;

	}
	//if there is a remainder
	if(part2 > 0){

		printf("[WRITE] goes into part2 \n");

		//loops through each block you need to copy and write
		for(int i = 0; i < blocksToCopy; i++){

			//now we need to update where we write to becuase we just wrote
			uint32_t next_block = get_next_block(fcbArray[fd].current_location);

			//If there is no next block, we need to allocate a new block
			if (next_block == EOF_BLOCK)
			{
				// Allocate 1 additional block
				allocate_additional_blocks(fcbArray[fd].current_location, 1);

				// Get the updated next block from FAT
				next_block = get_next_block(fcbArray[fd].current_location);
			}

			//Update the current_location to the next block
			fcbArray[fd].current_location = next_block;
			
			//write function for one block at a time
			if (LBAwrite(buffer + userBufferPosition, 1, fcbArray[fd].current_location) == 0) {	
				printf("[WRITE] failed at part2 \n");	
	 			return -1;
	 		}

			//update file size due to write
			fcbArray[fd].fi->de->dir_file_size += B_CHUNK_SIZE;

			//because we have memcpy the part1 of the buffer
			userBufferPosition += B_CHUNK_SIZE;
		}
	}

	//if there is remainder bytes that fits into one block
	if(part3 > 0){
		printf("[WRITE] goes into part3 \n");

		//grab block

		//now we need to update where we write to becuase we just wrote in either part1 or part2
		uint32_t next_block = get_next_block(fcbArray[fd].current_location);

		//If there is no next block, we need to allocate a new block
		if (next_block == EOF_BLOCK)
		{
			// Allocate 1 additional block
			allocate_additional_blocks(fcbArray[fd].current_location, 1);

			// Get the updated next block from FAT
			next_block = get_next_block(fcbArray[fd].current_location);
		}

		//Update the current_location to the next block
		fcbArray[fd].current_location = next_block;

		//We need to read whats currently in the buffer so we can make sure to get everything in the block
		LBAread(fcbArray[fd].buf, 1,fcbArray[fd].current_location);

		//bring it into to memory to then do a write
		memcpy(fcbArray[fd].buf, buffer + userBufferPosition, part3); 

		printf("[WRITE] buffer  part3 %s\n", fcbArray[fd].buf);

		//write function for one block at a time
		if (LBAwrite(fcbArray[fd].buf, 1, fcbArray[fd].current_location) == 0) {	
			printf("[WRITE] failed at part3 \n");	
			return -1;
		}

		//update file size due to write
		fcbArray[fd].fi->de->dir_file_size += part3;

		//keep positon of where what we have written from the user's buffer consistent
		userBufferPosition += part3;
	}

	//return value gets set to all parts to see how much you wrote
	int retValue = part1 + part2 + part3;
	printf("[WRITE retValue] checking part1 %d\n", part1);
	printf("[WRITE retValue] checking part2 %d\n", part2);
	printf("[WRITE retValue] checking part3 %d\n", part3);
	printf("[WRITE] checking userBufferPosition %d\n", userBufferPosition);

	//error checking if you wrote as much as you gone through their buffer
	if(userBufferPosition != retValue) {
		printf("[WRITE error] checking part1+part2+part3 %d\n", retValue);
		//printf("[WRITE error] checking userBufferPosition %d\n", userBufferPosition);
		return -1;
	}

	return userBufferPosition;

}


// Interface to read a buffer

// Filling the callers request is broken into three parts
// Part 1 is what can be filled from the current buffer, which may or may not be enough
// Part 2 is after using what was left in our buffer there is still 1 or more block
//        size chunks needed to fill the callers request.  This represents the number of
//        bytes in multiples of the blocksize.
// Part 3 is a value less than blocksize which is what remains to copy to the callers buffer
//        after fulfilling part 1 and part 2.  This would always be filled from a refill
//        of our buffer.
//  +-------------+------------------------------------------------+--------+
//  |             |                                                |        |
//  | filled from |  filled direct in multiples of the block size  | filled |
//  | existing    |                                                | from   |
//  | buffer      |                                                |refilled|
//  |             |                                                | buffer |
//  |             |                                                |        |
//  | Part1       |  Part 2                                        | Part3  |
//  +-------------+------------------------------------------------+--------+

/**
 * The function reads data from the buffered file associated with the given file descriptor
 * and stores it in the buffer.
 *
 * @param fd - The file descriptor of the buffered file from which data will be read.
 * @param buffer - A pointer to the buffer where the data will be stored.
 * @param count - The number of bytes to be read from the file.
 *
 * @return - On success, the function returns the total number of bytes read from the file.
 *         - If the end of file (EOF) is reached during reading, it returns the total number of bytes read until EOF.
 *         - If an error occurs during reading, it returns -1.
 */
int b_read(b_io_fd fd, char *buffer, int count)
{

	if (startup == 0)
		b_init(); // Initialize our system

	// check that fd is between 0 and (MAXFCBS-1)
	if ((fd < 0) || (fd >= MAXFCBS))
	{
		return (-1); // invalid file descriptor
	}

	if (fcbArray[fd].fi == NULL) // may not need this because of open.
	{
		return -1;
	}

	// Initialize variables to calculate how much data can be filled from the buffer.
	int part1, part2, part3;
	int remainingBytes = B_CHUNK_SIZE - fcbArray[fd].index; //remaining bytes from index of fd
	int blocksToCopy; //amount of blocks to copy used in part 2
	int bytesRead; //bytes read calculated from amount of blocks read in struct
	printf("[b_ioc -> b_read] count is %d\n", count);
	printf("[b_ioc -> b_read] the file name is %s\n", fcbArray[fd].fi->file_name);
	printf("[b_ioc -> b_read] the file location is %d\n", fcbArray[fd].fi->location);

	// TODO: check read flag
	// printf("[b_ioc -> b_read] file_size is %d\n", fcbArray[fd].file_size_index);

	// adjust count if greater than EOF
	if (count + fcbArray[fd].file_size_index > fcbArray[fd].fi->de->dir_file_size)
	{
		printf("[b_ioc -> b_read] file_size is %d\n", fcbArray[fd].fi->de->dir_file_size);
		//update count with the filesize with index to get much you need to read
		count = fcbArray[fd].fi->de->dir_file_size - fcbArray[fd].file_size_index;
		printf("[b_ioc -> b_read] Had to reduce count\n");
	}

	// Calculate how many bytes can be filled in multiples of the block size.
	if (remainingBytes >= count)
	{
		printf("[b_ioc -> b_read] filling in from current block\n");
		//if you can use the amount of remaining of bytes for the given count
		part1 = count;
		printf("[READ] in the if %d\n", count);
		part2 = 0;
		part3 = 0;
	}
	else
	{
		// // Set part1 to the remaining bytes in the current buffer.
		part1 = remainingBytes;

		// Calculates how many bytes need to be read.
		part3 = count - remainingBytes;
		blocksToCopy = part3 / B_CHUNK_SIZE;
		part2 = blocksToCopy * B_CHUNK_SIZE;

		// Calculates the remaining bytes after filling part1 and part2.
		part3 = part3 - part2;
	}

	// If there are bytes remaining in the buffer, copy them to the user's buffer.
	if (part1 > 0)
	{
		//we need to read the first block
		LBAread(fcbArray[fd].buf, 1, fcbArray[fd].current_location);


		printf("[b_ioc -> b_read] part1 fcbarray index %d\n", fcbArray[fd].index);

		printf("[b_ioc -> b_read] inside condition part 1\n");
		// Copy part1 number of bytes from the current position in the buffer
		// to the user's buffer, starting at the address pointed by buffer.
		memcpy(buffer, fcbArray[fd].buf + fcbArray[fd].index, part1);

		// Moves the buffer index forward by part1 number of bytes.
		fcbArray[fd].index = fcbArray[fd].index + part1;

		// Updates the file offset by adding the number of bytes read,
		// to keep track of the current position in the file.
		fcbArray[fd].file_size_index += part1;

		// // Increase the buffer length by part1,
		// // the buffer now has part1 more valid bytes.
		// fcbArray[fd].buflen += part1;

	}
	printf("[b_ioc -> b_read] par 1 is %d\n", part1);
	printf("[b_ioc -> b_read] par 2 is %d\n", part2);
	printf("[b_ioc -> b_read] par 3 is %d\n", part3);

	//  If there are blocks to be read, read them and copy to the user's buffer.
	if (part2 > 0)
	{
		printf("[b_ioc -> b_read] inside condition part 2\n");
		// Keeps track of the number of bytes copied in part2.
		int tempPart2 = 0;

		// Number of blocks read from the disk.
		int blocks_read = 0;

		// Loop to copy the number of blocks into the user's buffer.
		for (int i = 0; i < blocksToCopy; i++)
		{
			// Update the current_location to the logical block number of the next block.
			fcbArray[fd].current_location = get_next_block(fcbArray[fd].current_location);

			// Check if the end of file has been reached.
			if (fcbArray[fd].current_location == -1)
			{
				printf("[b_io.c -> b_read] reached EOF in part2\n");
			}

			// Read the block from the disk into the user's buffer.
			blocks_read = LBAread(buffer + part1 + tempPart2, 1, fcbArray[fd].current_location);

			bytesRead = blocks_read * B_CHUNK_SIZE;

			// Update the blocks_read field in the fcbArray to keep track of how many blocks have been read.
			fcbArray[fd].blocks_read++;

			// Update the buffer index, current file offset, and blocks_read in the fcbArray for the next iteration.
			fcbArray[fd].buflen = 0;
			fcbArray[fd].index = 0;
			fcbArray[fd].file_size_index += B_CHUNK_SIZE;

			// Update the total bytes read in part2.
			tempPart2 = tempPart2 + bytesRead;
			part2 = tempPart2;
		}
	}
	// If there are bytes remaining to be read, reads the next block and copy to the user's buffer
	if (part3 > 0)
	{
		printf("[b_ioc -> b_read] inside condition part 3\n");
		// Update the current_location to the logical block number of the next block.
		int bytes_readP3 = 0;
	
		fcbArray[fd].current_location = get_next_block(fcbArray[fd].current_location);

		printf("[b_ioc -> b_read] the current block part3 start %d\n", fcbArray[fd].current_location);

		// Check if the end of file has been reached.
		if (fcbArray[fd].current_location == -1)
		{
			printf("[b_io.c -> b_read] reached EOF in part3\n");
		}

		// Read the block from the disk into the buffer in the fcbArray.
		bytes_readP3 = LBAread(fcbArray[fd].buf, 1, fcbArray[fd].current_location);

		// Convert the number of blocks read to the actual number of bytes read for part3.
		bytes_readP3 = bytes_readP3 * B_CHUNK_SIZE;

		// Update the total number of blocks read in the fcbArray for this iteration.
		fcbArray[fd].blocks_read++;

		// Reset the buffer index and buflen in the fcbArray for part3.
		fcbArray[fd].buflen = 0;
		fcbArray[fd].index = 0;

		if (bytes_readP3 < part3)
		{
			part3 = bytes_readP3;
			printf("[b_io.c -> b_read] Bytes read less than part3\n");
		}

		// If bytes read in part3 are less than part3 it updates part3 to the actual bytes read.
		if (part3 > 0)
		{
			// Copy the remaining bytes from the buffer to the user's buffer.
			memcpy(buffer + part1 + part2, fcbArray[fd].buf + fcbArray[fd].index, part3);

			// Update the file offset and buffer length in the fcbArray for part3.
			fcbArray[fd].index = fcbArray[fd].index + part3;
			fcbArray[fd].file_size_index += part3;
			fcbArray[fd].buflen += part3;
		}
	}
	printf("[b_ioc -> b_read] return number of bytes copied is %d\n", (part1 + part2 + part3));
	// Returns the total number of bytes read from the file.
	return part1 + part2 + part3;
	//*/
}

/**
 * The function closes the buffered file associated with the given file descriptor.
 *
 * @param fd - The file descriptor of the buffered file to be closed.
 *
 * @return - On success, the function returns 0.
 *         - If the file descriptor is invalid, it returns -1.
 */
int b_close(b_io_fd fd)
{
	// Check if the file descriptor is within valid range,if it's not,
	// returns -1 to indicate an invalid file descriptor.
	if ((fd < 0) || (fd >= MAXFCBS))
	{
		return -1;
	}

	// Free the memory associated with the file_info struct.
	free(fcbArray[fd].fi);

	// Set the pointer to the file_info struct to NULL to avoid accessing freed memory.
	fcbArray[fd].fi = NULL;

	// Free the memory associated with the buffer.
	free(fcbArray[fd].buf);

	// Set the pointer to the buffer to NULL.
	fcbArray[fd].buf = NULL;

	// Reset the current position in the buffer.
	fcbArray[fd].index = 0;

	// Reset the length of data in the buffer to zero.
	fcbArray[fd].buflen = 0;

	// Reset the current block location to zero.
	fcbArray[fd].current_location = 0;

	// Reset the number of blocks read to zero.
	fcbArray[fd].blocks_read = 0;

	// Reset the file offset to zero.
	fcbArray[fd].file_size_index = 0;

	// Reset the flags associated with the file to zero.
	fcbArray[fd].flags = 0;

	// Returns 0 to indicate a successful closure of file.
	return 0;
}

/**
 * The function retrieves the last block number of a file given the location of a block in the file system.
 *
 * @param location - The location of a block in the file system.
 *
 * @return - The block number of the last block in the file (given its location).
 */
int get_last_block(int location)
{
	// Location of the previous block, initialized to -1.
	int prev = -1;
	// Current block location initialized with location.
	int curr = location;

	// Loop through the blocks until we reach the end.
	// Keeps updating prev to store the location of the current block and moves
	// to the next block using get_next_block().
	while (curr != -1)
	{
		prev = curr;
		curr = get_next_block(curr);
	}
	return prev;
}
