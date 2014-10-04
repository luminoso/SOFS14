/**
 *  \file soFreeInode.c (implementation file)
 *
 *  \author João Cravo
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

/**
 *  \brief Free the referenced inode.
 *
 *  The inode must be in use, belong to one of the legal file types and have no directory entries associated with it
 *  (refcount = 0).
 *  The inode is marked free in the dirty state and inserted in the list of free inodes.
 *
 *  Notice that the inode 0, supposed to belong to the file system root directory, can not be freed.
 *
 *  The only affected fields are:
 *     \li the free flag of mode field, which is set
 *     \li the <em>time of last file modification</em> and <em>time of last file access</em> fields, which change their
 *         meaning: they are replaced by the <em>prev</em> and <em>next</em> pointers in the double-linked list of free
 *         inodes.
 * *
 *  \param nInode number of the inode to be freed
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the <em>inode number</em> is out of range
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c EDCINVAL, if the data cluster header is inconsistent
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soFreeInode (uint32_t nInode)
{
   soColorProbe (612, "07;31", "soFreeInode (%"PRIu32")\n", nInode);

  int stat;
  SOSuperBlock *p_sb;
  
  SOInode *p_iNode;
  uint32_t nBlk, offset;
  
  SOInode *p_iTail;
  uint32_t nBlkiTail, offsetiTail;
 
  
  /* Carregar Super Bloco */
   if((stat = soLoadSuperBlock()) != 0)
    return stat;

  /* Ler Super Bloco */
   if ((p_sb = soGetSuperBlock()) == NULL)
		return -EIO;
   
  /* Verificar os parametros do nInode */
   if (nInode == 0 || nInode >= p_sb->iTotal)
		return -EINVAL;

  /* Verificar inconsistencia da Tabela de iNodes */
   if ((stat = soQCheckInT(p_sb)) != 0)
		return stat;
   
  /* Converter nInode no seu numero de bloco e seu offset */
   if((stat = soConvertRefInT(nInode, &nBlk, &offset)) != 0)
   	return stat;
   		
  /* Carrega o conteudo do bloco especifico da tabela de inodes */
   if((stat = soLoadBlockInT(nBlk)) != 0)
   	return stat;
   	
  /* Ler o conteudo carregado */
   if ((p_iNode = soGetBlockInT()) == NULL)
	return -EIO;

  /* Verificar a consistencia do iNode */ 
   if ((stat = soQCheckInodeIU(p_sb, p_iNode)) != 0) 
    return stat;

  /* Se estiver vazia */
  if( p_sb->iFree == 0)
  {
	  p_iNode[offset].vD2.prev = p_iNode[offset].vD1.next = NULL_INODE;
	  p_sb->iHead = p_sb->iTail = nInode;
	  
	  if ((stat = soStoreBlockInT()) != 0)
	   return stat; 
  }
  
  /* Se a lista tem pelo menos 1 elemento */
  else
  {
	p_iNode[offset].vD2.prev = p_sb->iTail;
	p_iNode[offset].vD1.next = NULL_INODE;
	
	/* Gravar a info do iNode */
	if ((stat = soStoreBlockInT()) != 0)
	 return stat;
			
	/* Converter iTail no seu numero de bloco e seu offset */
    if((stat = soConvertRefInT(p_sb->iTail, &nBlkiTail, &offsetiTail)) != 0)
   	 return stat;
	
	/* Carrega o conteudo do um bloco especifico da tabela de inodes */
	if ((stat = soLoadBlockInT(nBlkiTail)) != 0)
	 return stat;

	/* Ler o conteudo carregado */
	if ((p_iTail = soGetBlockInT()) == NULL)
	 return -EIO;
		
	p_iTail[offsetiTail].vD1.next = nInode;
	p_sb->iTail = nInode;	
  }
  
  p_sb->iFree +=1;
  
  /* Gravar o super bloco */
  if( (stat = soStoreSuperBlock()) != 0) 
   return stat;
   
  /* Gravar tabela de Inodes */
  if((stat = soStoreBlockInT()) != 0) 
   return stat;
  
   return 0;
}
