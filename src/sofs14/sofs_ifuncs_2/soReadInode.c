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
  SOInode *p_itable; // variavel auxiliar para mais tarde aceder a uma posição da tabela de iNodes

  	 /* carregar super bloco */
  if((stat = soLoadSuperBlock()) != 0)
  	return stat;

  p_sb = soGetSuperBlock(); /* ler super bloco */


  if((stat = soQCheckSuperBlock(p_sb)) != 0) /* quick check of the superblock metadata */
  	return stat;

  	/* if the buffer pointer is NULL or the inode number is out of range or the inode status is invalid */
  if(nInode >= p_sb->iTotal || nInode == NULL || nInode == 0)
  	return -EINVAL;

  /* if the inode in use is inconsistent */
  //if((stat = soQCheckInodeIU(p_sb, &p_inode[offset])) != 0)  /*AQUI DA SEGMENTATION FAULT*/
  	//return -EIUININVAL;  		

  	/* Check if inode is in use or if it's a free inode in dirty state */
  //if(status != IUIN && status != FDIN)
  	//return stat;

	/* Convert the inode number which translates to an entry of the inode table */
  if((stat = soConvertRefInT(nInode, &nBlk, &offset)) != 0)
  	return stat;

	/*Carrega o conteudo do um bloco especifico da tabela de inodes*/
  if((stat = soLoadBlockInT(nBlk)) != 0)
  	return stat;

  	/* se lido correctamente vamos obter o ponteiro para ele*/
  p_itable = soGetBlockInT(); 
  p_inode = &p_itable[offset]; 

  /*se o nó inode livre no estado sujo está inconsistente*/
  if((stat = soQCheckFDInode(p_sb, &p_itable[offset])) != 0) 
  	return -EFDININVAL;

  /*if inode is in use status*/
  if(status == IUIN)
  	p_inode->vD1.aTime = time(NULL);


  return 0;
}
