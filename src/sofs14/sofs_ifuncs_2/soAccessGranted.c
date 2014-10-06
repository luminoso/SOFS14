/**
 *  \file soAccessGranted.c (implementation file)
 *
 *  \author Guilherme Cardoso - 45726
 */

#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#include "sofs_probe.h"
#include "sofs_buffercache.h"
#include "sofs_superblock.h"
#include "sofs_inode.h"
#include "sofs_basicoper.h"
#include "sofs_basicconsist.h"

/** \brief inode in use status */
#define IUIN  0
/** \brief free inode in dirty state status */
#define FDIN  1

/** \brief performing a read operation */
#define R  0x0004
/** \brief performing a write operation */
#define W  0x0002
/** \brief performing an execute operation */
#define X  0x0001

/* allusion to internal function */

int soReadInode(SOInode *p_inode, uint32_t nInode, uint32_t status);

/**
 *  \brief Check the inode access rights against a given operation.
 *
 *  The inode must to be in use and belong to one of the legal file types.
 *  It checks if the inode mask permissions allow a given operation to be performed.
 *
 *  When the calling process is <em>root</em>, access to reading and/or writing is always allowed and access to
 *  execution is allowed provided that either <em>user</em>, <em>group</em> or <em>other</em> have got execution
 *  permission.
 *
 *  \param nInode number of the inode
 *  \param opRequested operation to be performed:
 *                    a bitwise combination of R, W, and X
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if <em>buffer pointer</em> is \c NULL or no operation of the defined class is described
 *  \return -\c EACCES, if the operation is denied
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c EDCINVAL, if the data cluster header is inconsistent
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soAccessGranted(uint32_t nInode, uint32_t opRequested) {
    soColorProbe(514, "07;31", "soAccessGranted (%"PRIu32", %"PRIu32")\n", nInode, opRequested);

    int stat; // function return status control
    uint32_t nBlk, offset; // inode block position and offset
    SOInode *p_itable; // pointer to inode table for nBlk block position
    SOSuperBlock *p_sb; // pointer to the Super Block
    unsigned int owner, group, other; // owner, group and other permissions

    // check permissions range
    if (opRequested > 7 || opRequested == 0) return -EINVAL;

    // load super block
    if ((stat = soLoadSuperBlock()) != 0)
        return stat;

    // get super block pointer
    p_sb = soGetSuperBlock();

    // check if requested inode is out of range
    if (nInode > p_sb->iTotal)
        return -EINVAL;

    // convert inode number to its block position and offset
    if ((stat = soConvertRefInT(nInode, &nBlk, &offset)) != 0)
        return stat;

    // get pointer to loaded inode table in nBlk block position
    if ((stat = soLoadBlockInT(nBlk)) != 0)
        return 0;

    p_itable = soGetBlockInT();

    //check if inode is in use
    if ((p_itable[offset].mode ^ 0 << 12) == 0) return -EINVAL;

    // check if inode in use is consistent
    if ((stat = soQCheckInodeIU(p_sb, &p_itable[offset])) != 0)
        return stat;

    owner = (p_itable[offset].mode >> 6) & 0x0007;
    group = (p_itable[offset].mode >> 3) & 0x0007;
    other = p_itable[offset].mode & 0x0007;

    if (getuid() == 0) {
        // root permissions
        if ((opRequested & (R | W)) > 0) return 0;
        if ((opRequested & X) > 0) return 0;
        return -EACCES;
    } else if (getuid() == p_itable[offset].owner) {
        if ((opRequested & owner) == opRequested) return 0;
    } else if (getuid() == p_itable[offset].group) {
        if ((opRequested & group) == opRequested) return 0;
    } else {
        if ((opRequested & other) == opRequested) return 0;
    }

    return -EACCES;
}
