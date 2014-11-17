/**
 *  \file soRemDetachDirEntry.c (implementation file)
 *
 *  \author Bruno Silva
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

/* Allusion to external functions */

int soGetDirEntryByName (uint32_t nInodeDir, const char *eName, uint32_t *p_nInodeEnt, uint32_t *p_idx);
int soCheckDirectoryEmptiness (uint32_t nInodeDir);

/** \brief operation remove a generic entry from a directory */
#define REM         0
/** \brief operation detach a generic entry from a directory */
#define DETACH      1

/**
 *  \brief Remove / detach a generic entry from a directory.
 *
 *  The entry whose name is <tt>eName</tt> is removed / detached from the directory associated with the inode whose
 *  number is <tt>nInodeDir</tt>. Thus, the inode must be in use and belong to the directory type.
 *
 *  Removal of a directory entry means exchanging the first and the last characters of the field <em>name</em>.
 *  Detachment of a directory entry means filling all the characters of the field <em>name</em> with the \c NULL
 *  character and making the field <em>nInode</em> equal to \c NULL_INODE.
 *
 *  The <tt>eName</tt> must be a <em>base name</em> and not a <em>path</em>, that is, it can not contain the
 *  character '/'. Besides there should exist an entry in the directory whose <em>name</em> field is <tt>eName</tt>.
 *
 *  Whenever the operation is removal and the type of the inode associated to the entry to be removed is of directory
 *  type, the operation can only be carried out if the directory is empty.
 *
 *  The <em>refcount</em> field of the inode associated to the entry to be removed / detached and, when required, of
 *  the inode associated to the directory are updated.
 *
 *  The file described by the inode associated to the entry to be removed / detached is only deleted from the file
 *  system if the <em>refcount</em> field becomes zero (there are no more hard links associated to it) and the operation
 *  is removal. In this case, the data clusters that store the file contents and the inode itself must be freed.
 *
 *  The process that calls the operation must have write (w) and execution (x) permissions on the directory.
 *
 *  \param nInodeDir number of the inode associated to the directory
 *  \param eName pointer to the string holding the name of the directory entry to be removed / detached
 *  \param op type of operation (REM / DETACH)
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the <em>inode number</em> is out of range or the pointer to the string is \c NULL or the
 *                      name string does not describe a file name or no operation of the defined class is described
 *  \return -\c ENAMETOOLONG, if the name string exceeds the maximum allowed length
 *  \return -\c ENOTDIR, if the inode type whose number is <tt>nInodeDir</tt> is not a directory
 *  \return -\c ENOENT,  if no entry with <tt>eName</tt> is found
 *  \return -\c EACCES, if the process that calls the operation has not execution permission on the directory
 *  \return -\c EPERM, if the process that calls the operation has not write permission on the directory
 *  \return -\c ENOTEMPTY, if the entry with <tt>eName</tt> describes a non-empty directory
 *  \return -\c EDIRINVAL, if the directory is inconsistent
 *  \return -\c EDEINVAL, if the directory entry is inconsistent
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soRemDetachDirEntry (uint32_t nInodeDir, const char *eName, uint32_t op)
{
    soColorProbe (314, "07;31", "soRemDetachDirEntry (%"PRIu32", \"%s\", %"PRIu32")\n", nInodeDir, eName, op);
    
    SOInode inodeEnt, inodeDir; // inode given and indode of the entry
    SODataClust dirEnt; //Data cluster of the entry
    uint32_t nInodeEnt, idx; //auxiliary variables
    int stat, i; //error variable and iterable variable 
   
    if((stat = soReadInode(&inodeDir, nInodeDir, IUIN)) != 0)      
        return stat;

    if((inodeDir.mode & INODE_DIR) != INODE_DIR)
        return -ENOTDIR;

    if((stat = soAccessGranted(nInodeDir, X)) != 0)
        return stat;

    if((stat = soAccessGranted(nInodeDir, W)) != 0)
        return -EPERM;

    if(eName == NULL)
        return -EINVAL;
   
    if(strlen(eName) == 0)
        return -EINVAL;

    if(strlen(eName) > MAX_NAME)
        return -ENAMETOOLONG;
   
    if(strchr(eName, '/') != 0)  
        return -EINVAL;

    if(op > 1)
        return -EINVAL;
   
    if((stat = soGetDirEntryByName(nInodeDir, eName, &nInodeEnt, &idx)) != 0)
        return stat;
   
    if((stat=soReadInode(&inodeEnt, nInodeEnt, IUIN)) !=0)      
        return stat;

    if(op == REM){
        //check if it's a directory and if it's empty
        if((inodeEnt.mode & INODE_DIR) == INODE_DIR){
            if((stat = soCheckDirectoryEmptiness(nInodeEnt)) != 0)
                return stat;
            //remove reference to him self
            inodeEnt.refCount--;
            //remove reference ..
            inodeDir.refCount--;
        }     
        
        if((stat=soReadFileCluster(nInodeDir, (idx/DPC), &dirEnt)) != 0)
                return stat;

        //switch the last characters
        dirEnt.info.de[idx%DPC].name[MAX_NAME] = dirEnt.info.de[idx%DPC].name[0];
        dirEnt.info.de[idx%DPC].name[0]='\0';

        //remove reference from inodeDir
        inodeEnt.refCount--;
        
           
        if((stat=soWriteFileCluster(nInodeDir, (idx/DPC), &dirEnt)) != 0)
                return stat;
        
        //if refcount == 0 we have to free the clusters and the inodes
        if(inodeEnt.refCount == 0){  
            //free all clusters of nInodeEnt
            if((stat=soHandleFileClusters(nInodeEnt, 0, FREE)) != 0)
                    return stat;
           
            if((stat=soWriteInode(&inodeEnt, nInodeEnt, IUIN)) != 0)
                    return stat;
            
            //free the inode it self
            if((stat=soFreeInode(nInodeEnt)) != 0)
                    return stat;
                   
            if((stat=soWriteInode(&inodeDir, nInodeDir, IUIN)) != 0)
                    return stat;
        }
        else{ // if not let's just write the inodes back
            if((stat=soWriteInode(&inodeEnt, nInodeEnt, IUIN)) != 0)
                    return stat;

            if((stat=soWriteInode(&inodeDir, nInodeDir, IUIN)) != 0)
                    return stat;
        }
    }
    else if(op == DETACH){
        if((inodeEnt.mode & INODE_DIR) == INODE_DIR){
            //remove reference to him self
            inodeEnt.refCount--;
            //remove reference ..
            inodeDir.refCount--;
        }

        if((stat=soReadFileCluster(nInodeDir, (idx/DPC), &dirEnt)) != 0)
                return stat;
        
        //remove reference from inodeDir
        inodeEnt.refCount--;

        //filling all the characters with the null character
        for(i = 0; i < MAX_NAME; i++){
            dirEnt.info.de[idx % DPC].name[i] = '\0';
        }
        //filling nInode field with NULL_INODE
        dirEnt.info.de[idx % DPC].nInode = NULL_INODE;

        //write cluster associated with the eName entry
        if((stat = soWriteFileCluster(nInodeDir, (idx / DPC), &dirEnt)) != 0)
            return stat;

        if((stat=soWriteInode(&inodeEnt, nInodeEnt, IUIN)) != 0)
                    return stat;

        if((stat=soWriteInode(&inodeDir, nInodeDir, IUIN)) != 0)
            return stat;
    }
   
    

    return 0;
}
