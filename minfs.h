/*Header file that contains useful structures and constants for reading the
 *Minix file system. 
 */

#ifndef MINFSH
#define MINFSH

#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*Ordered somewhat in terms of when they are needed*/

#define PARTITION_VALID_1 0x55 /*located @byte 510 of boot sector*/
#define PARTITION_VALID_2 0xAA /*located @byte 511 of boot sector*/

#define TABLE_START 0x1BE /*Start of the partition table*/
#define PARTITION_TYPE 0x81   /*Signifies fs is for minix*/

#define SECTOR_SIZE 512 /*Size of a sector*/

#define SUPER_START 1024 /*Start of the superblock in a partition*/
#define MAGIC 0x4D5A /*Magic number in superblock*/
#define MAGIC_REV 0x5A4D /*Magic number in superblock*/

#define INODE_SIZE 64 /*in bytes*/
#define DIR_SIZE 64   /*in bytes*/
#define ZONE_LEN 4    /*in bytes*/
#define PERM_LEN 11   /*in bytes*/

#define DIRECT_ZONES 7

/*Bit masks for inode modes*/
#define FILE_TYPE_MASK 0170000
#define REG_TYPE 0100000
#define DIR_TYPE 0040000
#define U_RD 0000400
#define U_WR 0000200
#define U_EX 0000100
#define G_RD 0000040
#define G_WR 0000020
#define G_EX 0000010
#define O_RD 0000004
#define O_WR 0000002
#define O_EX 0000001

/*Number of printable fields in the superblock*/
#define numSuperFields 10

/*Number of printable fields in the inode*/
#define numInodeFields 8

/*Helpful macros for file types*/
#define ISREG(m) (((m)&FILE_TYPE_MASK)==REG_TYPE)
#define ISDIR(m) (((m)&FILE_TYPE_MASK)==DIR_TYPE)
#define HASPERM(m,n) (((m)&(n))==n)

/*Structure of an entry in the partition table*/
typedef struct partition_entry
{
  uint8_t bootind;     /*Boot indicator*/
  uint8_t start_head;
  uint8_t start_sec;
  uint8_t start_cyl;
  uint8_t type;        /*Type of partition (0x81 is Minix)*/
  uint8_t end_head;
  uint8_t end_sec;
  uint8_t end_cyl;
  uint32_t lFirst;     /*First sector for linear addressing*/
  uint32_t size;       /*Size of partition*/
} *partEnt;

/*Structure of the superblock in a partition*/
typedef struct superblock
{
  uint32_t ninodes;      /*Number of inodes in this filesystem*/
  uint16_t pad1;
  int16_t i_blocks;      /*Num blocks used by inode bitmap*/
  int16_t z_blocks;      /*Num blocks used by zone bitmap*/
  uint16_t firstdata;    /*Number of first data zone*/
  int16_t log_zone_size; /*log2 of blocks per zone*/
  int16_t pad2;
  uint32_t max_file;     /*Maximum file size*/
  uint32_t zones;        /*Number of zones on disk*/
  int16_t magic;         /*Magic number*/
  int16_t pad3;
  uint16_t blocksize;    /*Block size (bytes)*/
  uint8_t subversion;    /*File system sub-version*/
} *super;

/*Structure of an inode*/
typedef struct inode
{
  uint16_t mode;
  uint16_t links;
  uint16_t uid;
  uint16_t gid;
  uint32_t size;
  int32_t atime;
  int32_t mtime;
  int32_t ctime;
  uint32_t zone[DIRECT_ZONES];
  uint32_t indirect;
  uint32_t two_indirect;
  uint32_t unused;
} *inode;

/*Structure of a directory entry*/
typedef struct __attribute__ ((__packed__)) directory_entry
{
  uint32_t inode;  /*inode number*/
  unsigned char name[60]; /*filename string*/
} *fileEnt;

/*Holds important stuff for minls to print out*/
typedef struct dir_listing
{
  fileEnt entry; /*For when we need to free it*/
  char *perms;   /*String version of the file permissions*/
  uint32_t size; /*in bytes*/
  char name[60];    /*name of the file*/
} *dirEnt;


/*This structure holds important values that we need to navigate the filesystem
 *It is filled out as we find the correct partition and inode. This structure
 *is passed back to the calling program (minls/get) for their specific use.
 */
typedef struct file_tools
{
  super superblock;  /*Contents of our filesystem's superblock*/
  inode inode;       /*Contents of the target's inode*/
  FILE *image;       /*Stream that we are reading from*/
  long offset;       /*Offset in the image file of where our filesystem is*/
  int zonesize;      /*Size of the zones in this filesystem (bytes)*/
  long inodeOff;     /*Offset to beginning of inode block*/
  long zoneOff;      /*Offset to beginning of zones*/
  int filePerZone;   /*Number of fileEnts per zone*/
  int zonesPerBlock; /*Number of zones in a block (indirect/2indirect)*/
  char *perms;       /*String version of inodes permissions*/
  int numFiles;      /*Number of files in a directory, 0 if regular file*/
  dirEnt *files;     /*List of dir_listings*/
} *tools;


/*Functions included*/
char *getMode(uint16_t perms);
int validatePart(FILE *image);
long findPartOffset(long offset, FILE *image, int sect);
long findPart(FILE *image, int part, int subpart);
tools getSuper(FILE *image, int part, int subpart);
inode getInode(tools target, int iNum);
void readZone(tools target, char *buffer, int zoneNum);
void readBlock(tools target, void *buffer, int zoneNum);
void readFEnt(tools target, fileEnt buffer, int zoneNum, int fIndex);
uint32_t getZoneNum(tools target, inode folder, int zoneNum);
fileEnt getMatch(tools target, inode folder, int numFiles, char *string);
int findFolder(tools target, char **path, int depth);
void getContents(tools target);
void readFile(tools target, FILE *destination);

#endif
