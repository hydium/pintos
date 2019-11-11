#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <syscall.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/pte.h"

#include "userprog/process.h"
#include "vm/frame-table.h"
#include "vm/page.h"


static void syscall_handler (struct intr_frame *);


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void 
exit_helper (int status)
{
  int buf_size = strlen(thread_current()->name) + strlen(": exit(0)\n") + 5;
  char buf[buf_size];
  int true_size = snprintf(buf, buf_size, "%s: exit(%d)\n", thread_current()->name, status);
  putbuf(buf, true_size);

  if (thread_current()->holds_filesys_lock)
    lock_release(&filesys_lock);

  if (thread_current()->holds_page_lock)
    lock_release(&page_lock);

  lock_acquire(&filesys_lock);
  thread_current()->holds_filesys_lock = true;
  file_close(thread_current ()->file_ptr);
  lock_release(&filesys_lock);
  thread_current()->holds_filesys_lock = false;

  struct corpse *corpse = thread_current()->corpse_ptr;
  corpse->exit_status = status;
  sema_up(&corpse->sema);

  struct corpse *child_corpse = corpse_array;
  int i;

  

  for (i = 2; i < 128; i++)
    {
      if (thread_current()->opened_files[i] != NULL)
        { 
          lock_acquire(&filesys_lock);
          thread_current()->holds_filesys_lock = true;

          file_close(thread_current()->opened_files[i]);

          lock_release(&filesys_lock);
          thread_current()->holds_filesys_lock = false;
        }
    }

  for (i = 0; i < 256; i++)
    {
      if (child_corpse->parent_tid == thread_current()->tid)
        memset(child_corpse, 0, sizeof(struct corpse));
      child_corpse++;
      unmap_helper(i);
    }

  thread_exit();
}

static void
syscall_handler (struct intr_frame *f)
{
  int i;
  int pages_to_pin;
  unsigned ofs;
  uint32_t esp_addr;
  uint32_t *pte;
  bool stack_growth;
  char *buffer_page;
  uint32_t buffer_addr;
  struct frame_table_entry *frame;


  int *esp = (int *) f->esp;
  if (esp >= PHYS_BASE || pagedir_get_page(active_pd(), esp) == NULL)
    {
      exit_helper(-1);
      return;
    }
  int number = *esp;

  switch(number) 
  	{
      case SYS_HALT:
        shutdown_power_off();
        break;

      case SYS_EXIT:
      {
      	esp = esp + 1;
        int status = *esp;
        if ((esp + 1) >= PHYS_BASE || pagedir_get_page(active_pd(), esp) == NULL)
          status = -1;
        f->eax = status;
        exit_helper(status);
      	break;
      }

      case SYS_WRITE:
        // printf("write\n");
      	esp = esp + 1;
      	int fd = *esp;
      	esp = esp + 1;

        const char *buffer = *esp;
        esp = esp + 1;
        unsigned size = *esp;

        if (*esp >= PHYS_BASE || buffer + size >= PHYS_BASE || fd <= 0  || fd > 127 || thread_current()->opened_files[fd] == NULL || esp >= PHYS_BASE)
          exit_helper(-1);


        buffer_addr = buffer;
        esp_addr = f->esp;
        ofs = pg_ofs(buffer);
        pages_to_pin = (ofs + size - 1) / PGSIZE + 1;
        pte = lookup_page(thread_current()->pagedir, buffer, false);

        stack_growth = false;
        
        if ((buffer_addr >= esp_addr || esp_addr - buffer_addr <= 100) && (*pte & PTE_P) == 0)
          {
            stack_growth = true;
          }
        
        buffer_page = pg_round_down(buffer);

        thread_current()->holds_page_lock = true;
        lock_acquire(&page_lock);
        for (i = 0; i < pages_to_pin; i++)
          {
            char *address = buffer_page + (i * PGSIZE);
            pte = lookup_page(thread_current()->pagedir, address, false);
            if ((*pte & PTE_P) == 0)
              {
                if (stack_growth)
                  {
                    uint8_t *kpage = palloc_get_page (PAL_USER | PAL_ZERO);
                    pagedir_set_page (thread_current()->pagedir, address, kpage, true, true, false, 0, NULL);
                  }

                struct supp_table_entry *entry = search_supp_page_table(thread_current()->pagedir, address);
                
                if (entry == NULL)
                  exit_helper(-1);

                if (entry->is_file)
                  {
                    if (entry->zero)
                      {
                        uint8_t *kpage = palloc_get_page (PAL_USER | PAL_ZERO);
                        pagedir_set_page (thread_current()->pagedir, address, kpage, entry->writable, true, true, entry->ofs, entry->file);
                      }
                    else
                      { 
                        uint8_t *kpage = palloc_get_page (PAL_USER);
                        pagedir_set_page (thread_current()->pagedir, address, kpage, entry->writable, true, true, entry->ofs, entry->file);

                        lock_acquire(&filesys_lock);
                        thread_current()->holds_filesys_lock = true;
                        file_seek(thread_current()->file_ptr, entry->ofs);
                        int read = file_read (entry->file, kpage, PGSIZE);
                        lock_release(&filesys_lock);
                        thread_current()->holds_filesys_lock = false;
                        
                        memset(kpage + read, 0, PGSIZE - read);
                      }
                  }
                else
                  { 
                    uint32_t *kpage = palloc_get_page (PAL_USER);
                    pagedir_set_page (thread_current()->pagedir, address, kpage, true, true, entry->is_file, entry->ofs, entry->file);
                    fetch_swap_table(entry->swap_location, kpage);
                  }
                entry->is_valid = false;
              }
            else
              {
                frame = frame_table_search(thread_current()->pagedir, address);
                ASSERT(frame != NULL);
                frame->pin = true;
              }
          }
        lock_release(&page_lock);
        thread_current()->holds_page_lock = false;
      	
      	
      	if (fd == 1) 
          putbuf(buffer, size);
        else
          { 
            lock_acquire(&filesys_lock);
            thread_current()->holds_filesys_lock = true;
            f->eax = file_write(thread_current()->opened_files[fd], buffer, size);
            lock_release(&filesys_lock);
            thread_current()->holds_filesys_lock = false;
          }

        for (i = 0; i < pages_to_pin; i++)
          {
            char *address = buffer_page + (i * PGSIZE);
            frame = frame_table_search(thread_current()->pagedir, address);
            frame->pin = false;
          }
		    break;

      case SYS_EXEC:
        esp = esp + 1;
        if (*esp >= PHYS_BASE || pagedir_get_page(active_pd(), *esp) == NULL || (esp + 1) >= PHYS_BASE)
          exit_helper(-1);
        const char *file_name;
        file_name = *esp;
        f->eax = process_execute(file_name);
        break;

      case SYS_WAIT:
        esp = esp + 1;
        if (pagedir_get_page(active_pd(), esp) == NULL || (esp + 1) >= PHYS_BASE)
          exit_helper(-1);
        int pid = *esp;
        f->eax = process_wait(pid);
        break;

      case SYS_CREATE:
        esp = esp + 1;
        if (*esp >= PHYS_BASE || pagedir_get_page(active_pd(), *esp) == NULL || (esp + 2) >= PHYS_BASE)
          exit_helper(-1);
        const char *name;
        name = *esp;
        esp = esp + 1;
        int initial_size = *esp;
        lock_acquire(&filesys_lock);
        thread_current()->holds_filesys_lock = true;
        f->eax = filesys_create(name, initial_size);
        lock_release(&filesys_lock);
        thread_current()->holds_filesys_lock = false;
        break;

      case SYS_REMOVE:
        esp = esp + 1;
        if (*esp >= PHYS_BASE || pagedir_get_page(active_pd(), *esp) == NULL || (esp + 1) >= PHYS_BASE)
          exit_helper(-1);
        const char *file_to_remove = *esp;
        lock_acquire(&filesys_lock);
        thread_current()->holds_filesys_lock = true;
        f->eax = filesys_remove(file_to_remove);
        lock_release(&filesys_lock);
        thread_current()->holds_filesys_lock = false;
        break;

      case SYS_OPEN:
        esp = esp + 1;
        if (*esp >= PHYS_BASE || pagedir_get_page(active_pd(), *esp) == NULL)
          exit_helper(-1);
        const char *file_to_open;
        file_to_open = *esp;

        lock_acquire(&filesys_lock);
        thread_current()->holds_filesys_lock = true;
        struct file *opened_file = filesys_open(file_to_open);
        lock_release(&filesys_lock);
        thread_current()->holds_filesys_lock = false;

        if (opened_file == NULL)
          f->eax = -1;
        else 
          {
            int i;
            for (i = 2; i < 128; i++)
              {
                if (thread_current()->opened_files[i] == NULL)
                  {
                    thread_current()->opened_files[i] = opened_file;
                    f->eax = i;
                    break;
                  }
              }
          }
        break;

      case SYS_FILESIZE:
        esp = esp + 1;
        int file_descriptor = *esp; //fix
        lock_acquire(&filesys_lock);
        thread_current()->holds_filesys_lock = true;
        f->eax = file_length(thread_current()->opened_files[file_descriptor]);
        lock_release(&filesys_lock);
        thread_current()->holds_filesys_lock = false;
        break;

      case SYS_READ:
        // printf("read\n");
        esp = esp + 1;
        int fd_to_read = *esp;

        esp = esp + 1;
        char *buffer_to_read = *esp;

        esp = esp + 1;
        unsigned size_to_read = *esp;

        if (*esp >= PHYS_BASE || buffer_to_read + size_to_read >= PHYS_BASE || fd_to_read == 1  || fd_to_read < 0 || fd_to_read > 127 || thread_current()->opened_files[fd_to_read] == NULL)
          {
            // printf("read4\n");
            exit_helper(-1);
          }

        buffer_addr = buffer_to_read;
        esp_addr = f->esp;
        ofs = pg_ofs(buffer_to_read);
        pages_to_pin = (ofs + size_to_read - 1) / PGSIZE + 1;
        pte = lookup_page(thread_current()->pagedir, buffer_to_read, false);

        stack_growth = false;
        
        // printf("buffer_addr: %x\n", buffer_addr);
        // printf("esp_addr: %x\n", esp_addr);
        if ((buffer_addr >= esp_addr || esp_addr - buffer_addr <= 100)) //&& (*pte & PTE_P) == 0)
          {
            stack_growth = true;
          }
        
        buffer_page = pg_round_down(buffer_to_read);

        // printf("PAGES: %d\n", pages_to_pin);
        // printf("esp: %x\n", f->esp);

        thread_current()->holds_page_lock = true;
        lock_acquire(&page_lock);
        for (i = 0; i < pages_to_pin; i++)
          {
            // printf("i: %d\n", i);
            char *address = buffer_page + (i * PGSIZE);
            pte = lookup_page(thread_current()->pagedir, address, false);
            if ((*pte & PTE_P) == 1 && (*pte & PTE_W) == 0)
              {
                // printf("read5\n");
                exit_helper(-1);
              }
            if ((*pte & PTE_P) == 0)
              {
                if (stack_growth)
                  {
                    uint8_t *kpage = palloc_get_page (PAL_USER | PAL_ZERO);
                    pagedir_set_page (thread_current()->pagedir, address, kpage, true, true, false, 0, NULL);
                  }
                else 
                  {
                    struct supp_table_entry *entry = search_supp_page_table(thread_current()->pagedir, address);
                    if (entry == NULL)
                      {
                        // printf("read8\n");
                        exit_helper(-1);
                      }
                    if (entry->is_file)
                      {
                        if (!entry->writable)
                          {
                            // printf("read7\n");
                            exit_helper(-1);
                          }
                        if (entry->zero)
                          {
                            uint8_t *kpage = palloc_get_page (PAL_USER | PAL_ZERO);
                            pagedir_set_page (thread_current()->pagedir, address, kpage, entry->writable, true, true, entry->ofs, entry->file);
                          }
                        else
                          { 
                            uint8_t *kpage = palloc_get_page (PAL_USER);
                            pagedir_set_page (thread_current()->pagedir, address, kpage, entry->writable, true, true, entry->ofs, entry->file);

                            lock_acquire(&filesys_lock);
                            thread_current()->holds_filesys_lock = true;
                            file_seek(thread_current()->file_ptr, entry->ofs);
                            int read = file_read (entry->file, kpage, PGSIZE);
                            lock_release(&filesys_lock);
                            thread_current()->holds_filesys_lock = false;


                            memset(kpage + read, 0, PGSIZE - read);
                          }
                      }
                    else
                      { 
                        uint32_t *kpage = palloc_get_page (PAL_USER);
                        pagedir_set_page (thread_current()->pagedir, address, kpage, true, true, entry->is_file, entry->ofs, entry->file);
                        fetch_swap_table(entry->swap_location, kpage);
                      }
                    entry->is_valid = false;
                  }
              }
            else
              {
                frame = frame_table_search(thread_current()->pagedir, address);
                frame->pin = true;
              }
          }
        lock_release(&page_lock);
        thread_current()->holds_page_lock = false;
        
        
       
        if (fd_to_read == 0)
          input_getc(); // not complete
        else
          {
            lock_acquire(&filesys_lock);
            thread_current()->holds_filesys_lock = true;
            f->eax = file_read(thread_current()->opened_files[fd_to_read], buffer_to_read, size_to_read);
            lock_release(&filesys_lock);
            thread_current()->holds_filesys_lock = false;
          }
        for (i = 0; i < pages_to_pin; i++)
          {
            char *address = buffer_page + (i * PGSIZE);
            frame = frame_table_search(thread_current()->pagedir, address);
            frame->pin = false;
          }
        // printf("read2\n");

        break;

      case SYS_SEEK:
        esp = esp + 1;
        int fd_to_seek = *esp;
        if (fd_to_seek < 2  || fd_to_seek > 127 || thread_current()->opened_files[fd_to_seek] == NULL || (esp+2) >= PHYS_BASE)
          exit_helper(-1);
        esp = esp + 1;
        unsigned position = *esp;
        lock_acquire(&filesys_lock);
        thread_current()->holds_filesys_lock = true;
        file_seek(thread_current()->opened_files[fd_to_seek], position);
        lock_release(&filesys_lock);
        thread_current()->holds_filesys_lock = false;
        break;

      case SYS_TELL:
        esp = esp + 1;
        int fd_to_tell = *esp;
        if (fd_to_tell < 2  || fd_to_tell > 127 || thread_current()->opened_files[fd_to_tell] == NULL || (esp + 1) >= PHYS_BASE)
          exit_helper(-1);
        lock_acquire(&filesys_lock);
        thread_current()->holds_filesys_lock = true;
        f->eax = file_tell(thread_current()->opened_files[fd_to_tell]);
        lock_release(&filesys_lock);
        thread_current()->holds_filesys_lock = false;
        break;

      case SYS_CLOSE:
        esp = esp + 1;
        int fd_to_close = *esp;
        if (fd_to_close < 2 || fd_to_close > 127 || thread_current()->opened_files[fd_to_close] == NULL || (esp + 1) >= PHYS_BASE) 
          exit_helper(-1);

        lock_acquire(&filesys_lock);
        thread_current()->holds_filesys_lock = true;
        file_close(thread_current()->opened_files[fd_to_close]);
        lock_release(&filesys_lock);
        thread_current()->holds_filesys_lock = false;

        thread_current()->opened_files[fd_to_close] = NULL;
        break;

      case SYS_MMAP:
        esp = esp + 1;
        int fd_to_map = *esp;

        if (fd_to_map < 2 || fd_to_map > 127 || thread_current()->opened_files[fd_to_map] == NULL)
          {
            f->eax = -1;
            return;
          }

        struct file *file = file_reopen(thread_current()->opened_files[fd_to_map]);

        esp = esp + 1;

        char *addr = *esp;
        int addr_int = addr;
        
        uint32_t *pd = thread_current()->pagedir;

        lock_acquire(&filesys_lock);
        thread_current()->holds_filesys_lock = true;
        int f_length = file_length(file);
        lock_release(&filesys_lock);
        thread_current()->holds_filesys_lock = false;

        uint32_t number_of_pages = (f_length - 1) / PGSIZE + 1;

        int page_alignment_mask = 0x00000111;
        int i;
        bool is_pagable = true;

        if (addr_int == 0)
          {
            f->eax = -1;
            return;
          }
        if (f_length == 0)
          {
            f->eax = -1;
            return;
          }
        if ((addr_int & page_alignment_mask) != 0) 
          {
            f->eax = -1;
            return;
          }


        for (i = 0; i < number_of_pages; i++)
          { 
            if (!is_user_vaddr(addr + i * PGSIZE))
              {
                is_pagable = false;
                break;
              }
            pte = lookup_page(pd, addr + i * PGSIZE, false);
            if ((pte != NULL && *pte == LAZY_FILE) || (pte != NULL && (*pte & PTE_P) == 1)) //consider evicted page
              {
                is_pagable = false;
                break;
              }
          }
        
        if (!is_pagable)
          {
            f->eax = -1;
            return;
          }
        else 
          {
            for (i = 0; i < number_of_pages; i++)
              {
                install_lazy_page(addr + i * PGSIZE, file, i * PGSIZE, false, true);
                pagedir_set_dirty(pd, addr + i * PGSIZE, false);
              }


            struct mapping *mapping = thread_current()->mappings;
            for (i = 0; i < 256; i++)
              {
                if (mapping->file == NULL)
                  {
                    mapping->file = file;
                    mapping->start_addr = addr;
                    mapping->map_id = i;
                    mapping->number_of_pages = number_of_pages;
                    f->eax = i;
                    break;
                  }
                mapping++;
              }
          }
        break;

      case SYS_MUNMAP:

        esp = esp + 1;
        int map_id = *esp;
        if (map_id < 0 || map_id > 255)
          {
            f->eax = -1;
            return;
          }
        unmap_helper(map_id);
        break;
    }
}

void 
unmap_helper(int map_id)
{
  int i;
  
  struct mapping *mapping = thread_current()->mappings;
  
  mapping += map_id;

  if (mapping->number_of_pages == 0)
    return;

  struct file *file = mapping->file;

  char *addr = mapping->start_addr;

  thread_current()->holds_page_lock = true;
  lock_acquire(&page_lock);
  for (i = 0; i < mapping->number_of_pages; i++)
    {
      uint32_t *page = pagedir_get_page(thread_current()->pagedir, addr);

      if (pagedir_is_dirty(thread_current()->pagedir, addr) && page != NULL)
        { 
          lock_acquire(&filesys_lock);
          thread_current()->holds_filesys_lock = true;

          file_seek(file, i * PGSIZE);
          file_write(file, page, PGSIZE);

          lock_release(&filesys_lock);
          thread_current()->holds_filesys_lock = false;
          palloc_free_page(page);
        }
      if (pagedir_is_dirty(thread_current()->pagedir, addr) && page == NULL)
        {
          uint8_t *kpage = palloc_get_page(PAL_USER);
          struct supp_table_entry *entry = search_supp_page_table(thread_current()->pagedir, addr);
          fetch_swap_table(entry->swap_location, kpage);

          lock_acquire(&filesys_lock);
          thread_current()->holds_filesys_lock = true;

          file_seek(file, i * PGSIZE);
          file_write(file, kpage, PGSIZE);

          lock_release(&filesys_lock);
          thread_current()->holds_filesys_lock = false;
          palloc_free_page(kpage);
          entry->is_valid = false;
        }
      pagedir_clear_page(thread_current()->pagedir, addr);
      addr += PGSIZE;
    }
  lock_release(&page_lock);
  thread_current()->holds_page_lock = false;

  unmap_supp_table(thread_current()->pagedir, file);

  lock_acquire(&filesys_lock);
  thread_current()->holds_filesys_lock = true;
  file_close(mapping->file);
  lock_release(&filesys_lock);
  thread_current()->holds_filesys_lock = false;
  
  memset(mapping, 0, sizeof(struct mapping));
}
