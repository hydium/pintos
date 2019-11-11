#ifndef USERPROG_PAGEDIR_H
#define USERPROG_PAGEDIR_H

#include <stdbool.h>
#include <stdint.h>
#include "filesys/file.h"

#define LAZY_FILE 0xf0000000
#define EVICTED_PAGE 0xe0000000

uint32_t *pagedir_create (void);
void pagedir_destroy (uint32_t *pd);
struct frame_table_entry * pagedir_set_page (uint32_t *pd, void *upage, void *kpage, bool writable, bool pin, bool is_file, off_t ofs, struct file *file);
void *pagedir_get_page (uint32_t *pd, const void *upage);
void pagedir_clear_page (uint32_t *pd, void *upage);
bool pagedir_is_dirty (uint32_t *pd, const void *upage);
void pagedir_set_dirty (uint32_t *pd, const void *upage, bool dirty);
bool pagedir_is_accessed (uint32_t *pd, const void *upage);
void pagedir_set_accessed (uint32_t *pd, const void *upage, bool accessed);
void pagedir_activate (uint32_t *pd);

// uint32_t *pagedir_set_lazy_page (uint32_t *pd, void *upage, off_t ofs, bool zero, bool writable);
uint32_t *lookup_page (uint32_t *pd, const void *vaddr, bool create);

uint32_t *active_pd (void);


#endif /* userprog/pagedir.h */
