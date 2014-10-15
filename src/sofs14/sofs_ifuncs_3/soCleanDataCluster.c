/**
 *  \file soCleanDataCluster.c (implementation file)
 *
 *  \author Bruno Silva
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
 *  \brief Clean a data cluster from the inode describing a file which was previously deleted.
 *
 *  The inode is supposed to be free in the dirty state.
 *
 *  The list of references is parsed until the logical number of the data cluster is found or until the list is
 *  exhausted. If found, the data cluster (and all data clusters in its dependency, if it belongs to the auxiliary
 *  data structure that entails the list of single indirect or double indirect references) is cleaned.
 *
 *  \param nInode number of the inode associated to the data cluster
 *  \param nLClust logical number of the data cluster
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the <em>inode number</em> or the <em>logical cluster number</em> are out of range
 *  \return -\c EFDININVAL, if the free inode in the dirty state is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c EDCINVAL, if the data cluster header is inconsistent
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soCleanDataCluster (uint32_t nInode, uint32_t nLClust)
{
  soColorProbe (415, "07;31", "soCleanDataCluster (%"PRIu32", %"PRIu32")\n", nInode, nLClust);

  int stat, i; // stat = error stat of the function; i= auxiliary variable for iterations
  SOSuperBlock *p_sb; // pointer to the super block
  SOInode inode; // inode instance to clean the references
  SODataClust *p_cluster;

  // Load the super block
  if((stat = soLoadSuperBlock()) != 0)
 		return stat;

 	//get pointer to the super block
	p_sb = soGetSuperBlock();


	//check if inode is in the allowed parameters
  if(!(nInode > 0 && nInode < p_sb->iTotal))
  	return -EINVAL;


  //check if nLClust is in allowed parameters
  if (!(nLClust > 0 && nLClust < p_sb->dZoneTotal) != 0)
  	return stat;


  //read nInode data into inode
  if((stat = soReadInode(&inode, nInode, FDIN)) != 0)
  	return stat;


  //check consistency of inode
  if((stat = soQCheckFDInode(p_sb, &inode)) != 0)
  	return stat;

  // if the inode cluCount is less than N_DIRECT there's only the direct reference array
  if(inode.cluCount < N_DIRECT){
  	
  	//search through the direct reference array
  	for(i = 0; i < inode.cluCount; i++){

  		//test if d[i] is the cluster we want
  		if(inode.d[i] == nLClust){

  			if((stat = soLoadDirRefClust(p_sb->dZoneStart + nLClust * BLOCKS_PER_CLUSTER)) != 0)
  				return stat;

  			p_cluster = soGetDirRefClust();

  			if(p_cluster->prev != NULL_CLUSTER)
  				soCleanDataCluster(nInode, p_cluster->prev);

  			if(p_cluster->next != NULL_CLUSTER)
  				soCleanDataCluster(nInode, p_cluster->next);

  			if((stat = soHandleFileCluster(nInode, i, CLEAN, NULL)) != 0)
  				return stat;
  		}
  	}
  }



  return 0;
}
