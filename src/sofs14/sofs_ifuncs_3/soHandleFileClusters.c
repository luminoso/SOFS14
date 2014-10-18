/**
 *  \file soHandleFileClusters.c (implementation file)
 *
 *  \author Catia Valente(60155)
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
 *  \brief Handle all data clusters from the list of references starting at a given point.
 *
 *  The file (a regular file, a directory or a symlink) is described by the inode it is associated to.
 *
 *  Several operations are available and can be applied to the file data clusters starting from the index to the list of
 *  direct references which is given.
 *
 *  The list of valid operations is
 *
 *    \li FREE:       free all data clusters starting from the referenced data cluster
 *    \li FREE_CLEAN: free all data clusters starting from the referenced data cluster and dissociate them from the
 *                    inode which describes the file
 *    \li CLEAN:      dissociate all data clusters starting from the referenced data cluster from the inode which
 *                    describes the file.
 *
 *  Depending on the operation, the field <em>clucount</em> and the lists of direct references, single indirect
 *  references and double indirect references to data clusters of the inode associated to the file are updated.
 *
 *  Thus, the inode must be in use and belong to one of the legal file types for the operations FREE and FREE_CLEAN and
 *  must be free in the dirty state for the operation CLEAN.
 *
 *  \param nInode number of the inode associated to the file
 *  \param clustIndIn index to the list of direct references belonging to the inode which is referred (it contains the
 *                    index of the first data cluster to be processed)
 *  \param op operation to be performed (FREE, FREE AND CLEAN, CLEAN)
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the <em>inode number</em> or the <em>index to the list of direct references</em> are out of
 *                      range or the requested operation is invalid
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
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

int soHandleFileClusters (uint32_t nInode, uint32_t clustIndIn, uint32_t op)
{
  soColorProbe (414, "07;31", "soHandleFileClusters (%"PRIu32", %"PRIu32", %"PRIu32")\n", nInode, clustIndIn, op);

 SOSuperBlock* p_sb; //carrega o super block para um ponteiro
    SOInode inode; //inode onde se vai operar
    SODataClust *clusti2, *clusti1; //data cluster as a node
    int stat; //retorno das validações
    int index, line, column; //variaveis de incremento dos ciclos
    int status; //estado do inode (dirty state or use state)

    /* carregar super block para um ponteiro */
    if ((stat = soLoadSuperBlock()) != 0) return stat;
    p_sb = soGetSuperBlock();

    /*Validação de Conformidade*/
    if ((nInode < 0) || (nInode >= p_sb->iTotal)) return -EINVAL; /* verificar se o índice do i-node é válido */

    if (clustIndIn >= MAX_FILE_CLUSTERS) return -EINVAL; /* verificar se o índice do cluster é válido */

    if (op < 2 || op > 4) return -EINVAL; /* verificar se a operação é válida: FREE(2), FREE_CLEAN(3) e CLEAN(4) */

    /* lê o inode */
    if (op == CLEAN)// FDIN = free inode in dirty state
    {
        if ((stat = soReadInode(&inode, nInode, FDIN)) != 0) return stat;
        status = FDIN;
    } else //IUIN = inode in use state 
    {
        if ((stat = soReadInode(&inode, nInode, IUIN)) != 0) return stat;
        status = IUIN;
        if ((inode.mode & INODE_TYPE_MASK) == 0) return -EIUININVAL; //if the inode in use is inconsistent
    }


    /*Referencias duplamente indirectas*/

    if (inode.i2 != NULL_CLUSTER)
    {

        if ((stat = soLoadSngIndRefClust((inode.i2 * BLOCKS_PER_CLUSTER) + p_sb->dZoneStart)) != 0) return stat; 

        clusti2 = soGetSngIndRefClust(); //pointer to the contents of a specific cluster of the table of single indirect references

        index = N_DIRECT + RPC; //tamanho da tabela das referencias simplesmentre indirectas

        while (inode.i2 != NULL_CLUSTER && index < MAX_FILE_CLUSTERS)
        {
            line = (index - (RPC + N_DIRECT)) / RPC;
            if (clusti2->info.ref[line] != NULL_CLUSTER) 
            {
                if ((stat = soLoadDirRefClust((clusti2->info.ref[line] * BLOCKS_PER_CLUSTER) + p_sb->dZoneStart)) != 0) return stat;//????

                clusti1 = soGetDirRefClust(); //pointer to the contents of a specific cluster of the table of direct references

                for (column = ((index - (RPC + N_DIRECT)) % RPC); column < RPC; column++, index++)
                {
                    if (clusti1->info.ref[column] != NULL_CLUSTER && clustIndIn <= index) 
                    {
                        if ((stat = soHandleFileCluster(nInode, index, op, NULL)) != 0) return stat;
                        if ((stat = soReadInode(&inode, nInode, status)) != 0) return stat;
                    }
                }
            } 
            else
                index += RPC;

        }
    }

    /*Referencias simplesmente indirectas*/
    if (inode.i1 != NULL_CLUSTER) //reference to the data cluster that holds the next group of direct references
    {
        if ((stat = soLoadDirRefClust((inode.i1 * BLOCKS_PER_CLUSTER) + p_sb->dZoneStart)) != 0) return stat;

        clusti1 = soGetDirRefClust(); //pointer to the contents of a specific cluster of the table of direct references

        index = N_DIRECT;

        while (inode.i1 != NULL_CLUSTER && index < N_DIRECT + RPC)//percorrer as referencias simplesmente indirectas
        {

            line = index - N_DIRECT; 

            if (clusti1->info.ref[line] != NULL_CLUSTER && clustIndIn <= index) /* verificar se esta referência é para um cluster válido */ {
                if ((stat = soHandleFileCluster(nInode, index, op, NULL)) != 0) return stat; /* executar a operação de HANDLE pretendida neste cluster */
                if ((stat = soReadInode(&inode, nInode, status)) != 0) return stat; //leitura do inode
            }
            index++;
        }
    }

    /*Referencias Directas*/
    for (index = 0; index < N_DIRECT; index++)/* percorrer as referencias directas */ 
    {
        if (inode.d[index] != NULL_CLUSTER && clustIndIn <= index)/* verificar se esta referência é para um cluster válido */
            if ((stat = soHandleFileCluster(nInode, index, op, NULL)) != 0) return stat; /* executar a operação de HANDLE pretendida neste cluster */
    }

    return 0;
}
