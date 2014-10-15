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
    SOInode *p_inode = NULL;

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
    if (op > 4)
        return -EINVAL;

    /* the pointer p_outVal is NULL when it should not be (GET / ALLOC) */
    if ((op == GET || op == ALLOC) && p_outVal == NULL)
        return -EINVAL;

    if (op == CLEAN) {
        if ((stat = soReadInode(p_inode, nInode, FDIN)) != 0)
            return stat;

        /* quick check of a free inode in the dirty state */
        if ((stat = soQCheckFDInode(p_sb, p_inode)) != 0)
            return stat;
    } else {
        if ((stat = soReadInode(p_inode, nInode, IUIN)) != 0)
            return stat;

        /* quick check of an inode in use */
        if ((stat = soQCheckInodeIU(p_sb, p_inode)) != 0)
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
 *  \return -\c EWGINODENB(clustInd - N_DIRECT - RPC) % RPC, if the <em>inode number</em> in the data cluster <tt>status</tt> field is different from the
 *                          provided <em>inode number</em> (FREE AND CLEAN / CLEAN)
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soHandleDirect(SOSuperBlock *p_sb, uint32_t nInode, SOInode *p_inode, uint32_t clustInd, uint32_t op,
        uint32_t *p_outVal) {

    uint32_t NLClt, NFClt;                  // NLClt: cluster logic number, NFClt: cluster physical number  
    
    NLClt = clustInd; // DIRTY FIX: apenas para compilar
    
    NFClt = p_sb->dZoneStart + NLClt * BLOCKS_PER_CLUSTER;

    /* requested operation is invalid */
    if (op > 4)
        return -EINVAL;

    switch(op)
    {
        case GET: 
            *p_outVal = p_inode->d[clustInd];    // get cluster logic number *p ou p? eu quero mudar o valor e nao o endereco
            break;                              // nao tenho de verificar se o nó i está em uso??
                                                // tenho de usar p_inode[offset] ?? talvez corrigido na funcao principal

        case ALLOC: 
                
            break;

        case CLEAN: break;

        case FREE:  break;

        case FREE_CLEAN: break;

        default : break;

    }


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
    SODataClust *dc = NULL; // pointer to datacluster
    uint32_t *p_nclust = NULL; // pointer no cluster number

    if (op > 4) return -EINVAL;

    ref_offset = (clustInd - N_DIRECT) % RPC;

    switch (op) {
        case GET:
        {
            if (p_inode->i1 == NULL_CLUSTER) {
                *p_outVal = NULL_CLUSTER;
            } else {
                if ((stat = soLoadDirRefClust(p_sb->dZoneStart + p_inode->i1 * BLOCKS_PER_CLUSTER)) != 0)
                    return stat;

                dc = soGetSngIndRefClust();

                *p_outVal = dc->info.ref[ref_offset];
            }

            break;
        }
        case ALLOC:
        {
            if (p_inode->i1 == NULL_CLUSTER) {
                if ((stat = soAllocDataCluster(nInode, p_nclust)) != 0)
                    return stat;

                p_inode->i1 = *p_nclust;

                if ((stat = soReadCacheCluster(p_sb->dZoneStart + *p_nclust * BLOCKS_PER_CLUSTER, dc)) != 0)
                    return stat;

                uint32_t i; // reference position 
                for (i = 0; i < RPC; i++) dc->info.ref[i] = NULL_CLUSTER;

                p_inode->cluCount++;
            }
            if ((stat = soLoadDirRefClust(p_sb->dZoneStart + p_inode->i1 * BLOCKS_PER_CLUSTER) != 0))
                return stat;

            dc = soGetSngIndRefClust();

            if (dc->info.ref[ref_offset] != NULL_CLUSTER) return -EDCARDYIL;

            if ((stat == soAllocDataCluster(nInode, p_nclust)) != 0)
                return stat;

            dc->info.ref[ref_offset] = *p_outVal = *p_nclust;

            if ((stat = soAttachLogicalCluster(p_sb, nInode, clustInd, dc->info.ref[ref_offset])) != 0)
                return stat;

            p_inode->cluCount++;

            break;
        }
        case FREE:
        {
            p_outVal = NULL;

            if (p_inode->i1 == NULL_CLUSTER) return -EDCNOTIL;

            if ((stat = soLoadDirRefClust(p_sb->dZoneStart + p_inode->i1 * BLOCKS_PER_CLUSTER)) != 0)
                return stat;

            dc = soGetSngIndRefClust();

            if (dc->info.ref[ref_offset] == NULL_CLUSTER) return -EDCNOTIL;

            if (dc->stat != nInode) return -EWGINODENB;

            if ((stat = soFreeDataCluster(dc->info.ref[ref_offset])) != 0)
                return stat;

            break;
        }
        case FREE_CLEAN:
        {
            p_outVal = NULL;

            if (p_inode->i1 == NULL_CLUSTER) return -EDCNOTIL;

            if ((stat = soLoadDirRefClust(p_sb->dZoneStart + p_inode->i1 * BLOCKS_PER_CLUSTER)) != 0)
                return stat;

            dc = soGetSngIndRefClust();

            if (dc->info.ref[ref_offset] == NULL_CLUSTER) return -EDCNOTIL;

            if (dc->stat != nInode) return -EWGINODENB;

            if ((stat = soFreeDataCluster(dc->info.ref[ref_offset])) != 0)
                return stat;

            p_inode->cluCount--;

            if ((stat = soCleanLogicalCluster(p_sb, nInode, dc->info.ref[ref_offset])) != 0)
                return stat;

            uint32_t clusterref_pos;
            uint32_t clustercount;

            for (clusterref_pos = 0, clustercount = 0; clusterref_pos < RPC; clusterref_pos++)
                if (dc->info.ref[clusterref_pos] != NULL_CLUSTER) {
                    clustercount++;
                    break;
                }

            if (clustercount != 0) {
                soCleanDataCluster(nInode, p_inode->i1);
                p_inode->cluCount--;
            }

            break;
        }
        case CLEAN:
        {
            p_outVal = NULL;

            if (p_inode->i1 == NULL_CLUSTER) return -EDCNOTIL;

            if ((stat = soLoadDirRefClust(p_sb->dZoneStart + p_inode->i1 * BLOCKS_PER_CLUSTER)) != 0)
                return stat;

            dc = soGetSngIndRefClust();

            if (dc->info.ref[ref_offset] == NULL_CLUSTER) return -EDCNOTIL;

            if (dc->stat != nInode) return -EWGINODENB;

            if ((stat = soCleanDataCluster(nInode, dc->info.ref[ref_offset])) != 0)
                return stat;

            p_inode->cluCount--;

            uint32_t clusterref_pos;
            uint32_t clustercount;

            for (clusterref_pos = 0, clustercount = 0; clusterref_pos < RPC; clusterref_pos++)
                if (dc->info.ref[clusterref_pos] != NULL_CLUSTER) {
                    clustercount++;
                    break;
                }

            if (clustercount != 0) {
                soCleanDataCluster(nInode, p_inode->i1);
                p_inode->cluCount--;
            }

            break;
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

    uint32_t ref_Soffset, ref_Doffset; // reference position
    int stat; // function return status control
    SODataClust *dc = NULL; // pointer to datacluster
    uint32_t *p_nclust = NULL; // pointer no cluster number

    if (op > 4) return -EINVAL;

    ref_Soffset = (clustInd - N_DIRECT + RPC) / RPC;
    ref_Doffset = (clustInd - N_DIRECT + RPC) % RPC;

    switch (op) {
        case GET:
        {
            if (p_inode->i2 == NULL_CLUSTER) {
                *p_outVal = NULL_CLUSTER;
            } else {
                if ((stat = soLoadSngIndRefClust(p_sb->dZoneStart + p_inode->i2 * BLOCKS_PER_CLUSTER)) != 0)
                    return stat;

                dc = soGetSngIndRefClust();

                if (dc->info.ref[ref_Soffset] == NULL_CLUSTER) {
                    *p_outVal = NULL_CLUSTER;
                } else {
                    if ((stat = soLoadDirRefClust(ref_Soffset)) != 0)
                        return stat;

                    dc = soGetDirRefClust();

                    *p_outVal = dc->info.ref[ref_Doffset];
                }
            }
            break;
        }
        case ALLOC:
        {
            if (p_inode->i2 == NULL_CLUSTER) {
                if ((stat = soAllocDataCluster(nInode, p_nclust)) != 0)
                    return stat;

                p_inode->i2 = *p_nclust;

                if ((stat = soReadCacheCluster(p_sb->dZoneStart + p_inode->i2 * BLOCKS_PER_CLUSTER, dc)) != 0)
                    return stat;

                uint32_t i;

                for (i = 0; i < RPC; i++) dc->info.ref[i] = NULL_CLUSTER;

                if ((stat = soWriteCacheCluster(p_sb->dZoneStart + p_inode->i2 * BLOCKS_PER_CLUSTER)) != 0)
                    return stat;

                p_inode->cluCount++;
            }

            if ((stat = soLoadSngIndRefClust(p_sb->dZoneStart + p_inode->i2 * BLOCKS_PER_CLUSTER)) != 0)
                return stat;

            dc = soGetSngIndRefClust();

            if (dc->info.ref[ref_Soffset] == NULL_CLUSTER) {
                if ((stat = soAllocDataCluster(nInode, p_nclust)) != 0)
                    return stat;

                dc->info.ref[ref_Soffset] = *p_nclust;

                if ((stat = soReadCacheCluster(p_sb->dZoneStart + dc->info.ref[ref_Soffset] * BLOCKS_PER_CLUSTER, dc)) != 0)
                    return stat;

                uint32_t i;

                for (i = 0; i < RPC; i++) dc->info.ref[i] = NULL_CLUSTER;

                if ((stat = soWriteCacheCluster(p_sb->dZoneStart + dc->info.ref[ref_Soffset] * BLOCKS_PER_CLUSTER)) != 0)
                    return stat;

                p_inode->cluCount++;
            }
            if ((stat = soAllocDataCluster(nInode, p_nclust)) != 0)
                return stat;

            if (dc->info.ref[ref_Doffset] != NULL_CLUSTER) return -EDCARDYIL;

            dc->info.ref[ref_Doffset] = *p_outVal = *p_nclust;

            p_inode->cluCount++;
        }
        case FREE:
        {
            p_outVal = NULL;

            if (p_inode->i2 == NULL_CLUSTER) return -EDCNOTIL;

            if ((stat = soLoadSngIndRefClust(p_sb->dZoneStart + p_inode->i2 * BLOCKS_PER_CLUSTER)) != 0)
                return stat;

            dc = soGetSngIndRefClust();

            if (dc->info.ref[ref_Soffset] == NULL_CLUSTER) return -EDCNOTIL;

            if ((stat = soLoadDirRefClust(p_sb->dZoneStart + dc->info.ref[ref_Soffset] * BLOCKS_PER_CLUSTER)) != 0)
                return stat;

            dc = soGetDirRefClust();

            if (dc->info.ref[ref_Doffset] == NULL_CLUSTER) {
                return -EDCNOTIL;
            } else {
                if ((stat = soFreeDataCluster(dc->info.ref[ref_Doffset])) != 0)
                    return stat;

                p_inode->cluCount--;
            }
            break;
        }
        case FREE_CLEAN:
        {
            p_outVal = NULL;

            if (p_inode->i2 == NULL_CLUSTER) return -EDCNOTIL;

            if ((stat = soLoadSngIndRefClust(p_sb->dZoneStart + p_inode->i2 * BLOCKS_PER_CLUSTER)) != 0)
                return stat;

            dc = soGetSngIndRefClust();

            if (dc->info.ref[ref_Soffset] == NULL_CLUSTER) return -EDCNOTIL;

            if ((stat = soLoadDirRefClust(p_sb->dZoneStart + dc->info.ref[ref_Soffset])) != 0)
                return stat;

            dc = soGetDirRefClust();

            if (dc->info.ref[ref_Doffset] == NULL_CLUSTER) {
                return -EDCNOTIL;
            } else {
                if ((stat = soFreeDataCluster(dc->info.ref[ref_Doffset])) != 0)
                    return stat;
                if ((stat = soCleanDataCluster(nInode, dc->info.ref[ref_Doffset])) != 0)
                    return stat;

                dc->info.ref[ref_Doffset] = NULL_CLUSTER;

                p_inode->cluCount--;
            }
            break;
        }
        case CLEAN:
        {
            p_outVal = NULL;

            if (p_inode->i2 == NULL_CLUSTER) return -EDCNOTIL;

            if ((stat = soLoadSngIndRefClust(p_sb->dZoneStart + p_inode->i2 * BLOCKS_PER_CLUSTER)) != 0)
                return stat;

            dc = soGetSngIndRefClust();

            if (dc->info.ref[ref_Soffset] == NULL_CLUSTER) return -EDCNOTIL;

            if ((stat = soLoadDirRefClust(p_sb->dZoneStart + p_inode->i2 * BLOCKS_PER_CLUSTER)) != 0)
                return stat;

            dc = soGetDirRefClust();

            if (dc->info.ref[ref_Doffset] == NULL_CLUSTER) {
                return -EDCNOTIL;
            } else {
                if ((stat = soCleanDataCluster(nInode, dc->info.ref[ref_Doffset])) != 0)
                    return stat;

                dc->info.ref[ref_Doffset] = NULL_CLUSTER;

                p_inode->cluCount--;

            }
            break;
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

    if ((stat = soReadInode(&p_inode, nInode, IUIN)) != 0)
        return stat;

    if ((stat = soHandleFileCluster(nInode, clustInd - 1, GET, &ind_prev)))
        return 0;

    if ((stat = soHandleFileCluster(nInode, clustInd + 1, GET, &ind_next)))
        return 0;

    if ((stat = soReadCacheCluster(p_sb->dZoneStart + nLClust * BLOCKS_PER_CLUSTER, &dc)) != 0)
        return stat;

    if (ind_prev != NULL_CLUSTER) dc.prev = ind_prev;

    if (ind_next != NULL_CLUSTER) dc.next = ind_next;

    if ((stat = soWriteCacheCluster(p_sb->dZoneStart + nLClust * BLOCKS_PER_CLUSTER, &dc)) != 0)
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

    int stat; // function return status control 
    uint32_t NFClt; // physical number of the cluster
    SODataClust dc; // datacluster to be retrieved, modified and saved

    // read the data cluster, converting it's logical number to physical number
    if ((stat = soReadCacheCluster(p_sb->dZoneStart + nLClust * BLOCKS_PER_CLUSTER, &dc)) != 0)
        return stat;

    // test if the given data cluster belongs to the right inode
    if (dc.stat != nInode) return -EWGINODENB;

    // mark as clean
    dc.stat = CLEAN;

    // save the data cluster
    if ((stat = soWriteCacheCluster(p_sb->dZoneStart + nLClust * BLOCKS_PER_CLUSTER, &dc)) != 0)
        return stat;

    return 0;
}
