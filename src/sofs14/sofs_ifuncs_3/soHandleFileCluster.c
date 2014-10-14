/**
 *  \file soHandleFileCluster.c (implementation file)
 *
 *  \author Guilherme Cardoso (45726), Tiago Oliveira (51687)
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
#include "../sofs_ifuncs_3.h"

/** \brief operation get the logical number of the referenced data cluster for an inode in use */
#define GET         0
/** \brief operation allocate a new data cluster and associate it to the inode which describes the file */
#define ALLOC       1
/** \brief operation free the referenced data cluster */
#define FREE        2
/** \brief operation free the referenced data cluster and dissociate it from the inode which describes the file */
#define FREE_CLEAN  3
/** \brief operation dissociate the referenced data cluster from the inode which describes the file */
#define CLEAN       4

/* allusion to internal functions */

int soHandleDirect(SOSuperBlock *p_sb, uint32_t nInode, SOInode *p_inode, uint32_t nClust, uint32_t op,
        uint32_t *p_outVal);
int soHandleSIndirect(SOSuperBlock *p_sb, uint32_t nInode, SOInode *p_inode, uint32_t nClust, uint32_t op,
        uint32_t *p_outVal);
int soHandleDIndirect(SOSuperBlock *p_sb, uint32_t nInode, SOInode *p_inode, uint32_t nClust, uint32_t op,
        uint32_t *p_outVal);
int soAttachLogicalCluster(SOSuperBlock *p_sb, uint32_t nInode, uint32_t clustInd, uint32_t nLClust);
int soCleanLogicalCluster(SOSuperBlock *p_sb, uint32_t nInode, uint32_t nLClust);

/**
 *  \brief Handle of a file data cluster.
 *
 *  The file (a regular file, a directory or a symlink) is described by the inode it is associated to.
 *
 *  Several operations are available and can be applied to the file data cluster whose logical number is given.
 *
 *  The list of valid operations is
 *
 *    \li GET:        get the logical number of the referenced data cluster for an inode in use
 *    \li ALLOC:      allocate a new data cluster and associate it to the inode which describes the file
 *    \li FREE:       free the referenced data cluster
 *    \li FREE_CLEAN: free the referenced data cluster and dissociate it from the inode which describes the file
 *    \li CLEAN:      dissociate the referenced data cluster from the inode which describes the file.
 *
 *  Depending on the operation, the field <em>clucount</em> and the lists of direct references, single indirect
 *  references and double indirect references to data clusters of the inode associated to the file are updated.
 *
 *  Thus, the inode must be in use and belong to one of the legal file types for the operations GET, ALLOC, FREE and
 *  FREE_CLEAN and must be free in the dirty state for the operation CLEAN.
 *
 *  \param nInode number of the inode associated to the file
 *  \param clustInd index to the list of direct references belonging to the inode which is referred
 *  \param op operation to be performed (GET, ALLOC, FREE, FREE AND CLEAN, CLEAN)
 *  \param p_outVal pointer to a location where the logical number of the data cluster is to be stored
 *                  (GET / ALLOC); in the other cases (FREE / FREE AND CLEAN / CLEAN) it is not used
 *                  (in these cases, it should be set to \c NULL)
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the <em>inode number</em> or the <em>index to the list of direct references</em> are out of
 *                      range or the requested operation is invalid or the <em>pointer to outVal</em> is \c NULL when it
 *                      should not be (GET / ALLOC)
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c EFDININVAL, if the free inode in the dirty state is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c EDCINVAL, if the data cluster header is inconsistent
 *  \return -\c EDCARDYIL, if the referenced data cluster is already in the list of direct references (ALLOC)
 *  \return -\c EDCNOTIL, if the referenced data cluster is not in the list of direct references
 *              (FREE / FREE AND CLEAN / CLEAN)
 *  \return -\c EWGINODENB, if the <em>inode number</em> in the data cluster <tt>status</tt> field is different from the
 *                          provided <em>inode number</em> (ALLOC / FREE AND CLEAN / CLEAN)
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soHandleFileCluster(uint32_t nInode, uint32_t clustInd, uint32_t op, uint32_t *p_outVal) {
    soColorProbe(413, "07;31", "soHandleFileCluster (%"PRIu32", %"PRIu32", %"PRIu32", %p)\n",
            nInode, clustInd, op, p_outVal);

    int stat;
    SOSuperBlock *p_sb;
    SOInode *p_inode;
    uint32_t nBlk, offset;

    if ((stat = soLoadSuperBlock()))
        return stat;

    p_sb = soGetSuperBlock();

    /* START OF VALIDATION */

    /* if nInode is out of range */
    if (nInode == 0 || nInode >= p_sb->iTotal)
        return -EINVAL;

    /* index (clustInd) to the list of direct references are out of range */
    if (clustInd > MAX_FILE_CLUSTERS)
        return -EINVAL;

    /* requested operation is invalid */
    if (op < 0 || op > 4)
        return -EINVAL;

    /* the pointer p_outVal is NULL when it should not be (GET / ALLOC) */
    if ((op == GET || op == ALLOC) && p_outVal == NULL) // duvida na comparacao do ponteiro, sera apenas NULL na comp.?
        return -EINVAL;

    /* convert the inode number into the logical number */
    if (stat = soConvertRefInT(nInode, &nBlk, &offset) != 0)
        return stat;

    /* load the contents of a specific block of the table of inodes into internal storage*/
    if ((stat = soLoadBlockInT(nBlk)) != 0)
        return stat;

    /* get a pointer to the contents of a specific block of the table of inodes */
    p_inode = soGetBlockInT();

    if (op == CLEAN) {
        /* quick check of a free inode in the dirty state */
        if ((stat = soQCheckFDInode(p_sb, &p_inode[offset])) != 0)
            return stat;
    } else {
        /* quick check of an inode in use */
        if ((stat = soQCheckInodeIU(p_sb, &p_inode[offset])) != 0)
            return stat;
    }

    /* END OF VALIDATION */

    if (clustInd <= N_DIRECT) {
        soHandleDirect(p_sb, nInode, p_inode, clustInd, op, p_outVal);
    } else if (clustInd <= N_DIRECT + RPC) {
        soHandleSIndirect(p_sb, nInode, p_inode, clustInd, op, p_outVal);
    } else {
        soHandleDIndirect(p_sb, nInode, p_inode, clustInd, op, p_outVal);
    }

    return 0;
}

/**
 *  \brief Handle of a file data cluster which belongs to the direct references list.
 *
 *  \param p_sb pointer to a buffer where the superblock data is stored
 *  \param nInode number of the inode associated to the file
 *  \param p_inode pointer to a buffer which stores the inode contents
 *  \param clustInd index to the list of direct references belonging to the inode which is referred
 *  \param op operation to be performed (GET, ALLOC, FREE, FREE AND CLEAN, CLEAN)
 *  \param p_outVal pointer to a location where the logical number of the data cluster is to be stored
 *                  (GET / ALLOC); in the other cases (FREE / FREE AND CLEAN / CLEAN) it is not used
 *                  (in these cases, it should be set to \c NULL)
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the requested operation is invalid
 *  \return -\c EDCARDYIL, if the referenced data cluster is already in the list of direct references (ALLOC)
 *  \return -\c EDCNOTIL, if the referenced data cluster is not in the list of direct references
 *              (FREE / FREE AND CLEAN / CLEAN)
 *  \return -\c EWGINODENB, if the <em>inode number</em> in the data cluster <tt>status</tt> field is different from the
 *                          provided <em>inode number</em> (FREE AND CLEAN / CLEAN)
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soHandleDirect(SOSuperBlock *p_sb, uint32_t nInode, SOInode *p_inode, uint32_t clustInd, uint32_t op,
        uint32_t *p_outVal) {

    /* insert your code here */

    return 0;
}

/**
 *  \brief Handle of a file data cluster which belongs to the single indirect references list.
 *
 *  \param p_sb pointer to a buffer where the superblock data is stored
 *  \param nInode number of the inode associated to the file
 *  \param p_inode pointer to a buffer which stores the inode contents
 *  \param clustInd index to the list of direct references belonging to the inode which is referred
 *  \param op operation to be performed (GET, ALLOC, FREE, FREE AND CLEAN, CLEAN)
 *  \param p_outVal pointer to a location where the logical number of the data cluster is to be stored
 *                  (GET / ALLOC); in the other cases (FREE / FREE AND CLEAN / CLEAN) it is not used
 *                  (in these cases, it should be set to \c NULL)
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the requested operation is invalid
 *  \return -\c EDCARDYIL, if the referenced data cluster is already in the list of direct references (ALLOC)
 *  \return -\c EDCNOTIL, if the referenced data cluster is not in the list of direct references
 *              (FREE / FREE AND CLEAN / CLEAN)
 *  \return -\c EWGINODENB, if the <em>inode number</em> in the data cluster <tt>status</tt> field is different from the
 *                          provided <em>inode number</em> (FREE AND CLEAN / CLEAN)
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soHandleSIndirect(SOSuperBlock *p_sb, uint32_t nInode, SOInode *p_inode, uint32_t clustInd, uint32_t op,
        uint32_t *p_outVal) {

    uint32_t ref_offset; // reference position
    int stat; // function return status control
    SODataClust *dc; // pointer to datacluster
    SOInode *p_inode;  // pointer to nInode inode
    uint32_t nBlk, offset; // inode block position and offset
    uint32_t *p_nclust; // pointer no cluster number
    uint32_t NFClt;

    if (op > 4) return -EINVAL;

    ref_offset = (clustInd - N_DIRECT - RPC) % RPC;
    
    NFClt = p_sb->dZoneStart + p_inode.i1 * BLOCKS_PER_CLUSTER;

    if((stat = soReadInode(p_inode,nInode,IUIN)) != 0)
        return stat;

    switch (op) {
        case GET:
        {
            if ((stat = soLoadSngIndRefClust(NFClt)) != 0)
                return stat;

            dc = soGetSngIndRefClust();

            p_outVal = dc->info.ref[ref_offset];

            break;
        }
        case ALLOC:
        {
            // EDCARDYIL, if the referenced data cluster is already in the list of direct references (ALLOC)
            if (p_inode->i1 == NULL_CLUSTER) {
                if ((stat = soAllocDataCluster(nInode, p_nclust)) != 0)
                    return stat;

                p_inode[offset]->i1 = *p_nclust;

                p_inode[offset]->cluCount++;
            }
            
            if ((stat = soLoadSngIndRefClust(NFClt) != 0))
                return stat;

            dc = soGetSngIndRefClust();
            
            if(dc->info.ref[ref_offset] != NULL_CLUSTER) return -EDCARDYIL;

            if ((stat == soAllocDataCluster(nInode, p_nclust)) != 0)
                return stat;

            dc->info.ref[ref_offset] = *p_nclust;
            
            soAttachLogicalCluster(p_sb,nInode,clustInd,NFClt);

            p_inode[offset].cluCount++;

            break;
        }
        case FREE:        {
            p_outVal = NULL;
            
            if(p_inode->i1 == NULL_CLUSTER) return -EDCNOTIL;
            
            if ((stat = soLoadSngIndRefClust(NFClt)) != 0)
                return stat;

            dc = soGetSngIndRefClust();
            
            if(dc->info.ref[ref_offset] == NULL_CLUSTER) return -EDCNOTIL;

            if (dc->info.ref[ref_offset] != clustInd) return -EDCNOTIL;

            if (dc->stat != nInode) return -EWGINODENB;

            if ((stat = soFreeDataCluster(dc->info.de[ref_offset])) != 0)
                return stat;
            
            break;
        }
        case FREE_CLEAN:
        {
            p_outVal = NULL;
            
            if(p_inode->i1 == NULL_CLUSTER) return -EDCNOTIL;
            
            if((stat = soLoadSngIndRefClust(NFClt)) != 0)
                return stat;
            
            dc = soGetSngIndRefClust();
            
            if (dc->info.ref[ref_offset] != clustInd) return -EDCNOTIL;

            if (dc->stat != nInode) return -EWGINODENB;
            
            if ((stat = soFreeDataCluster(dc->info.de[ref_offset])) != 0)
                return stat;
            
            p_inode[offset].cluCount--;
            
            soCleanLogicalCluster(p_sb,nInode,dc->info.ref[ref_offset]);
            
            uint32_t clusterref_pos;
            uint32_t clustercount;
            
            for(clusterref_pos = 0; clusterref_pos < RPC; clusterref_pos++)
                if(dc->info.ref[clusterref_pos] != NULL_CLUSTER){
                    clustercount++;
                    break;
                }
            
            if(clustercount != 0) soCleanDataCluster(p_sb,nInode,p_inode->i1);
            
            p_inode->cluCount--;
                               
            break;
        }
        case CLEAN:
        {
            // EDCNOTIL, if the referenced data cluster is not in the list of direct references
            // EWGINODENB, if the <em>inode number</em> in the data cluster <tt>status</tt> field is different from the provided <em>inode number</em> (FREE AND CLEAN / CLEAN)
            p_outVal = NULL;
        }
        default:
        {
            p_outVal = NULL;

            return -EINVAL;
        }
    }

    return 0;
}

/**
 *  \brief Handle of a file data cluster which belongs to the double indirect references list.
 *
 *  \param p_sb pointer to a buffer where the superblock data is stored
 *  \param nInode number of the inode associated to the file
 *  \param p_inode pointer to a buffer which stores the inode contents
 *  \param clustInd index to the list of direct references belonging to the inode which is referred
 *  \param op operation to be performed (GET, ALLOC, FREE, FREE AND CLEAN, CLEAN)
 *  \param p_outVal pointer to a location where the logical number of the data cluster is to be stored
 *                  (GET / ALLOC); in the other cases (FREE / FREE AND CLEAN / CLEAN) it is not used
 *                  (in these cases, it should be set to \c NULL)
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the requested operation is invalid
 *  \return -\c EDCARDYIL, if the referenced data cluster is already in the list of direct references (ALLOC)
 *  \return -\c EDCNOTIL, if the referenced data cluster is not in the list of direct references
 *              (FREE / FREE AND CLEAN / CLEAN)
 *  \return -\c EWGINODENB, if the <em>inode number</em> in the data cluster <tt>status</tt> field is different from the
 *                          provided <em>inode number</em> (FREE AND CLEAN / CLEAN)
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soHandleDIndirect(SOSuperBlock *p_sb, uint32_t nInode, SOInode *p_inode, uint32_t clustInd, uint32_t op,
        uint32_t *p_outVal) {

    /* insert your code here */

    return 0;
}

/**
 *  \brief Attach a file data cluster whose index to the list of direct references and logical number are known.
 *
 *  \param p_sb pointer to a buffer where the superblock data is stored
 *  \param nInode number of the inode associated to the file
 *  \param clustInd index to the list of direct references belonging to the inode which is referred
 *  \param nLClust logical number of the data cluster
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EWGINODENB, if the <em>inode number</em> in the data cluster <tt>status</tt> field is different from the
 *                          provided <em>inode number</em>
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soAttachLogicalCluster(SOSuperBlock *p_sb, uint32_t nInode, uint32_t clustInd, uint32_t nLClust) {

    int stat;
    SOInode p_inode;
    uint32_t ind_prev, ind_next;
    SODataClust dc;
    
    if((stat = soReadInode(p_inode,nInode,IUIN)) != 0)
        return stat;
    
    if((stat = soHandleFileCluster(nInode,clustInd-1,GET,&ind_prev)))
        return 0;
    
    if((stat = soHandleFileCluster(nInode,clustInd+1,GET,&ind_next)))
        return 0;
    
    if((stat = soReadCacheCluster(p_sb->dZoneStart + nLClust * BLOCKS_PER_CLUSTER,&dc))!= 0)
        return stat;
    
    if(ind_prev != NULL_CLUSTER) dc.prev = ind_prev;
    
    if(ind_next != NULL_CLUSTER) dc.next = ind_next;
    
    if((stat = soWriteCacheCluster(p_sb->dZoneStart + nLClust * BLOCKS_PER_CLUSTER,&dc)) != 0)
        return stat;
    
    return 0;
    
}

/**
 *  \brief Clean a file data cluster whose logical number is known.
 *
 *  \param p_sb pointer to a buffer where the superblock data is stored
 *  \param nInode number of the inode associated to the file
 *  \param nLClust logical number of the data cluster
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EWGINODENB, if the <em>inode number</em> in the data cluster <tt>status</tt> field is different from the
 *                          provided <em>inode number</em>
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soCleanLogicalCluster(SOSuperBlock *p_sb, uint32_t nInode, uint32_t nLClust) {

    //read
    //testa o stat
    //mete o stat a clean
    //write

    return 0;
}
