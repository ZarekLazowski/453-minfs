/*This file contains useful functions for navigating the given FILE * */

#include <stdio.h>
#include "minfs.h"

/*To stop gcc from yelling at me about how minls doesn't use the below
 *variables, I have moved them from minfs.h to here.*/

static uint8_t PART_SIG_1 = PARTITION_VALID_1;
static uint8_t PART_SIG_2 = PARTITION_VALID_2;
static uint8_t MIN_PART_TYPE = PARTITION_TYPE;

/*This function reads the mode and creates the permission string*/
char *getMode(uint16_t perms)
{
  char *string;

  string = malloc(sizeof(char) * PERM_LEN);
  
  /*If its a directory, save a 'd'*/
  string[0] = (ISDIR(perms) ? 'd': '-');
  /*User permissions*/
  string[1] = (HASPERM(perms,U_RD) ? 'r':'-');
  string[2] = (HASPERM(perms,U_WR) ? 'w':'-');
  string[3] = (HASPERM(perms,U_EX) ? 'x':'-');
  /*Group permissions*/
  string[4] = (HASPERM(perms,G_RD) ? 'r':'-');
  string[5] = (HASPERM(perms,G_WR) ? 'w':'-');
  string[6] = (HASPERM(perms,G_EX) ? 'x':'-');
  /*Other permissions*/
  string[7] = (HASPERM(perms,O_RD) ? 'r':'-');
  string[8] = (HASPERM(perms,O_WR) ? 'w':'-');
  string[9] = (HASPERM(perms,O_EX) ? 'x':'-');

  /*nul-byte*/
  string[10] = '\0';
  
  return string;
}

/*This function checks bytes 510 and 511 for the valid signature
 *Note that the valid bytes may not be with respect to the very beginning, 
 *    but instead to the beginning of the partition table.
 */
int validatePart(FILE *image)
{
  uint8_t bytesRead[2];
  
  /*Seek to signature bytes*/
  if( fseek(image, 510, SEEK_CUR) != 0 )
  {
    perror("fseek - validate");
    exit(EXIT_FAILURE);
  }

  /*Read 2 signature bytes into buffer*/
  if( fread(bytesRead, sizeof(uint8_t), 2, image) != 2 )
  {
    perror("fread - validate");
    exit(EXIT_FAILURE);
  }

  /*Starts out in the right order*/
  if(bytesRead[0] == PART_SIG_1)
  {
    if(bytesRead[1] == PART_SIG_2)
      return 0;
    else
    {
      fprintf(stderr, "Bad partition signature. (0x%X%X)", bytesRead[0],
	      bytesRead[1]);
      return -1;
    }
  }
  
  /*Starts out reversed*/
  /*TODO: flesh out logic to recognize big endian*/
  else if(bytesRead[0] == PART_SIG_2)
  {
    if(bytesRead[1] == PART_SIG_1)
    {
      fprintf(stderr, "Reversed partition signature. (0x%X%X)", bytesRead[0],
	      bytesRead[1]);
      return -1;
    }
    else
    {
      fprintf(stderr, "Bad partition signature. (0x%X%X)", bytesRead[0],
	      bytesRead[1]);
      return -1;
    }
  }

  else
  {
    fprintf(stderr, "Bad partition signature. (0x%X%X)", bytesRead[0],
	    bytesRead[1]);
    return -1;
  }
}

/*The workflow for finding the correct [sub]partition is more or less the same.
 *Given the offset of the current partition, this function goes to the 
 *partition table and finds the (verified) partition entry. The value 'lFirst'
 *in the partition table entry is returned as the new offset
 */
long findPartOffset(long offset, FILE *image, int sect)
{
  long newOffset;
  partEnt target;
  int err;

  /*Set the newOffset as the start of the partition*/
  newOffset = offset;
  
  /*Seek to partition table*/
  if( fseek(image, newOffset, SEEK_SET) != 0 )
  {
    perror("findPartOffset - fseek");
    exit(EXIT_FAILURE);
  }

  /*Validate the partition table*/
  if( (err = validatePart(image)) < 0 )
    /*Return the specific error*/
    return err;

  /*Calculate address for desired partition table entry.
  *Start of the partition + start of partition table
  * + (partition number * size of partition table entry) */
  newOffset = newOffset + TABLE_START +
    (sect * sizeof(struct partition_entry));
  
  if( fseek(image, newOffset , SEEK_SET) != 0 )
  {
    perror("findPartOffset - fseek");
    exit(EXIT_FAILURE);
  }

  /*Allocate memory for the partition entry*/
  target = malloc(sizeof(struct partition_entry));
  
  /*Read entry in partition table into data structure*/
  if( !fread(target, sizeof(struct partition_entry), 1, image) )
  {
    perror("findPartOffset - fread");
    exit(EXIT_FAILURE);
  }

  /*Confirm that the partition table is for minix*/
  if( target->type != MIN_PART_TYPE )
  {
    return -1;
    fprintf(stderr, "This partition table is not minix. (0x%X)\n",
	    target->type);
  }
  
  /*lFirst is sector number relative to beginning of the file, 
   *save the beginning of the partition as the new offset to read the 
   *file system from.
   */
  newOffset = (target->lFirst) * SECTOR_SIZE;

  /*The partition entry is no longer needed*/
  free(target);
  
  return newOffset;
}

/*This function, given partition and subpartition numbers, browses the image
 *file for the correct partition. It returns the offset of the desired file
 *system, or a negative number when the desired partition is not found.
 */
long findPart(FILE *image, int part, int subpart)
{
  long offset;

  /*Start at the beginning of the file*/
  offset = 0;

  /*Find the offset of the desired partition*/
  if( (offset = findPartOffset(offset, image, part)) < 0 )
    /*Return invalid offset*/
    return offset;

  /*If a subpartition is specified*/
  if(subpart >= 0)
    /*Find offset of that partition (offsets are relative to beginning of 
     *image). Do note that we just return immediately after this, so thats why
     *I'm not checking the return value.*/
    offset = findPartOffset(offset, image, subpart);
    
  /*Read partition table entry*/
  return offset;
}

/*This function fills out the superblock, offset, and zonesize portions of
 *the file_tools structure*/
tools getSuper(FILE *image, int part, int subpart)
{
  tools target;
  long targetOffset, ltemp;
  int temp;
  
  /*First determine the partition/subpartition, if a partition was specified*/
  if(part >= 0)
  {
    /*If the partition was not found, return an empty target. < 0 is invalid*/
    if( (targetOffset = findPart(image, part, subpart)) < 0 )
      return NULL;
  }
  else
    targetOffset = 0;

  /*Allocate memory for the tools*/
  target = malloc(sizeof(struct file_tools));

  /*Mark offset in the tools*/
  target->offset = targetOffset;

  /*Seek to the location of the superblock in filesystem*/
  if( fseek(image, targetOffset + SUPER_START, SEEK_SET) != 0 )
  {
    /*fseek failed*/
    perror("getTools super - fseek");
    exit(EXIT_FAILURE);
  }

  /*Allocate memory for the superblock*/
  target->superblock = malloc(sizeof(struct superblock));
  
  /*Lift the superblock data out of the image and into our data structure*/
  if( !fread(target->superblock, sizeof(struct superblock), 1, image) )
  {
    /*fread failed*/
    perror("getTools super - fread");
    exit(EXIT_FAILURE);
  }

  /*Validate the superblock by checking the magic number*/
  if( ((target->superblock)->magic) != MAGIC )
  {
    /*If it doesn't match the magic number, see if theres something wrong
     *(i.e. is backwards or is older)*/
    if( target->superblock->magic == MAGIC_REV )
    {
      fprintf(stderr, "Reversed magic number. (0x%X)\n",
	      target->superblock->magic);
      return NULL;
    }
    /*If it doesn't match any known magic numbers*/
    else
    {
      fprintf(stderr, "Bad magic number. (0x%X)\n", target->superblock->magic);
      return NULL;
    }
  }

  /*Figure out various values we will need to do everything*/
  
  /*Calculate zone size for reporting*/
  temp = target->superblock->blocksize << target->superblock->log_zone_size;
  target->zonesize = temp;

  /*Calculate offset for datazones*/
  target->zoneOff = target->offset +
    (target->superblock->firstdata * target->zonesize);
  
  /*Calculate how many fileEnt structs fit in one zone*/
  temp = target->zonesize / DIR_SIZE;
  target->filePerZone = temp;
  
  /*Calculate offset for inodes*/
  /*number of blocks before inode blocks*/
  temp = 2 + target->superblock->i_blocks + target->superblock->z_blocks;
  /*Convert to bytes and add partition offset*/
  ltemp = targetOffset + (temp * target->superblock->blocksize);
  target->inodeOff = ltemp;

  /*Find the number zones in a block*/
  temp = target->superblock->blocksize / ZONE_LEN;
  /*Save into our file tools*/
  target->zonesPerBlock = temp;
  
  /*Save file pointer to target*/
  target->image = image;
  
  return target;
}

/*This function reads and returns a pointer to desired the inode struct*/
inode getInode(tools target, int iNum)
{
  inode targetInode;
  long ltemp;

  /*Counting starts at 1, to skip the necessary amount of inodes, we minus 1*/
  ltemp = target->inodeOff + ((iNum-1) * INODE_SIZE);
  
  /*Seek to the location of the inode in filesystem*/
  if( fseek(target->image, ltemp, SEEK_SET) != 0 )
  {
    /*fseek failed*/
    perror("getInode - fseek");
    exit(EXIT_FAILURE);
  }

  /*Allocate memory for the inode*/
  targetInode = malloc(sizeof(struct inode));
  
  /*Lift the inode data out of the image*/
  if( !fread(targetInode, INODE_SIZE, 1, target->image) )
  {
    /*fread failed*/
    perror("getTools super - fread");
    exit(EXIT_FAILURE);
  }  
  
  return targetInode;
}

/*This function, given a zone number, goes to that zone and copies the entire
 *zone into the given buffer*/
void readZone(tools target, char *buffer, int zoneNum)
{
  long ltemp;

  ltemp = target->offset + (zoneNum * target->zonesize);
  
  /*Seek to the location of the zone in filesystem*/
  if( fseek(target->image, ltemp, SEEK_SET) != 0 )
  {
    /*fseek failed*/
    perror("readZone - fseek");
    exit(EXIT_FAILURE);
  }

  /*Read the contents of the entire zone and stuff it into the buffer*/
  if( !fread(buffer, sizeof(char), target->zonesize, target->image) )
  {
    /*fread failed*/
    perror("readZone - fread");
    exit(EXIT_FAILURE);
  }
}

/*Similar to readZone, but with blocks instead. Mostly useful for only
 *reading the first block of a zone, because only the first block of an 
 *indirect/2-indirect zone has zone numbers in it.*/
void readBlock(tools target, void *buffer, int zoneNum)
{
  long ltemp;

  ltemp = target->offset + (zoneNum * target->zonesize);
  
  /*Seek to the location of the zone in filesystem*/
  if( fseek(target->image, ltemp, SEEK_SET) != 0 )
  {
    /*fseek failed*/
    perror("readBlock - fseek");
    exit(EXIT_FAILURE);
  }

  /*Read the contents of the first block of the zone and stuff it into 
   *the buffer*/
  if( !fread(buffer, sizeof(char),
	       target->superblock->blocksize, target->image) )
  {
    /*fread failed*/
    perror("readBlock - fread");
    exit(EXIT_FAILURE);
  }
}

/*Another variation, given a zone and file number, seeks to that position and
 *reads a fileEnt sized chunk of data*/
void readFEnt(tools target, fileEnt buffer, int zoneNum, int fIndex)
{
  long ltemp;

  ltemp = target->offset + (zoneNum * target->zonesize) + (fIndex * DIR_SIZE);
  
  /*Seek to the location of the zone in filesystem*/
  if( fseek(target->image, ltemp, SEEK_SET) != 0 )
  {
    /*fseek failed*/
    perror("readFEnt - fseek");
    exit(EXIT_FAILURE);
  }

  /*Read the contents of the first block of the zone and stuff it into 
   *the buffer*/
  if( !fread(buffer, DIR_SIZE, 1, target->image) )
  {
    /*fread failed*/
    perror("readFEnt - fread");
    exit(EXIT_FAILURE);
  }
}

/*This function returns the zone number for a given index*/
uint32_t getZoneNum(tools target, inode folder, int zoneNum)
{
  uint32_t indirect[target->zonesPerBlock],
    two_indirect[target->zonesPerBlock];
  int two_index, one_index;
  
  /*If the zone is directly available*/
  if(zoneNum < DIRECT_ZONES)
    return folder->zone[zoneNum];

  /*If we have to get into the direct zones*/
  else if(zoneNum < DIRECT_ZONES + target->zonesPerBlock)
  {
    /*If the indirect zone is 0, then its always going to be zero*/
    if(folder->indirect == 0)
      return 0;
    /*Otherwise get the zone number from the indirect zone*/
    else
    {
      /*Read the indirect list of zones*/
      readBlock(target, indirect, folder->indirect);

      return indirect[zoneNum - DIRECT_ZONES];
    }
  }

  /*If we have to get into the twice indirect zones*/
  else
  {
    if(folder->two_indirect == 0)
      return 0;
    /*Assuming that n goes to an index inside of the two_indirect block, the
     *range of that block can be represented:
      
      [n] covers (n+1)zPB + 7 throught (n+2)zPB + 6

     */
    else
    {
      /*Read the two_indirect list of indirect zones*/
      readBlock(target, two_indirect, folder->two_indirect);

      /*Find the index for the two_indirect zone*/
      two_index = (zoneNum - (target->zonesPerBlock + 7))
	      / target->zonesPerBlock;

      /*If the indirect zone number is 0, return 0*/
      if(two_indirect[two_index] == 0)
	      return 0;

      /*Find the index for the one_indirect zone*/
      one_index = zoneNum -
	      ((two_index + 1) * target->zonesPerBlock + DIRECT_ZONES);
      
      /*Index the two_indirect block and read that zone*/
      readBlock(target, indirect, two_indirect[two_index]);

      /*Return the zone number at the one_index*/
      return indirect[one_index];
    }
  }
}

/*For a given (directory) inode and file name, this function attempts 
 *to find the matching file*/
fileEnt getMatch(tools target, inode folder, int numFiles, char *string)
{
  int i, fileInZone, zoneCount;
  fileEnt file;
  uint32_t currentZone;

  /*Linear count of zones*/
  zoneCount = 0;
  
  /*Get the very first zone*/
  currentZone = getZoneNum(target, folder, zoneCount);

  /*Starting at the first file entry in the zone*/
  fileInZone = 0;

  file = malloc(DIR_SIZE);
  
  /*Attempt to read numFiles amount of fileEnts from the inode's zones*/
  for(i = 0; i < numFiles; i++)
  {
    /*If the zone is 0, we don't actually need to do reading*/
    if(currentZone == 0)
    {
      /*Grab next zone*/
      currentZone = getZoneNum(target, folder, ++zoneCount);

      /*Turn back the clock and try another read, since inode 0 is invalid*/
      i -= 1;
    }
    else
    {
      /*Grab the file related file entry*/
      /*Not the most elegant because this seeks to the folder everytime*/
      readFEnt(target, file, currentZone, fileInZone);

      /*If the inode is 0, the entry is invalid. Don't count it*/
      if(file->inode == 0)
      {
	      /*Redo the file read*/
	      i -= 1;
      }
      else
      {
	      /*Compare at most the first 60 bytes of the two file names*/
	      if( strncmp(string, (char *) file->name, 60) == 0 )
	        /*If they match, return the file*/
	        return file;

	      /*If max number of fileEnts in a zone, reset, grab new zone*/
	      if(++fileInZone == target->filePerZone)
	      {
	        /*Reset file in zone*/
	        fileInZone = 0;
	
	        /*Grab next zone number*/
	        currentZone = getZoneNum(target, folder, ++zoneCount);
	      }
      }
    }      
  }

  return NULL;
}


/*This function, given the list of folders to search through, finds the inode
 *of the desired folder, and writes it to the target*/
int findFolder(tools target, char **path, int depth)
{
  inode current;
  int currInode, numFiles, i;
  fileEnt file;

  /*Get the root inode first*/
  currInode = 1;

  /*For each level of depth, */
  for(i = 0; i < depth; i++)
  {
    /*Get the inode information of the currInode number*/
    current = getInode(target, currInode);

    /*If this is not a folder and we aren't at the end of the path, error*/
    if(!ISDIR(current->mode) && (i != depth-1))
    {
      /*If the root isn't a directory (impressive)*/
      if(i == 0)
	      fprintf(stderr, "Root is not a directory, impressive.\n");
      else
	      fprintf(stderr, "\'%s\' is not a directory.\n", path[i-1]);
      return -1;
    }

    /*Find number of files in current folder*/
    numFiles = current->size / DIR_SIZE;

    /*Find a match for the given string in the path*/
    file = getMatch(target, current, numFiles, path[i]);

    /*If there was no match, then the path was invalid*/
    if(!file)
    {
      fprintf(stderr, "Could not file \'%s\' in path\n", path[i]);
      return -1;
    }

    /*Mark this file as the next current inode*/
    currInode = file->inode;

    /*We only needed its inode number*/
    free(file);
  }

  /*If we haven't returned an error, we likely found the inode we want*/
  /*Allocate memory and save the inode structure*/
  target->inode = getInode(target, currInode);

  /*Save a string of its permissions*/
  target->perms = getMode(target->inode->mode);

  /*If this is a folder, save the number of files in this directory*/
  target->numFiles = ISDIR(target->inode->mode) ?
    (target->inode->size/DIR_SIZE) : 0;
  
  /*No error :)*/
  return 0;
}

/*Read entire contents of the directory in the target inode*/
void getContents(tools target)
{
  int i, zoneNum, fileOffset;
  fileEnt currentEntry;
  inode currentInode;

  /*Allocate memory for the list*/
  target->files = malloc(sizeof(dirEnt) * target->numFiles);
  
  /*Read numFiles amount of entries from the directory*/
  for(i = 0; i < target->numFiles; i++)
  {
    /*Get the proper zone number*/
    zoneNum = getZoneNum(target, target->inode, i/target->filePerZone);
    /*Calculate the directory offset in the zone*/
    fileOffset = i % target->filePerZone;

    /*Allocate memory for the fileEnt*/
    currentEntry = malloc(DIR_SIZE);
    
    /*Read that fileEnt*/
    readFEnt(target, currentEntry, zoneNum, fileOffset);

    /*If the inode is 0*/
    if(currentEntry->inode == 0)
    {
      /*Get rid of this one*/
      free(currentEntry);
      
      /*Skip this entry*/
      i -= 1;
    }
      
    
    else
    {
      /*Get the inode for the other info*/
      currentInode = getInode(target, currentEntry->inode);

      /*Allocate memory for the directory listing*/
      target->files[i] = malloc(sizeof(struct dir_listing));
      
      /*Save the information*/
      target->files[i]->entry = currentEntry;
      target->files[i]->perms = getMode(currentInode->mode);
      target->files[i]->size = currentInode->size;
      strncpy(target->files[i]->name, (char *)currentEntry->name, 60);
    }
  }
}

/*This function copies the entirety of a given file from the minix image to
 *the specified destination*/
void readFile(tools target, FILE *destination)
{
  char *buffer;
  int i, zones, toRead, remainder;

  /*Calculate the number of full zones we need to copy*/
  zones = target->inode->size / target->zonesize;

  /*Calculate any remainder in the file*/
  remainder = target->inode->size % target->zonesize;
  
  /*Allocate buffer that is one zone's worth of bytes*/
  buffer = malloc(target->zonesize);

  /*Go through zones amount of full zones*/
  for(i = 0; i <= zones; i++)
  {
    /*Get actual zone number*/
    toRead = getZoneNum(target, target->inode, i);

    /*Check if zone 0*/
    if(toRead != 0)
    {
      /*Read zone into the buffer*/
      readZone(target, buffer, toRead);

      if(i == zones)
      {
	      /*Write only the remainder to the destination*/
	      if( fwrite(buffer, sizeof(char), remainder, destination)
	          != remainder)
	      {
	        perror("readFile - fwrite");
	        exit(EXIT_FAILURE);
	      }
      }
      else
      {
	      /*Write to the destination*/
	      if( fwrite(buffer, sizeof(char), target->zonesize, destination)
	          != target->zonesize)
        {
	        perror("readFile - fwrite");
	        exit(EXIT_FAILURE);
	      }
      }	  
    }
  }
}
