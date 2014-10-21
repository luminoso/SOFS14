/**
 *  \file soReadFileCluster.c (implementation file)
 *
 *  \author Gabriel Vieira
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>

#include "sofs_probe.h"
#include "sofs_buffercache.h"
#include "sofs_superblock.h"
#include "sofs_inode.h"
#include "sofs_datacluster.h"
#include "sofs_basicoper.h"
#include "sofs_basicconsist.h"
#include "sofs_ifuncs_1.h"
#include "sofs_ifuncs_2.h"

/** \brief operation get the physical number of the referenced data cluster */
#define GET         0
/** \brief operation allocate a new data cluster and associate it to the inode which describes the file */
#define ALLOC       1
/** \brief operation free the referenced data cluster */
#define FREE        2
/** \brief operation free the referenced data cluster and dissociate it from the inode which describes the file */
#define FREE_CLEAN  3
/** \brief operation dissociate the referenced data cluster from the inode which describes the file */
#define CLEAN       4

/* allusion to internal function */

int soHandleFileCluster (uint32_t nInode, uint32_t clustInd, uint32_t op, uint32_t *p_outVal);

/**
 *  \brief Read a specific data cluster.
 *
 *  Data is read from a specific data cluster which is supposed to belong to an inode associated to a file (a regular
 *  file, a directory or a symbolic link). Thus, the inode must be in use and belong to one of the legal file types.
 *
 *  If the cluster has not been allocated yet, the returned data will consist of a cluster whose byte stream contents
 *  is filled with the character null (ascii code 0).
 *
 *  \param nInode number of the inode associated to the file
 *  \param clustInd index to the list of direct references belonging to the inode where data is to be read from
 *  \param buff pointer to the buffer where data must be read into
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the <em>inode number</em> or the <em>index to the list of direct references</em> are out of
 *                      range or the <em>pointer to the buffer area</em> is \c NULL
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c EDCINVAL, if the data cluster header is inconsistent
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soReadFileCluster (uint32_t nInode, uint32_t clustInd, SODataClust *buff)
{
  soColorProbe (411, "07;31", "soReadFileCluster (%"PRIu32", %"PRIu32", %p)\n", nInode, clustInd, buff);

  int stat;  // variavel de estado
  SOSuperBlock *p_sb; // ponteiro para o superbloco
  //uint32_t offset, nBlk;	// variável para o numero do bloco e seu offset
  SOInode inode;		//   nó I
  SODataClust *p_directClust;
  uint32_t physicalNumCluster;
  uint32_t num_logic, clusterStat; // variaveis usadas para o cluster 

  int i; // variavel auxiliar para ciclos

  /*Load superblock */
  if( (stat = soLoadSuperBlock()) != 0)
  	return stat;

  p_sb = soGetSuperBlock(); /* super bloco carregado*/

  if((stat = soQCheckSuperBlock(p_sb)) != 0) /* quick check of the superblock metadata */
    return stat;

   /* if the inode number is out of range */
  if(nInode >= p_sb->iTotal)
  	return -EINVAL;  

  /* if the pointer to the buffer area is NULL*/
  if(buff == NULL)
  	return -EINVAL;

   /*if index to the list of direct references are out of range*/
  if(clustInd > MAX_FILE_CLUSTERS)
    return -EINVAL;


  /* //load inode table
  if ( (stat = soConvertRefInT(nInode, &nBlk, &offset)) != 0)
    return stat;

  Load the content of a specific block inode
  if( (stat = soLoadBlockInT(nBlk)) != 0)
    return stat;*/

    /*Get a pointer to the contents of a specific block of the table of inodes*/
 // p_inode = soGetBlockInT();

    /*the inode must be in use, load inode table, load the content of a specific block inode*/ 
   if((stat = soReadInode(&inode, nInode, IUIN)) != 0)
    return stat; 
  

  /*testar consistencia do nó I*, if the list of data cluster references belonging to an inode is inconsistent, and
    if datacluster header is inconsistent*/

  if( (stat = soQCheckInodeIU(p_sb, &inode)) != 0)
    return stat; /*return -EIUININVAL and -ELDCININVAL and -EDCINVAL*/


   /* Load the contents of a specific cluster of the table of direct references to data clusters into internal storage */
   // SEGMENTATION FAULT: falta verificar se inode.d é null_cluster
  if( (stat = soLoadDirRefClust(inode.d[clustInd])) != 0)
    return stat;            /*nClust = inode.d[clustInd]*/ 

      /*Get a pointer to the contents of a specific cluster of the table of direct references to data clusters*/
  p_directClust = soGetDirRefClust();  

  /*if the list of data cluster references belonging to the inode is inconsistent*/
  if( (stat = soQCheckStatDC(p_sb, inode.d[clustInd], &clusterStat)) != 0)
    return stat;

  //if(clustInd < N_DIRECT)
  if( (stat = soHandleFileCluster(nInode, clustInd, GET, &num_logic)) != 0)  
    return stat;

  

    // se não existir nenhum cluster de dados associado ao elemento da tabela de referencias cujo indice é fornecido
  if(inode.d[clustInd] == NULL_CLUSTER)
  {
    for(i = 0; i < BSLPC; i++)
    {
      p_directClust->info.data[i] = '\0';  //regiao de armazenamento preenchida com '\0'   
    }
  }

  else
  {
    physicalNumCluster = p_sb->dZoneStart + inode.d[clustInd] * BLOCKS_PER_CLUSTER;
    //Read a cluster of data from the buffercache
    if( (stat = soReadCacheCluster(physicalNumCluster, &p_directClust)) != 0)
      return stat;
  }

    /*copy the information to the destination buffer*/
  memcpy(buff, p_directClust, sizeof(SODataClust));

            
  /* guardar tabela de nós I*/
  if( (stat = soStoreBlockInT()) != 0)
    return stat;

  /*guardar super bloco*/
  if( (stat = soStoreSuperBlock()) != 0)
    return stat;

  return 0;
}
