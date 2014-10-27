/**
 *  \file soGetDirEntryByName.c (implementation file)
 *
 *  \author João Cravo, 63784
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

/**
 *  \brief Get an entry by name.
 *
 *  The directory contents, seen as an array of directory entries, is parsed to find an entry whose name is
 *  <tt>eName</tt>. Thus, the inode associated to the directory must be in use and belong to the directory type.
 *
 *  The <tt>eName</tt> must also be a <em>base name</em> and not a <em>path</em>, that is, it can not contain the
 *  character '/'.
 *
 *  The process that calls the operation must have execution (x) permission on the directory.
 *
 *  \param nInodeDir number of the inode associated to the directory
 *  \param eName pointer to the string holding the name of the directory entry to be located
 *  \param p_nInodeEnt pointer to the location where the number of the inode associated to the directory entry whose
 *                     name is passed, is to be stored
 *                     (nothing is stored if \c NULL)
 *  \param p_idx pointer to the location where the index to the directory entry whose name is passed, or the index of
 *               the first entry that is free in the clean state, is to be stored
 *               (nothing is stored if \c NULL)
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, if the <em>inode number</em> is out of range or the pointer to the string is \c NULL or the
 *                      name string does not describe a file name
 *  \return -\c ENAMETOOLONG, if the name string exceeds the maximum allowed length
 *  \return -\c ENOTDIR, if the inode type is not a directory
 *  \return -\c ENOENT,  if no entry with <tt>name</tt> is found
 *  \return -\c EACCES, if the process that calls the operation has not execution permission on the directory
 *  \return -\c EDIRINVAL, if the directory is inconsistent
 *  \return -\c EDEINVAL, if the directory entry is inconsistent
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soGetDirEntryByName (uint32_t nInodeDir, const char *eName, uint32_t *p_nInodeEnt, uint32_t *p_idx)
{
  soColorProbe (312, "07;31", "soGetDirEntryByName (%"PRIu32", \"%s\", %p, %p)\n",
                nInodeDir, eName, p_nInodeEnt, p_idx);

  int stat; //variavel para status
  SOSuperBlock *p_sb; //ponteiro para super bloco
  SOInode inode; //variavel para inode
  SODataClust dc; //variavel para data cluster
  int i = 0; //
  char *c;
  
  //Carregar e obter SUper Bloco
  if((stat = soLoadSuperBlock()) != 0)
      return stat;
  
  p_sb = soGetSuperBlock();
  
  //Verificar parametros do nInodeDir
  if(nInodeDir > p_sb->iTotal )
  	return -EINVAL;
  
  //Verificar eName
  if(eName == NULL)
  	return -EINVAL;
       
  //Validação da String 
  c = strchr(eName, '/');
  //eName não pode conter '/' porque senão seria um caminho 
  if(c != NULL) return -EINVAL;
  
  //Verificar comprimento do nome
  if(strlen(eName) > MAX_NAME)
    return -ENAMETOOLONG;
  
  //Ler o inode desejado
  if((stat = soReadInode(&inode, nInodeDir, IUIN)) != 0)
    return stat;
  
  //Verificar se o inode é um directorio
  if((inode.mode & INODE_DIR ) == 0)
    return -ENOTDIR;
  
  //verificar se o inode tem as permisoes de execução
  if((stat = soAccessGranted(nInodeDir, X)) != 0)
    return stat;
  
  //Verificar inconsistencia do directorio
  if((stat = soQCheckDirCont(p_sb, &inode)) != 0)
    return stat;
  
  //FIM das Validaçoes
  
  int clusterNumberTotal = inode.size/(DPC * sizeof(SODirEntry));
  int clusterNumber = 0;
  int freeEntryFound = 0;
  int tbindex = 0;
  
  for(clusterNumber = 0; clusterNumber < clusterNumberTotal; clusterNumber++){
    //Ler Cluster
      
    if((stat = soReadFileCluster(nInodeDir, clusterNumber, &dc)) != 0 ){
      return stat;
    }

    //Inspeccionar todas as entradas
    for(i = 0; i<DPC; i++){
	
	/*  if name not found and no free entry found yet  */
	if( (freeEntryFound == 0) && (dc.info.de[i].name[0] == '\0') && (dc.info.de[i].name[MAX_NAME] == '\0') ){
	
		if(p_idx != NULL)        
			tbindex = i + (clusterNumber * DPC);
        	
	freeEntryFound = 1;
		
	}

      /*  check if the name was found  */
      else if( strcmp((const char*)dc.info.de[i].name, eName) == 0){
	if(p_idx != NULL)        
		*p_idx = i + (clusterNumber * DPC);
	if(p_nInodeEnt != NULL)        
		*p_nInodeEnt = dc.info.de[i].nInode;

        /*  if name is found, all went well  */
        return 0;
      }
      
    }
 } 

  /*  no free entry was found, which means that all the directory entries were full  */
  if(freeEntryFound == 0){
        
    if(p_idx != NULL)
    	tbindex = clusterNumber * DPC;
  }

  if(p_idx != NULL)
      *p_idx = tbindex;

  return -ENOENT;
}
