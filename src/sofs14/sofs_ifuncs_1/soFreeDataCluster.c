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

int soDeplete(SOSuperBlock *p_sb);

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

int soFreeDataCluster(uint32_t nClust) {
    soColorProbe(614, "07;33", "soFreeDataCluster (%"PRIu32")\n", nClust);

    int stat; // function return status control
    SOSuperBlock *p_sb; // super block pointer
    uint32_t NFClt; // data cluster physical position
    SODataClust datacluster;
    uint32_t cluster_stat;

    // load super block
    if ((stat = soLoadSuperBlock()) != 0)
        return stat;

    // get superblock pointer data
    p_sb = soGetSuperBlock();

    // check if the data cluster number is in the right range
    if (nClust > p_sb->dZoneTotal || nClust == 0) return -EINVAL;

    // check if the data cluster is allocated
    if ((stat = soQCheckStatDC(p_sb, nClust, &cluster_stat)) != 0)
        return stat;

    if (cluster_stat == FREE_CLT) return -EDCNALINVAL;

    // calculate data cluster physical position
    NFClt = p_sb->dZoneStart + nClust * BLOCKS_PER_CLUSTER;

    // read data cluster in NFClt physical position to our variable
    if ((stat = soReadCacheCluster(NFClt, &datacluster)) != 0)
        return stat;

    // check if the data cluster was allocated
    if (datacluster.stat == NULL_INODE) return -EDCNALINVAL;

    // end of validations. proceed to data cluster freeing 

    // data cluster isn't in a double linked list anymore
    datacluster.prev = datacluster.next = NULL_CLUSTER;

    // write data cluster new information
    if ((stat = soWriteCacheCluster(NFClt, &datacluster)) != 0)
        return stat;

    // check if the insert cache is full. if it is, deplete it
    if (p_sb->dZoneInsert.cacheIdx == DZONE_CACHE_SIZE)
        if ((stat = soDeplete(p_sb)) != 0)
            return stat;

    // load super block
    if ((stat = soLoadSuperBlock()) != 0)
        return stat;

    // get superblock pointer data
    p_sb = soGetSuperBlock();

    // insert freed data cluster in insert cache list
    p_sb->dZoneInsert.cache[p_sb->dZoneInsert.cacheIdx] = nClust;
    p_sb->dZoneInsert.cacheIdx += 1;
    p_sb->dZoneFree += 1;

    // save super block new information
    if ((stat = soStoreSuperBlock()) != 0)
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

int soDeplete(SOSuperBlock *p_sb) {
    int stat; // function return status control
    SODataClust datacluster;
    uint32_t NFClt; // data cluster physical position
    unsigned int k; // dZoneInsert cache position

    /* 1- check if there's still data clusters blocks in the linked list
     * 2- if so, connect dZoneInsert cache first position to dTail.next
     */

    if (p_sb->dTail != NULL_CLUSTER) {

        NFClt = p_sb->dZoneStart + p_sb->dTail * BLOCKS_PER_CLUSTER;

        if ((stat = soReadCacheCluster(NFClt, &datacluster)) != 0)
            return stat;

        datacluster.next = p_sb->dZoneInsert.cache[0];

        if ((stat = soWriteCacheCluster(NFClt, &datacluster)) != 0)
            return stat;
    }

    /* Empty dZoneInsert cache
     * 1- the first cache position .previous of the cache is connected to dTail
     * 2- connect data cluster .prev and .next in the cache array to each other
     * 3- connect the last cache position .next to NULL_CLUSTER
     * 4- update dTail information
     */
    for (k = 0; k < p_sb->dZoneInsert.cacheIdx; k++) {

        // if first position of the cache
        if (k == 0) {
            NFClt = p_sb->dZoneStart + p_sb->dZoneInsert.cache[0] * BLOCKS_PER_CLUSTER;

            if ((stat = soReadCacheCluster(NFClt, &datacluster)) != 0)
                return stat;

            datacluster.prev = p_sb->dTail;

            if ((stat = soWriteCacheCluster(NFClt, &datacluster)) != 0)
                return stat;
        } else {
            NFClt = p_sb->dZoneStart + p_sb->dZoneInsert.cache[k] * BLOCKS_PER_CLUSTER;

            if ((stat = soReadCacheCluster(NFClt, &datacluster)) != 0)
                return stat;

            datacluster.prev = p_sb->dZoneInsert.cache[k - 1];

            if ((stat = soWriteCacheCluster(NFClt, &datacluster)) != 0)
                return stat;
        }
        // for all the cache positions except the first and last
        if (k != (p_sb->dZoneInsert.cacheIdx - 1)) {
            NFClt = p_sb->dZoneStart + p_sb->dZoneInsert.cache[k] * BLOCKS_PER_CLUSTER;

            if ((stat = soReadCacheCluster(NFClt, &datacluster)) != 0)
                return stat;

            datacluster.next = p_sb->dZoneInsert.cache[k + 1];

            if ((stat = soWriteCacheCluster(NFClt, &datacluster)) != 0)
                return stat;
        } else { // for the last cache position
            NFClt = p_sb->dZoneStart + p_sb->dZoneInsert.cache[p_sb->dZoneInsert.cacheIdx - 1] * BLOCKS_PER_CLUSTER;

            if ((stat = soReadCacheCluster(NFClt, &datacluster)) != 0)
                return stat;

            datacluster.next = NULL_CLUSTER;

            if ((stat = soWriteCacheCluster(NFClt, &datacluster)) != 0)
                return stat;
        }
    }

    // point dTail to the last dZoneInsert cache position
    p_sb->dTail = p_sb->dZoneInsert.cache[p_sb->dZoneInsert.cacheIdx - 1];

    // check if wasn't any space left
    if (p_sb->dHead == NULL_CLUSTER)
        p_sb->dHead = p_sb->dZoneInsert.cache[0];

    // empty all cache positions to NULL_CLUSTER...
    for (k = 0; k < p_sb->dZoneInsert.cacheIdx; k++)
        p_sb->dZoneInsert.cache[k] = NULL_CLUSTER;

    // ...and reset cache position
    p_sb->dZoneInsert.cacheIdx = 0;

    // save all super block changes
    //if ((stat = soStoreSuperBlock()) != 0)
    //    return stat;

    return 0;
}