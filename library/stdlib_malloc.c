/*
 * $Id: stdlib_malloc.c,v 1.20 2008-09-30 14:09:00 obarthel Exp $
 *
 * :ts=4
 *
 * Portable ISO 'C' (1994) runtime library for the Amiga computer
 * Copyright (c) 2002-2015 by Olaf Barthel <obarthel (at) gmx.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   - Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   - Neither the name of Olaf Barthel nor the names of contributors
 *     may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _STDLIB_HEADERS_H
#include "stdlib_headers.h"
#endif /* _STDLIB_HEADERS_H */

/****************************************************************************/

#ifndef _STDLIB_MEMORY_H
#include "stdlib_memory.h"
#endif /* _STDLIB_MEMORY_H */

/****************************************************************************/

#ifndef _STDLIB_CONSTRUCTOR_H
#include "stdlib_constructor.h"
#endif /* _STDLIB_CONSTRUCTOR_H */

/****************************************************************************/

#undef malloc
#undef __malloc

/****************************************************************************/

unsigned long NOCOMMON __maximum_memory_allocated;
unsigned long NOCOMMON __current_memory_allocated;
unsigned long NOCOMMON __maximum_num_memory_chunks_allocated;
unsigned long NOCOMMON __current_num_memory_chunks_allocated;

/****************************************************************************/

#if defined(__MEM_DEBUG) && defined(__USE_MEM_TREES)
struct MemoryTree NOCOMMON __memory_tree;
#endif /* __MEM_DEBUG && __USE_MEM_TREES */

/****************************************************************************/

APTR			NOCOMMON __memory_pool;
struct MinList	NOCOMMON __memory_list;

/****************************************************************************/

struct SlabData NOCOMMON __slab_data;

/****************************************************************************/

/* Free all currently unused slabs, regardless of whether they
 * are ready to be purged (SlabNode.sn_EmptyDecay == 0).
 */
void
__free_unused_slabs(void)
{
	struct MinNode * free_node;
	struct MinNode * free_node_next;
	struct SlabNode * sn;

	__memory_lock();

	for(free_node = (struct MinNode *)__slab_data.sd_EmptySlabs.mlh_Head ; 
	    free_node->mln_Succ != NULL ;
	    free_node = free_node_next)
	{
		free_node_next = (struct MinNode *)free_node->mln_Succ;

		/* free_node points to SlabNode.sn_EmptyLink, which
		 * directly follows the SlabNode.sn_MinNode.
		 */
		sn = (struct SlabNode *)&free_node[-1];

		/* Unlink from list of empty slabs. */
		Remove((struct Node *)free_node);

		/* Unlink from list of slabs of the same size. */
		Remove((struct Node *)sn);

		FreeVec(sn);
	}

	__memory_unlock();
}

/****************************************************************************/

size_t
__get_allocation_size(size_t size)
{
	#ifndef __MEM_DEBUG
	{
		size_t total_allocation_size;

		total_allocation_size = sizeof(struct MemoryNode) + size;

		/* Round up the allocation size to the physical allocation granularity. */
		size += ((total_allocation_size + MEM_BLOCKMASK) & ~((ULONG)MEM_BLOCKMASK)) - total_allocation_size;
	}
	#endif /* __MEM_DEBUG */

	return(size);
}

/****************************************************************************/

void *
__allocate_memory(size_t size,BOOL never_free,const char * UNUSED unused_file,int UNUSED unused_line)
{
	struct MemoryNode * mn;
	size_t allocation_size;
	void * result = NULL;
	size_t original_size;

	#if defined(UNIX_PATH_SEMANTICS)
	{
		original_size = size;

		/* The libunix.a flavour accepts zero length memory allocations
		   and quietly turns them into a pointer sized allocations. */
		if(size == 0)
			size = sizeof(char *);
	}
	#endif /* UNIX_PATH_SEMANTICS */

	__memory_lock();

	/* Zero length allocations are by default rejected. */
	if(size == 0)
	{
		__set_errno(EINVAL);
		goto out;
	}

	if(__free_memory_threshold > 0 && AvailMem(MEMF_ANY|MEMF_LARGEST) < __free_memory_threshold)
	{
		SHOWMSG("not enough free memory available to safely proceed with allocation");
		goto out;
	}

	#ifdef __MEM_DEBUG
	{
		assert( MALLOC_HEAD_SIZE > 0 && (MALLOC_HEAD_SIZE % 4) == 0 );
		assert( MALLOC_TAIL_SIZE > 0 && (MALLOC_TAIL_SIZE % 4) == 0 );
		assert( (sizeof(*mn) % 4) == 0 );

		allocation_size = sizeof(*mn) + MALLOC_HEAD_SIZE + size + MALLOC_TAIL_SIZE;
	}
	#else
	{
		/* Round up the allocation size to the physical allocation granularity. */
		size = __get_allocation_size(size);

		allocation_size = sizeof(*mn) + size;
	}
	#endif /* __MEM_DEBUG */

	/* Are we using the slab allocator? */
	if (__slab_data.sd_InUse)
	{
		mn = NULL;

		assert( __slab_data.sd_MaxSlabSize > 0 );

		/* Number of bytes to allocate exceeds the slab size?
		 * If so, allocate this memory chunk separately and
		 * keep track of it.
		 */
		if(allocation_size > __slab_data.sd_MaxSlabSize)
		{
			struct MinNode * single_allocation;

			#if defined(__amigaos4__)
			{
				single_allocation = AllocVec(sizeof(*single_allocation) + allocation_size,MEMF_PRIVATE);
			}
			#else
			{
				single_allocation = AllocVec(sizeof(*single_allocation) + allocation_size,MEMF_ANY);
			}
			#endif /* __amigaos4__ */

			if(single_allocation != NULL)
			{
				AddTail((struct List *)&__slab_data.sd_SingleAllocations,(struct Node *)single_allocation);
				__slab_data.sd_NumSingleAllocations++;

				mn = (struct MemoryNode *)&single_allocation[1];
			}
		}
		/* Otherwise allocate a chunk from a slab. */
		else
		{
			struct MinList * slab_list = NULL;
			ULONG entry_size;
			ULONG chunk_size;
			int slab_index;

			/* Chunks must be at least as small as a MinNode, because
			 * that's what we use for keeping track of the chunks which
			 * are available for allocation within each slab.
			 */
			entry_size = allocation_size;
			if(entry_size < sizeof(struct MinNode))
				entry_size = sizeof(struct MinNode);

			/* Find a slab which keeps track of chunks that are no
			 * larger than the amount of memory which needs to be
			 * allocated. We end up picking the smallest chunk
			 * size that still works.
			 */
			for(slab_index = 0, chunk_size = (1UL << slab_index) ;
			    slab_index < 31 ;
			    slab_index++, chunk_size += chunk_size)
			{
				assert( (chunk_size % sizeof(LONG)) == 0);
				
				if(entry_size <= chunk_size)
				{
					slab_list = &__slab_data.sd_Slabs[slab_index];
					break;
				}
			}

			if(slab_list != NULL)
			{
				struct SlabNode * sn;

				/* Find the first slab which has a free chunk and use it. */
				for(sn = (struct SlabNode *)slab_list->mlh_Head ;
				    sn->sn_MinNode.mln_Succ != NULL ;
				    sn = (struct SlabNode *)sn->sn_MinNode.mln_Succ)
				{
					assert( sn->sn_ChunkSize == chunk_size );
					
					mn = (struct MemoryNode *)RemHead((struct List *)&sn->sn_FreeList);
					if(mn != NULL)
					{
						/* Was this slab empty before we began using it again? */
						if(sn->sn_UseCount == 0)
						{
							/* Mark it as no longer empty. */
							Remove((struct Node *)&sn->sn_EmptyLink);
							sn->sn_EmptyDecay = 0;
						}

						sn->sn_UseCount++;

						/* Is this slab now fully utilized? Move it to the
						 * end of the queue so that it will not be checked
						 * before other slabs of the same size have been
						 * tested. Those at the front of the queue should
						 * still have room left.
						 */
						if(sn->sn_UseCount == sn->sn_Count && sn != (struct SlabNode *)slab_list->mlh_TailPred)
						{
							Remove((struct Node *)sn);
							AddTail((struct List *)slab_list, (struct Node *)sn);
						}

						break;
					}
				}

				/* There is no slab with a free chunk? Then we might have to
				 * allocate a new one.
				 */
				if(mn == NULL)
				{
					struct MinNode * free_node;
					struct MinNode * free_node_next;
					struct SlabNode * new_sn = NULL;

					/* Try to recycle an empty (unused) slab, if possible. */
					for(free_node = (struct MinNode *)__slab_data.sd_EmptySlabs.mlh_Head ; 
					    free_node->mln_Succ != NULL ;
					    free_node = free_node_next)
					{
						free_node_next = (struct MinNode *)free_node->mln_Succ;

						/* free_node points to SlabNode.sn_EmptyLink, which
						 * directly follows the SlabNode.sn_MinNode.
						 */
						sn = (struct SlabNode *)&free_node[-1];

						/* Is this empty slab ready to be reused? */
						if(sn->sn_EmptyDecay == 0)
						{
							/* Unlink from list of empty slabs. */
							Remove((struct Node *)free_node);

							/* Unlink from list of slabs which keep chunks
							 * of the same size.
							 */
							Remove((struct Node *)sn);
							
							new_sn = sn;
							break;
						}
					}

					/* We couldn't reuse an empty slab? Then we'll have to allocate
					 * memory for another one.
					 */
					if(new_sn == NULL)
					{
						#if defined(__amigaos4__)
						{
							new_sn = (struct SlabNode *)AllocVec(sizeof(*sn) + __slab_data.sd_MaxSlabSize,MEMF_PRIVATE);
						}
						#else
						{
							new_sn = (struct SlabNode *)AllocVec(sizeof(*sn) + __slab_data.sd_MaxSlabSize,MEMF_ANY);
						}
						#endif /* __amigaos4__ */
					}

					if(new_sn != NULL)
					{
						struct MinNode * free_chunk;
						ULONG num_free_chunks = 0;
						BYTE * first_byte;
						BYTE * last_byte;

						/* Split up the slab memory into individual chunks
						 * of the same size and keep track of them
						 * in the free list. The memory managed by
						 * this slab immediately follows the
						 * SlabNode header.
						 */
						first_byte	= (BYTE *)&new_sn[1];
						last_byte	= &first_byte[__slab_data.sd_MaxSlabSize - chunk_size];

						for(free_chunk = (struct MinNode *)first_byte ;
						    free_chunk <= (struct MinNode *)last_byte;
						    free_chunk = (struct MinNode *)(((BYTE *)free_chunk) + chunk_size))
						{
							AddTail((struct List *)&new_sn->sn_FreeList, (struct Node *)free_chunk);
							num_free_chunks++;
						}

						/* Grab the first free chunk (there has to be one). */
						mn = (struct MemoryNode *)RemHead((struct List *)&new_sn->sn_FreeList);
						
						assert( mn != NULL );

						/* Set up the new slab and put it where it belongs. */
						new_sn->sn_EmptyDecay	= 0;
						new_sn->sn_UseCount		= 1;
						new_sn->sn_Count		= num_free_chunks;
						new_sn->sn_ChunkSize	= chunk_size;

						AddHead((struct List *)slab_list,(struct Node *)&new_sn);
					}

					/* Mark unused slabs for purging, and purge those which
					 * are ready to be purged.
					 */
					for(free_node = (struct MinNode *)__slab_data.sd_EmptySlabs.mlh_Head ; 
					    free_node->mln_Succ != NULL ;
					    free_node = free_node_next)
					{
						free_node_next = (struct MinNode *)free_node->mln_Succ;

						/* free_node points to SlabNode.sn_EmptyLink, which
						 * directly follows the SlabNode.sn_MinNode.
						 */
						sn = (struct SlabNode *)&free_node[-1];

						/* Is this empty slab ready to be purged? */
						if(sn->sn_EmptyDecay == 0)
						{
							/* Unlink from list of empty slabs. */
							Remove((struct Node *)free_node);

							/* Unlink from list of slabs of the same size. */
							Remove((struct Node *)sn);

							FreeVec(sn);
						}
						/* Give it another chance. */
						else
						{
							sn->sn_EmptyDecay--;
						}
					}
				}
			}
		}
	}
	else if (__memory_pool != NULL)
	{
		mn = AllocPooled(__memory_pool,allocation_size);
	}
	else
	{
		#if defined(__amigaos4__)
		{
			mn = AllocMem(allocation_size,MEMF_PRIVATE);
		}
		#else
		{
			mn = AllocMem(allocation_size,MEMF_ANY);
		}
		#endif /* __amigaos4__ */
	}

	if(mn == NULL)
	{
		SHOWMSG("not enough memory");
		goto out;
	}

	mn->mn_Size			= size;
	mn->mn_NeverFree	= never_free;

	AddTail((struct List *)&__memory_list,(struct Node *)mn);

	__current_memory_allocated += allocation_size;
	if(__maximum_memory_allocated < __current_memory_allocated)
		__maximum_memory_allocated = __current_memory_allocated;

	__current_num_memory_chunks_allocated++;
	if(__maximum_num_memory_chunks_allocated < __current_num_memory_chunks_allocated)
		__maximum_num_memory_chunks_allocated = __current_num_memory_chunks_allocated;

	#ifdef __MEM_DEBUG
	{
		char * head = (char *)(mn + 1);
		char * body = head + MALLOC_HEAD_SIZE;
		char * tail = body + size;

		mn->mn_AlreadyFree		= FALSE;
		mn->mn_Allocation		= body;
		mn->mn_AllocationSize	= allocation_size;
		mn->mn_File				= (char *)file;
		mn->mn_Line				= line;
		mn->mn_FreeFile			= NULL;
		mn->mn_FreeLine			= 0;

		memset(head,MALLOC_HEAD_FILL,MALLOC_HEAD_SIZE);
		memset(body,MALLOC_NEW_FILL,size);
		memset(tail,MALLOC_TAIL_FILL,MALLOC_TAIL_SIZE);

		#ifdef __MEM_DEBUG_LOG
		{
			kprintf("[%s] + %10ld 0x%08lx [",__program_name,size,body);

			kprintf("allocated at %s:%ld]\n",file,line);
		}
		#endif /* __MEM_DEBUG_LOG */

		#ifdef __USE_MEM_TREES
		{
			__red_black_tree_insert(&__memory_tree,mn);
		}
		#endif /* __USE_MEM_TREES */

		result = mn->mn_Allocation;
	}
	#else
	{
		result = &mn[1];
	}
	#endif /* __MEM_DEBUG */

	#if defined(UNIX_PATH_SEMANTICS)
	{
		/* Set the zero length allocation contents to NULL. */
		if(original_size == 0)
			*(char **)result = NULL;
	}
	#endif /* UNIX_PATH_SEMANTICS */

	assert( (((ULONG)result) & 3) == 0 );

 out:

	#ifdef __MEM_DEBUG_LOG
	{
		if(result == NULL)
		{
			kprintf("[%s] + %10ld 0x%08lx [",__program_name,size,NULL);

			kprintf("FAILED: allocated at %s:%ld]\n",file,line);
		}
	}
	#endif /* __MEM_DEBUG_LOG */

	__memory_unlock();

	return(result);
}

/****************************************************************************/

__static void *
__malloc(size_t size,const char * file,int line)
{
	void * result = NULL;

	__memory_lock();

	/* Try to get rid of now unused memory. */
	if(__alloca_cleanup != NULL)
		(*__alloca_cleanup)(file,line);

	__memory_unlock();

	#ifdef __MEM_DEBUG
	{
		/*if((rand() % 16) == 0)
			__check_memory_allocations(file,line);*/
	}
	#endif /* __MEM_DEBUG */

	/* Allocate memory which can be put through realloc() and free(). */
	result = __allocate_memory(size,FALSE,file,line);

	return(result);
}

/****************************************************************************/

void *
malloc(size_t size)
{
	void * result;

	result = __malloc(size,NULL,0);

	return(result);
}

/****************************************************************************/

#if defined(__THREAD_SAFE)

/****************************************************************************/

static struct SignalSemaphore * memory_semaphore;

/****************************************************************************/

void
__memory_lock(void)
{
	if(memory_semaphore != NULL)
		ObtainSemaphore(memory_semaphore);
}

/****************************************************************************/

void
__memory_unlock(void)
{
	if(memory_semaphore != NULL)
		ReleaseSemaphore(memory_semaphore);
}

/****************************************************************************/

#endif /* __THREAD_SAFE */

/****************************************************************************/

STDLIB_DESTRUCTOR(stdlib_memory_exit)
{
	ENTER();

	#ifdef __MEM_DEBUG
	{
		kprintf("[%s] %ld bytes still allocated upon exit, maximum of %ld bytes allocated at a time.\n",
			__program_name,__current_memory_allocated,__maximum_memory_allocated);

		kprintf("[%s] %ld chunks of memory still allocated upon exit, maximum of %ld chunks allocated at a time.\n",
			__program_name,__current_num_memory_chunks_allocated,__maximum_num_memory_chunks_allocated);

		__check_memory_allocations(__FILE__,__LINE__);

		__never_free = FALSE;

		if(__memory_list.mlh_Head != NULL)
		{
			while(NOT IsListEmpty((struct List *)&__memory_list))
			{
				((struct MemoryNode *)__memory_list.mlh_Head)->mn_AlreadyFree = FALSE;

				__free_memory_node((struct MemoryNode *)__memory_list.mlh_Head,__FILE__,__LINE__);
			}
		}

		#if defined(__USE_MEM_TREES)
		{
			__initialize_red_black_tree(&__memory_tree);
		}
		#endif /* __USE_MEM_TREES */
	}
	#endif /* __MEM_DEBUG */

	/* Is the slab memory allocator enabled? */
	if (__slab_data.sd_InUse)
	{
		struct SlabNode * sn;
		struct SlabNode * sn_next;
		struct MinNode * mn;
		struct MinNode * mn_next;
		int i;

		/* Free the memory allocated for each slab. */
		for(i = 0 ; i < 31 ; i++)
		{
			for(sn = (struct SlabNode *)__slab_data.sd_Slabs[i].mlh_Head ;
			    sn->sn_MinNode.mln_Succ != NULL ;
			    sn = sn_next)
			{
				sn_next = (struct SlabNode *)sn->sn_MinNode.mln_Succ;

				FreeVec(sn);
			}

			NewList((struct List *)&__slab_data.sd_Slabs[i]);
		}

		/* Free the memory allocated for each allocation which did not
		 * go into a slab.
		 */
		for(mn = __slab_data.sd_SingleAllocations.mlh_Head ; mn->mln_Succ != NULL ; mn = mn_next)
		{
			mn_next = mn->mln_Succ;

			FreeVec(mn);
		}

		NewList((struct List *)&__slab_data.sd_SingleAllocations);

		NewList((struct List *)&__slab_data.sd_EmptySlabs);

		__slab_data.sd_InUse = FALSE;
	}
	else if (__memory_pool != NULL)
	{
		NewList((struct List *)&__memory_list);

		DeletePool(__memory_pool);
		__memory_pool = NULL;
	}
	else if (__memory_list.mlh_Head != NULL)
	{
		#ifdef __MEM_DEBUG
		{
			while(NOT IsListEmpty((struct List *)&__memory_list))
				__free_memory_node((struct MemoryNode *)__memory_list.mlh_Head,__FILE__,__LINE__);
		}
		#else
		{
			while(NOT IsListEmpty((struct List *)&__memory_list))
				__free_memory_node((struct MemoryNode *)__memory_list.mlh_Head,NULL,0);
		}
		#endif /* __MEM_DEBUG */
	}

	#if defined(__THREAD_SAFE)
	{
		__delete_semaphore(memory_semaphore);
		memory_semaphore = NULL;
	}
	#endif /* __THREAD_SAFE */

	LEAVE();
}

/****************************************************************************/

STDLIB_CONSTRUCTOR(stdlib_memory_init)
{
	BOOL success = FALSE;

	ENTER();

	#if defined(__THREAD_SAFE)
	{
		memory_semaphore = __create_semaphore();
		if(memory_semaphore == NULL)
			goto out;
	}
	#endif /* __THREAD_SAFE */

	#if defined(__USE_MEM_TREES) && defined(__MEM_DEBUG)
	{
		__initialize_red_black_tree(&__memory_tree);
	}
	#endif /* __USE_MEM_TREES && __MEM_DEBUG */

	NewList((struct List *)&__memory_list);

	/* Enable the slab memory allocator? */
	if(__slab_max_size > 0)
	{
		size_t size;

		/* If the maximum allocation size to be made from the slab
		 * is not already a power of 2, round it up. We do not
		 * support allocations larger than 2^31, and the maximum
		 * allocation size should be much smaller.
		 *
		 * Note that the maximum allocation size also defines the
		 * amount of memory which each slab manages.
		 */
		size = sizeof(struct MinNode);
		while(size < __slab_max_size && (size & 0x80000000) == 0)
			size += size;
		
		/* If the slab size looks sound, enable the slab memory allocator. */
		if((size & 0x80000000) == 0)
		{
			int i;

			assert( size <= __slab_max_size );

			/* Start with an empty slab list. */
			for(i = 0 ; i < 31 ; i++)
				NewList((struct List *)&__slab_data.sd_Slabs[i]);

			NewList((struct List *)&__slab_data.sd_SingleAllocations);
			NewList((struct List *)&__slab_data.sd_EmptySlabs);

			__slab_data.sd_MaxSlabSize	= size;
			__slab_data.sd_InUse		= TRUE;
		}
	}
	else
	{
		#if defined(__amigaos4__)
		{
			__memory_pool = CreatePool(MEMF_PRIVATE,(ULONG)__default_pool_size,(ULONG)__default_puddle_size);
		}
		#else
		{
			/* There is no support for memory pools in the operating system
			 * prior to Kickstart 3.0 (V39).
			 */
			if(((struct Library *)SysBase)->lib_Version >= 39)
				__memory_pool = CreatePool(MEMF_ANY,(ULONG)__default_pool_size,(ULONG)__default_puddle_size);
		}
		#endif /* __amigaos4__ */
	}

	success = TRUE;

 out:

	SHOWVALUE(success);
	LEAVE();

	if(success)
		CONSTRUCTOR_SUCCEED();
	else
		CONSTRUCTOR_FAIL();
}
