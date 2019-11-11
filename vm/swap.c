#include "vm/swap.h"
#include "devices/block.h"
#include "threads/vaddr.h"
#include <bitmap.h>
#include "threads/synch.h"

#include "threads/init.h"

struct lock swap_lock;

void 
init_swap_table() 
{
	swap_block = block_get_role(BLOCK_SWAP);
	int num_of_swap_slots = BLOCK_SECTOR_SIZE * block_size(swap_block) / PGSIZE;
	swap_table = bitmap_create(num_of_swap_slots);
	lock_init(&swap_lock);
}

block_sector_t
insert_swap_table(void *page_)
{
	lock_acquire(&swap_lock);
	block_sector_t idx = bitmap_scan_and_flip(swap_table, 0, 1, false); //find 0 in bitmap
	lock_release(&swap_lock);

	if (idx == BITMAP_ERROR)
		PANIC("No swap slots!\n");

	idx = idx * 8;

	int i;
	char *page = page_; 
	for (i = 0; i < 8; i++)
		{
			lock_acquire(&swap_lock);
			const void *buffer = block_buffer;
			memcpy(buffer, page + i * BLOCK_SECTOR_SIZE, BLOCK_SECTOR_SIZE);
			block_write(swap_block, idx + i, buffer);
			lock_release(&swap_lock);
		}
	return idx;
}

void
fetch_swap_table(block_sector_t idx, void *page_)
{
	char *page = page_;
	ASSERT(idx % 8 == 0);
	bitmap_set(swap_table, idx / 8, false);
	int i;
	for (i = 0; i < 8; i++)
		{
			lock_acquire(&swap_lock);
			const void *buffer = block_buffer;
			block_read(swap_block, idx + i, buffer);
			memcpy(page + i * BLOCK_SECTOR_SIZE, buffer, BLOCK_SECTOR_SIZE);
			lock_release(&swap_lock);
		}
}