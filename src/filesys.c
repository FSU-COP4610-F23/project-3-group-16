
#include "filesys.h"

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>

// From project 1:
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>


typedef struct __attribute__((packed)) BPB {
    // below 36 bytes are the main bpb
	uint8_t BS_jmpBoot[3];
	char BS_OEMName[8];
	uint16_t BPB_BytsPerSec;
	uint8_t BPB_SecPerClus;
	uint16_t BPB_RsvdSecCnt;
	uint8_t BPB_NumFATs;
	uint16_t BPB_RootEntCnt;
	uint16_t BPB_TotSec16;
	uint8_t BPB_Media;
	uint16_t BPB_FATSz16;
	uint16_t BPB_SecPerTrk;
	uint16_t BPB_NumHeads;
	uint32_t BPB_HiddSec;
	uint32_t BPB_TotSec32;
    // below are the extend bpb.
    // please declare them.
    // char pdding[18];
    // char BS_FilSysType[8];
    // //488
    // //4

    uint32_t BPB_FATSz32;
    uint32_t BPB_RootClus;

} bpb_t;


// Variables 
bpb_t bpb; // instance of the struct


typedef struct __attribute__((packed)) directory_entry {
    char DIR_Name[11];
    uint8_t DIR_Attr;
    char padding_1[8]; // DIR_NTRes, DIR_CrtTimeTenth, DIR_CrtTime, DIR_CrtDate, 
                       // DIR_LstAccDate. Since these fields are not used in
                       // Project 3, just define as a placeholder.
    uint16_t DIR_FstClusHI;
    char padding_2[4]; // DIR_WrtTime, DIR_WrtDate
    uint16_t DIR_FstClusLO;
    uint32_t DIR_FileSize;
} dentry_t;

//decoding directory entry
dentry_t *encode_dir_entry(int fat32_fd, uint32_t offset) {
    dentry_t *dentry = (dentry_t*)malloc(sizeof(dentry_t));
    ssize_t rd_bytes = pread(fat32_fd, (void*)dentry, sizeof(dentry_t), offset);
    
    // omitted: check rd_bytes == sizeof(dentry_t)

    return dentry;
}
// other data structure, global variables, etc. define them in need.
// e.g., 
// the opened fat32.img file
// the current working directory
// the opened files
// other data structures and global variables you need

// you can give it another name
// fill the parameters
void mount_fat32(FILE* fd)
{
    // 1. decode the bpb
    // read the bpb data structure
    fseek(fd, 0, SEEK_SET);
    fread(&bpb, sizeof(bpb_t), 1, fd);
    // printf("BPB_BytsPerSec: %u\n", bpb.BPB_BytsPerSec);
    
    // determine the root BPB_RootClus
    int root_clus = bpb.BPB_RootClus;
    // need a data structure to keep track of cwd (char array)
/* CWD: /DIR1/DIR2
all_direntries = get_entires_from_cluster(rootCLus)
for (token in cwd) {
	for entry in all_direntry {
		if token == entry->name {
			found = true;
			break;
		}
	}
	if found !=true {
		"token not found"
		break;
	}
	cluster_of_the_found_entry = entry->clusHigh << 16 + entry->ClusLow;
	all dirEntries = get_entries_from_cluster(cluster_of_the_found_entry)

}

void executeInfo(bpb_t *bpb)
{
    /*
    info -> [3 pts]
        Parses the boot sector. Prints the field name and corresponding value for each entry, one per
        line (e.g., Bytes Per Sector: 512).
        The fields you need to print out are:
            • position of root cluster
            • bytes per sector
            • sectors per cluster
            • total # of clusters in data region
            • # of entries in one fat
            • size of image (in bytes)
    */
    printf("Position of Root Cluster: %d\n", bpb->BPB_RootClus);
    printf("Bytes Per Sector: %d\n", bpb->BPB_BytsPerSec);
    printf("Sectors Per Cluster: %d\n", bpb->BPB_SecPerClus);
    int rootDirSectors = ((bpb->BPB_RootEntCnt * 32) + (bpb->BPB_BytsPerSec - 1)) / bpb->BPB_BytsPerSec;
    int dataSec = bpb->BPB_TotSec32 - (bpb->BPB_RsvdSecCnt + 
        (bpb->BPB_NumFATs * bpb->BPB_FATSz32) + rootDirSectors);
    int countOfClusters = dataSec / bpb->BPB_SecPerClus;
    printf("Total Number of Clusters in Data Region: %d\n", countOfClusters); // this might be wrong *****************************
    // printf("Number of Entries in One FAT: %d\n", bpb); // number of entries / number of FATs // should print 512 maybe?
    struct stat st;
    if (stat("fat32.img", &st) == 0)
    {
        //good
    }
    printf("Size of Image (in bytes): %d\n", (int)(((intmax_t)st.st_size)/8)); // This might be wrong. Look at slide 21 of S6


}

// function to process cd
void executeCD(bpb_t *bpb)
{
    // decode the directory -> find entry

}

// you can give it another name
// fill the parameters
void main_process(FILE* fd, const char* FILENAME)
{
    int done = 0;

    //may be in wrong place in main
    uint32_t offset = 0x100420;
    dentry_t *dentry = encode_dir_entry(fd, offset);
    dbg_print_dentry(dentry);
    
    while (!done)
    {

        printf("%s%s>", FILENAME, getenv("PWD")); // need to change this

        char *input = get_input();
		tokenlist *tokens = get_tokens(input); // tokanize

        // If no entry
		if(tokens->size == 0) //if no input, move on and ask for input again 
        {
			printf("\n"); // print a line
		}
        else 
        {
			if(strcmp(tokens->items[tokens->size-1], "info") == 0)
            {
                executeInfo(&bpb);
            }
            else if(strcmp(tokens->items[tokens->size-1], "exit") == 0)
            {
                done = 1;
            }
            else if(strcmp(tokens->items[tokens->size-1], "ls") == 0)
            {
                
            }
        }

		free(input);
		free_tokens(tokens);

        free(dentry);
        close(fd);

        // 1. get cmd from input.
        // you can use the parser provided in Project1

        // if cmd is "exit" break;
        // else if cmd is "cd" process_cd();
        // else if cmd is "ls" process_ls();
        // ...
    }

    printf("\n"); // New line after done the program; maybe delete this later
}

int main(int argc, char const *argv[]) //image file 
{
    // Variables

    // 1. open the fat32.img - original code was open the file in read only
    // fd = open(argv[1], O_RDWR); // read and write
    FILE* fd = fopen(argv[1], "rw");
    if (fd < 0) { 
        perror("Error opening file failed: ");
        return 1;
    }

    // 2. mount the fat32.img
    // mount: decode BPB --> locate root directory --> decode the directory
    // need to set cwd 
    // prompt format = file_name/path/to/cwd>
    mount_fat32(fd);   
    // setenv("USER", argv[1], 1);
    const char* FILENAME = argv[1];


    // 3. main procees
    main_process(fd, FILENAME);

    // 4. close all opened files

    // 5. close the fat32.img
    fclose(fd);

    return 0;
}










// lexer.c
char *get_input(void) {
	char *buffer = NULL;
	int bufsize = 0;
	char line[5];
	while (fgets(line, 5, stdin) != NULL)
	{
		int addby = 0;
		char *newln = strchr(line, '\n');
		if (newln != NULL)
			addby = newln - line;
		else
			addby = 5 - 1;
		buffer = (char *)realloc(buffer, bufsize + addby);
		memcpy(&buffer[bufsize], line, addby);
		bufsize += addby;
		if (newln != NULL)
			break;
	}
	buffer = (char *)realloc(buffer, bufsize + 1);
	buffer[bufsize] = 0;
	return buffer;
}

tokenlist *new_tokenlist(void) {
	tokenlist *tokens = (tokenlist *)malloc(sizeof(tokenlist));
	tokens->size = 0;
	tokens->items = (char **)malloc(sizeof(char *));
	tokens->items[0] = NULL; /* make NULL terminated */
	return tokens;
}

void add_token(tokenlist *tokens, char *item) {
	int i = tokens->size;

	tokens->items = (char **)realloc(tokens->items, (i + 2) * sizeof(char *));
	tokens->items[i] = (char *)malloc(strlen(item) + 1);
	tokens->items[i + 1] = NULL;
	strcpy(tokens->items[i], item);

	tokens->size += 1;
}

tokenlist *get_tokens(char *input) {
	char *buf = (char *)malloc(strlen(input) + 1);
	strcpy(buf, input);
	tokenlist *tokens = new_tokenlist();
	char *tok = strtok(buf, " ");
	while (tok != NULL)
	{
		add_token(tokens, tok);
		tok = strtok(NULL, " ");
	}
	free(buf);
	return tokens;
}

void free_tokens(tokenlist *tokens) {
	for (int i = 0; i < tokens->size; i++)
		free(tokens->items[i]);
	free(tokens->items);
	free(tokens);
}
