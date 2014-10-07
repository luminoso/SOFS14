/**
 *  \file soReadInode.c (implementation file)
 *
 *  \author Pedro Gabriel Fernandes Vieira
 */

/* #define CLEAN_INODE */

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
#ifdef CLEAN_INODE
#include "sofs_ifuncs_3.h"
#endif

/** \brief inode in use status */
#define IUIN  0
/** \brief free inode in dirty state status */
#define FDIN  1

/**
 *  \brief Read specific inode data from the table of inodes.
 *
 *  The inode may be either in use and belong to one of the legal file types or be free in the dirty state.
 *  Upon reading, the <em>time of last file access</em> field is set to current time, if the inode is in use.
 *
 *  \param p_inode pointer to the buffer where inode data must be read into
 *  \param nInode number of the inode to be read from
 *  \param status inode status (in use / free in the dirty state)
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the <em>buffer pointer</em> is \c NULL or the <em>inode number</em> is out of range or the
 *                      inode status is invalid
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c EFDININVAL, if the free inode in the dirty state is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c EDCINVAL, if the data cluster header is inconsistent
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soReadInode (SOInode *p_inode, uint32_t nInode, uint32_t status)
{
  soColorProbe (511, "07;31", "soReadInode (%p, %"PRIu32", %"PRIu32")\n", p_inode, nInode, status);
  /* insert your code here */

  SOSuperBlock *p_sb;  // ponteiro para o super bloco 
  int stat;				// variavel para indicar  estado
  uint32_t nBlk, offset; // variável para o numero do bloco e seu offset
  SOInode *pInode; // ponteiro para o inode a ser lido

  	 /* carregar super bloco */
  if((stat = soLoadSuperBlock()) != 0)
  	return stat;

  p_sb = soGetSuperBlock(); /* ler super bloco */


  if((stat = soQCheckSuperBlock(p_sb)) != 0) /* quick check of the superblock metadata */
  	return stat;

    /*If inode Table is consistency*/
  if((stat = soQCheckInT(p_sb)) != 0)
    return stat;  

  /*verifica se o ponteiro para o noI que está a ser lido não é NULL*/
  if(p_inode == NULL)
    return -EINVAL;

  	//if  nInode is within valid parameters
  if(nInode <= 0 || nInode > p_sb->iTotal)
  	return -EINVAL;



	/* Convert the inode number which translates to an entry of the inode table */
  if((stat = soConvertRefInT(nInode, &nBlk, &offset)) != 0)
  	return stat;

	/*Carrega o conteudo do um bloco especifico da tabela de inodes*/
  if((stat = soLoadBlockInT(nBlk)) != 0)
  	return stat;


  //obtem o ponteiro para o bloco que contem o nóI*/
  pInode = soGetBlockInT();


  //verifica se o nó em uso é inconsistente
  if(status == IUIN)
  {
    if((stat = soQCheckInodeIU(p_sb, &pInode[offset])) != 0)
      return stat;
  }
    
   // verifica se o nó I livre no estado sujo é insconsistente 
  if(status == FDIN)
  {

    if((stat = soQCheckFDInode(p_sb, &pInode[offset])) != 0)
      return stat;
  }  

  	/* se lido correctamente vamos obter o ponteiro para ele*/
  //p_itable = soGetBlockInT(); 
  //p_inode = &p_itable[offset];

  //update the access time to the current time
  p_inode[offset].vD1.aTime = time(NULL); 

  /*guardar tabela de nós I*/
  if( (stat = soStoreBlockInT()) != 0)
    return stat;

  /*guardar super bloco*/
  if( (stat = soStoreSuperBlock()) != 0)
    return stat;
  /*if inode is in use status*/
  /*if(status == IUIN)
  	p_inode->vD1.aTime = time(NULL);*/

  return 0;
}
