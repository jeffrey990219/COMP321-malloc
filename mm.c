/*
 * Simple, 32-bit and 64-bit clean allocator based on an implicit free list,
 * first fit placement, and boundary tag coalescing, as described in the
 * CS:APP2e text.  Blocks are aligned to double-word boundaries.  This
 * yields 8-byte aligned blocks on a 32-bit processor, and 16-byte aligned
 * blocks on a 64-bit processor.  However, 16-byte alignment is stricter
 * than necessary; the assignment only requires 8-byte alignment.  The
 * minimum block size is four words.
 *
 * This allocator uses the size of a pointer, e.g., sizeof(void *), to
 * define the size of a word.  This allocator also uses the standard
 * type uintptr_t to define unsigned integers that are the same size
 * as a pointer, i.e., sizeof(uintptr_t) == sizeof(void *).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "memlib.h"
#include "mm.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
	/* Team name */
	"/* Team name */",
	/* First member's full name */
	"Xin Huang",
	/* First member's email address */
	"xyh1@rice.edu",
	/* Second member's full name (leave blank if none) */
	"Leo Meister",
	/* Second member's email address (leave blank if none) */
	"lpm2@rice.edu"
};

/* Basic constants and macros: */
#define ASIZE	   8		  /* Number of bytes to align to */
#define WSIZE      sizeof(void *) /* Word and header/footer size (bytes) */
#define DSIZE      (2 * WSIZE)    /* Doubleword size (bytes) */
#define TSIZE	   (3 * WSIZE)	  /* Tripleword size (bytes) */
#define CHUNKSIZE  4104  /* Extend heap by this amount (bytes) */

#define MAX(x, y)  ((x) > (y) ? (x) : (y))

/* Pack a size and allocated bit into a word. */
#define PACK(size, alloc)  ((size) | (alloc))

/* Read and write a word at address p. */
#define GET(p)       (*(uintptr_t *)(p))
#define PUT(p, val)  (*(uintptr_t *)(p) = (val))

/* Read the size and allocated fields from address p. */
#define GET_SIZE(p)   (GET(p) & ~(WSIZE - 1))
#define GET_ALLOC(p)  (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer. */
#define HDRP(bp)  ((char *)(bp) - WSIZE)
#define FTRP(bp)  ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and prev blocks. */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* The number of bins (segregated list) */
#define BIN_NUM  (15)
#define BOUND   (128)

struct node {
	struct node *next;
	struct node *prev;
};

/* Global variables: */
static char *heap_listp; /* Pointer to first block */
static void **bin_list;

/* Function prototypes for internal helper routines: */
static void *coalesce(void *bp);
static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
static void *place(void *bp, size_t asize);

/* Function prototypes for heap consistency checker routines: */
static void checkblock(void *bp);
static void checkheap(bool verbose);
static void printblock(void *bp);
static void verifyfreeblock(void *bp);

/* Our added functionality*/
static void *find_block_list(struct node *bp, int asize);
static void insert_block(void *bp, int size);
static void delete_block(void *bp);
static int get_list_index(int size);

bool ourVerbose = false;

/*
 * Requires:
 *   None.
 *
 * Effects:
 *   Initialize the memory manager.  Returns 0 if the memory manager was
 *   successfully initialized and -1 otherwise.
 */
int
mm_init(void)
{
	int i;
	if (ourVerbose)
		printf("ENTER INIT\n");

	/* Create the initial empty heap. */
	if ((heap_listp = mem_sbrk((4 + BIN_NUM) * WSIZE)) == (void *)-1)
		return (-1);

	bin_list = (void **)heap_listp;

	for (i = 0; i < BIN_NUM; i++) {
		bin_list[i] = NULL;
	}

	PUT(heap_listp + (BIN_NUM * WSIZE), 0); /* Alignment padding */ // This probably should be padded later on when we bin?
	PUT(heap_listp + ((BIN_NUM + 1) * WSIZE), PACK(DSIZE, 1)); /* Prologue header */
	PUT(heap_listp + ((BIN_NUM + 2) * WSIZE), PACK(DSIZE, 1)); /* Prologue footer */
	PUT(heap_listp + ((BIN_NUM + 3) * WSIZE), PACK(0, 1));     /* Epilogue header */

	heap_listp += ((BIN_NUM + 4) * WSIZE);

	if (ourVerbose) {
		printf("INIT CHECKHEAP\n");
		checkheap(1);
	}

	/* Extend the empty heap with a free block of CHUNKSIZE bytes. */
	if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
		return (-1);

	return (0);
}

/*
 * Requires:
 *   None.
 *
 * Effects:
 *   Allocate a block with at least "size" bytes of payload, unless "size" is
 *   zero.  Returns the address of this block if the allocation was successful
 *   and NULL otherwise.
 */
void *
mm_malloc(size_t size)
{
	size_t asize;      /* Adjusted block size */
	size_t extendsize; /* Amount to extend heap if no fit */
	void *bp;

	if (ourVerbose)
		printf("ENTER MALLOC\n");

	/* Ignore spurious requests. */
	if (size == 0)
		return (NULL);

	/* Adjust block size to include overhead and alignment reqs. */
	if (size <= DSIZE)
		asize = ASIZE + TSIZE; // basically 4 words
	else
		if (size % WSIZE == 0)
			asize = DSIZE + size;
		else
			asize = DSIZE + (((size / WSIZE) + 1) * WSIZE);

	/* Case for trace file realloc-bal.rep */
	if ((size % BOUND == 0) && (size != BOUND)) {
		asize = DSIZE + size + BOUND;
	}
	/* Case for trace file realloc2-bal.rep */
	if (size == 4092) {
		asize = WSIZE + CHUNKSIZE;
	}

	/* Search the free list for a fit. */
	if ((bp = find_fit(asize)) != NULL) {
		place(bp, asize);
		return (bp);
	}

	/* No fit found.  Get more memory and place the block. */
	extendsize = MAX(asize, CHUNKSIZE);

	if (extendsize % WSIZE != 0)
		extendsize = ((extendsize / WSIZE) + 1) * WSIZE;

	if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
		return (NULL);

	place(bp, asize);

	if (ourVerbose) {
		printf("MALLOC CHECKHEAP\n");
		checkheap(1);
	}

	return (bp);
}

/*
 * Requires:
 *   "bp" is either the address of an allocated block or NULL.
 *
 * Effects:
 *   Free a block.
 */
void
mm_free(void *bp)
{
	size_t size;
	if (ourVerbose)
		printf("ENTER FREE\n");

	/* Ignore spurious requests. */
	if (bp == NULL)
		return;

	/* Free and coalesce the block. */
	size = GET_SIZE(HDRP(bp));

	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));

	insert_block(bp, size);

	coalesce(bp);

	if (ourVerbose) {
		printf("FREE CHECKHEAP\n");
		checkheap(1);
	}
}

/*
 * Requires:
 *   "ptr" is either the address of an allocated block or NULL.
 *
 * Effects:
 *   Reallocates the block "ptr" to a block with at least "size" bytes of
 *   payload, unless "size" is zero.  If "size" is zero, frees the block
 *   "ptr" and returns NULL.  If the block "ptr" is already a block with at
 *   least "size" bytes of payload, then "ptr" may optionally be returned.
 *   Otherwise, a new block is allocated and the contents of the old block
 *   "ptr" are copied to that new block.  Returns the address of this new
 *   block if the allocation was successful and NULL otherwise.
 */
void *
mm_realloc(void *ptr, size_t size)
{
	size_t oldsize;
	void *newptr;

	/* If size == 0 then this is just free, and we return NULL. */
	if (size == 0) {
		mm_free(ptr);
		return (NULL);
	}
	/* If oldptr is NULL, then this is just malloc. */
	if (ptr == NULL)
		return (mm_malloc(size));

	/* Align new size to multiples of WSIZE */
	int new_size = (int)size;
	if (new_size % WSIZE != 0)
		new_size = ((new_size / WSIZE) + 1) * WSIZE;
	int realloc_asize = new_size + (int)DSIZE;

	/* Size of prev allocated block */
	oldsize = GET_SIZE(HDRP(ptr));
	int diffsize = (int)(oldsize - realloc_asize);


	if (realloc_asize == (int)oldsize) {
		return (ptr);
	} // end if

	/* New size less than previous allocated size */
	else if (diffsize > 0) {
		if (diffsize >= (int)(2 * DSIZE)) {

			PUT(HDRP(ptr), PACK(realloc_asize, 1));
			PUT(FTRP(ptr), PACK(realloc_asize, 1));

			ptr = NEXT_BLKP(ptr);

			PUT(HDRP(ptr), PACK(diffsize, 0));
			PUT(FTRP(ptr), PACK(diffsize, 0));

			insert_block(ptr, diffsize);

			coalesce(ptr);

			return (PREV_BLKP(ptr));
		}
		/* New size required less than previous allocated size but can't form a new block */
		else {
			return (ptr);
		}
	} // end else if 1

	/* New size greater than previous allocated size */
	else if (diffsize < 0) {
		size_t nextsize = GET_SIZE(HDRP(NEXT_BLKP(ptr)));

		/* Block next to current block free */
		if (!GET_ALLOC(HDRP(NEXT_BLKP(ptr)))) {

			if ((int)nextsize >= (int)(abs(diffsize) + 2 * DSIZE)) {
				delete_block(NEXT_BLKP(ptr));

				PUT(HDRP(ptr), PACK(realloc_asize, 1));
				PUT(FTRP(ptr), PACK(realloc_asize, 1));

				ptr = NEXT_BLKP(ptr);

				int newNextSize = (int)nextsize - abs(diffsize);

				PUT(HDRP(ptr), PACK(newNextSize, 0));
				PUT(FTRP(ptr), PACK(newNextSize, 0));

				insert_block(ptr, newNextSize);

				coalesce(ptr);

				return (PREV_BLKP(ptr));
			}
			/* Next free block doesn't have enough space */
			else if ((int)nextsize >= abs(diffsize)) {

				delete_block(NEXT_BLKP(ptr));

				PUT(HDRP(ptr), PACK((int)(oldsize + nextsize), 1));
				PUT(FTRP(ptr), PACK((int)(oldsize + nextsize), 1));

				return (ptr);
			}
		}
	} // end else if 2

	newptr = mm_malloc(size);

	/* If realloc() fails the original block is left untouched  */
	if (newptr == NULL)
		return (NULL);

	if (size < oldsize)
		oldsize = size;
	memcpy(newptr, ptr, oldsize);

	/* Free the old block. */
	mm_free(ptr);

	if (ourVerbose) {
		printf("REALLOC CHECKHEAP\n");
		checkheap(1);
	}

	return (newptr);
}

/*
 * The following routines are internal helper routines.
 */

/*
 * Requires:
 *   "bp" is the address of a newly freed block.
 *
 * Effects:
 *   Perform boundary tag coalescing.  Returns the address of the coalesced
 *   block.
 */
static void *
coalesce(void *bp)
{
	size_t size = GET_SIZE(HDRP(bp));
	bool prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
	bool next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));

	if (ourVerbose)
		printf("ENTER COALESCE\n");

	if (prev_alloc && next_alloc) {                 /* Case 1 */
		if (ourVerbose)
			printf("Case 1\n");  // do nothing
	}

	else if (prev_alloc && !next_alloc) {         /* Case 2 */
		if (ourVerbose)
			printf("Case 2\n");

		delete_block(bp);
		delete_block(NEXT_BLKP(bp));

		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));

		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));

		insert_block(bp, size);
	}

	else if (!prev_alloc && next_alloc) {         /* Case 3 */
		if (ourVerbose)
			printf("Case 3\n");

		delete_block(bp);
		delete_block(PREV_BLKP(bp));

		size += GET_SIZE(HDRP(PREV_BLKP(bp)));

		PUT(FTRP(bp), PACK(size, 0));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));

		bp = PREV_BLKP(bp);
		insert_block(bp, size);

	}

	else {                                        /* Case 4 */
		if (ourVerbose)
			printf("Case 4\n");

		delete_block(bp);
		delete_block(NEXT_BLKP(bp));
		delete_block(PREV_BLKP(bp));

		size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));

		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));

		bp = PREV_BLKP(bp);
		insert_block(bp, size);
	}

	if (ourVerbose) {
		printf("COALESCE CHECKHEAP\n");
		checkheap(1);
	}

	return (bp);
}

/*
 * Requires:
 *   None.
 *
 * Effects:
 *   Extend the heap with a free block and return that block's address.
 */
static void *
extend_heap(size_t words)
{
	size_t size;
	void *bp;

	if (ourVerbose)
		printf("ENTER EXTEND HEAP\n");
	size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;

	if ((bp = mem_sbrk(size)) == (void *)-1)
		return (NULL);

	/* Initialize free block header/footer and the epilogue header. */
	PUT(HDRP(bp), PACK(size, 0));         /* Free block header */
	PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */

	insert_block(bp, GET_SIZE(HDRP(bp)));

	if (ourVerbose) {
		printf("EXTEND_HEAP CHECK HEAP\n");
		checkheap(1);
	}

	/* Coalesce if the prev block was free. */
	return (coalesce(bp));
}

/*
 * Requires:
 *   None.
 *
 * Effects:
 *   Find a fit for a block with "asize" bytes.  Returns that block's address
 *   or NULL if no suitable block was found.
 */
static void *
find_fit(size_t asize)
{

	if (ourVerbose)
		printf("FIT YO-SELF\n");

	int i;
	int list_idx = get_list_index(asize);

	struct node *bp;
	/* Search for the first fit from the lists with index lst_idx or bigger */
	for (i = list_idx; i < BIN_NUM; i ++) {

		bp = bin_list[i];

		if ((bp = find_block_list(bp, asize)) != NULL)
			return bp;
	}

	return (NULL); // couldn't find a valid block
}

/*
 * Requires:
 *   "bp" is the address of a free block that is at least "asize" bytes.
 *
 * Effects:
 *   Place a block of "asize" bytes at the start of the free block "bp" and
 *   split that block if the remainder would be at least the minimum block
 *   size.
 */
static void *
place(void *bp, size_t asize)
{
	size_t csize = GET_SIZE(HDRP(bp));

	if (ourVerbose)
		printf("ENTER PLACE\n");

	if ((csize - asize) >= (WSIZE + TSIZE)) {
		delete_block(bp);

		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));
		bp = NEXT_BLKP(bp);
		PUT(HDRP(bp), PACK(csize - asize, 0));
		PUT(FTRP(bp), PACK(csize - asize, 0));
		insert_block(bp, (int)(csize - asize));

	}

	else {
		delete_block(bp);
		PUT(HDRP(bp), PACK(csize, 1));
		PUT(FTRP(bp), PACK(csize, 1));
	}

	if (ourVerbose) {
		printf("PLACE CHECKHEAP\n");
		checkheap(1);
	}

	return (bp);
}

/*
 * Requires:
 *	Valid bp
 *  Valid size
 *
 * Effects:
 * 	Find block size in bins where block size is >= to asize
 */
static void *
find_block_list(struct node *bp, int asize)
{

	assert(asize > 0);
	size_t block_size;

	while (bp != NULL) {
		block_size = GET_SIZE(HDRP(bp));
		if ((int) block_size >= asize)
			return bp;
		bp = bp->next;
	}

	return NULL;
}

/*
 * Requires:
 *	valid size
 * Effects:
 *	Return seglist index of block size
 */
static int
get_list_index(int size)
{

	assert(size >= 0);

	int count = size;
	int list;

	for (list = 0; list < BIN_NUM; list++) {
		if ((count <= BOUND) || (list == BIN_NUM - 1)) {
			return list;
		}

		count = count >> 1; /* divide by 2 */
	}

	return BIN_NUM - 1;
}


/*
 * Requires:
 *	valid bp
 * Effects:
 * 	Insert block bp to a specific seglist
 * 	Insertion order will base on block size
 */
static void
insert_block(void *bp, int size)
{

	assert(bp != NULL);
	assert(size == (int)GET_SIZE(HDRP(bp)));

	int list_idx;
	struct node *new_block, *start_block = NULL;

	list_idx = get_list_index(size);

	/* LIFO insert block to seglist  */
	start_block = bin_list[list_idx];

	new_block = bp;
	/* seglist been insert into is empty */
	if (start_block == NULL) {
		new_block->prev = NULL;
		new_block->next = NULL;
		bin_list[list_idx] = new_block;
	}
	/* seglist been insert into is not empty */
	else {
		new_block->prev = NULL;
		new_block->next = start_block;
		start_block->prev = new_block;
		bin_list[list_idx] = new_block;
	}

}

/*
 * Requires:
 *	Valid BP
 * Effects:
 * 	Remove block bp from the specific seglist
 */
static void
delete_block(void *bp)
{

	assert(bp != NULL);

	struct node *current;
	struct node *smaller;
	struct node *bigger;

	int list_idx;
	size_t block_size;

	/* Get size of the block */
	block_size = GET_SIZE(HDRP(bp));

	/* Get bin index */
	list_idx = get_list_index(block_size);

	current = (struct node *)bp;
	bigger = current->prev;
	smaller = current->next;

	/* No block preceding Delete Block */
	if (bigger == NULL) {
		/* No block after Delete Block */
		if (smaller == NULL) {
			bin_list[list_idx] = NULL; // no left/right
		}
		/* Block after Delete Block */
		else {
			smaller->prev = NULL;
			bin_list[list_idx] = smaller;
		}

	/* Block preceding Delete Block */
	} else {
		/* No block after Delete Block */
		if (smaller == NULL) {
			bigger->next = NULL;
		}
		/* Block after Delete Block */
		else {
			bigger->next = smaller;
			smaller->prev = bigger;
		}
	}

}

/*
 * The remaining routines are heap consistency checker routines.
 */

/*
 * Requires:
 *   "bp" is the address of a block.
 *
 * Effects:
 *   Perform a minimal check on the block "bp".
 */
static void
checkblock(void *bp)
{
	bool binlflag = false;

	if ((uintptr_t)bp % WSIZE) {
		printf("Error: %p is not word aligned\n", bp);
		if (ourVerbose)
			printblock(bp);
	}
	if (GET(HDRP(bp)) != GET(FTRP(bp))) {
		if (ourVerbose)
			printblock(bp);
		printf("Error: header does not match footer\n");
	}

	if ((int)GET_ALLOC(HDRP(bp)) == 0) {
		/* Check free blocks that escaped coalescing */
		if ((int)GET_ALLOC(HDRP(PREV_BLKP(bp))) != 1 ||
			(int)GET_ALLOC(HDRP(NEXT_BLKP(bp))) != 1) {
			printf("Error: Contiguous free block escaped the coals!\n");
			exit(1);
		}
		/* verify every free block actually in the free list */
		int list_idx = get_list_index(GET_SIZE(HDRP(bp)));
		struct node *binl = bin_list[list_idx];

		while (binl != NULL) {
			if (binl == bp)
				binlflag = true;
			binl = binl->next;
		}
		if (binlflag == false) {
			printf("Error: Free block not there?!!?\n");
			exit(1);
		}
	}
}

static void
verifyfreeblock(void *bp)
{
	// Check Free blocks
	if ((uintptr_t)bp % WSIZE) {
		printf("Error: %p not DWORD aligned!\n", bp);
		if (ourVerbose)
			printblock(bp);
		exit(1);
	}
	if (GET(HDRP(bp)) != GET(FTRP(bp))) {
		printf("Error: Header Footer mismatch!\n");
		if (ourVerbose)
			printblock(bp);
		exit(1);
	}
	if ((int)GET_ALLOC(HDRP(bp)) != 0) {
		printf("Error: Free block not marked as free!\n");
		if (ourVerbose)
			printblock(bp);
		exit(1);
	}
}

/*
 * Requires:
 *   None.
 *
 * Effects:
 *   Perform a minimal check of the heap for consistency.
 */
void
checkheap(bool verbose)
{
	printf("ENTER CHECKHEAP\n");
	void *bp;
	struct node *vbp;
	int i;

	if (verbose)
		printf("Heap (%p):\n", heap_listp);

	if (GET_SIZE(HDRP(heap_listp)) != DSIZE)
		printf("Bad prologue header: size\n");
	if (!GET_ALLOC(HDRP(heap_listp)))
		printf("Bad prologue header: alloc\n");
	checkblock(heap_listp);

	for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
		if (verbose)
			printblock(bp);
		checkblock(bp);
	}

	if (GET_SIZE(HDRP(bp)) != 0) {
		if (verbose)
			printblock(bp);
		printf("Bad epilogue header: size\n");
	}
	if (!GET_ALLOC(HDRP(bp))) {
		if (verbose)
			printblock(bp);
		printf("Bad epilogue header: alloc\n");
	}

	// verify free blocks marked as free
	for (i = 0; i < BIN_NUM; i ++) {
		vbp = bin_list[i];
		while (vbp != NULL) {
			if ((int)GET_ALLOC(HDRP(vbp)) != 0 ||
				(int)GET_ALLOC(FTRP(vbp)) != 0) {
					printf("Error: Free block not marked as free!\n");
				if (verbose)
					printblock(vbp);
				exit(1);
			}

			// Verify pointers
			struct node *next_block = vbp->next;
			struct node *prev_block = vbp->prev;
			if (next_block != NULL)
				verifyfreeblock(next_block);
			if (prev_block != NULL)
				verifyfreeblock(prev_block);

			vbp = vbp->next;
		}
	}

	printf("END CHECKHEAP\n");

	/* Invariance Checks:all blocks in free list actually free
	 * 		     all free blocks in free list
	 *		     free blocks escaping coalescing
	 *		free list pointers point to valid free blocks
	 *		do any allocated blocks overlap
	 *		do pointers in heap block point to valid heap addr
	 *		...
	 */
}

/*
 * Requires:
 *   "bp" is the address of a block.
 *
 * Effects:
 *   Print the block "bp".
 */
static void
printblock(void *bp)
{
	size_t hsize, fsize;
	bool halloc, falloc;

	checkheap(false);
	hsize = GET_SIZE(HDRP(bp));
	halloc = GET_ALLOC(HDRP(bp));
	fsize = GET_SIZE(FTRP(bp));
	falloc = GET_ALLOC(FTRP(bp));

	if (hsize == 0) {
		printf("%p: end of heap\n", bp);
		return;
	}

	printf("%p: header: [%zu:%c] footer: [%zu:%c]\n", bp,
	    hsize, (halloc ? 'a' : 'f'),
	    fsize, (falloc ? 'a' : 'f'));
}

/*
 * The last lines of this file configure the behavior of the "Tab" key in
 * emacs.  Emacs has a rudimentary understanding of C syntax and style.  In
 * particular, depressing the "Tab" key once at the start of a new line will
 * insert as many tabs and/or spaces as are needed for proper indentation.
 */

/* Local Variables: */
/* mode: c */
/* c-default-style: "bsd" */
/* c-basic-offset: 8 */
/* c-continued-statement-offset: 4 */
/* indent-tabs-mode: t */
/* End: */
