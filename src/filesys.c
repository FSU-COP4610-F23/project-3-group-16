/*
Questions:
- what is the offset for lsof?
- why is the path not storing corectly when we open a file

- how do you find the next cluster chain and how do you know where the file ends?
    if it is not a valid clusterNumber then it is the end of the chain(file), section 3.5 of spec, 0x000002 to MAX
    MAX = max valid cluster number (countOfClusters + 1)

- why does cd BLUE1 not work
- How do you cd .. (storing where you came from)
- how do we print promt

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

typedef struct fileInformation
{
    int index; // index does not change once established
    char name[11];
    char mode[2];
    uint32_t offset;
    char path[256];
    int pathSize;
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

//MATH IS WRONG SO INFO IS WRONG ******************************************************************************************************
int getRootDirSectors()
{
    return ((bpb.BPB_RootEntCnt * 32) + (bpb.BPB_BytsPerSec - 1)) / bpb.BPB_BytsPerSec;
}

//MATH IS WRONG  ******************************************************************************************************

int getDirSectorsForClusNum(uint32_t clus_num) // gives you the sector for clus_num cluster
{
    return ((clus_num * 32) + (bpb.BPB_BytsPerSec - 1)) / bpb.BPB_BytsPerSec; // BytsPerSec is 512
}

// Bing Sudo
/*
clus_num = get_next_clus_num() //assumign this is correct then do the following
dataSector = getDirSectorsForClusNum(clus_num)
cluster_addr = dataSector * bytespersector;
fseek(cluster_addr) // use same for loop
*/

// int getFirstSectorOfCluster(int clusterNumber, int firstDataSector)
// {
//     return ((clusterNumber - 2) * bpb.BPB_SecPerClus) + firstDataSector;
// }









int getDirectoryClusterNumber(int clusterNumber, dentry_t *dirToGoTo) // clusterNumber = N
{
    // int firstDataSector = bpb.BPB_RsvdSecCnt + (bpb.BPB_NumFATs * bpb.BPB_FATSz32) + getRootDirSectors();
    // int firstSectorOfCluster = getFirstSectorOfCluster(clusterNumber, firstDataSector);
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


// you can give it another name
// fill the parameters
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
    initializeVariables();
    printf("Total Number of Clusters in Data Region: %d\n", countOfClusters); // this might be wrong *****************************
    // printf("Number of Entries in One FAT: %d\n", ); // number of entries / number of FATs // should print 129152 
    // 129152 = 1009 * 128, which is BPB_FatSz32 * 128, and 128 = 32 * 4
    
    struct stat st;
    if (stat("fat32.img", &st) == 0)
    {
        //good
        printf("Size of Image (in bytes): %d\n", (int)(((intmax_t)st.st_size))); // This might be wrong. Look at slide 21 of S6
    }
}

int getClusterOffset(bpb_t *bpb, int32_t clusterNumber)
{
    // printf("Inside getClusterOffset\n");

    int clusterOffset;
    
    if(clusterNumber == 0)
        clusterNumber = 2;
    else
    {
        clusterOffset = ((clusterNumber - 2) * bpb->BPB_BytsPerSec) + 
        (bpb->BPB_BytsPerSec * bpb->BPB_RsvdSecCnt) + (bpb->BPB_NumFATs * bpb->BPB_FATSz32 * bpb->BPB_BytsPerSec);
        printf("Cluster offset = %d\n", clusterOffset);
    }

    return clusterOffset;
    
    // return ((clusterNumber -2) * bpb->BPB_BytsPerSec) + 
    //     (bpb->BPB_BytsPerSec * bpb->BPB_RsvdSecCnt) + 
    //     (bpb->BPB_NumFATs * bpb->BPB_FATSz32 * bpb->BPB_BytsPerSec);
}

/*
//This funciton is currently not used
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
*/

void changeDentry(dentry_t *new)
{
    for(int i = 0; i  < 11; i++)
    {
        dentry.DIR_Name[i] = new->DIR_Name[i];
    }
     // Name of directory retrieved
    dentry.DIR_Attr = new->DIR_Attr;   // Attribute count of directory retreived
    // dentry.padding_1 = new.padding_1; // DIR_NTRes, DIR_CrtTimeTenth, DIR_CrtTime, DIR_CrtDate, 
                       // DIR_LstAccDate. Since these fields are not used in
                       // Project 3, just define as a placeholder.
    dentry.DIR_FstClusHI = new->DIR_FstClusHI;
    // dentry.padding_2 = new.padding_2; // DIR_WrtTime, DIR_WrtDate
    dentry.DIR_FstClusLO = new->DIR_FstClusLO;
    dentry.DIR_FileSize = new->DIR_FileSize; // Size of directory (always 0)
}

void enterNEWCD(FILE *fd, tokenlist *tokens)
{
    // decode the directory -> find entry
    fseek(fd, currentDirectory, SEEK_SET);

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
                changeDentry(&dir[i]);
                initializeVariables();
                // getDirectoryClusterNumber(2, &dir[i]);
                // dentry = dir[i]; // found new directory so changing dentry (dentry = current dentry)
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
        i++;
    }
    printf("\n");
}

void executeCD(FILE *fd, tokenlist *tokens)
{
    if(strcmp(tokens->items[1], ".."))
    {
        // not .., so enter new
        enterNEWCD(fd, tokens);
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

void openAndStoreFile(FILE* fd, uint32_t address, tokenlist *tokens)
{
    printf("inside openAndStoreFile\n");
    // find open position in openedFiles array
    for(int i = 0; i < 10; i++)
    {
        if(openedFiles[i].index == -1)
        {
            printf("inserting file now\n");
            // found available spot
            openedFiles[i].index = i;
            printf("seeing if index saved: %d\n", openedFiles[i].index);
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
            
            break; // leave for loopyloop
        }
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
int readOneCluster(FILE *fd, uint32_t address, int toDo, tokenlist *tokens)
{
    fseek(fd, address, SEEK_SET);

    for(int i = 0; i < 16; i++) // cluster size/entry size
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

            if(toDo == 0)
            { 
                printf("%s", directoryContent);
                // printf("%d\t", getDirectoryClusterNumber(0, &dir[i]));
                // int clusNumber = ((dir[i].DIR_FstClusHI << 16) + dir[i].DIR_FstClusLO);
                // printf("%d\t", clusNumber); // print cluster number
                // printf("%d\n", FATRegionStart + (4 * clusNumber));
            }
            else if(toDo == 2)
            {
                if(dir[i].DIR_Attr != 0x10) // not a directory so could be file we are looking to open
                {
                    if(!strncmp(tokens->items[1], directoryContent, strlen(tokens->items[1])))
                    {
                        printf("YOU HAVE FOUND THE FILE TO OPEN\n");
                        // already know the command line arguments are good when you reach here
                        // function that opens and stores the opened file
                        openAndStoreFile(fd, address, tokens);
                    }
                }
            }
            else if(toDo == 4)
            {

            }
        }
    }
    return 1;
}


void traverseClusterChain(FILE* fd, uint32_t startAddress, tokenlist *tokens)
{
    // i know im not at the end so i do countOfClusters + 1 ??? 0x4000 + 4 bytes * 434
    // then check if it is a valid clusnum by checking if it is within the range 0x00002 to MAX
    // MAX is countOfClusters + 1

    // uint32_t currentAddress = startAddress;

    readOneCluster(fd, startAddress, 0, tokens);

    // while(1)
    // {
    //     if(readOneCluster(fd, currentAddress) == 0)
    //         break;

    //     uint32_t nextCluster;
    //     fseek(fd, FATEntryOffset, SEEK_SET);
    //     printf("fseek no seg fault\n");
    //     fread(&nextCluster, 32, 1, fd);
    //     printf("fread no seg fault\n");

    //     // if(nextCluster >= MAXValidClusterNum)
    //     // {
    //     //     break; // hit the end, no more to print, invalid number
    //     // }

    //     // if(nextCluster >= 0x0FFFFFF8 && nextCluster <= 0x0FFFFFFF)
    //     //     break;

    //     currentAddress = &nextCluster; // move to next cluster
    //     currentAddress *= 32;
    //     printf("address is %d", currentAddress);

    // }

}


void executeLS(FILE *fd, tokenlist *tokens)
{
    traverseClusterChain(fd, currentDirectory, tokens);
    // fseek(fd, currentDirectory, SEEK_SET);

    // int i = 0;
    
    // //need to add a while loop here for the flag
    // int done = 0;
    // while(!done)
    // {
    //     for(i; i < 16; i++) // cluster size/entry size
    //     {
    //     }
    //     if(!done)
    //     {
    //         // fseek(fd, currentDirectory, SEEK_SET);
    //     }
    // }
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
            readOneCluster(fd, currentDirectory, 2, tokens);
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

void executeClose(FILE* fd, tokenlist *tokens)
{
    for(int i = 0; i < 10; i++)
    {
        // current entry is opened file
        if(openedFiles[i].index != -1) 
        {
            // checking the name
            if(!strncmp(openedFiles[i].name, tokens->items[1], sizeof(tokens->items[1])))
            {
                openedFiles[i].name[0] = '\0';
                // also name could be longer (like found cat1 open but want to close cat)
                // Found the open file but maybe not correct directory
                // if(!strncmp(openedFiles[i].path, prompt, sizeOfPrompt))
                // {d
                    // path matches 
                    openedFiles[i].index = -1;
                // }
                
            }
        }
    }
}

void executeLSEEK(FILE* fd, tokenlist *tokens)
{
    // see if file is open
    readOneCluster(fd, currentDirectory, 5, tokens);
    // change off set
}

// you can give it another name
// fill the parameters
void main_process(FILE* fd, const char* FILENAME)
{
    for(int i = 0; i < 10; i++)
    {
        openedFiles[i].index = -1;
    }

    int done = 0;
    
    while (!done)
    {
        printf("%s", FILENAME); // need to change this
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
                executeClose(fd, tokens);
            }
            else if(strcmp(tokens->items[0], "lsof") == 0)
            {
                executeLSOF(fd);
            }
            else if(strcmp(tokens->items[0], "lseek") == 0)
            {
                executeLSEEK(fd, tokens);
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
    sizeOfPrompt = 0;
    // 1. open the fat32.img - original code was open the file in read only
    // fd = open(argv[1], O_RDWR); // read and write
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
        printf("file name passed in not found, exiting program\n");
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