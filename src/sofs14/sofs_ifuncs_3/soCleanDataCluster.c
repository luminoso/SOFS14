/**
 *  \file soCleanDataCluster.c (implementation file)
 *
 *  \author Bruno Silva  68535
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

    int stat, i, j; // stat = error stat of the function; i= auxiliary variable for iterations
    SOSuperBlock *p_sb; // pointer to the super block
    SOInode inode; // inode instance to clean the references
    uint32_t clusterCount = 0; // physical number of the cluster
    SODataClust *p_clusterS, *p_clusterD;

    // Load the super block
    if((stat = soLoadSuperBlock()) != 0)
    	return stat;

    //get pointer to the super block
    p_sb = soGetSuperBlock();

    //check if inode is in the allowed parameters
    if(nInode >= p_sb->iTotal)
    	return -EINVAL;

    //check if nLClust is in allowed parameters
    if (!(nLClust > 0 && nLClust < p_sb->dZoneTotal))
    	return -EINVAL;

    //read nInode data into inode
    if((stat = soReadInode(&inode, nInode, FDIN)) != 0)
    	return stat;

    //Direct References   
    for(i = 0; i < N_DIRECT; i++){

        if(inode.d[i] != NULL_CLUSTER){
            if(inode.d[i] == nLClust){
                //if cluster is in d[i] clean it
                if((stat = soHandleFileCluster(nInode, i, CLEAN, NULL)) != 0)
                    return stat;
                return 0;
            }
            else
                clusterCount++;
        }
        //if we parsed all the clusters in the inode and it's not found
        if(clusterCount == inode.cluCount)
            return -EDCINVAL; 
    }
    

    //Direct References Cluster
    //if nLClust is i1
    if(inode.i1 == nLClust){
        if ((stat = soLoadDirRefClust(p_sb->dZoneStart + inode.i1 * BLOCKS_PER_CLUSTER)) != 0)
            return stat;
        p_clusterS = soGetDirRefClust();
        //clean all references in i1
        for(i = 0; i < RPC; i++){
            if(p_clusterS->info.ref[i-N_DIRECT] != NULL_CLUSTER){
                if((stat = soHandleFileCluster(nInode, i + N_DIRECT, CLEAN, NULL)) != 0)
                    return stat;
            }
        }
        return 0;
    }
    //if nLClust is not i1 it can be any of the RPC
    else{
        if((stat = soLoadDirRefClust(p_sb->dZoneStart + inode.i1 * BLOCKS_PER_CLUSTER)) != 0)
            return stat;

        p_clusterS = soGetDirRefClust();

        for(i = 0; i < RPC; i++){
            if(p_clusterS->info.ref[i] != NULL_CLUSTER){
                //if nLCuster is found clean it
                if(p_clusterS->info.ref[i] == nLClust){
                    if((stat = soHandleFileCluster(nInode, i + N_DIRECT, CLEAN, NULL)) != 0)
                        return stat;
                    return 0;
                }
                else
                    clusterCount++;
            }
            //if we parsed all the clusters in the inode and it's not found
            if(clusterCount == inode.cluCount)
                return -EDCINVAL;
        }
    }


    //Referencias duplamente indirectas
    //if nLClust is i2
    if(inode.i2 == nLClust){
        if ((stat = soLoadSngIndRefClust(p_sb->dZoneStart + inode.i2 * BLOCKS_PER_CLUSTER)) != 0)
            return stat;

        p_clusterS = soGetSngIndRefClust();
        //clean all clusters in indirect and direct references
        for(i = 0; i < RPC; i++){
            if(p_clusterS->info.ref[i] != NULL_CLUSTER){
                if ((stat = soLoadDirRefClust(p_sb->dZoneStart + p_clusterS->info.ref[i] * BLOCKS_PER_CLUSTER)) != 0)
                    return stat;

                p_clusterD = soGetDirRefClust();

                for(j = 0; j < RPC; j++){
                    if(p_clusterD->info.ref[j] != NULL_CLUSTER)
                        if((stat = soHandleFileCluster(nInode, N_DIRECT + (RPC*(i+1)) + j, CLEAN, NULL)) != 0)
                            return stat;
                }  
            }
        }
        return 0;
    }
    //if nLClust is not i2
    else{
        if ((stat = soLoadSngIndRefClust(p_sb->dZoneStart + inode.i2 * BLOCKS_PER_CLUSTER)) != 0)
            return stat;

        p_clusterS = soGetSngIndRefClust();
        //parse i2 references
        for(i = 0; i < RPC; i++){
            if(p_clusterS->info.ref[i] != NULL_CLUSTER){

                //if nLClust is in direct references
                if(p_clusterS->info.ref[i] == nLClust){
                    if ((stat = soLoadDirRefClust(p_sb->dZoneStart + p_clusterS->info.ref[i] * BLOCKS_PER_CLUSTER)) != 0)
                        return stat;

                    p_clusterD = soGetDirRefClust();
                    //clean all indirect references for the direct reference entry
                    for(j = 0; j < RPC; j++){
                        if(p_clusterD->info.ref[j] != NULL_CLUSTER)
                            if((stat = soHandleFileCluster(nInode, N_DIRECT + (RPC*(i+1)) + j, CLEAN, NULL)) != 0)
                                return stat;
                    } 
                }
                //if it's not in the direct references, let's look in all the indirect
                else{
                    if ((stat = soLoadDirRefClust(p_sb->dZoneStart + p_clusterS->info.ref[i] * BLOCKS_PER_CLUSTER)) != 0)
                        return stat;

                    p_clusterD = soGetDirRefClust();
                    //search all the indirect references for each of the direct references
                    for(j = 0; j < RPC; j++){
                        if(p_clusterD->info.ref[j] != NULL_CLUSTER){
                            //if nLCluster is found clean it
                            if(p_clusterD->info.ref[j] == nLClust){
                                if((stat = soHandleFileCluster(nInode, N_DIRECT + (RPC*(i+1)) + j, CLEAN, NULL)) != 0)
                                    return stat;
                                return 0;
                            }
                            else
                                clusterCount++;
                        }
                        if(clusterCount == inode.cluCount)
                            return -EDCINVAL;      
                    }
                    clusterCount++;
                }
                //if we parsed all the clusters in the inode and it's not found
                if(clusterCount == inode.cluCount)
                    return -EDCINVAL;
            }
        }
    }
    return 0;
}
