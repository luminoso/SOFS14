/**
 *  \file mkfs_sofs14.c (implementation file)
 *
 *  \brief The SOFS14 formatting tool.
 *
 *  It stores in predefined blocks of the storage device the file system metadata. With it, the storage device may be
 *  envisaged operationally as an implementation of SOFS14.
 *
 *  The following data structures are created and initialized:
 *     \li the superblock
 *     \li the table of inodes
 *     \li the data zone
 *     \li the contents of the root directory seen as empty.
 *
 *  SINOPSIS:
 *  <P><PRE>                mkfs_sofs14 [OPTIONS] supp-file
 *
 *                OPTIONS:
 *                 -n name --- set volume name (default: "SOFS14")
 *                 -i num  --- set number of inodes (default: N/8, where N = number of blocks)
 *                 -z      --- set zero mode (default: not zero)
 *                 -q      --- set quiet mode (default: not quiet)
 *                 -h      --- print this help.</PRE>
 *
 *  \author Artur Carneiro Pereira - September 2008
 *  \author Miguel Oliveira e Silva - September 2009
 *  \author António Rui Borges - September 2010 - August 2011, September 2014
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "sofs_const.h"
#include "sofs_buffercache.h"
#include "sofs_superblock.h"
#include "sofs_inode.h"
#include "sofs_direntry.h"
#include "sofs_basicoper.h"
#include "sofs_basicconsist.h"

/* Allusion to internal functions */

static int fillInSuperBlock (SOSuperBlock *p_sb, uint32_t ntotal, uint32_t itotal, uint32_t nclusttotal,
                             unsigned char *name);
static int fillInINT (SOSuperBlock *p_sb);
static int fillInRootDir (SOSuperBlock *p_sb);
static int fillInGenRep (SOSuperBlock *p_sb, int zero);
static int checkFSConsist (void);
static void printUsage (char *cmd_name);
static void printError (int errcode, char *cmd_name);

/* The main function */

int main (int argc, char *argv[])
{
  char *name = "SOFS14";                         /* volume name */
  uint32_t itotal = 0;                           /* total number of inodes, if kept set value automatically */
  int quiet = 0;                                 /* quiet mode, if kept set not quiet mode */
  int zero = 0;                                  /* zero mode, if kept set not zero mode */

  /* process command line options */

  int opt;                                       /* selected option */

  do
  { switch ((opt = getopt (argc, argv, "n:i:qzh")))
    { case 'n': /* volume name */
                name = optarg;
                break;
      case 'i': /* total number of inodes */
                if (atoi (optarg) < 0)
                   { fprintf (stderr, "%s: Negative inodes number.\n", basename (argv[0]));
                     printUsage (basename (argv[0]));
                     return EXIT_FAILURE;
                   }
                itotal = (uint32_t) atoi (optarg);
                break;
      case 'q': /* quiet mode */
                quiet = 1;                       /* set quiet mode for processing: no messages are issued */
                break;
      case 'z': /* zero mode */
                zero = 1;                        /* set zero mode for processing: the information content of all free
                                                    data clusters are set to zero */
                break;
      case 'h': /* help mode */
                printUsage (basename (argv[0]));
                return EXIT_SUCCESS;
      case -1:  break;
      default:  fprintf (stderr, "%s: Wrong option.\n", basename (argv[0]));
                printUsage (basename (argv[0]));
                return EXIT_FAILURE;
    }
  } while (opt != -1);
  if ((argc - optind) != 1)                      /* check existence of mandatory argument: storage device name */
     { fprintf (stderr, "%s: Wrong number of mandatory arguments.\n", basename (argv[0]));
       printUsage (basename (argv[0]));
       return EXIT_FAILURE;
     }

  /* check for storage device conformity */

  char *devname;                                 /* path to the storage device in the Linux file system */
  struct stat st;                                /* file attributes */

  devname = argv[optind];
  if (stat (devname, &st) == -1)                 /* get file attributes */
     { printError (-errno, basename (argv[0]));
       return EXIT_FAILURE;
     }
  if (st.st_size % BLOCK_SIZE != 0)              /* check file size: the storage device must have a size in bytes
                                                    multiple of block size */
     { fprintf (stderr, "%s: Bad size of support file.\n", basename (argv[0]));
       return EXIT_FAILURE;
     }

  /* evaluating the file system architecture parameters
   * full occupation of the storage device when seen as an array of blocks supposes the equation bellow
   *
   *    NTBlk = 1 + NBlkTIN + NTClt*BLOCKS_PER_CLUSTER
   *
   *    where NTBlk means total number of blocks
   *          NTClt means total number of clusters of the data zone
   *          NBlkTIN means total number of blocks required to store the inode table
   *          BLOCKS_PER_CLUSTER means number of blocks which fit in a cluster
   *
   * has integer solutions
   * this is not always true, so a final adjustment may be made to the parameter NBlkTIN to warrant this
   */

  uint32_t ntotal;                               /* total number of blocks */
  uint32_t iblktotal;                            /* number of blocks of the inode table */
  uint32_t nclusttotal;                          /* total number of clusters */

  ntotal = st.st_size / BLOCK_SIZE;
  if (itotal == 0) itotal = ntotal >> 3;
  if ((itotal % IPB) == 0)
     iblktotal = itotal / IPB;
     else iblktotal = itotal / IPB + 1;
  nclusttotal = (ntotal - 1 - iblktotal) / BLOCKS_PER_CLUSTER;
                                                 /* final adjustment */
  iblktotal = ntotal - 1 - nclusttotal * BLOCKS_PER_CLUSTER;
  itotal = iblktotal * IPB;

  /* formatting of the storage device is going to start */

  SOSuperBlock *p_sb;                            /* pointer to the superblock */
  int status;                                    /* status of operation */

  if (!quiet)
     printf("\e[34mInstalling a %"PRIu32"-inodes SOFS11 file system in %s.\e[0m\n", itotal, argv[optind]);

  /* open a buffered communication channel with the storage device */

  if ((status = soOpenBufferCache (argv[optind], BUF)) != 0)
     { printError (status, basename (argv[0]));
       return EXIT_FAILURE;
     }

  /* read the contents of the superblock to the internal storage area
   * this operation only serves at present time to get a pointer to the superblock storage area in main memory
   */

  if ((status = soLoadSuperBlock ()) != 0)
     return status;
  p_sb = soGetSuperBlock ();

  /* filling in the superblock fields:
   *   magic number should be set presently to 0xFFFF, this enables that if something goes wrong during formating, the
   *   device can never be mounted later on
   */

  if (!quiet)
     { printf ("Filling in the superblock fields ... ");
       fflush (stdout);                          /* make sure the message is printed now */
     }

  if ((status = fillInSuperBlock (p_sb, ntotal, itotal, nclusttotal, (unsigned char *) name)) != 0)
     { printError (status, basename (argv[0]));
       soCloseBufferCache ();
       return EXIT_FAILURE;
     }

  if (!quiet) printf ("done.\n");

  /* filling in the inode table:
   *   only inode 0 is in use (it describes the root directory)
   */

  if (!quiet)
     { printf ("Filling in the inode table ... ");
       fflush (stdout);                          /* make sure the message is printed now */
     }

  if ((status = fillInINT (p_sb)) != 0)
     { printError (status, basename (argv[0]));
       soCloseBufferCache ();
       return EXIT_FAILURE;
     }

  if (!quiet) printf ("done.\n");

  /* filling in the contents of the root directory:
   *   the first 2 entries are filled in with "." and ".." references
   *   the other entries are kept empty
   */

  if (!quiet)
     { printf ("Filling in the contents of the root directory ... ");
       fflush (stdout);                          /* make sure the message is printed now */
     }

  if ((status = fillInRootDir (p_sb)) != 0)
     { printError (status, basename (argv[0]));
       soCloseBufferCache ();
       return EXIT_FAILURE;
     }

  if (!quiet) printf ("done.\n");

  /*
   * create the general repository of free data clusters as a double-linked list where the data clusters themselves are
   * used as nodes
   * zero fill the remaining data clusters if full formating was required:
   *   zero mode was selected
   */

  if (!quiet)
     { printf ("Creating the general repository of free data clusters ... ");
       fflush (stdout);                          /* make sure the message is printed now */
     }

  if ((status = fillInGenRep (p_sb, zero)) != 0)
     { printError (status, basename (argv[0]));
       soCloseBufferCache ();
       return EXIT_FAILURE;
     }

  if (!quiet) printf ("done.\n");

  /* magic number should now be set to the right value before writing the contents of the superblock to the storage
     device */

  p_sb->magic = MAGIC_NUMBER;
  if ((status = soStoreSuperBlock ()) != 0)
     return status;

  /* check the consistency of the file system metadata */

  if (!quiet)
     { printf ("Checking file system metadata... ");
       fflush (stdout);                          /* make sure the message is printed now */
     }

  if ((status = checkFSConsist ()) != 0)
     { fprintf(stderr, "error # %d - %s\n", -status, soGetErrorMessage (p_sb, -status));
       soCloseBufferCache ();
       return EXIT_FAILURE;
     }

  if (!quiet) printf ("done.\n");

  /* close the unbuffered communication channel with the storage device */

  if ((status = soCloseBufferCache ()) != 0)
     { printError (status, basename (argv[0]));
       return EXIT_FAILURE;
     }

  /* that's all */

  if (!quiet) printf ("Formating concluded.\n");

  return EXIT_SUCCESS;

} /* end of main */

/*
 * print help message
 */

static void printUsage (char *cmd_name)
{
  printf ("Sinopsis: %s [OPTIONS] supp-file\n"
          "  OPTIONS:\n"
          "  -n name --- set volume name (default: \"SOFS14\")\n"
          "  -i num  --- set number of inodes (default: N/8, where N = number of blocks)\n"
          "  -z      --- set zero mode (default: not zero)\n"
          "  -q      --- set quiet mode (default: not quiet)\n"
          "  -h      --- print this help\n", cmd_name);
}

/*
 * print error message
 */

static void printError (int errcode, char *cmd_name)
{
  fprintf(stderr, "%s: error #%d - %s\n", cmd_name, -errcode,
          soGetErrorMessage (soGetSuperBlock (),-errcode));
}

  /* filling in the superblock fields:
   *   magic number should be set presently to 0xFFFF, this enables that if something goes wrong during formating, the
   *   device can never be mounted later on
   */

static int fillInSuperBlock (SOSuperBlock *p_sb, uint32_t ntotal, uint32_t itotal, uint32_t nclusttotal,
                             unsigned char *name)
{  
  /* HEADER */
  p_sb->magic = 0xFFFF;
  p_sb->version = VERSION_NUMBER;
  unsigned int l=0;
  while(name[l]!='\0' && l < PARTITION_NAME_SIZE){
    p_sb->name[l] = name[l];
    l++;
  }
  p_sb->name[l] = '\0';           			// string terminator
  p_sb->nTotal = ntotal;				// dado pelo argumento da funcao
  p_sb->mStat = PRU;					// o filesystem é novo, está bem desmontado

  /* Inode table */
  p_sb->iTableStart = 1;				// o bloco 0 é o superbloco
  p_sb->iTableSize = (itotal / IPB) + (itotal % IPB);	// numero de blocos que a tabela i ocupa
							// ((itotal / IPB) + (itotal % IPB)) ja está calculado nas linhas 144 a 151?
  p_sb->iTotal = itotal;				// o numero total de nos-i
  p_sb->iFree = itotal - 1;				// o primeiro inode está ocupado com a raiz "/"
  p_sb->iHead = 1;					// 1, pois o zero esta ocupado com o inode-raiz
  p_sb->iTail = itotal - 1;				// descontamos o inode da raiz

  /* DataZone */
  p_sb->dZoneStart = 1 + itotal/IPB + (itotal % IPB);	// superbloco + o numero de clusters que os blocos i ocupam
  p_sb->dZoneTotal = nclusttotal;			// o total de clusters
  p_sb->dZoneFree = nclusttotal - 1;			// a raiz ocupa um bloco
  p_sb->dZoneRetriev.cacheIdx = DZONE_CACHE_SIZE;
  unsigned int i;
  for(i = 0; i < DZONE_CACHE_SIZE; i++)
    p_sb->dZoneRetriev.cache[i] = p_sb->dZoneInsert.cache[i] = NULL_CLUSTER;
  p_sb->dZoneInsert.cacheIdx = 0;
  p_sb->dHead = 1;					// o primeir está ocupado com o directorio raiz
  p_sb->dTail = nclusttotal - 1;			// não tenho a certeza

  for(i = 0; i < RESERV_AREA_SIZE; i++)
    p_sb->reserved[i] = 0xee;				// 0xEE foi sugerido pelo professor

  int stat;
  if( (stat = soStoreSuperBlock()) != 0)
    return stat;  

  return 0;
}

/*
 * filling in the inode table:
 *   only inode 0 is in use (it describes the root directory)
 */

static int fillInINT (SOSuperBlock *p_sb)
{
  //Função completa e a funcionar
  int stat, i, j;
  uint32_t nBlk, offset;

  SOInode *p_itable;

  /*
  *
  *preencher o inode 0
  *
  */
  if((stat = soConvertRefInT(0, &nBlk, &offset)) != 0)
    return stat;

  if((stat = soLoadBlockInT(nBlk)) != 0)
    return stat;

  // se lido correctamente vamos ober o ponteiro para ele
  p_itable = soGetBlockInT();// verificar se null ?	


  p_itable[offset].mode = INODE_DIR | INODE_RD_USR | INODE_WR_USR | 
                          INODE_EX_USR | INODE_RD_GRP | INODE_WR_GRP |
                          INODE_EX_GRP | INODE_RD_OTH | INODE_WR_OTH | 
                          INODE_EX_OTH; //definir inode 0 como directorio, permissoes..
  p_itable[offset].refCount = 2; //.-> ele proprio  ..-> directorio imediamtamente acima   retainCount(); //nao sei
  p_itable[offset].owner = getuid(); // retorna o id do utilizador 
  p_itable[offset].group = getgid(); // retorna o id do grupo
  p_itable[offset].cluCount = 1;  // size in clusters
  p_itable[offset].size = CLUSTER_SIZE - (sizeof(p_itable[offset]));
  p_itable[offset].vD1.aTime = time(NULL); // recebe o tempo em segundos
  p_itable[offset].vD2.mTime = p_itable[offset].vD1.aTime;
  p_itable[offset].d[0] = 0;
  for (i = 1; i < N_DIRECT; i++)
  {
    p_itable[offset].d[i] = NULL_INODE; //inicializar todas as referencias a clusters a null
  }
  p_itable[offset].i1 = NULL_INODE; // referencias indirectas
  p_itable[offset].i2 = NULL_INODE;	

  if( (stat = soStoreBlockInT()) != 0)
    return stat;
  /*
  *
  *preencher o inode 1
  *
  */
  if((stat = soConvertRefInT(1, &nBlk, &offset)) != 0)
    return stat;

  if((stat = soLoadBlockInT(nBlk)) != 0)
    return stat;

  // se lido correctamente vamos ober o ponteiro para ele
  p_itable = soGetBlockInT();

  p_itable[offset].mode = INODE_FREE;   // definir inode como livre
  p_itable[offset].refCount = 0;      // não tem referencias
  p_itable[offset].owner = 0;     // utilizador default e 0 
  p_itable[offset].group = 0;     // grupo default e 0
  p_itable[offset].size = 0;      // não tem tamanho
  p_itable[offset].cluCount = 0;      // size in clusters
  for (j = 0; j < N_DIRECT; j++)
  {
    p_itable[offset].d[j] = NULL_INODE;   // inicializar todas as referencias a clusters a null
  }
  p_itable[offset].vD1.next = offset +1;  // como inode esta vazio o campo da union usado e o next que contem o indice do proximo indode na lista bi-ligada
  p_itable[offset].vD2.prev = NULL_INODE;
  p_itable[offset].i1 = NULL_INODE;   // referencias indirectas
  p_itable[offset].i2 = NULL_INODE;
  
  if( (stat = soStoreBlockInT()) != 0)
    return stat;

  /*
  *
  *preencher os restantes inodes
  *
  */
  for(i = 2; i < p_sb->iTotal - 1; i++)
  {
    if((stat = soConvertRefInT(i, &nBlk, &offset)) != 0)
      return stat;

    if((stat = soLoadBlockInT(nBlk)) != 0)
      return stat;

    // se lido correctamente vamos ober o ponteiro para ele
    p_itable = soGetBlockInT();

    p_itable[offset].mode = INODE_FREE;   // definir inode como livre
    p_itable[offset].refCount = 0;      // não tem referencias
    p_itable[offset].owner = 0;     // utilizador default e 0 
    p_itable[offset].group = 0;     // grupo default e 0
    p_itable[offset].size = 0;      // não tem tamanho
    p_itable[offset].cluCount = 0;      // size in clusters
    for (j = 0; j < N_DIRECT; j++)
    {
      p_itable[offset].d[j] = NULL_INODE;   // inicializar todas as referencias a clusters a null
    }
    p_itable[offset].vD1.next = i +1;  // como inode esta vazio o campo da union usado e o next que contem o indice do proximo indode na lista bi-ligada
    p_itable[offset].vD2.prev = i -1;
    p_itable[offset].i1 = NULL_INODE;   // referencias indirectas
    p_itable[offset].i2 = NULL_INODE;
    
    if( (stat = soStoreBlockInT()) != 0)
    return stat;
  }

  /*
  *
  *preencher o ultimo inode (itotal -1)
  *
  */
  if((stat = soConvertRefInT(p_sb->iTotal - 1, &nBlk, &offset)) != 0)
    return stat;

  if((stat = soLoadBlockInT(nBlk)) != 0)
    return stat;

  // se lido correctamente vamos ober o ponteiro para ele
  p_itable = soGetBlockInT();

  p_itable[offset].mode = INODE_FREE;   // definir inode como livre
  p_itable[offset].refCount = 0;      // não tem referencias
  p_itable[offset].owner = 0;     // utilizador default e 0 
  p_itable[offset].group = 0;     // grupo default e 0
  p_itable[offset].size = 0;      // não tem tamanho
  p_itable[offset].cluCount = 0;      // size in clusters
  for (j = 0; j < N_DIRECT; j++)
  {
    p_itable[offset].d[j] = NULL_INODE;   // inicializar todas as referencias a clusters a null
  }
  p_itable[offset].vD1.next = NULL_INODE;  // como inode esta vazio o campo da union usado e o next que contem o indice do proximo indode na lista bi-ligada
  p_itable[offset].vD2.prev = p_sb->iTotal - 2;
  p_itable[offset].i1 = NULL_INODE;   // referencias indirectas
  p_itable[offset].i2 = NULL_INODE;
  
  if( (stat = soStoreBlockInT()) != 0)
    return stat;

  // gravar as alteracoes que fizemos na tabela de inodes

  return 0;
}

/*
 * filling in the contents of the root directory:
 the first 2 entries are filled in with "." and ".." references
 the other entries are empty
 */

static int fillInRootDir (SOSuperBlock *p_sb)
{
  /* FUNCAO 3*/
  SODataClust NoRaiz;
  
  int i,k;
  for(i = 0; i < DPC ; i++){
      NoRaiz.info.de[i].nInode = NULL_INODE;
      for(k = 0; k < MAX_NAME + 1 ; k++){
	  NoRaiz.info.de[i].name[k] = '\0';
      }
  }    
  
  NoRaiz.prev = NULL_CLUSTER;
  NoRaiz.next = NULL_CLUSTER;
  NoRaiz.stat = 0;			// copiado pelo ./showblock do mkfs_sofs14_bin_64
  
  NoRaiz.info.de[0].name[0] = '.';
  NoRaiz.info.de[0].name[1] = '\0';
  NoRaiz.info.de[0].nInode = 0;
  NoRaiz.info.de[1].name[0] = '.';
  NoRaiz.info.de[1].name[1] = '.';
  NoRaiz.info.de[1].name[2] = '\0';
  NoRaiz.info.de[1].nInode = 0;

  // gravar o nó raiz
  int stat;
  if( (stat = soWriteCacheCluster(p_sb->dZoneStart,&NoRaiz)) != 0)
      return stat;
  
  return 0;
}

/*
 * create the general repository of free data clusters as a double-linked list where the data clusters themselves are
 * used as nodes
 * zero fill the remaining data clusters if full formating was required:
 *  zero mode was selected
 */

static int fillInGenRep (SOSuperBlock *p_sb, int zero)
{
  /* A zona de dados está organizada num array de cluster de dados.
   * A referencia a um cluster é o indico ou o numero logico do cluster no array.
   * O número fisico  é o indice do primeiro bloco que forma.
   * A relacao entre os dois é dada por
   * NFClt = dzone_start + NLClt * BLOCKS_PER_CLUSTER;
   * (SOFS14.pdf, pagina 10)
   */
  int stat;
  SODataClust datacluster;
  //NFCLt posicao do cluster, clustercount contador de clusters
  uint32_t NFClt, clustercount;
  
  // preencher informacao genérica a todos os clusters
  datacluster.stat = NULL_INODE;
  if(zero) memset(datacluster.info.data,0x00,BSLPC); //byte stream per data cluster 
  
  // criacao da lista bi-ligada
  // a comecar em um, pois o 0 está com o directorio raiz
  NFClt = p_sb->dZoneStart + BLOCKS_PER_CLUSTER;
  for(clustercount = 1; clustercount < p_sb->dZoneTotal; clustercount++, NFClt += BLOCKS_PER_CLUSTER){
      
      // no primeiro nó, o prev liga à terra
      if(clustercount == 1) datacluster.prev = NULL_CLUSTER;
      else datacluster.prev = clustercount -1;
      
      // no ultimo nó o next liga à terra
      if(clustercount == p_sb->dZoneTotal - 1) datacluster.next = NULL_CLUSTER;
      else datacluster.next = clustercount +1;
      
      // a cada nó da lista bi-ligada, gravamos o cluster
      if( (stat = soWriteCacheCluster(NFClt,&datacluster)) != 0)
	  return stat;
  }
  return 0;
}

/*
   check the consistency of the file system metadata
 */

static int checkFSConsist (void)
{
  SOSuperBlock *p_sb;                            /* pointer to the superblock */
  SOInode *inode;                                /* pointer to the contents of a block of the inode table */
  int stat;                                      /* status of operation */

  /* read the contents of the superblock to the internal storage area and get a pointer to it */

  if ((stat = soLoadSuperBlock ()) != 0) return stat;
  p_sb = soGetSuperBlock ();

  /* check superblock and related structures */

  if ((stat = soQCheckSuperBlock (p_sb)) != 0) return stat;

  /* read the contents of the first block of the inode table to the internal storage area and get a pointer to it */

  if ((stat = soLoadBlockInT (0)) != 0) return stat;
  inode = soGetBlockInT ();

  /* check inode associated with root directory (inode 0) and the contents of the root directory */

  if ((stat = soQCheckInodeIU (p_sb, &inode[0])) != 0) return stat;
  if ((stat = soQCheckDirCont (p_sb, &inode[0])) != 0) return stat;

  /* everything is consistent */

  return 0;
}
