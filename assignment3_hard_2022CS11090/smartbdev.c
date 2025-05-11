#include "smartbdev.h" 

static int smartbdev_major = 240;     
static size_t cache_size = DEFAULT_CACHE_SIZE; 

module_param(cache_size, ulong, S_IRUGO);
MODULE_PARM_DESC(cache_size, "Size of write cache in bytes");
MODULE_LICENSE("GPL");

static struct smartbdev main_dev; 
static struct smartbdev* smartbdev = &main_dev; 


static void flush_cache_entry(struct smartbdev *dev, struct cache_entry *entry)
{
    unsigned long pos = entry->sector << SECTOR_SHIFT;
    unsigned long flags;
   
    if (pos + entry->size <= NUM_SECTORS * SMART_SECTOR_SIZE) {
        spin_lock_irqsave(&dev->lock, flags);
        memcpy(dev->data + pos, entry->buffer, entry->size);
        spin_unlock_irqrestore(&dev->lock, flags);
        atomic_inc(&dev->write_count);
    } else {
        pr_err("smartbdev: Cache entry beyond device size\n");
    }
}


static void flush_entry_work(struct work_struct *work)
{
    struct cache_entry *entry = container_of(work, struct cache_entry, work);
    struct smartbdev *dev = smartbdev; 
    unsigned long flags;
   
      if (!entry) {
        pr_err("smartbdev: NULL entry in flush_entry_work\n");
        return;
    }

    if(atomic_read(&entry->ref_count)) 
    {
        flush_cache_entry(dev, entry);
        spin_lock_irqsave(&dev->cache_lock, flags);
    if(atomic_read(&entry->ref_count))
        {
        list_del(&entry->list);
        dev->used_cache -= entry->size;
        entry->list.next = LIST_POISON1;
        entry->list.prev = LIST_POISON2;
        atomic_set(&entry->ref_count, 0);
        }   
    spin_unlock_irqrestore(&dev->cache_lock, flags);
    
    }

    // kfree(entry->buffer);
    // kfree(entry);
    vfree(entry->buffer); 
    entry->buffer = NULL; 
    vfree(entry);
    entry = NULL;
        
    // atomic_inc(&dev->flush_count);
}





static int add_to_cache(struct smartbdev *dev, sector_t sector, void *buffer, unsigned int size)
{
    struct cache_entry *entry = NULL;
    unsigned long flags;
    unsigned long disk_flags; 
    unsigned long pos;

    
    if (dev->cache_size == 0 || size > dev->cache_size) {
        pos = sector << SECTOR_SHIFT;
        // pr_info("size given is %u\n",size); 

        if (pos + size <= NUM_SECTORS * SMART_SECTOR_SIZE) {
            spin_lock_irqsave(&dev->lock, disk_flags);
            memcpy(dev->data + pos, buffer, size);
            spin_unlock_irqrestore(&dev->lock, disk_flags);
            atomic_inc(&dev->write_count);
            return 0;
        } else {
            pr_err("smartbdev: Write request beyond device size\n");
            return -EIO;
        }
    }
    

    // entry = kmalloc(sizeof(struct cache_entry), GFP_KERNEL);
    entry = vmalloc(sizeof(struct cache_entry)); 
    if (!entry)
        return -ENOMEM;
    
    // entry->buffer = malloc(size, GFP_KERNEL);
    entry->buffer = vmalloc(size); 
    if (!entry->buffer) {
        // kfree(entry);
        vfree(entry);
        entry = NULL; 
        return -ENOMEM;
    }
    
   
    memcpy(entry->buffer, buffer, size);
    
    
    entry->sector = sector;
    entry->size = size;
    entry->dev = dev;
    // entry->ref_count = 1; 
    atomic_set(&entry->ref_count, 1);
    
    INIT_WORK(&entry->work, flush_entry_work);
    // pr_info("used cache and size are %zu and %u\n", dev->used_cache, size);
    spin_lock_irqsave(&dev->cache_lock, flags);
    
    
    if (dev->used_cache + size > dev->cache_size) {
        // pr_info("flush getting called\n");  
        spin_unlock_irqrestore(&dev->cache_lock, flags);
        flush_all_cache(dev);  
        // pr_info("after flush , used cache and size are %zu and %u\n", dev->used_cache, size);
        spin_lock_irqsave(&dev->cache_lock, flags);
        
        if (dev->used_cache + size > dev->cache_size) {
            spin_unlock_irqrestore(&dev->cache_lock, flags);
            // kfree(entry->buffer);
            // kfree(entry);
            // pr_info("size given is %u\n",size);
            // pr_info("used cache and size are %zu and %u\n", dev->used_cache, size);
            vfree(entry->buffer);
            vfree(entry); 
            entry = NULL;
            pos = sector << SECTOR_SHIFT;
            if (pos + size <= NUM_SECTORS * SMART_SECTOR_SIZE) {
                spin_lock_irqsave(&dev->lock, disk_flags);
                memcpy(dev->data + pos, buffer, size);
                spin_unlock_irqrestore(&dev->lock, disk_flags);
                atomic_inc(&dev->write_count);
                return 0;
            } else {
                pr_err("smartbdev: Write request beyond device size\n");
                return -EIO;
            }
            return -ENOSPC;
        }
    }
    
    list_add_tail(&entry->list, &dev->cache_list);
    dev->used_cache += size;
    atomic_inc(&dev->cache_hits);

    spin_unlock_irqrestore(&dev->cache_lock, flags);
    // INIT_WORK(&entry->work, flush_entry_work);
    queue_work(dev->flush_wq, &entry->work);
    return 0;
}


// static struct cache_entry *find_in_cache(struct smartbdev *dev, sector_t sector, unsigned int size)
// {
//     struct cache_entry *entry = NULL;
//     struct list_head *pos;
//     unsigned long flags;    
//     spin_lock_irqsave(&dev->cache_lock, flags);
//     list_for_each(pos, &dev->cache_list) {
//         entry = list_entry(pos, struct cache_entry, list);
//         if (entry->sector <= sector && (entry->sector + (entry->size >> SECTOR_SHIFT)) >= (sector + (size >> SECTOR_SHIFT))) {
//             spin_unlock_irqrestore(&dev->cache_lock, flags);
//             return entry;
//         }
//     }
    
//     spin_unlock_irqrestore(&dev->cache_lock, flags);
//     return NULL;
// }

static int flush_all_cache(struct smartbdev *dev)
{
    struct cache_entry *entry, *tmp;
    unsigned long flags;
    int count = 0;
    
    spin_lock_irqsave(&dev->cache_lock, flags);
    
    list_for_each_entry_safe(entry, tmp, &dev->cache_list, list) {
        if(atomic_read(&entry->ref_count)) {
        list_del_init(&entry->list);
        dev->used_cache -= entry->size;
        flush_cache_entry(dev, entry);
            // entry->ref_count = 0;
        atomic_set(&entry->ref_count, 0);
        }
        // kfree(entry->buffer);
        // kfree(entry);
        // entry=NULL; 
        count++;
    }
    
    if (count > 0) {
        atomic_inc(&dev->flush_count);
    }
    
    spin_unlock_irqrestore(&dev->cache_lock, flags);
    return count;
}


static int process_read_request(struct request *rq, unsigned long long *nr_bytes)
{
    struct smartbdev *dev = rq->q->queuedata;
    struct bio_vec bvec;
    struct req_iterator iter;
    unsigned long long pos = blk_rq_pos(rq) << SECTOR_SHIFT;
    void *buffer;
    unsigned int len;
    struct cache_entry *entry;
    int err = 0;
    unsigned long flags; 
    *nr_bytes = 0;
    flush_all_cache(dev); 
    rq_for_each_segment(bvec, rq, iter) {
        buffer = page_address(bvec.bv_page) + bvec.bv_offset;
        len = bvec.bv_len;
        
        if (pos + len > NUM_SECTORS * SMART_SECTOR_SIZE) {
            pr_err("smartbdev: Request beyond device size\n");
            return -EIO;
        }
        // entry = find_in_cache(dev, pos >> SECTOR_SHIFT, len);

        if (false) {
           
            unsigned int offset = (pos - (entry->sector << SECTOR_SHIFT));

            memcpy(buffer, entry->buffer + offset, len);
        } else {
           
            spin_lock_irqsave(&dev->lock, flags);
            memcpy(buffer, dev->data + pos, len);
            spin_unlock_irqrestore(&dev->lock, flags);
        }
        
        pos += len;
        *nr_bytes += len;
    }
    
    return err;
}

static int process_write_request(struct request *rq, unsigned long long *nr_bytes)
{
    struct smartbdev *dev = rq->q->queuedata;
    struct bio_vec bvec;
    struct req_iterator iter;
    unsigned long long pos = blk_rq_pos(rq) << SECTOR_SHIFT;
    void *buffer;
    unsigned int len;
    int err = 0;
    
    *nr_bytes = 0;
    
   
    rq_for_each_segment(bvec, rq, iter) {
        buffer = page_address(bvec.bv_page) + bvec.bv_offset;
        len = bvec.bv_len;
        
         
        if (pos + len > NUM_SECTORS * SMART_SECTOR_SIZE) {
            pr_err("smartbdev: Request beyond device size\n");
            return -EIO;
        }
        err = add_to_cache(dev, pos >> SECTOR_SHIFT, buffer, len);
        if (err)
            return err;
        
        pos += len;
        *nr_bytes += len;
    }
    
    return err;
}


static inline int process_request(struct request *rq, unsigned long long *nr_bytes)
{
    if (rq_data_dir(rq) == READ)
        return process_read_request(rq, nr_bytes);
    else
        return process_write_request(rq, nr_bytes);
}

static blk_status_t smartbdev_queue_rq(struct blk_mq_hw_ctx *hctx,
    const struct blk_mq_queue_data *bd)
{
    struct request *rq = bd->rq;
    blk_status_t status = BLK_STS_OK;
    unsigned long long nr_bytes = 0;
    blk_mq_start_request(rq);
    
    if (process_request(rq, &nr_bytes) != 0) {
        status = BLK_STS_IOERR;
    }
    pr_debug("smartbdev: request %llu:%d processed (%llu bytes)\n", blk_rq_pos(rq), rq->cmd_flags, nr_bytes);
    
    blk_mq_end_request(rq, status);
    return status;
}

/* procfs functions */

static int smartbdev_stats_show(struct seq_file *m, void *v)
{
    seq_printf(m, "Statistics for smartbdev:\n");
    seq_printf(m, "Cache size:      %zu bytes\n", smartbdev->cache_size);
    seq_printf(m, "Cache used:      %zu bytes\n", smartbdev->used_cache);
    seq_printf(m, "Write operations: %d\n", atomic_read(&smartbdev->write_count));
    seq_printf(m, "Cache hits:       %d\n", atomic_read(&smartbdev->cache_hits));
    seq_printf(m, "Cache flushes:    %d\n", atomic_read(&smartbdev->flush_count));
    
    return 0;
}

static int smartbdev_stats_open(struct inode *inode, struct file *file)
{
    return single_open(file, smartbdev_stats_show, NULL);
}

static const struct proc_ops smartbdev_stats_fops = {
    .proc_open = smartbdev_stats_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};


static ssize_t smartbdev_flush_write(struct file *file, const char __user *buffer,
                                     size_t count, loff_t *ppos)
{
    int flushed;
    flushed = flush_all_cache(smartbdev);
    return count;
}

static const struct proc_ops smartbdev_flush_fops = {
    .proc_write = smartbdev_flush_write,
};


static ssize_t smartbdev_cache_size_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
    char kbuf[32];
    size_t len;
    unsigned long flags ; 
    if (*ppos > 0)
        return 0;
    
    spin_lock_irqsave(&smartbdev->cache_lock, flags);
    len = snprintf(kbuf, sizeof(kbuf), "%zu\n", smartbdev->cache_size);
    spin_unlock_irqrestore(&smartbdev->cache_lock, flags);
    
    if (len > count)
        len = count;
        
    if (copy_to_user(buffer, kbuf, len))
        return -EFAULT;
        
    *ppos += len;
    return len;
}

static ssize_t smartbdev_cache_size_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    char kbuf[32];
    size_t new_size;
    unsigned long flags;
    
    if (count >= sizeof(kbuf))
        return -EINVAL;
        
    if (copy_from_user(kbuf, buffer, count))
        return -EFAULT;
        
    kbuf[count] = '\0';
    
    if (kstrtoul(kbuf, 10, &new_size) != 0)
        return -EINVAL;
    
    if(new_size < 0) new_size = 0; 
    new_size = new_size >> SECTOR_SHIFT;
    new_size = new_size << SECTOR_SHIFT;   
    if (new_size != 0 && (new_size < SMART_SECTOR_SIZE || new_size > 1024 * 1024 * 1024))  /* 1GB max */
            return -EINVAL;
            
    
    if (new_size == 0 || new_size < smartbdev->used_cache) {
            flush_all_cache(smartbdev);
        }
        
    spin_lock_irqsave(&smartbdev->cache_lock, flags);
    
    if (new_size < smartbdev->used_cache) {
        spin_unlock_irqrestore(&smartbdev->cache_lock, flags);
        flush_all_cache(smartbdev);
        spin_lock_irqsave(&smartbdev->cache_lock, flags);
    }
    smartbdev->cache_size = new_size;
    spin_unlock_irqrestore(&smartbdev->cache_lock, flags);
    
    pr_info("smartbdev: Cache size changed to %zu bytes\n", new_size);
    
    return count;
}

static const struct proc_ops smartbdev_cache_size_fops = {
    .proc_read = smartbdev_cache_size_read,
    .proc_write = smartbdev_cache_size_write,
};

static struct block_device_operations smartbdev_fops = {
    .owner = THIS_MODULE,
};

const struct blk_mq_ops smartbdev_mq_ops = {
    .queue_rq = smartbdev_queue_rq,
};



static int smartbdev_proc_init(struct smartbdev *dev)
{
  
    dev->proc_dir = proc_mkdir("smartbdev", NULL);
    if (!dev->proc_dir)
        return -ENOMEM;

    dev->proc_stats = proc_create_data("stats", 0444, dev->proc_dir,&smartbdev_stats_fops, dev);
    if (!dev->proc_stats)
        goto remove_dir;

    dev->proc_flush = proc_create_data("flush", 0222, dev->proc_dir,&smartbdev_flush_fops, dev);
    if (!dev->proc_flush)
        goto remove_stats;

    dev->proc_cache_size = proc_create_data("cache_size", 0644, dev->proc_dir,&smartbdev_cache_size_fops, dev);
    if (!dev->proc_cache_size)
        goto remove_flush;
        
    return 0;
remove_flush:
    proc_remove(dev->proc_flush);
remove_stats:
    proc_remove(dev->proc_stats);
remove_dir:
    proc_remove(dev->proc_dir);
    return -ENOMEM;
}


static void smartbdev_proc_cleanup(struct smartbdev *dev)
{
    proc_remove(dev->proc_cache_size);
    proc_remove(dev->proc_flush);
    proc_remove(dev->proc_stats);
    proc_remove(dev->proc_dir);
}

static int __init smartbdev_init(void)
{
    int err;

    // register major 
    err = register_blkdev(smartbdev_major, "smartbdev");
    if (err < 0) 
    {
        pr_err("smartbdev: register_blkdev failed\n");
        return err;
    }
    if (smartbdev_major == 0)
        smartbdev_major = err;  
    
    // main_dev init 
    spin_lock_init(&smartbdev->lock);
    spin_lock_init(&smartbdev->cache_lock);
    INIT_LIST_HEAD(&smartbdev->cache_list);
    smartbdev->cache_size = cache_size;
    smartbdev->used_cache = 0;
    
    // stats 
    atomic_set(&smartbdev->write_count, 0);
    atomic_set(&smartbdev->cache_hits, 0);
    atomic_set(&smartbdev->flush_count, 0);
    
    // work queue for flushing 
    smartbdev->flush_wq = create_singlethread_workqueue("smartbdev_flush");
    if (!smartbdev->flush_wq) 
    {
        unregister_blkdev(smartbdev_major, "smartbdev");
        return -ENOMEM;
    }

    // alloc disk space 
    smartbdev->data = vmalloc(NUM_SECTORS * SMART_SECTOR_SIZE);
    if (!smartbdev->data)
    {
        destroy_workqueue(smartbdev->flush_wq);
        unregister_blkdev(smartbdev_major, "smartbdev");
        return -ENOMEM;
    }
    
    // tag set up 
    smartbdev->tag_set.ops = &smartbdev_mq_ops;
    smartbdev->tag_set.nr_hw_queues = 1;
    smartbdev->tag_set.queue_depth = 128;
    smartbdev->tag_set.numa_node = NUMA_NO_NODE;
    smartbdev->tag_set.cmd_size = 0; 
    smartbdev->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
    
    err = blk_mq_alloc_tag_set(&smartbdev->tag_set);

    if (err) 
    {
        destroy_workqueue(smartbdev->flush_wq);
        vfree(smartbdev->data); // 
        unregister_blkdev(smartbdev_major, "smartbdev");
        return err;
    }
    
    // request queue 
    smartbdev->queue = blk_mq_init_queue(&smartbdev->tag_set);
    if (IS_ERR(smartbdev->queue)) {
        blk_mq_free_tag_set(&smartbdev->tag_set);
        destroy_workqueue(smartbdev->flush_wq);
        vfree(smartbdev->data);
        unregister_blkdev(smartbdev_major, "smartbdev");
        return -ENOMEM;
    }
    
    blk_queue_logical_block_size(smartbdev->queue, SMART_SECTOR_SIZE);
    smartbdev->queue->queuedata = smartbdev; 
    
    // disk and gendisk 
    smartbdev->gd = blk_mq_alloc_disk(&smartbdev->tag_set, smartbdev);
    if (!smartbdev->gd) 
    {
        err = -ENOMEM;
        blk_mq_destroy_queue(smartbdev->queue);
        blk_mq_free_tag_set(&smartbdev->tag_set);
        vfree(smartbdev->data);
        destroy_workqueue(smartbdev->flush_wq);
        unregister_blkdev(smartbdev_major, "smartbdev");
        return err;
    }
    
    smartbdev->gd->major = smartbdev_major;
    smartbdev->gd->first_minor = 0;
    smartbdev->gd->minors = 1; 
    smartbdev->gd->fops = &smartbdev_fops;   
    smartbdev->gd->private_data = smartbdev;
    snprintf(smartbdev->gd->disk_name, 32, "smartbdev");
    set_capacity(smartbdev->gd, NUM_SECTORS);
    
    // procfs 
    err = smartbdev_proc_init(smartbdev);
    if (err)
    {
        del_gendisk(smartbdev->gd);
        put_disk(smartbdev->gd);
        blk_mq_destroy_queue(smartbdev->queue);
        blk_mq_free_tag_set(&smartbdev->tag_set);
        vfree(smartbdev->data);
        destroy_workqueue(smartbdev->flush_wq);
        unregister_blkdev(smartbdev_major, "smartbdev");
        return err;
    }
    
    err = add_disk(smartbdev->gd);
    if (err) 
    {
        smartbdev_proc_cleanup(smartbdev);
        del_gendisk(smartbdev->gd);
        put_disk(smartbdev->gd);
        blk_mq_destroy_queue(smartbdev->queue);
        blk_mq_free_tag_set(&smartbdev->tag_set);
        vfree(smartbdev->data);
        destroy_workqueue(smartbdev->flush_wq);
        unregister_blkdev(smartbdev_major, "smartbdev");
        return err;
    }
    pr_info("smartbdev: loaded (major=%d, cache_size=%zu bytes)\n", 
            smartbdev_major, smartbdev->cache_size);
    return 0;
}

static void __exit smartbdev_exit(void)
{
    flush_all_cache(smartbdev);

    smartbdev_proc_cleanup(smartbdev);
    
    del_gendisk(smartbdev->gd);
    put_disk(smartbdev->gd);
    blk_mq_destroy_queue(smartbdev->queue);        
    blk_mq_free_tag_set(&smartbdev->tag_set);
    flush_workqueue(smartbdev->flush_wq);
    destroy_workqueue(smartbdev->flush_wq);
    unregister_blkdev(smartbdev_major, "smartbdev");
    vfree(smartbdev->data);
    pr_info("smartbdev: unloaded\n");
}

module_init(smartbdev_init);
module_exit(smartbdev_exit);
