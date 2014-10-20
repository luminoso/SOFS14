/**
 *  \file soWriteInode.c (implementation file)
 *
 *  \author Bruno Silva - 68535
 */

#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>				//for mencpy()

#include "sofs_probe.h"
#include "sofs_superblock.h"
#include "sofs_inode.h"
#include "sofs_basicoper.h"
#include "sofs_basicconsist.h"

/** \brief inode in use status */
#define IUIN  0
/** \brief free inode in dirty state status */
#define FDIN  1

/**
 *  \brief Write specific inode data to the table of inodes.
 *
 *  The inode must be in use and belong to one of the legal file types.
 *  Upon writing, the <em>time of last file modification</em> and <em>time of last file access</em> fields are set to
 *  current time, if the inode is in use.
 *
 *  \param p_inode pointer to the buffer containing the data to be written from
 *  \param nInode number of the inode to be written into
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

int soWriteInode (SOInode *p_inode, uint32_t nInode, uint32_t status)
{
	soColorProbe (512, "07;31", "soWriteInode (%p, %"PRIu32", %"PRIu32")\n", p_inode, nInode, status);
	
	int stat; // variavel para o estado de erro
	uint32_t nBlock, offset; //variaveis para localizar o inode pretendido
	SOInode *p_in; // ponteiro para o inode a ser escrito
	SOSuperBlock *p_sb; //ponteiro para o superbloco


	//Loading the super block
 	if((stat = soLoadSuperBlock()) != 0)
		return stat;

	p_sb = soGetSuperBlock();

    //Quick check on the inode Table consistency
    if((stat = soQCheckInT(p_sb)) != 0)
    	return stat;

    //check if pointer to the inode beeing written is not NULL
    if(p_inode == NULL)
    	return -EINVAL;

   	if(status != IUIN && status != FDIN)
   		return -EINVAL;

    //check nInode is within valid parameters
    if(!(nInode >0 && nInode < p_sb->iTotal))
    	return -EINVAL;

    //find the inode we want in the specific block and it's offset
    if((stat = soConvertRefInT(nInode, &nBlock, &offset)) != 0)
  		return stat;
  	
  	//load the inode block we want
	if((stat = soLoadBlockInT(nBlock)) != 0)
		return stat;

	//get a pointer to the block containing the inode we want
	p_in = soGetBlockInT();

    //copy the inode data to the inode we want
	memcpy(&p_in[offset], p_inode, sizeof(SOInode));

	//Quick check of the inode in use consistency
	if(status == IUIN){
		if((stat = soQCheckInodeIU(p_sb, &p_in[offset])) != 0)
			return stat;

		

		//update the access time and modified time to the current time
		p_in[offset].vD1.aTime = time(NULL);
		p_in[offset].vD2.mTime = p_in[offset].vD1.aTime;
	}


	//Quick check of the inode in the dirty state consistency
	if(status == FDIN){
		if((stat = soQCheckFDInode(p_sb, &p_in[offset])) != 0)
			return stat;

	}
		

	//store the inode back to the inode table
	if( (stat = soStoreBlockInT()) != 0)
    	return stat;

    //store the super block back
    if((stat = soStoreSuperBlock()) != 0)
    return stat;

	return 0;
}

