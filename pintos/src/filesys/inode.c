#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define CACHE_SIZE 32
/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[125];               /* Not used. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

struct cache_entry
  {
    block_sector_t sector;
    struct lock lock;
    int ref_count;
    bool write;
    char data[BLOCK_SECTOR_SIZE];
  };

struct block_cache
  {
    struct lock entry_num_lock;
    int entry_num;
    struct cache_entry cache_entrys[CACHE_SIZE];
  };

struct block_cache cache;


void cache_write(struct block *block, const block_sector_t sector, void *data);

void cache_init()
{
  cache.entry_num = 0;
  lock_init (&(cache.entry_num_lock));
  int i = 0;
  for( i = 0; i < CACHE_SIZE; i++)
    {
      cache.cache_entrys[i].sector = -1;
      cache.cache_entrys[i].ref_count = 0;
      lock_init(&(cache.cache_entrys[i].lock));
      cache.cache_entrys[i].write = false;
    }
}


bool cache_read(struct block *block, const block_sector_t sector, void *data)
{
  (char*)data;
  int i = 0;
  bool found = false;
  lock_acquire(&cache.entry_num_lock);
  for ( i = 0; i < cache.entry_num; i++)
    {
      lock_acquire(&(cache.cache_entrys[i].lock));
      if (cache.cache_entrys[i].sector == sector )
      {
        cache.cache_entrys[i].ref_count = 0;
        memcpy(data, cache.cache_entrys[i].data, BLOCK_SECTOR_SIZE);
        found = true;
      }
      else
      {
        cache.cache_entrys[i].ref_count+=1;
      }
      lock_release(&(cache.cache_entrys[i].lock));
    }
  lock_release(&cache.entry_num_lock);
  if (found)
  {
    return found;
  }

  lock_acquire(&cache.entry_num_lock);
  for (i = 0; i < cache.entry_num; i++)
    {
      lock_acquire(&(cache.cache_entrys[i].lock));
      cache.cache_entrys[i].ref_count -= 1;
      lock_release(&(cache.cache_entrys[i].lock));
    }
  lock_release(&cache.entry_num_lock);


  block_read (fs_device,sector,(void*)data);

  cache_write (fs_device,sector,data);
  return found;
}

void cache_read_bytes(fs_deive, const block_sector_t block, void *buff, int offset, int size )
{
  

}

void cache_write(struct block *block, const block_sector_t sector, void *data)
{
  
  (char*)data;
  int i = 0; 
  int replace = 0;
  int max_index = 0, max = 0;
  int found = 0;
  
  lock_acquire(&cache.entry_num_lock);
  for ( i = 0; i < cache.entry_num; i++)
  {
    lock_acquire (&(cache.cache_entrys[i].lock));
    if (cache.cache_entrys[i].sector == sector )
    {
      cache.cache_entrys[i].ref_count = 0;
      found = 1;
      replace = i;
    }
    else
    {
      cache.cache_entrys[i].ref_count+=1;
      if (cache.cache_entrys[i].ref_count > max)
      {
        max = cache.cache_entrys[i].ref_count;
        max_index = i;
      }
    }
    lock_release (&(cache.cache_entrys[i].lock));
  }
  lock_release(&cache.entry_num_lock);
  if (found)
  {
    lock_acquire(&(cache.cache_entrys[replace].lock));
    cache.cache_entrys[replace].sector = sector;
    cache.cache_entrys[replace].ref_count = 0;
    cache.cache_entrys[replace].write = true;
    memcpy(cache.cache_entrys[replace].data, data, BLOCK_SECTOR_SIZE);
    lock_release(&(cache.cache_entrys[replace].lock));
    return;
  }
  lock_acquire(&(cache.entry_num_lock));
  if (cache.entry_num < CACHE_SIZE)
  {
    lock_acquire(&(cache.cache_entrys[cache.entry_num].lock));
    cache.cache_entrys[cache.entry_num].sector = sector;
    cache.cache_entrys[cache.entry_num].ref_count = 0;
    cache.cache_entrys[cache.entry_num].write = true;
    memcpy(cache.cache_entrys[cache.entry_num].data, data, BLOCK_SECTOR_SIZE);
    lock_release(&(cache.cache_entrys[cache.entry_num].lock));
    cache.entry_num+=1;
  }
  lock_release(&(cache.entry_num_lock));
  if (cache.entry_num < CACHE_SIZE)
  return;

  lock_acquire(&(cache.entry_num_lock));
  if (cache.entry_num == CACHE_SIZE)
  {
    lock_acquire(&(cache.cache_entrys[max_index].lock));
    if (cache.cache_entrys[max_index].write)
    {
      block_write(fs_device,cache.cache_entrys[max_index].sector, \
      cache.cache_entrys[max_index].data);
    }
    cache.cache_entrys[max_index].sector = sector;
    cache.cache_entrys[max_index].ref_count = 0;
    cache.cache_entrys[max_index].write = true;
    memcpy(cache.cache_entrys[max_index].data, data, BLOCK_SECTOR_SIZE);
    lock_release(&(cache.cache_entrys[max_index].lock));
  }
  lock_release(&(cache.entry_num_lock));
}


void cache_sync()
{
  int i = 0;
  lock_acquire(&cache.entry_num_lock);
  for (i = 0; i < cache.entry_num ; i++)
  {
    lock_acquire(&(cache.cache_entrys[i].lock));
    block_write(fs_device,cache.cache_entrys[i].sector,cache.cache_entrys[i].data);
    lock_release(&(cache.cache_entrys[i].lock));
  }
  lock_release(&cache.entry_num_lock);
};


/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
//need re-implement because it assumes that we have continuou allocated disk space
//or just dispose it
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
  cache_init();
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;

      // here we need to refractor `free_map_allocate ` which only allocate consecutive 
      // free space however we could reuse the function to `bitmap_scan_and_flip` to allocate one block
      // by call this function like  this bitmap_scan_and_flip (free_map, 0, 1, false)
      if (free_map_allocate (sectors, &disk_inode->start))
        {
          cache_write (fs_device, sector, disk_inode);
          if (sectors > 0)

            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;

              for (i = 0; i < sectors; i++)
                cache_write (fs_device, disk_inode->start + i, zeros);
            }
          success = true;
        }
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */

// need cache   
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  cache_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_map_release (inode->sector, 1);
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length));
        }

      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  //uint8_t *bounce = NULL;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          cache_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          cache_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          cache_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else
        {
          /* We need a bounce buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left)
            cache_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          cache_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
