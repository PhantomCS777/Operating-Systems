#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/device.h>
#include <linux/blk-mq.h>
#include <linux/list.h>
#include <linux/module.h>
// #include <linux/hdreg.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>

typedef unsigned long long ull;

#define SMART_SECTOR_SIZE 512
#define NUM_SECTORS 4096
#define DEFAULT_CACHE_SIZE (4 * 1024)

struct cache_entry {
    struct list_head list; 
    sector_t sector; 
    unsigned int size;
    void *buffer; 

    // unsigned long ref_count;
    atomic_t ref_count;
    struct work_struct work;     
    struct smartbdev *dev;
};

struct smartbdev {
    spinlock_t lock;
    struct request_queue *queue;
    struct blk_mq_tag_set tag_set;
    struct gendisk *gd;
    u8 *data; 
    struct list_head cache_list;
    size_t used_cache; 
    size_t cache_size; 
    spinlock_t cache_lock; 

 
    struct workqueue_struct *flush_wq;
   
    
    atomic_t write_count; 
    atomic_t cache_hits; 
    atomic_t flush_count; 

    
    struct proc_dir_entry *proc_dir;
    struct proc_dir_entry *proc_stats;
    struct proc_dir_entry *proc_flush;
    struct proc_dir_entry *proc_cache_size;
};


static int flush_all_cache(struct smartbdev *dev); 