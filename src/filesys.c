/*
Questions:
- offset?
- how do you find the next cluster chain and how do you know where the file ends?
- how do you calculate the FAT offset (0x4000)

Things To Do:
- Check if the file exists (command line argument)

Next time:
-start with getting 0x4000 from github ex - FAT region offset
-keep in mind its just doing the same process over and over again, finding next cluster chain then finding contents of file/dir
*/

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

// STRUCTS
typedef struct __attribute__((packed)) BPB
{
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
    uint16_t BPB_ExtFlags;
    uint16_t BPB_FSVer;
    uint32_t BPB_RootClus;
    uint16_t BPB_FSInfo;
    uint16_t BPB_BkBootSec;
    char BPS_Reserved[12]; // maybe 12 not 96
    char padding1[18]; // maybe 18 not 144
    char BS_FilSysType[8];

} bpb_t;

typedef struct __attribute__((packed)) directory_entry
{
    char DIR_Name[11]; // Name of directory retrieved
    uint8_t DIR_Attr;   // Attribute count of directory retreived
    char padding_1[8]; // DIR_NTRes, DIR_CrtTimeTenth, DIR_CrtTime, DIR_CrtDate, 
                       // DIR_LstAccDate. Since these fields are not used in
                       // Project 3, just define as a placeholder.
    uint16_t DIR_FstClusHI;
    char padding_2[4]; // DIR_WrtTime, DIR_WrtDate
    uint16_t DIR_FstClusLO;
    uint32_t DIR_FileSize; // Size of directory (always 0)
} dentry_t;

// VARIABLES 
bpb_t bpb; // instance of the struct
dentry_t dir[16]; // 16 is arbitrary and magic number
dentry_t dentry; // this might be our current directory but right now it is not saved that way
int32_t currentDirectory;
int currentOffset;
char *prompt[256];




// FUNCTIONS
//decoding directory entry
dentry_t *encode_dir_entry(FILE *fd, uint32_t offset)
{
    dentry_t *dentry = (dentry_t*)malloc(sizeof(dentry_t));
    fseek(fd, offset, SEEK_SET);

    size_t rd_bytes = fread((void*)dentry, sizeof(dentry_t), 1, fd);
    
    // omitted: check rd_bytes == sizeof(dentry_t)
    if(rd_bytes != 1)
    {
        fprintf(stderr, "Error reading directory entry from file. \n");
        free(dentry);
        return NULL;
    }

    return dentry;
}
// other data structure, global variables, etc. define them in need.
// e.g., 
// the opened fat32.img file
// the current working directory
// the opened files
// other data structures and global variables you need
int getRootDirSectors()
{
    return ((bpb.BPB_RootEntCnt * 32) + (bpb.BPB_BytsPerSec - 1)) / bpb.BPB_BytsPerSec;
}

int getFirstSectorOfCluster(int clusterNumber, int firstDataSector)
{
    return ((clusterNumber - 2) * bpb.BPB_SecPerClus) + firstDataSector;
}

int getDirectoryClusterNumber(int clusterNumber, dentry_t dirToGoTo) // clusterNumber = N
{
    int firstDataSector = bpb.BPB_RsvdSecCnt + (bpb.BPB_NumFATs * bpb.BPB_FATSz32) + getRootDirSectors();
    int firstSectorOfCluster = getFirstSectorOfCluster(clusterNumber, firstDataSector);
    // << is binary left shift 
    uint16_t newClusterNumber = (dirToGoTo.DIR_FstClusHI << 16) + dirToGoTo.DIR_FstClusLO; // gives us hex
    printf("first data sector is calculated to be: %d\n", firstDataSector); // right now printing 2050, should be 100400
    printf("first sector of cluster is calculated to be: %d\n", firstSectorOfCluster);
    printf("low is %d, high is %d\n", dirToGoTo.DIR_FstClusLO, dirToGoTo.DIR_FstClusHI);
    printf("new cluster number is calculated to be: %d\n", newClusterNumber);

    int newAddress = 0x100400 + (newClusterNumber - 2) * bpb.BPB_BytsPerSec;
    printf("new address is calculated to be: %d\n", newAddress);
    return newAddress;
    // newClusterNumber gets you to a place that tells you how many fiels and directories are in the new directory
        // the number tells you how many entries you need to update in dir
    // then you go to the first cluster int he data region (using 100400 +433-2) * 512 = the number on the left in hexedit
}


// you can give it another name
// fill the parameters
void mount_fat32(FILE* fd)
{
    // 1. decode the bpb
    // read the bpb data structure
    fseek(fd, 0, SEEK_SET);
    fread(&bpb, sizeof(bpb_t), 1, fd);
        
    // determine the root BPB_RootClus
    // int32_t root_clus = bpb.BPB_RootClus;
    currentDirectory = bpb.BPB_RootClus;

    fread(&dir[0], 32, 16, fd);

    int clusterNumber = 2;
    currentOffset = getDirectoryClusterNumber(clusterNumber, dentry); // passing in 2 becuase that is original cluster number 
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
    int rootDirSectors = getRootDirSectors();
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

int getClusterOffset(bpb_t *bpb, int32_t clusterNumber)
{
    // printf("Inside getClusterOffset\n");

    int clusterOffset;
    
    if(clusterNumber == 0)
        clusterNumber = 2;
    else
    {
        clusterOffset = ((clusterNumber -2) * bpb->BPB_BytsPerSec) + 
        (bpb->BPB_BytsPerSec * bpb->BPB_RsvdSecCnt) + (bpb->BPB_NumFATs * bpb->BPB_FATSz32 * bpb->BPB_BytsPerSec);
        printf("Cluster offset = %d\n", clusterOffset);
    }

    return clusterOffset;
    
    // return ((clusterNumber -2) * bpb->BPB_BytsPerSec) + 
    //     (bpb->BPB_BytsPerSec * bpb->BPB_RsvdSecCnt) + 
    //     (bpb->BPB_NumFATs * bpb->BPB_FATSz32 * bpb->BPB_BytsPerSec);
}

void traverseDirectoryChain(FILE* fd, uint32_t clusterNumber)
{
    printf("clusterNumber: %d\n", clusterNumber);
    while (clusterNumber != 0xFFFFFFFF && clusterNumber >= 2)
    {
        printf("got here\n");
        // Assuming each entry is of type dentry_t
        dentry_t *dentry = encode_dir_entry(fd, getClusterOffset(&bpb, clusterNumber)); // clusterNumber may be wrong

        // Process the entry as needed (e.g., print the directory name)
        printf("%s\n", dentry->DIR_Name);

        // Move to the next cluster in the chain
        clusterNumber = dentry->DIR_FstClusLO | (dentry->DIR_FstClusHI << 16);

        // Free the memory allocated for the entry
        free(dentry);
    }
}

void findCWD(FILE* fd, int32_t currentDirectory)
{
    // Assuming currentDirectory is the cluster number of the desired directory
    traverseDirectoryChain(fd, currentDirectory);
}

void executeCD(FILE *fd, tokenlist *tokens)
{
    // decode the directory -> find entry
    int offset = 0x100400; // NEED TO CHANGE THIS TO A FUNCTION LATER PLZ NO MAGIC IN THIS CODE THX ********************************************
    
    fseek(fd, offset, SEEK_SET);

    int i = 0;
    while(1)
    {
        fread(&dir[i], 32, 1, fd); // 32 because of FAT32

        // no more directory entries or files to read
        if (dir[i].DIR_Attr == 0x00)
            break;

        // printf("tokens->items[1] is *%s*"\n, tokens->items[1]);

        if(dir[i].DIR_Attr == 0x10) // only look at directories
        {
            // 11 becuase there are 11 hexedecimal places for the directory/file name
            char *directory = malloc(11);
            memset(directory, '\0', 11);
            memcpy(directory, dir[i].DIR_Name, 11);
            // This string compare only checks the frist x amount of letters 
            // x is the number of letters of the word passed into via cd
            // So if you pass in BLUE, but there is BLUE1 and BLUE then it might go into wrong one
            if(!strncmp(tokens->items[1], directory, strlen(tokens->items[1])))
            {
                printf("YOU HAVE FOUND THE DIRECTORY TO GO INTO\n");
                getDirectoryClusterNumber(2, dir[i]);
                dentry = dir[i];
            }
        }
        i++;
    }
    printf("\n");

}

void executeLS(FILE *fd)
{
    int offset = 0x100400; // NEED TO CHANGE THIS TO A FUNCTION LATER PLZ NO MAGIC IN THIS CODE THX ********************************************
    fseek(fd, offset, SEEK_SET);

    int i = 0;
    while(1)
    {
        fread(&dir[i], 32, 1, fd); // 32 because of FAT32

        // no more directory entries or files to read
        if (dir[i].DIR_Attr == 0x00)
            break;

        if(
            (dir[i].DIR_Attr == 0x1 || dir[i].DIR_Attr == 0x10 || dir[i].DIR_Attr == 0x20))
        {
            // 11 becuase there are 11 hexedecimal places for the directory/file name
            char *directory = malloc(11);
            memset(directory, '\0', 11);
            memcpy(directory, dir[i].DIR_Name, 11);
            printf("%s", directory);
        }
        i++;
    }
    printf("\n");


// Bing:
// 1. alternative: we can read all entries inside root here - doing this one
// dentry_t 
// all_entries = get_entries_from_cluster()


// 2. need a data structure to keep track of cwd (char array)

// CWD: /DIR1/DIR2
// all_direntries = get_entries_from_cluster(rootCLuster) //getCluster
// for (token in cwd) {
//     for entry in all_direntry {
//         if toekn == entry->name {
//             found = true;
//             break;
//         }
//     }

//     if found != true {
//         "token not found"
//         brek;
//     }

//     cluster_of_the_found_entry = entry->cluHi << 16 + entry->cluLo;
//     all_direntries = get_entries_from_cluster(cluster_of_the_found_entry)
// }

}

// you can give it another name
// fill the parameters
void main_process(FILE* fd, const char* FILENAME)
{
    int done = 0;
    
    while (!done)
    {
        printf("%s", FILENAME); // need to change this
        // for(int i = 0; i < sizeOfCWD; i++)
        // {
        //     printf("%c", cwd[i]);

        // }
        printf("> ");

        char *input = get_input();
		tokenlist *tokens = get_tokens(input); // tokanize

        // If no entry
		if(tokens->size != 0) //if no input, move on and ask for input again 
        {
			if(strcmp(tokens->items[tokens->size-1], "info") == 0)
            {
                executeInfo(&bpb);
            }
            else if(strcmp(tokens->items[tokens->size-1], "exit") == 0)
            {
                done = 1;
            }
            else if(strcmp(tokens->items[0], "ls") == 0)
            {
                executeLS(fd);
            }
            else if(strcmp(tokens->items[0], "cd") == 0)
            {
                executeCD(fd, tokens);
            }
        }

		free(input);
		free_tokens(tokens);


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
    // 1. open the fat32.img - original code was open the file in read only
    // fd = open(argv[1], O_RDWR); // read and write
    FILE* fd = fopen(argv[1], "rw");
    if (fd < 0) { 
        perror("Error opening file failed: ");
        return 1;
    }

    //may be in wrong place in main
    uint32_t offset = 0x100420;
    dentry_t *dentry = encode_dir_entry(fd, offset);

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

    free(dentry);
    // close(fd);

    // 5. close the fat32.img
    fclose(fd);

    return 0;
}










// lexer.c
char *get_input(void)
{
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

tokenlist *new_tokenlist(void)
{
	tokenlist *tokens = (tokenlist *)malloc(sizeof(tokenlist));
	tokens->size = 0;
	tokens->items = (char **)malloc(sizeof(char *));
	tokens->items[0] = NULL; /* make NULL terminated */
	return tokens;
}

void add_token(tokenlist *tokens, char *item)
{
	int i = tokens->size;

	tokens->items = (char **)realloc(tokens->items, (i + 2) * sizeof(char *));
	tokens->items[i] = (char *)malloc(strlen(item) + 1);
	tokens->items[i + 1] = NULL;
	strcpy(tokens->items[i], item);

	tokens->size += 1;
}

tokenlist *get_tokens(char *input)
{
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

void free_tokens(tokenlist *tokens)
{
	for (int i = 0; i < tokens->size; i++)
		free(tokens->items[i]);
	free(tokens->items);
	free(tokens);
}