/*
Things to fix:
- close FILE, ls, lsof, ls --> makes BLUE print ILEBLUE(
- open does not check if file already open
- Check if the file exists (command line argument)
- readOneCluster for loop has magic number


Things to implement from project description
- cd (.. and error checking no name)
- mkdir
- creat
- open (started)
- close (started)
- read
- append
- rm


Questions:
- what is the offset for lsof?

- how do you find the next cluster chain and how do you know where the file ends?
    if it is not a valid clusterNumber then it is the end of the chain(file), section 3.5 of spec, 0x000002 to MAX
    MAX = max valid cluster number (countOfClusters + 1)

- How do you cd .. (storing where you came from)

Things To Do:

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
    uint32_t DIR_FileSize; // Size of directory - When you write to the file you need to update this number :)
} dentry_t;

typedef struct fileInformation
{
    int index; // index does not change once established
    char name[11];
    char mode[2];
    uint32_t offset;
    char path[256];
    int pathSize;
    uint32_t fileSize;
} fileInfo;

// VARIABLES 
bpb_t bpb; // instance of the struct
dentry_t dir[16]; // 16 is arbitrary and magic number
dentry_t dentry; // this might be our current directory but right now it is not saved that way
uint32_t currentDirectory; // this is the address of our current directory
char *prompt[256];
int sizeOfPrompt;
uint32_t FATRegionStart;
uint32_t dataRegionStart;
uint32_t FATEntryOffset;
uint32_t MAXValidClusterNum;
int rootDirSectors;
int dataSec;
int countOfClusters;
fileInfo openedFiles[10];


// FUNCTIONS
//decoding directory entry
dentry_t *encode_dir_entry(FILE *fd, uint32_t offset)
{
    dentry_t *temp = (dentry_t*)malloc(sizeof(dentry_t));
    fseek(fd, offset, SEEK_SET);

    size_t rd_bytes = fread((void*)temp, sizeof(dentry_t), 1, fd);
    
    // omitted: check rd_bytes == sizeof(dentry_t)
    if(rd_bytes != 1)
    {
        fprintf(stderr, "Error reading directory entry from file. \n");
        free(temp);
        return NULL;
    }

    return temp;
}

// The following is from Bing and is related to the ls function

int getRootDirSectors()
{
    return ((bpb.BPB_RootEntCnt * 32) + (bpb.BPB_BytsPerSec - 1)) / bpb.BPB_BytsPerSec;
}


int getDirSectorsForClusNum(uint32_t clus_num) // gives you the sector for clus_num cluster
{
    return ((clus_num * 32) + (bpb.BPB_BytsPerSec - 1)) / bpb.BPB_BytsPerSec; // BytsPerSec is 512
}


int getDirectoryClusterNumber(int clusterNumber, dentry_t *dirToGoTo) // clusterNumber = N
{
    // int firstDataSector = bpb.BPB_RsvdSecCnt + (bpb.BPB_NumFATs * bpb.BPB_FATSz32) + getRootDirSectors();
    // << is binary left shift 
    uint16_t newClusterNumber = (dirToGoTo->DIR_FstClusHI << 16) + dirToGoTo->DIR_FstClusLO; // gives us hex
    // printf("first data sector is calculated to be: %d\n", firstDataSector); // right now printing 2050, should be 100400
    // printf("first sector of cluster is calculated to be: %d\n", firstSectorOfCluster);
    // printf("low is %d, high is %d\n", dirToGoTo->DIR_FstClusLO, dirToGoTo->DIR_FstClusHI);
    // printf("new cluster number is calculated to be: %d\n", newClusterNumber);

    uint32_t newAddress = dataRegionStart + (newClusterNumber - 2) * bpb.BPB_BytsPerSec;
    // printf("new address is calculated to be: %d\n", newAddress);
    return newAddress;

    // newClusterNumber gets you to a place that tells you how many fiels and directories are in the new directory
        // the number tells you how many entries you need to update in dir
    // then you go to the first cluster int he data region (using 100400 +433-2) * 512 = the number on the left in hexedit
}

void initializeVariables()
{
    FATRegionStart = bpb.BPB_BytsPerSec * bpb.BPB_RsvdSecCnt; // this is the 0x4000
    dataRegionStart = FATRegionStart + (bpb.BPB_FATSz32 * bpb.BPB_NumFATs * bpb.BPB_BytsPerSec); // this is 0x100400
    
    int clusterNumber = (dentry.DIR_FstClusHI << 16) + dentry.DIR_FstClusLO; // gives us hex
    // int clusterNumber = 2;
    currentDirectory = getDirectoryClusterNumber(clusterNumber, &dentry); // passing in 2 because that is original cluster number 

    FATEntryOffset = FATRegionStart + (4 * clusterNumber);

    rootDirSectors = getRootDirSectors();
    dataSec = bpb.BPB_TotSec32 - (bpb.BPB_RsvdSecCnt + 
        (bpb.BPB_NumFATs * bpb.BPB_FATSz32) + rootDirSectors);
    countOfClusters = dataSec / bpb.BPB_SecPerClus;

    MAXValidClusterNum = countOfClusters + 1;

}

void mount_fat32(FILE* fd)
{
    // 1. decode the bpb
    // read the bpb data structure
    fseek(fd, 0, SEEK_SET);
    fread(&bpb, sizeof(bpb_t), 1, fd); // initializing bpb here

    fread(&dir[0], 32, 16, fd);
    initializeVariables();
    FATRegionStart = bpb.BPB_BytsPerSec * bpb.BPB_RsvdSecCnt; // this is the 0x4000
    currentDirectory = FATRegionStart + (bpb.BPB_FATSz32 * bpb.BPB_NumFATs * bpb.BPB_BytsPerSec); // this is 0x100400
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
    // int rootDirSectors = getRootDirSectors();
    // int dataSec = bpb->BPB_TotSec32 - (bpb->BPB_RsvdSecCnt + 
    //     (bpb->BPB_NumFATs * bpb->BPB_FATSz32) + rootDirSectors);
    // int countOfClusters = dataSec / bpb->BPB_SecPerClus;
    printf("Total Number of Clusters in Data Region: %d\n", countOfClusters); // this might be wrong *****************************
    // printf("Number of Entries in One FAT: %d\n", ); // number of entries / number of FATs // should print 129152 
    // 129152 = 1009 * 128, which is BPB_FatSz32 * 128, and 128 = 32 * 4
    
    struct stat st;
    if (stat("fat32.img", &st) == 0)
    {
        //good
        printf("Size of Image (in bytes): %d\n", (int)(((intmax_t)st.st_size))); // Look at slide 21 of S6
    }
}

int getClusterOffset(bpb_t *bpb, int32_t clusterNumber)
{
    int clusterOffset;
    
    if(clusterNumber == 0)
        clusterNumber = 2;
    else
    {
        clusterOffset = ((clusterNumber - 2) * bpb->BPB_BytsPerSec) + 
        (bpb->BPB_BytsPerSec * bpb->BPB_RsvdSecCnt) + (bpb->BPB_NumFATs * bpb->BPB_FATSz32 * bpb->BPB_BytsPerSec);
    }

    return clusterOffset;
}



/*
Bing notes
- each cluster is 512
- each entry is 32
- 512/32 = 16 which is why our for loop in ls is going to be 16
- the cluster chain is a linked link
- outer for loop for when the next thing is not 00
- Another function that is the next cluster number for the current cluster number
*/
//

void changeDentry(dentry_t *new)
{
    for(int i = 0; i  < 11; i++)
    {
        dentry.DIR_Name[i] = new->DIR_Name[i];
    }
     // Name of directory retrieved
    dentry.DIR_Attr = new->DIR_Attr;   // Attribute count of directory retreived
    dentry.DIR_FstClusHI = new->DIR_FstClusHI;
    dentry.DIR_FstClusLO = new->DIR_FstClusLO;
    dentry.DIR_FileSize = new->DIR_FileSize; // Size of directory (always 0)
}

void openAndStoreFile(FILE* fd, uint32_t address, tokenlist *tokens, uint32_t fileSize)
{
    int flag = 0;
    // find open position in openedFiles array
    for(int i = 0; i < 10; i++)
    {
        if(openedFiles[i].index == -1)
        {
            // found available spot
            openedFiles[i].index = i;
            for(int j = 0; j < strlen(tokens->items[1]); j++)
            {
                openedFiles[i].name[j] = tokens->items[1][j];
            }
            // start at 1 because don't want to save the "-"
            for(int j = 1; j < strlen(tokens->items[2]); j++)
            {
                openedFiles[i].mode[j-1] = tokens->items[2][j];
            }
            openedFiles[i].offset  = 0;

            openedFiles[i].pathSize = sizeOfPrompt;
            for(int j = 0; j < openedFiles[i].pathSize; j++)
            {
                openedFiles[i].path[j] = prompt[j];
            }
            openedFiles[i].path[sizeOfPrompt] = '\0';
            openedFiles[i].fileSize = fileSize;
            printf("Opened %s\n", tokens->items[1]);
            flag = 1;
            break; // leave for loopyloop
        }
    }
    if(flag == 0)
    {
        printf("Cannot open file because 10 already open\n");
    }
}


// this is going through the data region information
// toDo: 
// 0. LS
// 1. CD
// 2. open (file)
// 3. close (file) -> I don't think this is needed but keeping for now
// 4. lseek (file, changes offset)
// 5. read (file)
int readOneCluster(FILE* fd, uint32_t address, int toDo, tokenlist *tokens)
{
    fseek(fd, address, SEEK_SET);

    for(int i = 0; i < 16; i++) // cluster size/entry size *************************************************************************************************
    {
        fread(&dir[i], 32, 1, fd); // 32 because of FAT32

        // no more directory entries or files to read
        //change this to a flag not a break;
        if (dir[i].DIR_Attr == 0x00) // || dir[i].DIR_Name[0] == 0xE5
        {
            // break; //if you don't break you need to go to next cluster chain
            // done = 1;
            return 0;
        }

        if(dir[i].DIR_Attr == 0x1 || dir[i].DIR_Attr == 0x10 || dir[i].DIR_Attr == 0x20)
        {
            // 11 becuase there are 11 hexedecimal places for the directory/file name
            char *directoryContent = malloc(11);
            memset(directoryContent, '\0', 11);
            memcpy(directoryContent, dir[i].DIR_Name, 11);

            if(toDo == 0) // ls
            { 
                printf("%s", directoryContent);
            }
            if(toDo == 1) // cd
            {
                if(dir[i].DIR_Attr == 0x10) // only look at directories
                {
                    if(!strncmp(tokens->items[1], directoryContent, strlen(tokens->items[1])))
                    {
                        changeDentry(&dir[i]);
                        initializeVariables();
                        // add new directory to prompt
                        // add "/" then name
                        prompt[sizeOfPrompt] = '/';
                        sizeOfPrompt++;
                        for(int j = 0; j < strlen(tokens->items[1]); j++)
                        {
                            prompt[j+sizeOfPrompt] = dir[i].DIR_Name[j];
                        }
                        sizeOfPrompt += strlen(tokens->items[1]);
                        break;
                    }
                }
            }
            else if(toDo == 2) // open
            {
                if(dir[i].DIR_Attr != 0x10) // not a directory so could be file we are looking to open
                {
                    if(!strncmp(tokens->items[1], directoryContent, strlen(tokens->items[1])))
                    {
                        // already know the command line arguments are good when you reach here
                        // function that opens and stores the opened file
                        openAndStoreFile(fd, address, tokens, dir[i].DIR_FileSize);
                    }
                }
            }
            // 3 is close file (might not need this later)
            // 4 is lseek but not in this code
            // 5 is read
        }
    }
    return 1;
}

uint32_t convert_clus_num_to_offset_in_fat_region(uint32_t clus_num) {
    // uint32_t fat_region_offset = 0x4000;
    return FATRegionStart + clus_num * 4;
}

uint32_t convert_clus_num_to_offset_in_data_region(uint32_t clus_num, uint32_t dataRegionStart) {
    uint32_t clus_size = bpb.BPB_BytsPerSec * bpb.BPB_SecPerClus;
    // uint32_t data_region_offset = 0x100400;
    return dataRegionStart + (clus_num - 2) * clus_size;
}

void traverseClusterChain(FILE* fd, uint32_t startAddress, tokenlist *tokens, int toDo)
{
    // i know im not at the end so i do countOfClusters + 1 ??? 0x4000 + 4 bytes * 434
    // then check if it is a valid clusnum by checking if it is within the range 0x00002 to MAX
    // MAX is countOfClusters + 1

    uint32_t curr_clus_num = (startAddress - dataRegionStart) / (bpb.BPB_BytsPerSec*bpb.BPB_SecPerClus); // how many clusters before address
    curr_clus_num = curr_clus_num + 2;
    uint32_t max_clus_num = bpb.BPB_FATSz32 / bpb.BPB_SecPerClus;
    uint32_t min_clus_num = 2;
    uint32_t next_clus_num = 0;
    uint32_t offset = 0;

    fseek(fd, startAddress, SEEK_SET);
    uint32_t currentAddress;
    while (curr_clus_num >= min_clus_num && curr_clus_num <= max_clus_num) 
    {
        currentAddress = convert_clus_num_to_offset_in_data_region(curr_clus_num, dataRegionStart);
        if(readOneCluster(fd, currentAddress, toDo, tokens) == 0)
            break;

        //Example 3
        // if the cluster number is not in the range, this cluster is:
        // 1. reserved cluster
        // 2. end of the file
        // 3. bad cluster.
        // No matter which kind of number, we can consider it is the end of a file.
        offset = convert_clus_num_to_offset_in_fat_region(curr_clus_num);

        fseek(fd, offset, SEEK_SET);
        fread(&next_clus_num, sizeof(uint32_t), 1, fd);

        curr_clus_num = next_clus_num;
    }
}

void executeLS(FILE *fd, tokenlist *tokens)
{
    traverseClusterChain(fd, currentDirectory, tokens, 0);
    printf("\n");
}

void executeCD(FILE *fd, tokenlist *tokens)
{
    if(strcmp(tokens->items[1], ".."))
    {
        // not .., so enter new
        traverseClusterChain(fd, currentDirectory, tokens, 1);
    }
    else
    {
        for(int i = sizeOfPrompt-1; i >= 0; i--)
        {
            if(prompt[i] == '/')
            {
                sizeOfPrompt = i;
                break;
            }
        }
    }
}

void executeOpen(FILE* fd, tokenlist *tokens)
{
    // legit just open a file and print "opened LONGFILE"
    // look for file, then open
    // file name and access level is passed in through tokens 

    if(tokens->size != 3)
    {
        printf("Wrong number of command line arguments, file not opened\n");
    }
    else
    {
        //check if already open
        // *code*
        // if invalid flag passed in
        if((!strcmp(tokens->items[2], "-w")) || (!strcmp(tokens->items[2], "-r")) || 
            (!strcmp(tokens->items[2], "-rw")) || (!strcmp(tokens->items[2], "-wr")))
        {
            // good
            traverseClusterChain(fd, currentDirectory, tokens, 2);
        }
        else
        {
            printf("Passed in flag incorrect\n");
        }
    }
}

void executeLSOF(FILE* fd)
{
    printf("INDEX\t\tNAME\t\tMODE\t\tOFFSET\t\tPATH\n");
    // go through open file and print
    // find open position in openedFiles array
    for(int i = 0; i < 10; i++)
    {
        if(openedFiles[i].index != -1)
        {
            // found available spot
            printf("%-12d\t", openedFiles[i].index);
            printf("%-12s\t", openedFiles[i].name);
            printf("%-12s\t", openedFiles[i].mode);
            printf("%-12d\t", openedFiles[i].offset);
            printf("%s", openedFiles[i].path);
            // for(int j = 0; j < openedFiles[i].pathSize; j++)
            // {
            //     printf("%c", openedFiles[i].path[j]);
            // }
            printf("\n");
        }
    }
}

// toDo:
// 0 for close
// 1 for lseek
void executeCloseFileAndLSEEK(FILE* fd, tokenlist *tokens, int toDo)
{
    for(int i = 0; i < 10; i++)
    {
        // current entry is opened file
        if(openedFiles[i].index != -1) 
        {
            // checking the name
            if(!strncmp(openedFiles[i].name, tokens->items[1], sizeof(tokens->items[1])))
            {
                // check path
                // first check they are the same size
                if(openedFiles[i].pathSize == sizeOfPrompt)
                {
                    int flag = 0;
                    for(int j = 0; j < openedFiles[i].pathSize; j++)
                    {
                        if(openedFiles[i].path[j] != prompt[j])
                        {
                            printf("Path and prompt do not match\n");
                            flag = 1;
                        }
                    }
                    if(flag == 0)
                    {
                        if(toDo == 0) // close
                        {
                            for(int j = 0; j < openedFiles[i].pathSize; j++)
                            {
                                openedFiles[i].name[j] = '\0'; // fixes the HELLOILE issue
                            }
                            // close the file
                            openedFiles[i].index = -1;
                        }
                        else if(toDo == 1) // lseek
                        {
                            // change the offset
                            int offset = atoi(tokens->items[2]);
                            if(offset <= openedFiles[i].fileSize)
                            {
                                openedFiles[i].offset = offset; 
                            }
                        }
                    }
                }
                break;
            } 
        }
    }
}

void main_process(FILE* fd, const char* FILENAME)
{
    int FILENAMESize = sizeof(FILENAME) + 1;
    for(int i = 0; i < FILENAMESize; i++)
    {
        prompt[i] = FILENAME[i];
    }
    sizeOfPrompt = FILENAMESize;

    for(int i = 0; i < 10; i++)
    {
        // there are no open files
        openedFiles[i].index = -1; 
    }

    int done = 0;
    
    while (!done)
    {
        for(int i = 0; i < sizeOfPrompt; i++)
        {
            printf("%c", prompt[i]);
        }
        printf("> ");

        char *input = get_input();
		tokenlist *tokens = get_tokens(input); // tokenize

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
                executeLS(fd, tokens);
            }
            else if(strcmp(tokens->items[0], "cd") == 0)
            {
                executeCD(fd, tokens);
            }
            else if(strcmp(tokens->items[0], "open") == 0)
            {
                executeOpen(fd, tokens);
            }
            else if(strcmp(tokens->items[0], "close") == 0)
            {
                executeCloseFileAndLSEEK(fd, tokens, 0);
            }
            else if(strcmp(tokens->items[0], "lsof") == 0)
            {
                executeLSOF(fd);
            }
            else if(strcmp(tokens->items[0], "lseek") == 0)
            {
                executeCloseFileAndLSEEK(fd, tokens, 1);
            }
        }

		free(input);
		free_tokens(tokens);
    }

    printf("\n"); // New line after done the program; maybe delete this later
}

int main(int argc, char const *argv[])
{
    sizeOfPrompt = 0;
    FILE* fd = fopen(argv[1], "rw");
    if (fd < 0) { 
        perror("Error opening file failed: ");
        return 1;
    }

    //may be in wrong place in main
    uint32_t offset = 0x100420;
    dentry_t *dentryDecoded = encode_dir_entry(fd, offset); 
    if(dentryDecoded == NULL)
    {
        printf("File name passed in not found, exiting program\n");
        return 1;
    }

    // 2. mount the fat32.img
    // mount: decode BPB --> locate root directory --> decode the directory
    mount_fat32(fd);   
    const char* FILENAME = argv[1];

    // 3. main procees
    main_process(fd, FILENAME);

    // 4. close all opened files

    free(dentryDecoded);
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