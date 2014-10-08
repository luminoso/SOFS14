/**
 *  \file soCleanInode.c (implementation file)
 *
 *  \author Tiago Oliveira - 51687
 */

#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#include "sofs_probe.h"
#include "sofs_superblock.h"
#include "sofs_inode.h"
#include "sofs_basicoper.h"
#include "sofs_basicconsist.h"
/* #define  CLEAN_INODE */
#ifdef CLEAN_INODE
#include "sofs_ifuncs_3.h"
#endif

/** \brief inode in use status */
#define IUIN  0
/** \brief free inode in dirty state status */
#define FDIN  1

/* allusion to internal function */

int soReadInode (SOInode *p_inode, uint32_t nInode, uint32_t status);

/**
 *  \brief Clean an inode.
 *
 *  The inode must be free in the dirty state.
 *  The inode is supposed to be associated to a file, a directory, or a symbolic link which was previously deleted.
 *
 *  This function cleans the list of data cluster references.
 *
 *  Notice that the inode 0, supposed to belong to the file system root directory, can not be cleaned.
 *
 *  \param nInode number of the inode
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the <em>inode number</em> is out of range
 *  \return -\c EFDININVAL, if the free inode in the dirty state is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c EDCINVAL, if the data cluster header is inconsistent
 *  \return -\c EWGINODENB, if the <em>inode number</em> in the data cluster <tt>status</tt> field is different from the
 *                          provided <em>inode number</em> (FREE AND CLEAN / CLEAN)
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soCleanInode (uint32_t nInode)
{
    soColorProbe (513, "07;31", "soCleanInode (%"PRIu32")\n", nInode);

    int stat;               // return var status
    SOSuperBlock *p_sb;     // pointer of type SOSuperBlock
    SOInode *p_inode;       // pointer of type SOInode
    uint32_t nBlk, offset;  // block number and offset of the inside block

    /* any type of previous error on loading/storing the superblock data will disable the operation */
    if ((stat = soLoadSuperBlock()) != 0)
        return stat;
    /* can return ELIBBAD, EBADF, EIO */


    /* get a pointer to the contents of the superblock */
    p_sb = soGetSuperBlock();

    /* if nInode is out of range */
    if (nInode == 0 || nInode >= p_sb->iTotal)
        return -EINVAL;


    /* convert the inode number, which translates to an entry of the inode table,
     * into the logical number and the offset of the block where it is stored
     */
    if ((stat = soConvertRefInT(nInode,&nBlk,&offset))!=0)
        return stat; 

    /* load the contents of a specific block of the table of inodes into internal storage */
    if ((stat = soLoadBlockInT(nBlk)) != 0)
        return stat;

    /* get a pointer to the contents of a specific block of the table of inodes */
    p_inode = soGetBlockInT();

    /* quick check of a free inode in the dirty state */
    if ((stat = soQCheckFDInode(p_sb, &p_inode[offset])) != 0)
        return stat;
    /* can return EFDININVAL, ELDCINIVAL, EDCINVAL */




    return 0;
}
