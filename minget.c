/*minget is another unix program designed to read and copy out files from a 
 *minix file system. 

 minget [-v] [-p part [-s subpart]] imagefile srcpath [dstpath]
  
*/

#include "minfs.h"
#include <time.h>
#include <ctype.h>

/*To stop gcc from yelling at me about how minfs doesn't use the below
 *variables, I have moved them from minfs.h to here.*/

/*Strings of the superblock fields for printing*/
static char *superFields[numSuperFields] = {"ninodes", "i_blocks", "z_blocks",
					    "firstdata", "log_zone_size",
					    "max_file", "magic", "zones",
					    "blocksize", "subversion"};

/*Strings of the inode fields for printing*/
static char *inodeFields[numInodeFields] = {"uint16_t mode", "uint16_t links",
					    "uint16_t uid", "uint16_t gid",
					    "uint32_t size", "uint32_t atime",
					    "uint32_t mtime","uint32_t ctime"};

/*Clean up everything so it looks nice and neat*/
void cleanup(tools target, char *imageFile, char **path, int depth)
{
  int i;
  
  /*If a path was provided*/
  if(path)
    free(path);

  for(i = 0; i < target->numFiles; i++)
  {
    free(target->files[i]->entry);
    free(target->files[i]->perms);
    free(target->files[i]);
  }

  free(target->files);
  
  free(imageFile);
  
  free(target->inode);
  free(target->superblock);
  free(target);
}

/*Prints usage*/
void usage()
{
  fprintf(stderr,
	  "usage: minls [-v] [-p num [-s num]] imagefile [path]\n");
  fprintf(stderr,
	  "Options:\n");
  fprintf(stderr,
	  "-p  part    --- select partition for filesystem (default: none)\n");
  fprintf(stderr,
	  "-s  sub     --- select subpartition"
	 " for filesystem (default: none)\n");
  fprintf(stderr,
	  "-h  help    --- print usage information and exit\n");
  fprintf(stderr,
	  "-v  verbose --- select partition for filesystem (default: none)\n");
  exit(EXIT_FAILURE);
}

/*This function prints out the contents of the superblock and inode*/
void printInfo(tools target)
{
  int i;
  time_t aTime, mTime, cTime;
  
  /*Printing for the superblock*/
  fprintf(stderr, "\nSuperblock Contents:\nStored Fields:\n");
  fprintf(stderr, "  %-13s %11u\n",
	  superFields[0], target->superblock->ninodes);
  fprintf(stderr, "  %-13s %11d\n",
	  superFields[1], target->superblock->i_blocks);
  fprintf(stderr, "  %-13s %11d\n",
	  superFields[2], target->superblock->z_blocks);
  fprintf(stderr, "  %-13s %11u\n",
	  superFields[3], target->superblock->firstdata);

  /*Print zone size*/
  fprintf(stderr, "  %-13s %11d",
	  superFields[4], target->superblock->log_zone_size);
  fprintf(stderr, " (zone size: %d)\n", target->zonesize);
  
  fprintf(stderr, "  %-13s %11u\n",
	  superFields[5], target->superblock->max_file);
  fprintf(stderr, "  %-13s %11X\n",
	  superFields[6], target->superblock->magic);
  fprintf(stderr, "  %-13s %11u\n",
	  superFields[7], target->superblock->zones);
  fprintf(stderr, "  %-13s %11u\n",
	  superFields[8], target->superblock->blocksize);
  fprintf(stderr, "  %-13s %11u\n",
	  superFields[9], target->superblock->subversion);

  /*Printing for the inode*/
  fprintf(stderr, "\nFile inode:\n");

  /*Print the string permission as well*/
  fprintf(stderr, "  %-14s          0x%4X",
	  inodeFields[0], target->inode->mode);
  fprintf(stderr, " (%s)\n", target->perms);
  fprintf(stderr, "  %-14s %15u\n", inodeFields[1], target->inode->links);
  fprintf(stderr, "  %-14s %15u\n", inodeFields[2], target->inode->uid);
  fprintf(stderr, "  %-14s %15u\n", inodeFields[3], target->inode->gid);
  fprintf(stderr, "  %-14s %15u\n", inodeFields[4], target->inode->size);

  aTime = target->inode->atime;
  mTime = target->inode->mtime;
  cTime = target->inode->ctime;
  
  /*Print the actual time after each*/
  fprintf(stderr, "  %-14s %15u --- %s", inodeFields[5],
	 target->inode->atime, ctime(&aTime));
  fprintf(stderr, "  %-14s %15u --- %s", inodeFields[6],
	 target->inode->mtime, ctime(&mTime));
  fprintf(stderr, "  %-14s %15u --- %s", inodeFields[7],
	 target->inode->ctime, ctime(&cTime));

  /*Print the zones*/
  fprintf(stderr, "\n  Direct zones:\n");

  for(i = 0; i < DIRECT_ZONES; i++)
    fprintf(stderr, "%-12szone[%d]   =   %8d\n",
	    "", i, target->inode->zone[i]);

  fprintf(stderr, "  uint32_t indirect       %8d\n",
	  target->inode->indirect);
  fprintf(stderr, "  uint32_t double         %8d\n",
	  target->inode->two_indirect);
}


int main(int argc, char *argv[])
{
  int i, depth, verbose, err;
  long int partition, subpart;

  char *imageFile, **path, *destination, delim, *access;

  FILE *image, *dest;

  tools target;
  
  verbose = 0;
  partition = -1;
  subpart = -1;

  imageFile = NULL;
  path = NULL;
  destination = NULL;
  
  /*If the arguments are blatantly wrong, print usage and exit*/
  if(argc < 3)
  {
    usage();
  }
  
  /*--- ARG PARSING ---*/
  while((i = getopt(argc, argv, "vp:s:")) != -1)
    switch(i)
    {
      case 'v':
    	  verbose = 1;
    	  break;
      case 'p':
	      partition = strtol(optarg, NULL, 10);
	      break;
      case 's':
	      subpart = strtol(optarg, NULL, 10);
	      break;
      case '?':
	      usage();
	      break;
      default:
	      usage();
	      break;
    }

  for(i = optind; i < argc; i++)
  {
    /*Minix image first*/
    if(!imageFile)
    {
      /*Malloc string*/
      imageFile = malloc(strlen(argv[i]) + 1);
      /*Copy string*/
      strcpy(imageFile, argv[i]);
    }
    
    /*Now for the file path*/    
    else if(!path)
    {
      char *temp;
      
      depth = 0;
      delim = '/';
      
      temp = strtok(argv[i], &delim);
      
      /*While we are creating tokens*/
      while(temp)
      {
	      /*Allocate one more for depth*/
	      path = realloc(path, sizeof(char *) * (++depth));
	
	      /*Store in path variable*/
	      path[depth-1] = temp;
	  
	      /*Get next token*/
	      temp = strtok(NULL, &delim);
      }
    }

    /*Now for the destination path, if it exists*/
    else
    {
      /*Malloc string*/
      destination = malloc(strlen(argv[i]) + 1);

      /*Copy contents*/
      strcpy(destination, argv[i]);
    }
  }

  /*An imagefile and source path are mandatory*/
  if(!imageFile || !path)
    usage();
  
  /*--- END PARSING ARGS ---*/
  
  /*Set up the access*/
  access = "r";
  
  /*Attempt to open image file for reading*/
  if( !(image = fopen(imageFile, access)) )
  {
    perror(imageFile);
    exit(EXIT_FAILURE);
  }

  /*Set up write access*/
  access = "w+";
  
  /*Attempt to open destination for writing (if it exists)*/
  if(destination)
  {
    if( !(dest = fopen(destination, access)))
    {
      perror(destination);
      exit(EXIT_FAILURE);
    }
  }
  else
  {
    /*If no destination was given, write to stdout*/
    dest = stdout;
  }
    
  /*Get the superblock and inode information*/
  target = getSuper(image, partition, subpart);

  /*If the target is null*/
  if(!target)
  {
    fprintf(stderr, "This doesn't look like a minix file system.\n");
    exit(EXIT_FAILURE);
  }
  
  /*Find the correct folder in the file system, if path is provided*/
  if(path)
  {
    err = findFolder(target, path, depth);

    /*Report error*/
    if(err == -1)
    {
      /*Possible errors: incorrect path*/
      fprintf(stderr, "The provided path does not seem correct.\n");
      exit(EXIT_FAILURE);
    }
  }
  
  /*If no path is specified, get the root inode*/
  else
  {
    target->inode = getInode(target, 1);
  }

  /*If this is not a regular file, error*/
  if(!ISREG(target->inode->mode))
  {
    fprintf(stderr, "minget copies regular files only.\n");
    exit(EXIT_FAILURE);
  }
  
  /*Output contents of superblock and inode if verbose*/
  if(verbose)
    printInfo(target);
  
  /*Output file*/
  readFile(target, dest);
  
  /*Clean up our mess*/
  cleanup(target, imageFile, path, depth);
  
  return 0;  
}
