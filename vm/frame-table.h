#ifndef VM_FRAME_TABLE_H
#define VM_FRAME_TABLE_H

#include "threads/thread.h"

struct frame_table_entry *frame_table_start;
uint32_t frame_table_idx;
size_t number_of_frames;

struct frame_table_entry 
	{
		bool is_valid;
		bool pin;
		bool zero;
		bool is_file;
		bool writable;
		void *kpage;
		uint32_t *upage;
		struct file *file;
		uint32_t *pd;
		off_t ofs;
	};

void init_frame_table(void);
struct frame_table_entry * add_to_frame_table(uint32_t *pd, uint32_t *upage, uint32_t *kpage, bool writable, bool pin, bool is_file, off_t ofs, struct file *file);
void frame_table_clean(uint32_t *pd);
void evict_from_frame_table(void);
struct frame_table_entry *frame_table_search(uint32_t *pd, uint32_t *upage);

#endif