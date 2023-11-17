
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>


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
    uint32_t BPB_RootClus;
} bpb_t;


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

// other data structure, global variables, etc. define them in need.
// e.g., 
// the opened fat32.img file
// the current working directory
// the opened files
// other data structures and global variables you need

// you can give it another name
// fill the parameters
void mount_fat32(int fd) {
    // 1. decode the bpb
    // read the bpb data structure
    bpb_t bpb;
    fseek(fd, 0, SEEK_SET);
    fread(&bpb, sizeof(bpb_t), 1, fd);
    // printf("BPB_BytsPerSec: %u\n", bpb.BPB_BytsPerSec);
    setenv("USER", argv[1], 1);
    
    // determine the root BPB_RootClus
    int root_clus = bpb.BPB_RootClus;

}

// you can give it another name
// fill the parameters
void main_process() {
    while (1) {

        printf("%s/%s>", getenv("USER"), getenv("PWD"));

        // 1. get cmd from input.
        // you can use the parser provided in Project1

        // if cmd is "exit" break;
        // else if cmd is "cd" process_cd();
        // else if cmd is "ls" process_ls();
        // ...
    }
}

int main(int argc, char const *argv[]) //image file 
{
    // Variables
    int fd;

    // 1. open the fat32.img - original code was open the file in read only
    fd = open(argv[1], O_RDWR); // read and write
    if (fd < 0) { 
        perror("Error opening file failed: ");
        return 1;
    }

    // 2. mount the fat32.img
    // mount: decode BPB --> locate root directory --> decode the directory
    // need to set cwd 
    // prompt format = file_name/path/to/cwd>
    mount_fat32(fd);    



    // 3. main procees
    main_process();

    // 4. close all opened files

    // 5. close the fat32.img
    close(fd);

    return 0;
}

