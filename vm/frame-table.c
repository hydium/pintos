#include "vm/frame-table.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/loader.h"
#include <stdint.h>

#include "vm/page.h"
#include "vm/swap.h"

static uint32_t count;
static uint32_t index;

#define DEBUG false

void init_frame_table(void)
{
	if (DEBUG)
		printf("number: %x\n", number_of_frames);
	number_of_frames = number_of_frames * 2;
	frame_table_start = (struct frame_table_entry *) palloc_get_multiple(PAL_ZERO | PAL_ASSERT, (number_of_frames * sizeof(struct frame_table_entry) - 1) / PGSIZE + 1);
	frame_table_idx = 0;
}

struct frame_table_entry *
add_to_frame_table(uint32_t *pd, uint32_t *upage, uint32_t *kpage, bool writable, bool pin, bool is_file, off_t ofs, struct file *file) 
{
	struct frame_table_entry *entry = frame_table_start;
	int i;
	bool found = false;
	for (i = 0; i < number_of_frames; i++)
		{
			if (entry->pd == 0)
				{
					found = true;
					break;
				}
			entry++;
		}
	if (!found)
		PANIC("add to frame table not found");
	entry->pin = pin;
	entry->upage = upage;
	entry->kpage = kpage;
	entry->pd = pd;
	entry->is_file = is_file;
	entry->writable = writable;
	entry->file = file;
	entry->ofs = ofs;
	entry->is_valid = true;

	return entry;
}


void 
evict_from_frame_table(void)
{
	// printf("lksmdaslkdma\n");
	if (DEBUG)
		{
			count++;
			printf("COUNT: %d\n", count);
		}
	// if (count == 157)
	// 	printf("NAME: %s\n", thread_current()->name);
	// 	hex_dump(0, 0xbffff000, PGSIZE, true);
	struct frame_table_entry *entry = frame_table_start + frame_table_idx;

	// if (count == 1)
		// {
			// printf("KJDKALSDAS\n");
			// int i;
			// char *ppage = 0x8048000;
			// for (i = 0; i < 4; i++)
			// 	{
			// 		putbuf(ppage + PGSIZE * i, PGSIZE);
			// 	}
			// hex_dump(0, 0xbffff000, PGSIZE, true);
		// }
	// printf("poisjdfmoif: %x\n", entry->pd);
	while(true)
		{
			// printf("poisjdfmoif: %x\n", entry->pd);
			// count++;
			// printf("COUNT: %d\n", count);
			// if (count == 7)
			// 	printf("VALID: %d\n", entry->is_valid);
			if (entry->is_valid)
				{
					if (!pagedir_is_accessed(entry->pd, entry->upage) && !pagedir_is_accessed(entry->pd, entry->kpage) && !entry->pin)	
						{

							
							// uint32_t *pte = lookup_page(entry->pd, entry->upage, false);
							// printf("ppppppppppp: %x\n", entry->pd);
							
							if (entry->is_file && !pagedir_is_dirty(entry->pd, entry->upage) && !pagedir_is_dirty(entry->pd, entry->kpage))
								{
									// printf("PDD: %x\n", entry->pd);
									insert_file_supp_page_table(entry->pd, entry->upage, entry->file, entry->ofs, entry->zero, entry->writable);
								}
							else
								{
									// printf("PDDD: %x\n", entry->pd);
									// printf("DIRTY UPAGE: %x\n", entry->upage);
									block_sector_t idx = insert_swap_table(entry->kpage);
									
									if (entry->upage == 0x804c000 && DEBUG)
										{
											index = idx;
											printf("index: %x\n", index);
										}
									insert_supp_page_table(entry->pd, entry->upage, entry->writable, idx);
								}
							pagedir_clear_page (entry->pd, entry->upage);
							palloc_free_page(entry->kpage);
							memset(entry, 0, sizeof(struct frame_table_entry));
							break;
						}
					else
						{
		        	pagedir_set_accessed (entry->pd, entry->upage, false);
		        	pagedir_set_accessed (entry->pd, entry->kpage, false);
		        }
		    }
      if (frame_table_idx < number_of_frames - 1)
     		frame_table_idx++;
     	else
     		frame_table_idx = 0;
     	entry = frame_table_start + frame_table_idx;
		}
}


void 
frame_table_clean(uint32_t *pd) 
{
	lock_acquire(&page_lock);
	struct frame_table_entry *entry = frame_table_start;
	int i;
	for (i = 0; i < number_of_frames; i++)
		{
			if (entry->pd == pd)
				memset(entry, 0, sizeof(struct frame_table_entry));
			entry++;
		}
	lock_release(&page_lock);
}

struct frame_table_entry *
frame_table_search(uint32_t *pd, uint32_t *upage)
{
	struct frame_table_entry *entry = frame_table_start;
	int i;
	for (i = 0; i < number_of_frames; i++)
		{
			if (entry->upage == upage && entry->pd == pd && entry->is_valid)
				return entry;
			entry++;
		}
	return NULL;
}