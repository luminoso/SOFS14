/**
 *  \file soFreeDataCluster.c (implementation file)
 *
 *  \author Guilherme Cardoso - 45726
 */

#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#include "sofs_probe.h"
#include "sofs_buffercache.h"
#include "sofs_superblock.h"
#include "sofs_inode.h"
#include "sofs_datacluster.h"
#include "sofs_basicoper.h"
#include "sofs_basicconsist.h"

/* Allusion to internal function */

int soDeplete (SOSuperBlock *p_sb);

/**
 *  \brief Free the referenced data cluster.
 *
 *  The cluster is inserted into the insertion cache of free data cluster references. If the cache is full, it has to be
 *  depleted before the insertion may take place. The data cluster should be put in the dirty state (the <tt>stat</tt>
 *  of the header should remain as it is), the other fields of the header, <tt>prev</tt> and <tt>next</tt>, should be
 *  put to NULL_CLUSTER. The only consistency check to carry out at this stage is to check if the data cluster was
 *  allocated.
 *
 *  Notice that the first data cluster, supposed to belong to the file system root directory, can never be freed.
 *
 *  \param nClust logical number of the data cluster
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, the <em>data cluster number</em> is out of range or the data cluster is not allocated
 *  \return -\c EDCNALINVAL, if the data cluster has not been previously allocated
 *  \return -\c EDCINVAL, if the data cluster header is inconsistent
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soFreeDataCluster (uint32_t nClust)
{
  soColorProbe (614, "07;33", "soFreeDataCluster (%"PRIu32")\n", nClust);
  
  int stat;
  SOSuperBlock *p_sb;
  uint32_t  NFClt;
  SODataClust datacluster;
  
  if ((stat = soLoadSuperBlock ()) != 0)
    return stat;
  p_sb = soGetSuperBlock();
  
  // validação EINVAL
  if(nClust > p_sb->dZoneTotal) return  -EINVAL;
  if(nClust <= 0) return -EINVAL;
  
  NFClt = p_sb->dZoneStart + nClust * BLOCKS_PER_CLUSTER;
  
  if((stat = soReadCacheCluster(NFClt,&datacluster)) != 0)
    return stat;
  
  if(datacluster.stat == NULL_INODE) return -EDCNALINVAL;
  
  uint32_t p_stat;
  if((stat = soQCheckStatDC(p_sb,nClust,&p_stat)) != 0)
    return stat;
  
  if(p_stat != ALLOC_CLT) return -EDCNALINVAL;
  
  datacluster.prev = datacluster.next = NULL_CLUSTER;
  
  // verificar se a cache de insercao está cheia
  if(p_sb->dZoneInsert.cacheIdx == DZONE_CACHE_SIZE)
    soDeplete(p_sb);
  
  p_sb->dZoneInsert.cache[p_sb->dZoneInsert.cacheIdx] = nClust;
  p_sb->dZoneInsert.cacheIdx +=1;
  p_sb->dZoneFree += 1;
  
  if((stat = soWriteCacheCluster(NFClt,&datacluster)) != 0)
    return stat;
  
  return 0;
}

/**
 *  \brief Deplete the insertion cache of free data cluster references.
 *
 *  \param p_sb pointer to a buffer where the superblock data is stored
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soDeplete (SOSuperBlock *p_sb)
{
  int stat;                                             // variavel para controlar o retorno das funcoes chamadas
  SODataClust datacluster;
  uint32_t NFClt;
  
  if(p_sb->dTail != NULL_CLUSTER){
    NFClt =  p_sb->dZoneStart + p_sb->dTail*BLOCKS_PER_CLUSTER;
    if((stat = soReadCacheCluster(NFClt,&datacluster)) != 0)
      return stat;
    datacluster.next = p_sb->dZoneInsert.cache[0];
    
    if((stat = soWriteCacheCluster(p_sb->dTail,&datacluster)) !=0)
      return stat;
  }
  
  int k;
  for(k = 0; k < p_sb->dZoneInsert.cacheIdx; k++){
    if(k == 0){
      NFClt =  p_sb->dZoneStart + p_sb->dZoneInsert.cache[0]*BLOCKS_PER_CLUSTER;
      if((stat = soReadCacheCluster(NFClt,&datacluster)) != 0)
        return stat;
      datacluster.prev = p_sb->dZoneInsert.cache[k-1];
      if((stat = soWriteCacheCluster(NFClt,&datacluster)) != 0)
        return stat;
    } else {
      NFClt = p_sb->dZoneStart + p_sb->dZoneInsert.cache[k] * BLOCKS_PER_CLUSTER;
      if((stat = soReadCacheCluster(NFClt,&datacluster)) != 0)
        return stat;
      datacluster.prev = p_sb->dZoneInsert.cache[k-1];
      if((stat = soWriteCacheCluster(NFClt,&datacluster)) != 0)
        return stat;
    }
    if(k != (p_sb->dZoneInsert.cacheIdx -1)){
      NFClt =  p_sb->dZoneStart + p_sb->dZoneInsert.cache[k]*BLOCKS_PER_CLUSTER;
      if((stat = soReadCacheCluster(NFClt,&datacluster)) != 0)
        return stat;
      datacluster.next = p_sb->dZoneInsert.cache[k+1];
      if((stat = soWriteCacheCluster(NFClt,&datacluster)) != 0)
        return stat;
    } else {
      NFClt =  p_sb->dZoneStart + p_sb->dZoneInsert.cache[k]*BLOCKS_PER_CLUSTER;
      if((stat = soReadCacheCluster(NFClt,&datacluster)) != 0)
        return stat;
      datacluster.next = NULL_CLUSTER;
    }
  }
  
  p_sb->dTail = p_sb->dZoneInsert.cache[p_sb->dZoneInsert.cacheIdx - 1];
  
  if(p_sb->dHead == NULL_CLUSTER)
    p_sb->dHead = p_sb->dZoneInsert.cache[0];
  
  for(k = 0; k < p_sb->dZoneInsert.cacheIdx; k++)
    p_sb->dZoneInsert.cache[k] = NULL_CLUSTER;
  
  p_sb->dZoneInsert.cacheIdx = 0;
  
  // gravar as alteracoes feitas no super bloco
  if((stat = soStoreSuperBlock()) != 0)
    return stat;
  
  // se tudo OK retornar 0
  return 0;
}
