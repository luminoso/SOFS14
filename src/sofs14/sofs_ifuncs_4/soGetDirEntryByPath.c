/**
 *  \file soGetDirEntryByPath.c (implementation file)
 *
 *  \author Tiago Oliveira 51687
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <errno.h>
#include <libgen.h>
#include <string.h>
#include <unistd.h>

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

//#include "syscall.h"
/* Allusion to external function */

int soGetDirEntryByName(uint32_t nInodeDir, const char *eName, uint32_t *p_nInodeEnt, uint32_t *p_idx);

/* Allusion to internal function */

int soTraversePath(const char *ePath, uint32_t *p_nInodeDir, uint32_t *p_nInodeEnt);

/** \brief Number of symbolic links in the path */

static uint32_t nSymLinks = 0;

/** \brief Old directory inode number */

static uint32_t oldNInodeDir = 0;

/**
 *  \brief Get an entry by path.
 *
 *  The directory hierarchy of the file system is traversed to find an entry whose name is the rightmost component of
 *  <tt>ePath</tt>. The path is supposed to be absolute and each component of <tt>ePath</tt>, with the exception of the
 *  rightmost one, should be a directory name or symbolic link name to a path.
 *
 *  The process that calls the operation must have execution (x) permission on all the components of the path with
 *  exception of the rightmost one.
 *
 *  \param ePath pointer to the string holding the name of the path
 *  \param p_nInodeDir pointer to the location where the number of the inode associated to the directory that holds the
 *                     entry is to be stored
 *                     (nothing is stored if \c NULL)
 *  \param p_nInodeEnt pointer to the location where the number of the inode associated to the entry is to be stored
 *                     (nothing is stored if \c NULL)
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the pointer to the string is \c NULL
 *  \return -\c ENAMETOOLONG, if the path or any of the path components exceed the maximum allowed length
 *  \return -\c ERELPATH, if the path is relative and it is not a symbolic link
 *  \return -\c ENOTDIR, if any of the components of <tt>ePath</tt>, but the last one, is not a directory
 *  \return -\c ELOOP, if the path resolves to more than one symbolic link
 *  \return -\c ENOENT,  if no entry with a name equal to any of the components of <tt>ePath</tt> is found
 *  \return -\c EACCES, if the process that calls the operation has not execution permission on any of the components
 *                      of <tt>ePath</tt>, but the last one
 *  \return -\c EDIRINVAL, if the directory is inconsistent
 *  \return -\c EDEINVAL, if the directory entry is inconsistent
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soGetDirEntryByPath(const char *ePath, uint32_t *p_nInodeDir, uint32_t *p_nInodeEnt) {
    soColorProbe(311, "07;31", "soGetDirEntryByPath (\"%s\", %p, %p)\n", ePath, p_nInodeDir, p_nInodeDir);

    int stat;

    // SIMPLE VALIDATIONS
    if (ePath == NULL) // ePath cannot be NULL
        return -EINVAL;

    if (strlen(ePath) > MAX_PATH) // ePath cannot be greater than MAX_PATH error 36
        return -ENAMETOOLONG; // TODO MAX_NAME -> component of the path 

    if (ePath[0] != '/')
        return -ERELPATH;
    // END SIMPLE VALIDATIONS

    if ((stat = soTraversePath(ePath, p_nInodeDir, p_nInodeEnt)) != 0)
        return stat;

    return 0;
}

/**
 *  \brief Traverse the path.
 *
 *  \param ePath pointer to the string holding the name of the path
 *  \param p_nInodeDir pointer to the location where the number of the inode associated to the directory that holds the
 *                     entry is to be stored
 *  \param p_nInodeEnt pointer to the location where the number of the inode associated to the entry is to be stored
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c ENAMETOOLONG, if any of the path components exceed the maximum allowed length
 *  \return -\c ERELPATH, if the path is relative and it is not a symbolic link
 *  \return -\c ENOTDIR, if any of the components of <tt>ePath</tt>, but the last one, is not a directory
 *  \return -\c ELOOP, if the path resolves to more than one symbolic link
 *  \return -\c ENOENT,  if no entry with a name equal to any of the components of <tt>ePath</tt> is found
 *  \return -\c EACCES, if the process that calls the operation has not execution permission on any of the components
 *                      of <tt>ePath</tt>, but the last one
 *  \return -\c EDIRINVAL, if the directory is inconsistent
 *  \return -\c EDEINVAL, if the directory entry is inconsistent
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soTraversePath(const char *ePath, uint32_t *p_nInodeDir, uint32_t *p_nInodeEnt) {

    char pathArr[MAX_PATH + 1];
    char *path;
    char nameArr[MAX_PATH + 1];
    char *name;
    uint32_t entry;
    int stat;
    SOInode inode;
    SODataClust dc;

    strcpy(pathArr, ePath);
    path = dirname(pathArr);
    strcpy(nameArr, ePath);
    name = basename(nameArr);

    //printf("1 dirname: %s\n", path);
    //printf("1 basename: %s\n", name);

    if (strcmp(path, ".") == 0) { //condicao de paragem para atalhos
        if (nSymLinks) {
            *p_nInodeEnt = oldNInodeDir;
            nSymLinks--;
            if ((stat = soGetDirEntryByName(*p_nInodeEnt, name, p_nInodeEnt, NULL)) != 0) {
                return stat;
            }
        }
        return 0;
    }

    //printf("MyPath: %s\n", path);

    if (strcmp(name, "/") == 0) {
        name[0] = '.'; // semantic problem FIX
        name[1] = '\0';
    }

    if (strlen(name) > MAX_NAME) // name component cannot be greater than 59 MAX_NAME
        return -ENAMETOOLONG;

    if ((strcmp(path, "/") == 0) && (strcmp(name, ".") == 0)) {

        if ((stat = soGetDirEntryByName(0, name, &entry, NULL)) != 0) { // duvida, que eu sei que barra"/" é sempre zero
            return stat;
        }
        *p_nInodeDir = *p_nInodeEnt = entry;
        return 0;

    } else {

        //  if ((stat = soReadInode(&inode, *p_nInodeEnt, IUIN)) != 0) // nao pode ficar aqui
        //      return stat;

        if ((stat = soTraversePath(path, p_nInodeDir, p_nInodeEnt)) != 0)
            return stat;

        *p_nInodeDir = *p_nInodeEnt;

        if ((stat = soAccessGranted(*p_nInodeEnt, 1)) != 0) // 1 = X execute
            return stat;

        if ((stat = soGetDirEntryByName(*p_nInodeEnt, name, &entry, NULL)) != 0) {
            return stat;
        }

        *p_nInodeEnt = entry; // update p_nInodeEnt 

        if ((stat = soReadInode(&inode, *p_nInodeEnt, IUIN)) != 0) // duvida se fica aqui 2x
            return stat;

        if (inode.mode & INODE_SYMLINK) {


            //printf("SymLink: %s\n", name);
            char save[MAX_PATH + 1];
            if ((stat = soReadFileCluster(*p_nInodeEnt, 0, &dc)) != 0) //  0 -> clusterNumber,symlinks fits in clust 0
                return stat;

            if (dc.info.de[0].name[0] != '/') { // se nao comecar por barra, nao e caminho absoluto

                //              printf("Not an absolut path\n");
                strcpy(save, (char*) dc.info.de[0].name);

                nSymLinks++;
                oldNInodeDir = *p_nInodeDir;
                if ((stat = soTraversePath(save, p_nInodeDir, p_nInodeEnt)) != 0)
                    return stat;


            } else {
                //                printf("Absolut path\n");
                strcpy(save, (char*) dc.info.de[0].name);
                if ((stat = soTraversePath(save, p_nInodeDir, p_nInodeEnt)) != 0)
                    return stat;

            }

        }

    }

    return 0;
}
