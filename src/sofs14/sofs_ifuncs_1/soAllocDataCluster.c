/**
 *  \file soAllocDataCluster.c (implementation file)
 *
 *  \author Bruno Silva - 68535 
 */

#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
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
/* #define  CLEAN_CLUSTER */
#ifdef CLEAN_CLUSTER
#include "sofs_ifuncs_3.h"
#endif

/* Allusion to internal functions */

int soReplenish (SOSuperBlock *p_sb);
int soDeplete (SOSuperBlock *p_sb);

/**
 *  \brief Allocate a free data cluster and associate it to an inode.
 *
 *  The inode is supposed to be associated to a file (a regular file, a directory or a symbolic link), but the only
 *  consistency check at this stage should be to check if the inode is not free.
 *
 *  The cluster is retrieved from the retrieval cache of free data cluster references. If the cache is empty, it has to
 *  be replenished before the retrieval may take place. If the data cluster is in the dirty state, it has to be cleaned
 *  first. The header fields of the allocated cluster should be all filled in: <tt>prev</tt> and <tt>next</tt> should be
 *  set to \c NULL_CLUSTER and <tt>stat</tt> to the given inode number.
 *
 *  \param nInode number of the inode the data cluster should be associated to
 *  \param p_nClust pointer to the location where the logical number of the allocated data cluster is to be stored
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c EINVAL, the <em>inode number</em> is out of range or the <em>pointer to the logical data cluster
 *                      number</em> is \c NULL
 *  \return -\c ENOSPC, if there are no free data clusters
 *  \return -\c EIUININVAL, if the inode in use is inconsistent
 *  \return -\c EFDININVAL, if the free inode in the dirty state is inconsistent
 *  \return -\c ELDCININVAL, if the list of data cluster references belonging to an inode is inconsistent
 *  \return -\c EDCINVAL, if the data cluster header is inconsistent
 *  \return -\c EDCNOTIL, if the referenced data cluster is not in the list of direct references
 *  \return -\c EWGINODENB, if the <em>inode number</em> in the data cluster <tt>status</tt> field is different from the
 *                          provided <em>inode number</em>
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected at some internal storage lower level
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soAllocDataCluster (uint32_t nInode, uint32_t *p_nClust)
{
	soColorProbe (613, "07;33", "soAllocDataCluster (%"PRIu32", %p)\n", nInode, p_nClust);

	int stat; // variavel para o estado de erro
	uint32_t nBlock, offset, nClust, clusterStat, NFClt; //variaveis para localizar o inode pretendido, e o cluster, contem variavel extra usada no teste de consistencia do header do cluster e outra para calcular o numero fisico do bloco
	SOSuperBlock *p_sb; //ponteiro para o superbloco
	SOInode *p_inode; // ponteiro para o inode que vai ser reservado o cluster
  SODataClust cluster; //ponteiro para o cluster que vai ser reservado

  //carregar o super bloco
	if((stat = soLoadSuperBlock()) != 0)
		return stat;

	p_sb = soGetSuperBlock();

  if((stat = soQCheckSuperBlock(p_sb)) != 0)
    return stat;

  if((stat = soQCheckDZ(p_sb)) != 0)
    return stat;

  //teste se o inode pretendido de encontra dentro dos valores possiveis e se o ponteiro nao vem com NULL_CLUSTER associado
	if(nInode <= 0  || nInode > p_sb->iTotal -1 || p_nClust == NULL)
		return -EINVAL;

  //verificar se ha clusters livres
  if(p_sb->dZoneFree == 0)
  	return -ENOSPC;

  //carregar inode pretendido
  if((stat = soConvertRefInT(nInode, &nBlock, &offset)) != 0)
  	return stat;
  	
	if((stat = soLoadBlockInT(nBlock)) != 0)
		return stat;

	p_inode = soGetBlockInT();

  //teste de consistencia ao inode
	if((stat = soQCheckInodeIU(p_sb, &p_inode[offset])) != 0)
		return stat;

  //guardar o inode so precisavamos de testar a consistencia
  if( (stat = soStoreBlockInT()) != 0)
    return stat;

  //se a cache estiver vazia, enche-la
	if(p_sb->dZoneRetriev.cacheIdx == DZONE_CACHE_SIZE)
		soReplenish(p_sb);

  //nclust = numero logico do cluster
	nClust = p_sb->dZoneRetriev.cache[p_sb->dZoneRetriev.cacheIdx]; // passar o numero do proximo cluster livre para nClust

  //teste de consistencia ao proximo cluster a reservar
  if((stat = soQCheckStatDC(p_sb, nClust, &clusterStat)) != 0)
    return stat;

	p_sb->dZoneRetriev.cache[p_sb->dZoneRetriev.cacheIdx] = NULL_CLUSTER; //esse cluster já nao vai estar disponivel, por isso NULL_CLUSTER
	p_sb->dZoneRetriev.cacheIdx += 1;
	p_sb->dZoneFree -= 1;


	//ir buscar o cluster nClust. nClust precisa de ser o numero fisico
  //relação entre numero fisico e numero logico
  //NFClt = dzone_start + NLClt * BLOCKS_PER_CLUSTER;
  NFClt = p_sb->dZoneStart + nClust * BLOCKS_PER_CLUSTER;

  if ((stat = soReadCacheCluster(NFClt, &cluster)) != 0)
    return stat;

  cluster.prev = cluster.next = NULL_CLUSTER;
  cluster.stat = nInode;
  
  //atribuir o numero do cluster ao ponteiro fornecido nos argumentos para esse efeito
  *p_nClust = nClust;

  //testar se o stat do cluster indica o inode pretendido
  if(cluster.stat != nInode)
    return -EWGINODENB;

  //voltar a guardar o cluster
  if ((stat = soWriteCacheCluster(NFClt, &cluster)) != 0)
    return stat;

  //voltar a guardar o super bloco
  if((stat = soStoreSuperBlock()) != 0)
    return stat;
  
  return 0;
}

/**
 *  \brief Replenish the retrieval cache of free data cluster references.
 *
 *  \param p_sb pointer to a buffer where the superblock data is stored
 *
 *  \return <tt>0 (zero)</tt>, on success
 *  \return -\c ELIBBAD, if some kind of inconsistency was detected
 *  \return -\c EBADF, if the device is not already opened
 *  \return -\c EIO, if it fails reading or writing
 *  \return -<em>other specific error</em> issued by \e lseek system call
 */

int soReplenish (SOSuperBlock *p_sb)
{
  int stat, nctt, n; // stat = variavel para o estado de erro; nctt = para o numero de clusters a transmitir; n = usada nos for's
  SODataClust cluster; // ponteiro para manipulacao de um cluster
  uint32_t nLCluster, NFClt; // variavel para atribuir ao numero logico do proximo cluster a manipular, e variavel para calcular o numero fisico do bloco


  //teste de o ponteiro para o super bloco nao é NULL
  if(p_sb == NULL)
    return -EBADF;

  //teste de consistencia ao super bloco
  if((stat = soQCheckSuperBlock(p_sb)) != 0)
    return -ELIBBAD;

  // numero de clusters a transmitir, caso nao haja menos clusters livres do que a cache apenas podemos transmitir esse numero
  nctt = (p_sb->dZoneFree < DZONE_CACHE_SIZE) ? p_sb->dZoneFree : DZONE_CACHE_SIZE;
  nLCluster = p_sb->dHead; // numero logico do primeiro cluster a trasmitir encontra-se no dHead

  for(n = DZONE_CACHE_SIZE - nctt; n < DZONE_CACHE_SIZE; n++){


    if(nLCluster == NULL_CLUSTER)
      break;

    NFClt = p_sb->dZoneStart + nLCluster * BLOCKS_PER_CLUSTER;

    //carregar o cluster nLClust usando o seu numero fisico
    if ((stat = soReadCacheCluster(NFClt, &cluster)) != 0)
      return stat;

    //inseri-lo na cache de retirada
    p_sb->dZoneRetriev.cache[n] = nLCluster;

    //avancar para o proximo cluster livre
    nLCluster = cluster.next;

    //como o cluster esta referenciado na cache de retirada do super bloco o next e prev sao NULL_CLUSTER
    cluster.prev = cluster.next = NULL_CLUSTER;


    if ((stat = soWriteCacheCluster(NFClt, &cluster)) != 0)
      return stat;
  }

  //se o for anterior nao chegou ao fim por nao haverem mais clusters livres na zona de dados, eles estao na cache de insercao
  if(n != DZONE_CACHE_SIZE){

    p_sb->dHead = p_sb->dTail = NULL_CLUSTER;
    
    soDeplete(p_sb); //retirar os restantes clusters da cache de insercao

    nLCluster = p_sb->dHead;

    for( ; n < DZONE_CACHE_SIZE; n++){
      
      NFClt = p_sb->dZoneStart + nLCluster * BLOCKS_PER_CLUSTER;

      if ((stat = soReadCacheCluster(NFClt, &cluster)) != 0)
        return stat;

      p_sb->dZoneRetriev.cache[n] = nLCluster;
      nLCluster = cluster.next;
      cluster.prev = cluster.next = NULL_CLUSTER;

      if ((stat = soWriteCacheCluster(NFClt, &cluster)) != 0)
        return stat;

    }
  }

  //se no fim ainda houver mais clusters o proximo tem de deixar de refenciar o anterior pois ele esta na cache de retirada
  if(nLCluster != NULL_CLUSTER){

    NFClt = p_sb->dZoneStart + nLCluster * BLOCKS_PER_CLUSTER;

    if ((stat = soReadCacheCluster(NFClt, &cluster)) != 0)
      return stat;

    cluster.prev = NULL_CLUSTER;

    if ((stat = soWriteCacheCluster(NFClt, &cluster)) != 0)
      return stat;
  }

  //actualizar o indice da cache de retirada
  p_sb->dZoneRetriev.cacheIdx = DZONE_CACHE_SIZE - nctt;

  //actualizar o dHead para o numero logico do proximo cluster livre
  p_sb->dHead = nLCluster;


  if(nLCluster == NULL_CLUSTER)
    p_sb->dTail = NULL_CLUSTER;


  if((stat = soStoreSuperBlock()) != 0)
    return stat;

  return 0;
}
