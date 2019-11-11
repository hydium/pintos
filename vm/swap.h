#include <bitmap.h>
#include "devices/block.h"

struct bitmap *swap_table;
struct block *swap_block;

void init_swap_table(void); 
block_sector_t insert_swap_table(void *page_);
void fetch_swap_table(block_sector_t idx, void *page_);