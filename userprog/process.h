#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

void install_lazy_page (void *upage, struct file *file, off_t ofs, bool zero, bool writable);

#endif /* userprog/process.h */
