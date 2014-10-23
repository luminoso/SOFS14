/**
 *  \file soAllocInode.c (implementation file)
 *
 *  \author  Gabriel Vieira - 68021
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
#include "sofs_datacluster.h"
#include "sofs_basicoper.h"
#include "sofs_basicconsist.h"
#define  CLEAN_INODE
#ifdef CLEAN_INODE
#include "sofs_ifuncs_2.h"
#endif

/**
 *  \brief Allocate a free inode.
 *
 *  The inode is retrieved from the list of free inodes, marked in use, associated to the legal file type passed as
 *  a parameter and generally initialized. It must be free and if is free in the dirty state, it has to be cleaned
 *  first.
 *
 *  Upon initialization, the new inode has:
 *     \li the field mode set to the given type, while the free flag and the permissions are reset
 *     \li the owner and group fields set to current userid and groupid
 *     \li the <em>prev</em> and <em>next</em> fields, pointers in the double-linked list of free inodes, change their
 *         meaning: they are replaced by the <em>time of last file modification</em> and <em>time of last file
 *         access</em> which are set to current time
 *     \li the reference fields set to NULL_CLUSTER
 *     \li all other fields reset.

 *  \param type the inode type (it must represent either a file, or a directory, or a symbolic link)
 *  \param p_nInode pointer to the location where the number of the just allocated inode is to be stored
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the <em>type</em> is illegal or the <em>pointer to inode number</em> is \c NULL
 *  \return -\c ENOSPC, if the list of free inodes is empty
 *  \return -\c EFININVAL, if the free inode is inconsistent
 *  \return -\c EFDININVAL, if the free inode in the dirty state is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c EDCINVAL, if the data cluster header is inconsistent
 *  \return -\c EWGINODENB, if the <em>inode number</em> in the data cluster <tt>status</tt> field is different from the
 *                          provided <em>inode number</em>
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soAllocInode(uint32_t type, uint32_t* p_nInode) {
    soColorProbe(611, "07;31", "soAllocInode (%"PRIu32", %p)\n", type, p_nInode);

    int stat; // variavel para indicar o estado
    uint32_t nBlk, offset; // numero de blocos e respectivo offset para poder aceder ao bloco
    SOSuperBlock *p_sb; // ponteiro para o superbloco
    SOInode *p_itable; // ponteiro para o nó i a revervar
    uint32_t nextInode; // qual o inode que fica seguinte ao iHead

    /* carregar super bloco */
    if ((stat = soLoadSuperBlock()) != 0)
        return stat;

    p_sb = soGetSuperBlock(); /* ler super bloco */

    /*verifica se a lista de nos i livres está vazia*/
    if (p_sb->iFree == 0)
        return -ENOSPC;

    /*  
        falha em 100% dos casos. porque?
        if(*p_nInode < 1 || *p_nInode > p_sb->iTotal)
            return -EINVAL;
     */
    /*converter inode no 1º arg. no seu numero de bloco e seu offset*/
    if ((stat = soConvertRefInT(p_sb->iHead, &nBlk, &offset)) != 0)
        return stat;

    /*Carrega o conteudo do um bloco especifico da tabela de inodesLoad the contents of a specific block of the table of inodes into internal storage*/
    if ((stat = soLoadBlockInT(nBlk)) != 0)
        return stat;

    // se lido correctamente vamos obter o ponteiro para ele
    p_itable = soGetBlockInT();
    nextInode = p_itable[offset].vD1.next; // pois é limpo quando é atribuido ou na limpeza do inode

    /*verifica se o type é ilegal ou se o ponteiro para inode number é nulo*/
    if (((type & INODE_TYPE_MASK) == 0) || p_nInode == NULL) //esta condicao nao verifica se é directorio E regular ao mesmo tempo numa situacao de erro
        return -EINVAL;

    /*
    int 	soQCheckFCInode (SOInode *p_inode)
            Quick check of a free inode in the clean state.
            The contents of all the fields of the inode are checked. Only the values legal in the clean state are allowed.

    int 	soQCheckFDInode (SOSuperBlock *p_sb, SOInode *p_inode)
            Quick check of a free inode in the dirty state.
            The contents of all the fields of the inode, except owner, group and size, are checked for consistency. Only legal values are allowed.

    int 	soQCheckInodeIU (SOSuperBlock *p_sb, SOInode *p_inode)
            The contents of the mode and refcount fields of the inode are checked for consistency. Only combinations of the mode field which lead for the inode to be either in the clean or in the dirty state are allowed.
     */
    if ((stat = soQCheckFCInode(&p_itable[offset])) != 0) { // significa que o inode nao está clean. é preciso "limpar"
        //printf("\n numEro: %i \n iTotal: %i \n stat: %i\n",*p_nInode,p_sb->iTotal,stat);

        // se não está clean, então só pode estar dirty

        // check if the inode is dirty
        if ((stat = soQCheckFCInode(&p_itable[offset])) != 0) {
            // codigo deste if, vem do pdf "manipulacao do cluster de dados", slide 23
            // "it is, clean it"
            if ((stat = soCleanInode(p_sb->iHead)) != 0)
                return stat;
            if ((stat = soLoadBlockInT(nBlk)) != 0)
                return stat;
            p_itable = soGetBlockInT();
        }


        /* limpeza do inode
        p_itable[offset].mode = 0;
        p_itable[offset].owner = 0; // fica sem dono
        p_itable[offset].group = 0; // fica sem grupo
        p_itable[offset].refCount = 0; // fica sem referencias
        p_itable[offset].size = 0; // fica sem tamanho (nao sabemos para o que vai servir o inode)
        p_itable[offset].cluCount = 0; // fica sem data clusters associados
        p_itable[offset].vD1.next = NULL_INODE;
        p_itable[offset].vD2.prev = NULL_INODE;
        p_itable[offset].vD1.aTime = p_itable[offset].vD2.mTime = time(NULL);

        uint32_t referencescount;

        // limpar as referencias directas
        for (referencescount = 0; referencescount < N_DIRECT; referencescount++)
            p_itable[offset].d[referencescount] = NULL_INODE;

        // limpar as referencias indirectas
        p_itable[offset].i1 = p_itable[offset].i2 = NULL_INODE;

        // e agora, está clean?
         a limpeza do inode é insuficiente e/ou incorrecta. Provoca bug no ./ex1.sh se em seguida alocarmos todos os inodes que sobram
          com este if a funcao faz: (...)->14->15->"inode errado" sem if tem o comportamento certo: (...)->14->15->3->2->1->"no space left"
        if ((stat = soQCheckFDInode(p_sb, &p_itable[offset])) != 0) {
            return stat;
        }
         */
    }

    // atribuição dos valores certos ao inode
    p_itable[offset].mode = type; // se e directorio, ficheiro 
    p_itable[offset].owner = getuid(); // retorna o id do utilizador
    p_itable[offset].group = getgid(); // retorna o id do grupo
    p_itable[offset].vD1.aTime = p_itable[offset].vD2.mTime = time(NULL);

    *p_nInode = p_sb->iHead; /* 1º elemento*/


    // se tiver apenas 1 elemento
    if (p_sb->iFree == 1) {
        p_sb->iHead = p_sb->iTail = NULL_INODE;
    } else { // se tiver 2 ou mais elementos
        p_sb->iHead = nextInode;

        if ((stat = soConvertRefInT(nextInode, &nBlk, &offset)) != 0)
            return stat;

        p_itable = soGetBlockInT();

        p_itable[offset].vD2.prev = NULL_INODE; // aponta para a terra
    }

    p_sb->iFree -= 1; /* decrementa nº de Inodes livres*/

    if ((stat = soStoreSuperBlock()) != 0) /* gravar o Super Bloco */
        return stat;

    if ((stat = soStoreBlockInT()) != 0) /* gravar tabela de nosI */
        return stat;

    return 0;
}
