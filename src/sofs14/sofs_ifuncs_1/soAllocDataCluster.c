/**
 *  \file soAllocDataCluster.c (implementation file)
 *
 *  \author Bruno Silva - 68535 
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
/* #define  CLEAN_CLUSTER */
#ifdef CLEAN_CLUSTER
#include "sofs_ifuncs_3.h"
#endif

/* Allusion to internal functions */

int soReplenish (SOSuperBlock *p_sb);
int soDeplete (SOSuperBlock *p_sb);

/**
 *  \brief Allocate a free data cluster and associate it to an inode.
 *
 *  The inode is supposed to be associated to a file (a regular file, a directory or a symbolic link), but the only
 *  consistency check at this stage should be to check if the inode is not free.
 *
 *  The cluster is retrieved from the retrieval cache of free data cluster references. If the cache is empty, it has to
 *  be replenished before the retrieval may take place. If the data cluster is in the dirty state, it has to be cleaned
 *  first. The header fields of the allocated cluster should be all filled in: <tt>prev</tt> and <tt>next</tt> should be
 *  set to \c NULL_CLUSTER and <tt>stat</tt> to the given inode number.
 *
 *  \param nInode number of the inode the data cluster should be associated to
 *  \param p_nClust pointer to the location where the logical number of the allocated data cluster is to be stored
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, the <em>inode number</em> is out of range or the <em>pointer to the logical data cluster
 *                      number</em> is \c NULL
 *  \return -\c ENOSPC, if there are no free data clusters
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c EFDININVAL, if the free inode in the dirty state is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c EDCINVAL, if the data cluster header is inconsistent
 *  \return -\c EDCNOTIL, if the referenced data cluster is not in the list of direct references
 *  \return -\c EWGINODENB, if the <em>inode number</em> in the data cluster <tt>status</tt> field is different from the
 *                          provided <em>inode number</em>
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soAllocDataCluster (uint32_t nInode, uint32_t *p_nClust)
{
	soColorProbe (613, "07;33", "soAllocDataCluster (%"PRIu32", %p)\n", nInode, p_nClust);

	int stat;
	uint32_t nBlock, offset, nClust; //variaveis para localizar o inode pretendido, e o cluster
	SOSuperBlock *p_sb; //ponteiro para o superbloco
	SOInode *p_inode; // ponteiro para o inode que vai ser reservado o cluster
  SODataClust *cluster; //ponteiro para o cluster que vai ser reservado

	if((stat = soLoadSuperBlock()) != 0)
		return stat;

	p_sb = soGetSuperBlock();

	if(nInode <= 0  || nInode > p_sb->iTotal -1 || p_nClust == NULL)
		return -EINVAL;

  if(p_sb->dZoneFree == 0)
  	return -ENOSPC;

  if((stat = soConvertRefInT(nInode, &nBlock, &offset)) != 0)
  	return stat;
  	
	if((stat = soLoadBlockInT(nBlock)) != 0)
		return stat;

	p_inode = soGetBlockInT();

	if((stat = soQCheckInodeIU(p_sb, &p_inode[offset])) != 0)
		return stat;

	//Falta testar EDCNOTIL e EWGINODENB
	//acho que so é feito no fim

	if(p_sb->dZoneRetriev.cacheIdx == DZONE_CACHE_SIZE)
		soReplenish(p_sb);

  //nclust = logical number of cluster
	nClust = p_sb->dZoneRetriev.cache[p_sb->dZoneRetriev.cacheIdx]; // passar o numero do proximo cluster livre para nClust
	p_sb->dZoneRetriev.cache[p_sb->dZoneRetriev.cacheIdx] = NULL_CLUSTER; //esse cluster já nao vai estar disponivel, por isso NULL_CLUSTER
	p_sb->dZoneRetriev.cacheIdx += 1;
	p_sb->dZoneFree -= 1;


	//ir buscar o cluster nClust. nclust needs to be physical number
  //relação entre numero fisico e numero logico
  //NFClt = dzone_start + NLClt * BLOCKS_PER_CLUSTER;
  if((stat = soLoadDirRefClust(p_sb->dZoneStart + (nClust * BLOCKS_PER_CLUSTER))) != 0)
    return stat;

  cluster = soGetDirRefClust();

  cluster->prev = cluster->next = NULL_CLUSTER;
  cluster->stat = nInode;
  *p_nClust = nClust;
  

  return 0;
}

/**
 *  \brief Replenish the retrieval cache of free data cluster references.
 *
 *  \param p_sb pointer to a buffer where the superblock data is stored
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soReplenish (SOSuperBlock *p_sb)
{

  /* insert your code here */

  return 0;
}
