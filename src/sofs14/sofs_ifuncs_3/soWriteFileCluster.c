/**
 *  \file soWriteFileCluster.c (implementation file)
 *
 *  \author João Cravo - 63784
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <errno.h>

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
 *  \brief Write a specific data cluster.
 *
 *  Data is written into a specific data cluster which is supposed to belong to an inode
 *  associated to a file (a regular file, a directory or a symbolic link). Thus, the inode must be in use and belong
 *  to one of the legal file types.
 *
 *  If the cluster has not been allocated yet, it will be allocated now so that data can be stored there.
 *
 *  \param nInode number of the inode associated to the file
 *  \param clustInd index to the list of direct references belonging to the inode where data is to be written into
 *  \param buff pointer to the buffer where data must be written from
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

int soWriteFileCluster (uint32_t nInode, uint32_t clustInd, SODataClust *buff)
{
  soColorProbe (412, "07;31", "soWriteFileCluster (%"PRIu32", %"PRIu32", %p)\n", nInode, clustInd, buff);

  int stat; //variavel para o estado de erro
  uint32_t numDC; //variavel para numero dataclusters
  SODataClust *p_dc; //ponteiro para datacluster
  SOSuperBlock *p_sb; //ponteiro para o superbloco
  SOInode inode; //variavel para inode
  
  //Ler e carregar o Super Bloco
  if((stat = soLoadSuperBlock()) != 0)
          return stat;
  
  p_sb = soGetSuperBlock();
  
  //Verificar parametros clustInd e buff
  if(clustInd >= MAX_FILE_CLUSTERS) 
      return -EINVAL;
  
  //Verificar parametros do buff
  if(buff == NULL)
      return -EINVAL;
  
  //Verificar parametros do nInode
  if(nInode < 1 || nInode >= p_sb->iTotal)
      return -EINVAL;
	
  //Obter numero logico do cluster
  if((stat = soHandleFileCluster(nInode, clustInd, GET, &numDC)) != 0)
      return stat;
  
  //Se não houver um cluster associado
  if(numDC == NULL_CLUSTER){
      if((stat = soHandleFileCluster(nInode, clustInd, ALLOC, &numDC)) != 0)
          return stat;
  }
  
  //Carrega o conteudo do cluster especifico
  if((stat = soLoadDirRefClust(p_sb->dZoneStart+numDC*BLOCKS_PER_CLUSTER)) != 0)
      return stat;
  
  //Obter ponteiro para referencia do datacluster
  p_dc = soGetDirRefClust();
  
  //Copiar info do buff para o datacluster
  p_dc->info=buff->info;
  
  //Ler inode 
  if((stat = soReadInode(&inode,nInode, IUIN)) != 0)
      return stat;

  //Escrever iNode lido
  if((stat = soWriteInode(&inode, nInode, IUIN)) != 0)
      return stat;
  
  //Gravar conteudo do cluster
  if((stat = soStoreDirRefClust()) != 0)
      return stat;
  
  //Gravar conteudo do Super Bloco
  if((stat = soStoreSuperBlock()) != 0)
      return stat;
  
  return 0;
}
