#include "vm/page.h"
#include "threads/palloc.h"
#include "threads/loader.h"
#include "threads/vaddr.h"
#include "vm/frame-table.h"
#include "vm/swap.h"

static int number_of_slots;
struct lock supp_lock;

void 
init_supp_page_table() 
{
  lock_init(&supp_lock);
	int num_of_swap_slots = 512 * block_size(swap_block)/4096;
	struct block *filesys_block = block_get_role(BLOCK_FILESYS);
	int num_of_filesys_slots = 512 * block_size(filesys_block)/4096;
	number_of_slots = (num_of_swap_slots + num_of_filesys_slots) * 3;
  printf("NUMBER OF SLOTS : %d\n", number_of_slots * sizeof(struct supp_table_entry) / PGSIZE + 1);
	supp_page_table = (struct supp_table_entry *) palloc_get_multiple(PAL_ZERO | PAL_ASSERT, number_of_slots * sizeof(struct supp_table_entry) / PGSIZE + 1);
}

struct supp_table_entry *
search_supp_page_table(uint32_t *pd, uint32_t *upage)
{
	int i;
  bool found = false;
	struct supp_table_entry *entry = supp_page_table;
	for (i = 0; i < number_of_slots; i++)
		{
			if (upage == entry->upage && entry->pd == pd && entry->is_valid)
        {
          found = true;
          break;
        }
			entry++;
		}

  if (!found)
    return NULL;


	// entry->is_valid = false;
	return entry;
}

void 
clean_supp_page_table(void *pd)
{
  int i;
  struct supp_table_entry *entry = supp_page_table;
  for (i = 0; i < number_of_slots; i++)
    {
      if (entry->pd == pd)
        entry->is_valid = false;
      entry++;
    }
}


void 
insert_supp_page_table(void *pd, uint32_t *upage, bool writable, block_sector_t swap_location)
{
	int i;
	struct supp_table_entry *entry = supp_page_table;
  bool found = false;

  lock_acquire(&supp_lock);
	for (i = 0; i < number_of_slots; i++)
		{
			if (!entry->is_valid)
        {
          found = true;
				  break;
        }
			entry++;
		}
  if (!found)
    PANIC("no supp table slots!");

	entry->upage = upage;
	entry->pd = pd;
	entry->is_valid = true;
  entry->is_file = false;
  entry->writable = writable;
  entry->file = NULL;
  entry->zero = false;
  entry->ofs = 0;
  entry->swap_location = swap_location;

  lock_release(&supp_lock);
}

void 
insert_file_supp_page_table(void *pd, uint32_t *upage, struct file *file, off_t ofs, bool zero, bool writable)
{
  lock_acquire(&supp_lock);
  int i;
  struct supp_table_entry *entry = supp_page_table;
  bool found;
  for (i = 0; i < number_of_slots; i++)
    {
      if (!entry->is_valid)
        {
          found = true;
          break;
        }
      entry++;
    }


  if (!found)
    PANIC("no supp table slots!");

  entry->file = file;
  entry->upage = upage;
  entry->pd = pd;
  entry->zero = zero;
  entry->ofs = ofs;
  entry->writable = writable;
  entry->is_file = true;
  entry->is_valid = true;
  lock_release(&supp_lock);
}

void 
unmap_supp_table(void *pd, struct file *file)
{
  int i;
  struct supp_table_entry *entry = supp_page_table;
  for (i = 0; i < number_of_slots; i++)
    {
      if (entry->is_valid && entry->is_file && entry->file == file)
        entry->is_valid = false;
      entry++;
    }
}

