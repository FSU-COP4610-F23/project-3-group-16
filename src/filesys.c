/*
Things to fix:
- close LONGFILE, ls, lsof, ls --> makes BLUE print ILEBLUE(
- Check if the file exists (command line argument)
- readOneCluster for loop has magic number
- read

Things to implement from project description
- cd (.. and error checking no name)
- read

Questions:
- how do you find the next cluster chain and how do you know where the file ends?
    if it is not a valid clusterNumber then it is the end of the chain(file), section 3.5 of spec, 0x000002 to MAX
    MAX = max valid cluster number (countOfClusters + 1)

Grading Policy:
- Repository layout
- ReadME/Division of Labor
- Review Makefile
- cd
    - *cd .. and ls at the root
- read is failing us rn (hard coded to HELLO)

*/

#include "filesys.h"
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
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
    uint32_t BPB_FATSz32;
    uint16_t BPB_ExtFlags;
    uint16_t BPB_FSVer;
    uint32_t BPB_RootClus;
    uint16_t BPB_FSInfo;
    uint16_t BPB_BkBootSec;
    char BPS_Reserved[12];
    char padding1[18];
    char BS_FilSysType[8];

} bpb_t;

typedef struct __attribute__((packed)) directory_entry
{
    char DIR_Name[11]; // Name of directory retrieved
    uint8_t DIR_Attr;  // Attribute count of directory retreived
    char padding_1[8]; // DIR_NTRes, DIR_CrtTimeTenth, DIR_CrtTime, DIR_CrtDate, 
                       // DIR_LstAccDate. Since these fields are not used in
                       // Project 3, just define as a placeholder.
    uint16_t DIR_FstClusHI;
    char padding_2[4]; // DIR_WrtTime, DIR_WrtDate
    uint16_t DIR_FstClusLO;
    uint32_t DIR_FileSize;
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
    uint32_t startingLocation;
} fileInfo;

// VARIABLES 
bpb_t bpb;
const int ENTRIESINDIR = 16;
dentry_t dir[16]; // store the entries of directory
dentry_t dentry; // this is our current directory 
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
dentry_t dentryPath[25];
int dentryPathLocation;


// FUNCTIONS
//decoding directory entry
dentry_t *encode_dir_entry(FILE *fd, uint32_t offset)
{
    dentry_t *temp = (dentry_t*)malloc(sizeof(dentry_t));
    fseek(fd, offset, SEEK_SET);

    size_t rd_bytes = fread((void*)temp, sizeof(dentry_t), 1, fd);
    
    if(rd_bytes != 1)
    {
        fprintf(stderr, "Error reading directory entry from file. \n");
        free(temp);
        return NULL;
    }

    return temp;
}

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
    dentry.DIR_FileSize = new->DIR_FileSize; // Size of directory
}

int getRootDirSectors()
{
    return ((bpb.BPB_RootEntCnt * 32) + (bpb.BPB_BytsPerSec - 1)) / bpb.BPB_BytsPerSec;
}

int getDirSectorsForClusNum(uint32_t clus_num) // gives you the sector for clus_num cluster
{
    return ((clus_num * 32) + (bpb.BPB_BytsPerSec - 1)) / bpb.BPB_BytsPerSec; // BytsPerSec is 512
}

uint32_t getDirectoryOffset(dentry_t *dirToGoTo) // clusterNumber = N
{
    uint16_t newClusterNumber = (dirToGoTo->DIR_FstClusHI << 16) + dirToGoTo->DIR_FstClusLO; // gives us hex
    uint32_t newAddress = dataRegionStart + (newClusterNumber - 2) * bpb.BPB_BytsPerSec;
    return newAddress;
    // newClusterNumber gets you to a place that tells you how many fiels and directories are in the new directory
    // the number tells you how many entries you need to update in dir
}

void initializeVariables() // when you enter a new directoory, change the variables
{
    FATRegionStart = bpb.BPB_BytsPerSec * bpb.BPB_RsvdSecCnt; // this is the 0x4000
    dataRegionStart = FATRegionStart + (bpb.BPB_FATSz32 * bpb.BPB_NumFATs * bpb.BPB_BytsPerSec); // this is 0x100400
    
    int clusterNumber = (dentry.DIR_FstClusHI << 16) + dentry.DIR_FstClusLO; // gives us hex
    currentDirectory = getDirectoryOffset(&dentry);

    // add dentry to array
    for(int i = 0; i  < 11; i++)
    {
        dentryPath[dentryPathLocation].DIR_Name[i] = dentry.DIR_Name[i];
    }
    dentryPath[dentryPathLocation].DIR_Attr = dentry.DIR_Attr;
    dentryPath[dentryPathLocation].DIR_FstClusHI = dentry.DIR_FstClusHI;
    dentryPath[dentryPathLocation].DIR_FstClusLO = dentry.DIR_FstClusLO;
    dentryPath[dentryPathLocation].DIR_FileSize = dentry.DIR_FileSize;

    dentryPathLocation++;

    FATEntryOffset = FATRegionStart + (4 * clusterNumber);

    rootDirSectors = getRootDirSectors();
    dataSec = bpb.BPB_TotSec32 - (bpb.BPB_RsvdSecCnt + 
        (bpb.BPB_NumFATs * bpb.BPB_FATSz32) + rootDirSectors);
    countOfClusters = dataSec / bpb.BPB_SecPerClus;

    MAXValidClusterNum = countOfClusters + 1;
}

void mount_fat32(FILE* fd)
{
    dentry_t empty;
    for(int i = 0; i  < 11; i++)
    {
        dentryPath[0].DIR_Name[i] = empty.DIR_Name[i];
    }
     // Name of directory retrieved
    dentryPath[0].DIR_Attr = empty.DIR_Attr;   // Attribute count of directory retreived
    dentryPath[0].DIR_FstClusHI = empty.DIR_FstClusHI;
    dentryPath[0].DIR_FstClusLO = empty.DIR_FstClusLO;
    dentryPath[0].DIR_FileSize = empty.DIR_FileSize; // Size of directory (always 0)

    // read the bpb data structure
    fseek(fd, 0, SEEK_SET);
    fread(&bpb, sizeof(bpb_t), 1, fd); // initializing bpb here

    fread(&dir[0], 32, ENTRIESINDIR, fd);
    initializeVariables();
    FATRegionStart = bpb.BPB_BytsPerSec * bpb.BPB_RsvdSecCnt; // this is the 0x4000
    currentDirectory = FATRegionStart + (bpb.BPB_FATSz32 * bpb.BPB_NumFATs * bpb.BPB_BytsPerSec); // this is 0x100400
}

void executeInfo(bpb_t *bpb)
{
    printf("Position of Root Cluster: %d\n", bpb->BPB_RootClus);
    printf("Bytes Per Sector: %d\n", bpb->BPB_BytsPerSec);
    printf("Sectors Per Cluster: %d\n", bpb->BPB_SecPerClus);
    printf("Total Number of Clusters in Data Region: %d\n", countOfClusters);
    printf("Number of Entries in One FAT: %d\n", (bpb->BPB_FATSz32 * (bpb->BPB_BytsPerSec / 4)));
    struct stat st;
    if (stat("fat32.img", &st) == 0)
    {
        printf("Size of Image (in bytes): %d\n", (int)(((intmax_t)st.st_size)));
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

void openAndStoreFile(FILE* fd, uint32_t address, tokenlist *tokens, uint32_t fileSize, dentry_t *fileDentry)
{
    if(executeCloseLseekRead(fd, tokens, 3) == 0)
    {
        // file already open
        printf("Error: file already open\n");
        return;
    }
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
            openedFiles[i].startingLocation = getDirectoryOffset(&fileDentry);
            // printf("Inside Open and Store File, starting location is: %d\n", openedFiles[i].startingLocation);

            printf("Opened %s\n", tokens->items[1]);
            flag = 1;
            break;
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
// 3. close (file) 
// 4. lseek (file, changes offset)
// 5. read (file)
// return 0 means you are done reading
// return 3 if you have read everything in the directory and not found what you need to find
int readOneCluster(FILE* fd, uint32_t address, int toDo, tokenlist *tokens)
{
    fseek(fd, address, SEEK_SET);

    for(int i = 0; i < ENTRIESINDIR; i++)
    {
        fread(&dir[i], 32, 1, fd); // 32 because of FAT32

        // no more directory entries or files to read
        if (dir[i].DIR_Attr == 0x00)
        {
            return 3;
        }

        if(dir[i].DIR_Attr == 0x1 || dir[i].DIR_Attr == 0x10 || dir[i].DIR_Attr == 0x20)
        {
            // 11 because there are 11 hexedecimal places for the directory/file name
            char *directoryContent = malloc(11);
            memset(directoryContent, '\0', 11);
            memcpy(directoryContent, dir[i].DIR_Name, 11);

            if(toDo == 0) // ls
            { 
                printf("%s", directoryContent);
            }
            else if(toDo == 1 || (toDo == 3)) // cd-->1 close-->3
            {
                if(!strncmp(tokens->items[1], directoryContent, strlen(tokens->items[1])))
                {
                    if(dir[i].DIR_Attr == 0x10) // only look at directories
                    {
                        if(toDo == 1)
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
                            return 0;
                        }
                    }
                    else
                    {
                        if(toDo == 3)
                        {
                            printf("Error: I found what you are looking for, but it's not open so I can't close it\n");
                            return 0;
                        }
                        else // cd
                        {
                            printf("Error: whatever you are looking for is not a directory\n");
                            return 0;
                        }
                    }
                }
            }
            else if(toDo == 2) // open
            {
                if(!strncmp(tokens->items[1], directoryContent, strlen(tokens->items[1])))
                {
                    if(dir[i].DIR_Attr != 0x10) // not a directory so could be file we are looking to open
                    {
                        // already know the command line arguments are good when you reach here
                        // function that opens and stores the opened file
                        openAndStoreFile(fd, address, tokens, dir[i].DIR_FileSize, &dir[i]);
                        return 0;
                    }
                    else
                    {
                        printf("Error: you tried to open a directory not a file\n");
                        return 1;
                    }
                }
            }
        }
    }
    return 1;
}

uint32_t convert_clus_num_to_offset_in_fat_region(uint32_t clus_num)
{
    return FATRegionStart + clus_num * 4;
}

uint32_t convert_clus_num_to_offset_in_data_region(uint32_t clus_num, uint32_t dataRegionStart)
{
    uint32_t clus_size = bpb.BPB_BytsPerSec * bpb.BPB_SecPerClus;
    return dataRegionStart + (clus_num - 2) * clus_size;
}

void traverseClusterChain(FILE* fd, uint32_t startAddress, tokenlist *tokens, int toDo)
{
    uint32_t curr_clus_num = (startAddress - dataRegionStart) / 
        (bpb.BPB_BytsPerSec*bpb.BPB_SecPerClus); // how many clusters before address
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
        int value = readOneCluster(fd, currentAddress, toDo, tokens);
        if(value == 0)
        {
            break;
        }
        else if(value == 3)
        {
            if(toDo != 0) // ls so don't print this
            printf("Error: did not find what you are looking for. so sorry\n");
        }

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
    // only passed in cd not where to go
    if(tokens->size < 2)
    {
        dentryPathLocation = 1;
        changeDentry(&dentryPath[0]); // changes dentry to previous dentry
        currentDirectory = getDirectoryOffset(&dentryPath[0]);
        return;
    }
    // if not ..
    if(strcmp(tokens->items[1], ".."))
    {
        // not .., so enter new
        traverseClusterChain(fd, currentDirectory, tokens, 1);
    }
    else // cd ..
    {
        if(dentryPathLocation == 1)
        {
            printf("Can not go back a directory, already at root\n");
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
            dentryPathLocation--;
            currentDirectory = getDirectoryOffset(&dentryPath[dentryPathLocation - 1]);
        }
    }
}

void executeOpen(FILE* fd, tokenlist *tokens)
{
    // open a file and print "opened LONGFILE"
    // look for file, then open
    // file name and access level is passed in through tokens 

    if(tokens->size != 3)
    {
        printf("Wrong number of command line arguments, file not opened\n");
    }
    else
    {
        // if invalid flag passed in
        if((!strcmp(tokens->items[2], "-w")) || (!strcmp(tokens->items[2], "-r")) || 
            (!strcmp(tokens->items[2], "-rw")) || (!strcmp(tokens->items[2], "-wr")))
        {
            // good flag passed in
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
            printf("\n");
        }
    }
}

// toDo:
// 0 for close
// 1 for lseek
// 2 for read
// 3 for open
int executeCloseLseekRead(FILE* fd, tokenlist *tokens, int toDo) 
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
                        if (toDo == 3)
                        {
                            // file already open;
                            return 0;
                        }
                    }
                    if(flag == 0) // If you found the file name (and directory) in the array
                    {
                        if(toDo == 0) // close
                        {
                            for(int j = 0; j < openedFiles[i].pathSize; j++)
                            {
                                openedFiles[i].name[j] = '\0'; // fixes the HELLOILE issue
                            }
                            // close the file
                            openedFiles[i].index = -1;
                            printf("Closed file\n");
                            return 1; // you closed the file so stop looking to close the file 
                        }
                        else if(toDo == 1) // lseek
                        {
                            // change the offset
                            int offset = atoi(tokens->items[2]);
                            if(offset <= openedFiles[i].fileSize)
                            {
                                openedFiles[i].offset = offset; 
                            }
                            return 1; // lseek did it's job 
                        }
                        else if(toDo == 2) // read
                        {
                            // changes passed in offset to number
                            int requestedSize = atoi(tokens->items[2]); 
                            int BUFFER_SIZE = 1000;

                            if(!strcmp(openedFiles[i].mode, "w"))
                            {
                                printf("Error: File is not open for reading\n");
                                break;
                            }

                            // read and print the data until reaching end of file or requested size (from command line)
                            char buffer[BUFFER_SIZE]; // this is a temp char array to hold the 10 in "read HELLO 10"
                            
                            // check that read number not too high
                            if(openedFiles[i].fileSize > (requestedSize + openedFiles[i].offset))
                            {
                                printf("startingLocation is: %d\n", openedFiles[i].startingLocation); // 1195008: 1142272
                                // 1333248 1350656
                                // fseek(fd, openedFiles[i].startingLocation, SEEK_SET);
                                fseek(fd, 1270272, SEEK_SET);
                                

                                // BELOW WORKS
                                fread(&buffer, sizeof(buffer), 1, fd);

                                // print the read data
                                for(int j = 0; j < requestedSize; j++)
                                {
                                    printf("%c", buffer[j]);
                                }
                                printf("\n");
                                
                                // change offset after reading;
                                openedFiles[i].offset += requestedSize;

                                break;
                            }
                            else 
                            {
                                printf("Error: file not large enough\n");
                                break;
                            }













                            // int requestedSize = atoi(tokens->items[2]); // read HELLO 10 - this line takes the "10" and turns it into an int (from char)
                            // int BUFFER_SIZE = 1000;
                            // // need to check if file is even opened first - which we do above (?)
                            
                            // // check if file I want to open is in read mode
                            // if(openedFiles[i].mode == "w")
                            // {
                            //     printf("Error: File is not open for reading\n");
                            //     break;
                            // }

                            // // find find the cluster number for the offset -> read data

                            // //convert the offset of the open file to the cluster number
                            // //convert the cluster number to the data region offset 

                            // // convert offset of opened file to cluster number using this formula
                            //     // cluster_num  = file_first_cluster_number + offset / cluster_size
                            //     // cluster_num = HELLO_first_cluster_number + HELLO_offset / 4 or 32 - idk if this is right
                            
                            // openedFiles[i].offset = 1049692; // starting address of HELLO

                            // // we want to see 1049692 which is 10045C (start of HELLO)
                            // // we want to see 1270272 which is 136200 (start of Hi there!)
                            // int clusterNumber = openedFiles[i].offset / 4;        
                            // int clusterNumber32 = openedFiles[i].offset / 32;                        
                            // printf("clusterNumber is %d\nclusterNumber32 is %d\n", clusterNumber, clusterNumber32);
                            
                            // int dataRegionOffset = dataRegionStart + (clusterNumber * 4);
                            // int dataRegionOffset32 = dataRegionStart + (clusterNumber32 * 32);
                            // printf("dataRegionOffset is %d\ndataRegionOffset32 %d", dataRegionOffset, dataRegionOffset32);

                            // // uint32_t dataRegionOffset32 = convert_clus_num_to_offset_in_data_region(clusterNumber32, dataRegionStart);
                            // // uint32_t dataRegionOffsetOF = convert_clus_num_to_offset_in_data_region(openedFiles[i].offset, dataRegionStart);
                            // // printf("dataRegionOffset32 is %d\ndataRegionOffsetOF %d", dataRegionOffset32, dataRegionOffsetOF);
                            
                            // fseek(fd, dataRegionOffset, SEEK_SET);
                            
                            // // read and print the data until reaching end of file or requested size (from command line)
                            // int bytesRead = 0;
                            // char buffer[BUFFER_SIZE]; // this is a temp char array to hold the 10 in "read HELLO 10"
                            
                            // while(bytesRead < requestedSize && !feof(fd)) // reads to end of file
                            // {
                            //     int bytesToRead = requestedSize - bytesRead;
                                
                            //     if(bytesToRead > BUFFER_SIZE)
                            //     {
                            //         bytesToRead = BUFFER_SIZE;
                            //         printf("Error: reached end of file\n");
                            //     }

                            //     int read = fread(buffer, 1, bytesToRead, fd);
                            //     // int read = fread(fd, 1, bytesToRead, buffer);
                            //     bytesRead += read;

                            //     // print the read data
                            //     for(int j = 0; j < read; j++)
                            //     {
                            //         printf("%c", buffer[j]);
                            //     }
                            // }

                            // printf("\n");

                            // // update the offset in openedFiles struct
                            // openedFiles[i].offset += bytesRead; // idk what this is, might be wrong

                            break;
                        }
                        break;
                    }
                }
                break;
            } 
        }
    } // went through opened file and did not find what your looking for
    if(toDo == 0) // close
    {
        //travers to see if the file is in our directory, if traverse return 3 then you print file not found and not open yet
        traverseClusterChain(fd, currentDirectory, tokens, 3);
    }
    else if(toDo == 1) // lseek
    {
        printf("Error: cannot lseek a file that is not open\n");
    }
    else if(toDo == 2) // read
    {
        printf("Error: cannot read a file that is not open yet\n");
    }
    return 1;
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
        // initializing the openedFiles array
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
                executeCloseLseekRead(fd, tokens, 0);
            }
            else if(strcmp(tokens->items[0], "lsof") == 0)
            {
                executeLSOF(fd);
            }
            else if(strcmp(tokens->items[0], "lseek") == 0)
            {
                executeCloseLseekRead(fd, tokens, 1);
            }
            else if(strcmp(tokens->items[0], "read") == 0)
                executeCloseLseekRead(fd, tokens, 2);
        }
		free(input);
		free_tokens(tokens);
    }
    printf("\n"); // New line after done the program
}

int main(int argc, char const *argv[])
{
    sizeOfPrompt = 0;
    dentryPathLocation = 0; 
    // check that file name was passed in ********************************************************************************************************************
    // check that file exists ******************************************************************************************************************** 
    FILE* fd = fopen(argv[1], "rw");
    if (fd < 0)
    { 
        perror("Error opening file failed: ");
        return 1;
    }

    // mount the fat32.img
    mount_fat32(fd);   
    const char* FILENAME = argv[1];

    // main procees
    main_process(fd, FILENAME);

    // close the fat32.img
    fclose(fd);

    return 0;
}

/*
    The following copy and pasted from project 1 to tokenize the input
*/
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