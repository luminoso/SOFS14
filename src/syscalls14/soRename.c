/**
 *  \file soRename.c (implementation file)
 *
 *  \author Guilherme Cardoso, 45726, gjc@ua.pt
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <time.h>
#include <utime.h>
#include <libgen.h>
#include <string.h>

#include "sofs_probe.h"
#include "sofs_const.h"
#include "sofs_rawdisk.h"
#include "sofs_buffercache.h"
#include "sofs_superblock.h"
#include "sofs_inode.h"
#include "sofs_direntry.h"
#include "sofs_datacluster.h"
#include "sofs_basicoper.h"
#include "sofs_basicconsist.h"
#include "sofs_ifuncs_1.h"
#include "sofs_ifuncs_2.h"
#include "sofs_ifuncs_3.h"
#include "sofs_ifuncs_4.h"
#include "sofs_syscalls.h"

/**
 *  \brief Change the name or the location of a file in the directory hierarchy of the file system.
 *
 *  It tries to emulate <em>rename</em> system call.
 *
 *  \param oldPath path to an existing file
 *  \param newPath new path to the same file in replacement of the old one
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the pointer to either of the strings is \c NULL or any of the path strings is a \c NULL string
 *                      or any of the paths do not describe absolute paths or <tt>oldPath</tt> describes a directory
 *                      and is a substring of <tt>newPath</tt> (attempt to make a directory a subdirectory of itself)
 *  \return -\c ENAMETOOLONG, if the paths names or any of their components exceed the maximum allowed length
 *  \return -\c ENOTDIR, if any of the components of both paths, but the last one, are not directories, or
 *                       <tt>oldPath</tt> describes a directory and <tt>newPath</tt>, although it exists, does not
 *  \return -\c EISDIR, if <tt>newPath</tt> describes a directory and <tt>oldPath</tt> does not
 *  \return -\c ELOOP, if either path resolves to more than one symbolic link
 *  \return -\c EMLINK, if <tt>oldPath</tt> is a directory and the directory containing <tt>newPath</tt> has already
 *                      the maximum number of links, or <tt>oldPath</tt> has already the maximum number of links and
 *                      is not contained in the same directory that will contain <tt>newPath</tt>
 *  \return -\c ENOENT, if no entry with a name equal to any of the components of <tt>oldPath</tt>, or to any of the
 *                      components of <tt>newPath</tt>, but the last one, is found
 *  \return -\c ENOTEMPTY, if both <tt>oldPath</tt> and <tt>newPath</tt> describe directories and <tt>newPath</tt> is
 *                         not empty
 *  \return -\c EACCES, if the process that calls the operation has not execution permission on any of the components
 *                      of both paths, but the last one
 *  \return -\c EPERM, if the process that calls the operation has not write permission on the directories where
 *                     <tt>newPath</tt> entry is to be added and <tt>oldPath</tt> is to be detached
 *  \return -\c ENOSPC, if there are no free data clusters
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails on writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soRename(const char *oldPath, const char *newPath) {
    soColorProbe(227, "07;31", "soRename (\"%s\", \"%s\")\n", oldPath, newPath);

    int stat;
    uint32_t nInodeOld_dir, nInodeOld_ent; // oldPath inode numbers for directory and entry
    uint32_t nInodeNew_dir, nInodeNew_ent; // newPath inode numbers for directory and entry
    uint32_t nnewPathEinode; // if the newPath exists, what is the inode number for Existing entry
    uint32_t newPathEinodeIdx; // if the newPath exists, what is the index position for Existing entry
    SOInode oldPathinode, newPathEinode; // oldPath inode and if newPath entry exits its inode
    
    // new strings to avoid compiler warnings
    char oldPathStr[MAX_PATH + 1]; // oldPath copy
    char newPathStr[MAX_PATH + 1]; // newPath copy
    char oldPathStrB[MAX_NAME + 1]; // basename of oldPath copy
    char newPathStrB[MAX_NAME + 1]; // basename of newPath copy
    //char newPathStr2[MAX_PATH + 1]; // str to compare if oldpath is a substring of newpath

    if (oldPath == NULL || newPath == NULL) return -EINVAL;

    if ((strlen(oldPath) == 0) || (strlen(newPath) == 0)) return -EINVAL; // empty strings

    if (strlen(oldPath) > MAX_PATH) return -ENAMETOOLONG;

    if (strlen(newPath) > MAX_PATH) return -ENAMETOOLONG;

    // avoid compiler warning "passing argument 1 of ‘__xpg_basename’ discards ‘const’ qualifier from pointer target type" for basename
    // (what if at this point basename of arguments is longer than MAX_NAME+1 ?)
    strcpy(oldPathStr, oldPath);
    strcpy(oldPathStrB, basename(oldPathStr));
    strcpy(newPathStr, newPath);
    strcpy(newPathStrB, basename(newPathStr));

    // END OF VALIDATIONS

    /* Four operations:
     * 1 - rename
     * 2 - rename and remove directory 
     * 3 - move
     * 4 - rename and remove directory
     */

    // Read function parameters and process them

    if ((stat = soGetDirEntryByPath(oldPath, &nInodeOld_dir, &nInodeOld_ent)) != 0)
        return stat;

    if ((stat = soGetDirEntryByPath(newPath, &nInodeNew_dir, &nInodeNew_ent)) != 0)
        return stat;

    if ((stat = soReadInode(&oldPathinode, nInodeOld_ent, IUIN)) != 0)
        return stat;

    // check if newPath is an existing entry
    if ((stat = soGetDirEntryByName(nInodeNew_ent, newPathStrB, &nnewPathEinode, &newPathEinodeIdx)) == 0) {
        // it is
        if ((stat = soReadInode(&newPathEinode, nInodeNew_ent, IUIN)) != 0)
            return stat;

        if ((oldPathinode.mode & INODE_DIR) == INODE_DIR) {
            // if rename destiny exists and it's not a directory there's no possible solution
            if ((newPathEinode.mode & INODE_DIR) != INODE_DIR) return -ENOTDIR;

            //if (strcmp(oldPathStr, strncpy(newPathStr2, newPathStr, strlen(newPathStr) - strlen(basename(newPathStrB)))) == 0) return -EINVAL;
            if (strstr(newPath, oldPath) != NULL) return -EINVAL; // can't move an directory to it self sub directory

            if ((stat = soCheckDirectoryEmptiness(nInodeNew_ent)) != 0)
                return stat;
        }
        
        if ((stat = soRenameDirEntry(nInodeOld_dir,oldPathStrB,newPathStrB)) != 0)
            return stat;
        
        if ((stat = soRemDetachDirEntry(nInodeNew_ent, newPathStrB, REM)) != 0)
                return stat;

    } else if (stat != -ENOENT) return stat;

    // it does not exist, check if we're just renaming or moving
    if (nInodeOld_dir == nInodeNew_dir) {
        // operation 1: renaming
        if ((stat = soRenameDirEntry(nInodeOld_dir, oldPathStrB, newPathStrB)) != 0)
            return stat;

    } else { // operation 3 or 4
        // operation 4: move and remove directory entry
        if ((oldPathinode.mode & INODE_DIR) == INODE_DIR) {
            if ((stat = soAddAttDirEntry(nInodeNew_ent, newPathStrB, nInodeOld_ent, ATTACH)) != 0)
                return stat;

            // if success, detach from old location. detach because directory may not be empty
            if ((stat = soRemDetachDirEntry(nInodeOld_dir, oldPathStrB, DETACH)) != 0)
                return stat;

        } else { // operation 3: move
            if ((stat = soAddAttDirEntry(nInodeNew_ent, newPathStrB, nInodeOld_ent, ADD)) != 0)
                return stat;

            if ((stat = soRemDetachDirEntry(nInodeOld_dir, oldPathStrB, REM)) != 0)
                return stat;
        }

    }

    return 0;
}
