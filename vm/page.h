#include "devices/block.h"
#include <stdbool.h>
#include "filesys/off_t.h"


struct supp_table_entry 
	{
		bool is_valid;
		bool zero;
		bool is_file;
		bool writable;
		uint32_t *upage;
		struct file *file;
		block_sector_t swap_location;
		void *pd;
		off_t ofs;
	};

struct supp_table_entry *supp_page_table;

void init_supp_page_table(void);
struct supp_table_entry * search_supp_page_table(uint32_t *pd, uint32_t *upage);
void insert_supp_page_table(void *pd, uint32_t *upage, bool writable, block_sector_t swap_location);
void insert_file_supp_page_table(void *pd, uint32_t *upage, struct file *file, off_t ofs, bool zero, bool writable);
void clean_supp_page_table(void *pd);
void unmap_supp_table(void *pd, struct file *file);