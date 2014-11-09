/**
 *  \file soRead.c (implementation file)
 *
 *  \author Pedro Gabriel Fernandes Vieira
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

/**
 *  \brief Read data from an open regular file.
 *
 *  It tries to emulate <em>read</em> system call.
 *
 *  \param ePath path to the file
 *  \param buff pointer to the buffer where data to be read is to be stored
 *  \param count number of bytes to be read
 *  \param pos starting [byte] position in the file data continuum where data is to be read from
 *
 *  \return <em>number of bytes effectively read</em>, on success
 *  \return -\c EINVAL, if the pointer to the string is \c NULL or or the path string is a \c NULL string or the path does
 *                      not describe an absolute path
 *  \return -\c ENAMETOOLONG, if the path name or any of its components exceed the maximum allowed length
 *  \return -\c ENOTDIR, if any of the components of <tt>ePath</tt>, but the last one, is not a directory
 *  \return -\c EISDIR, if <tt>ePath</tt> describes a directory
 *  \return -\c ELOOP, if the path resolves to more than one symbolic link
 *  \return -\c ENOENT, if no entry with a name equal to any of the components of <tt>ePath</tt> is found
 *  \return -\c EFBIG, if the starting [byte] position in the file data continuum assumes a value passing its maximum
 *                     size
 *  \return -\c EACCES, if the process that calls the operation has not execution permission on any of the components
 *                      of <tt>ePath</tt>, but the last one
 *  \return -\c EPERM, if the process that calls the operation has not read permission on the file described by
 *                     <tt>ePath</tt>
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails on writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soRead (const char *ePath, void *buff, uint32_t count, int32_t pos)
{
  soColorProbe (229, "07;31", "soRead (\"%s\", %p, %u, %u)\n", ePath, buff, count, pos);

  int stat; // variavel para o estado
  SOSuperBlock *p_sb; // ponteiro para o super bloco
  SOInode p_inode; // pointer to inode table for nBlk block position
  uint32_t p_nInodeEnt; // ponteiro para a localizacao onde o nº do nó I associado à entrada é armazenado
  uint32_t offset; // numero do offset ()
  uint32_t p_clustInd; // ponteiro para indice do cluster
  uint32_t p_clustIndF; // ponteiro para indice do cluster final
  uint32_t offsetFinal; // offset final
  int bytesRead = 0; // variável para indicar o numero de bytes lidos em sucesso

  SODataClust p_buff; // ponteiro para o buffer


  /*------VALIDATIONS----------*/

  /* carregar super bloco*/
  if( (stat = soLoadSuperBlock()) != 0)
  	return stat;

  /* obter super bloco*/
  p_sb = soGetSuperBlock();

  /* if the pointer to the string is null */
  if(ePath == NULL)
  	return -EINVAL;

 /* if the path does not describe a absolute path*/
  if(ePath[0] != '/')
  	return -EINVAL;

  /*if the path name or any of its components exceed the maximum allowed length*/
  if(strlen(ePath) > MAX_PATH) 
  	return -ENAMETOOLONG;

   /* Obter o Inode associado ao ePath e verificar se há erros */
  if( (stat = soGetDirEntryByPath(ePath, NULL, &p_nInodeEnt)) != 0)
    return stat;

  /* Ler e verificar o inode (tem que ser um ficheiro regular) */
  if( (stat = soReadInode(&p_inode, p_nInodeEnt, IUIN)) != 0)
  	return stat;

  /*verificar se ePath descreve um directorio*/
  if( (stat = soQCheckDirCont(p_sb, &p_inode)) != 0)
    return -EISDIR;

  /*if any of the components of ePath, but the last one, is not a directory*/
  if( (stat != -ENOTDIR))
    return stat;

  /*verificar se o processo tem permissão de execucção*/
  if( (stat = soAccessGranted(p_nInodeEnt, X)) != 0)
  	return -EACCES;

  /*verificar se o processo tem permissao de leitura*/
  if( (stat = soAccessGranted(p_nInodeEnt, R)) != 0)
  	return -EPERM;

  /*if the starting [byte] position in the file data continuum assumes a value passing its maximum size*/
  /*Verifica se o pos está fora do ficheiro*/
  if(pos > p_inode.size)
    return -EFBIG;

  /*------END OF VALIDATIONS-------*/


  /* Corrige o count se pos+count ultrapassar o limite do ficheiro */
  if((pos + count) > p_inode.size)
    count = p_inode.size - pos;

  /* vai obter o clustInd e offset apartir do pos */
  if( (stat = soConvertBPIDC(pos, &p_clustInd, &offset)) != 0)
    return stat;

  // byte position é definido como :
    // pos = clustInd * BSLPC + offset

  /* vai obter o clustInd e offset do final do ficheiro apartir do pos + count */
  if( (stat = soConvertBPIDC(pos + count, &p_clustIndF, &offsetFinal)) != 0)
    return stat;

  /* leitura do 1º cluster do inode*/
  if( (stat = soReadFileCluster(p_nInodeEnt, p_clustInd, &p_buff)) != 0)
    return stat;

 /* Se for necessario ler um pedaço do mesmo cluster */
  if(p_clustInd == p_clustIndF)
  {
    memcpy(buff, &p_buff.info.data[BSLPC] + offset, (offsetFinal - offset));
    bytesRead = offsetFinal - offset;
    return bytesRead; 
  }

  /*Se for para ler mais que 1 cluster, ler o resto do primeiro */
  memcpy(buff, &p_buff.info.data[BSLPC] + offset, (BSLPC - offset));
  bytesRead = BSLPC - offset;

  p_clustInd++; // incrementa o indice do cluster

  /* ler proximo cluster*/
  if( (stat = soReadFileCluster(p_nInodeEnt, p_clustInd, &p_buff)) != 0)
    return stat;

  /* ler clusters "intermédios" */ 
  while(p_clustInd < p_clustIndF)
  {
    memcpy(buff+bytesRead, &p_buff.info.data[BSLPC], BSLPC);
    bytesRead += BSLPC;

    /*ler proximo cluster */
    if( (stat = soReadFileCluster(p_nInodeEnt, p_clustInd, &p_buff)) != 0)
      return stat;
  }


  /* caso o que cluster que vamos ler é o ultimo*/
  memcpy(buff + bytesRead, &p_buff.info.data[BSLPC], offsetFinal);
  bytesRead += offsetFinal;

  return bytesRead;
}
