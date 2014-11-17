/**
 *  \file soAddAttDirEntry.c (implementation file)
 *
 *  \author Guilherme Cardoso, 45726, gjc@ua.pt
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <errno.h>
#include <libgen.h>
#include <string.h>

#include "sofs_probe.h"
#include "sofs_buffercache.h"
#include "sofs_superblock.h"
#include "sofs_inode.h"
#include "sofs_direntry.h"
#include "sofs_basicoper.h"
#include "sofs_basicconsist.h"
#include "sofs_ifuncs_1.h"
#include "sofs_ifuncs_2.h"
#include "sofs_ifuncs_3.h"

/* Allusion to external function */

int soGetDirEntryByName(uint32_t nInodeDir, const char *eName, uint32_t *p_nInodeEnt, uint32_t *p_idx);

/** \brief operation add a generic entry to a directory */
#define ADD         0
/** \brief operation attach an entry to a directory to a directory */
#define ATTACH      1

/**
 *  \brief Add a generic entry / attach an entry to a directory to a directory.
 *
 *  In the first case, a generic entry whose name is <tt>eName</tt> and whose inode number is <tt>nInodeEnt</tt> is added
 *  to the directory associated with the inode whose number is <tt>nInodeDir</tt>. Thus, both inodes must be in use and
 *  belong to a legal type, the former, and to the directory type, the latter.
 *
 *  Whenever the type of the inode associated to the entry to be added is of directory type, the directory is initialized
 *  by setting its contents to represent an empty directory.
 *
 *  In the second case, an entry to a directory whose name is <tt>eName</tt> and whose inode number is <tt>nInodeEnt</tt>
 *  is attached to the directory, the so called <em>base directory</em>, associated to the inode whose number is
 *  <tt>nInodeDir</tt>. The entry to be attached is supposed to represent itself a fully organized directory, the so
 *  called <em>subsidiary directory</em>. Thus, both inodes must be in use and belong to the directory type.
 *
 *  The <tt>eName</tt> must be a <em>base name</em> and not a <em>path</em>, that is, it can not contain the
 *  character '/'. Besides there should not already be any entry in the directory whose <em>name</em> field is
 *  <tt>eName</tt>.
 *
 *  The <em>refcount</em> field of the inode associated to the entry to be added / updated and, when required, of the
 *  inode associated to the directory are updated. This may also happen to the <em>size</em> field of either or both
 *  inodes.
 *
 *  The process that calls the operation must have write (w) and execution (x) permissions on the directory.
 *
 *  \param nInodeDir number of the inode associated to the directory
 *  \param eName pointer to the string holding the name of the entry to be added / attached
 *  \param nInodeEnt number of the inode associated to the entry to be added / attached
 *  \param op type of operation (ADD / ATTACH)
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if any of the <em>inode numbers</em> are out of range or the pointer to the string is \c NULL
 *                      or the name string does not describe a file name or no operation of the defined class is described
 *  \return -\c ENAMETOOLONG, if the name string exceeds the maximum allowed length
 *  \return -\c ENOTDIR, if the inode type whose number is <tt>nInodeDir</tt> (ADD), or both the inode types (ATTACH),
 *                       are not directories
 *  \return -\c EEXIST, if an entry with the <tt>eName</tt> already exists
 *  \return -\c EACCES, if the process that calls the operation has not execution permission on the directory where the
 *                      entry is to be added / attached
 *  \return -\c EPERM, if the process that calls the operation has not write permission on the directory where the entry
 *                     is to be added / attached
 *  \return -\c EMLINK, if the maximum number of hardlinks in either one of inodes has already been attained
 *  \return -\c EFBIG, if the directory where the entry is to be added / attached, has already grown to its maximum size
 *  \return -\c ENOSPC, if there are no free data clusters
 *  \return -\c EDIRINVAL, if the directory is inconsistent
 *  \return -\c EDEINVAL, if the directory entry is inconsistent
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c EDCMINVAL, if the mapping association of the data cluster is invalid
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soAddAttDirEntry(uint32_t nInodeDir, const char *eName, uint32_t nInodeEnt, uint32_t op) {
    soColorProbe(313, "07;31", "soAddAttDirEntry (%"PRIu32", \"%s\", %"PRIu32", %"PRIu32")\n", nInodeDir,
            eName, nInodeEnt, op);

    SOSuperBlock *p_sb; // pointer to superblock
    uint32_t stat; // function return status control
    SOInode inodeDir, inodeEnt; // inodes to be filled for current directory
    char *c; // pointer to a char, return of strchr function
    uint32_t dirIdx, clusterIdx; // directory index position and cluster index position
    uint32_t nLClust; // logical cluster number
    SODataClust dcDir; // insertion directory data cluster
    unsigned int i; // counting variable
    SODataClust dcEnt; // entry directory data cluster

    if ((stat = soLoadSuperBlock()) != 0)
        return stat;

    p_sb = soGetSuperBlock();

    if (nInodeDir > p_sb->iTotal) return -EINVAL;

    if (eName == NULL) return -EINVAL;

    if (strlen(eName) > MAX_NAME) return -ENAMETOOLONG;

    if (op > 2) return -EINVAL;

    c = strchr(eName, '/');

    if (c != NULL) return -EINVAL;

    if ((stat = soReadInode(&inodeDir, nInodeDir, IUIN)) != 0)
        return stat;

    if ((stat = soAccessGranted(nInodeDir, (W))) != 0)
        return stat;

    if ((stat = soReadInode(&inodeEnt, nInodeEnt, IUIN)) != 0)
        return stat;

    if ((stat = soAccessGranted(nInodeEnt, (R))) != 0)
        return stat;

    // check if inode dir is a directory
    if ((inodeDir.mode & INODE_DIR) == 0) return -ENOTDIR;

    // if operation is add, inode entry myst be a legal type and if it attach must be a directory
    if ((op == ADD) && ((inodeEnt.mode & INODE_TYPE_MASK) == 0))
        return -EINVAL;
    else if ((op == ATTACH) && ((inodeEnt.mode & INODE_DIR) == 0))
        return -ENOTDIR;

    /* END OF VALIDATIONS */

    // check for next free directory position. must not match any existing eName already inserted
    if ((stat = soGetDirEntryByName(nInodeDir, eName, NULL, &dirIdx)) == 0)
        return -EEXIST;
    else if (stat != -ENOENT)
        return stat;

    // calculate cluster position
    clusterIdx = dirIdx / DPC;

    // get the right cluster
    if ((stat = soHandleFileCluster(nInodeDir, clusterIdx, GET, &nLClust)) != 0)
        return 0;

    // if retrieved cluster position is null, allocate a new one and format it accordingly 
    if (nLClust == NULL_CLUSTER) {
        if ((stat = soHandleFileCluster(nInodeDir, clusterIdx, ALLOC, &nLClust)) != 0)
            return stat;

        if ((stat = soReadFileCluster(nInodeDir, clusterIdx, &dcDir)) != 0)
            return stat;

        for (i = 0; i < DPC; i++) {
            memset(dcDir.info.de[i].name, '\0', MAX_NAME + 1);
            dcDir.info.de[i].nInode = NULL_INODE;
        }

        if ((stat = soReadInode(&inodeDir, nInodeDir, IUIN)) != 0)
            return stat;

        inodeDir.size += sizeof (dcDir.info.de);

    } else {
        if ((stat = soReadFileCluster(nInodeDir, clusterIdx, &dcDir)) != 0)
            return stat;
    }

    // fill directory name and directory inode information
    memcpy(dcDir.info.de[dirIdx % DPC].name, eName, strlen(eName));
    dcDir.info.de[dirIdx % DPC].nInode = nInodeEnt;

    switch (op) {
        case ADD:
        {
            // if we're adding a directory, allocate a cluster and format it
            if ((inodeEnt.mode & INODE_DIR) == INODE_DIR) {

                if ((stat = soHandleFileCluster(nInodeEnt, 0, ALLOC, &nLClust)) != 0)
                    return stat;

                if ((stat = soReadFileCluster(nInodeEnt, 0, &dcEnt)) != 0)
                    return stat;

                int i;

                for (i = 0; i < DPC; i++) {
                    memset(dcEnt.info.de[i].name, '\0', MAX_NAME + 1);
                    dcEnt.info.de[i].nInode = NULL_INODE;
                }

                dcEnt.info.de[0].name[0] = '.';
                dcEnt.info.de[1].name[0] = '.';
                dcEnt.info.de[1].name[1] = '.';

                dcEnt.info.de[0].nInode = nInodeEnt;
                dcEnt.info.de[1].nInode = nInodeDir;

                if ((stat = soWriteFileCluster(nInodeEnt, 0, &dcEnt)) != 0)
                    return stat;

                if ((stat = soReadInode(&inodeEnt, nInodeEnt, IUIN)) != 0)
                    return stat;

                // refcount is one of "." of added directory and one from the directory itself
                inodeEnt.refCount += 2;
                inodeEnt.size += sizeof (dcEnt.info.de);

                if ((stat = soWriteInode(&inodeEnt, nInodeEnt, IUIN)) != 0)
                    return stat;

                inodeDir.refCount += 1;

                dcDir.info.de[dirIdx % DPC].nInode = nInodeEnt;

            } else {
                if ((stat = soReadInode(&inodeEnt, nInodeEnt, IUIN)) != 0)
                    return stat;

                // if it is not a directory, refcount increases one value
                inodeEnt.refCount += 1;

                if ((stat = soWriteInode(&inodeEnt, nInodeEnt, IUIN)) != 0)
                    return stat;

            }
            break;
        }
        case ATTACH:
        {
            if ((stat = soReadFileCluster(nInodeEnt, 0, &dcEnt)) != 0)
                return stat;

            // set correct ".." inode
            dcEnt.info.de[1].nInode = nInodeDir;

            if ((stat = soWriteFileCluster(nInodeEnt, 0, &dcEnt)) != 0)
                return stat;

            if ((stat = soReadInode(&inodeEnt, nInodeEnt, IUIN)) != 0)
                return stat;

            // attach is only for directories, therefore refcount increases by 2
            inodeEnt.refCount += 2;

            if ((stat = soWriteInode(&inodeEnt, nInodeEnt, IUIN)) != 0)
                return stat;

            // ".." reference of inserted directory
            inodeDir.refCount += 1;

            break;
        }
        default:
            return -EINVAL;
    }

    if ((stat = soWriteFileCluster(nInodeDir, clusterIdx, &dcDir)) != 0)
        return stat;

    if ((stat = soWriteInode(&inodeDir, nInodeDir, IUIN)) != 0)
        return stat;

    return 0;
}
