#include "../include/flexcan_dma.h"
#include <linux/compat.h>
#include <linux/timer.h> // 添加定时器头文件
#define DMA_POOL_SIZE 64
#define CAN_FRAME_SIZE sizeof(struct can_frame)
#define FRAME_FREE    0
#define FRAME_READY   1
#define FRAME_IN_USE  2
#define FRAME_SENT    3
#define FRAME_PENDING 4


#define CIRC_CNT_ME(head, tail, size) ((head) >= (tail) ? (head) - (tail) : (size) - (tail) + (head))
#define CIRC_SPACE_ME(head, tail, size) ((size) - CIRC_CNT_ME(head, tail, size) - 1)

// 添加缺失的成员
struct can_dma_buf {
    struct dma_buf *dma_buf;
    dma_addr_t dma_handle;
    void *vaddr;
    atomic_t status;
    struct device *dev;
    struct flexcan_dma_ring *ring;  // 添加ring指针
    int index;                      // 添加索引
    unsigned long timestamp;        // 添加时间戳
};

// 添加timeout_timer成员
struct flexcan_dma_ring {
    struct net_device *ndev;
    struct device *dev;
    struct can_dma_buf tx_bufs[DMA_POOL_SIZE];
    struct can_dma_buf rx_bufs[DMA_POOL_SIZE];
    
    atomic_t tx_prod, tx_cons;
    atomic_t rx_head, rx_tail;
    
    spinlock_t tx_lock;
    spinlock_t rx_lock;
    wait_queue_head_t tx_wq;
    wait_queue_head_t rx_wq;
    
    // 字符设备相关
    dev_t devno;
    struct cdev cdev;
    struct delayed_work tx_work;
    struct timer_list timeout_timer; // 添加定时器
};

static struct class *class;
static struct flexcan_dma_ring *dma_ring;

/* DMA-BUF 操作回调 */
static struct sg_table* flexcan_dmabuf_map(
    struct dma_buf_attachment *attachment,
    enum dma_data_direction dir)
{
    struct can_dma_buf *priv = attachment->dmabuf->priv;
    struct sg_table *sgt;
    int ret;

    sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
    if (!sgt) return ERR_PTR(-ENOMEM);

    ret = dma_get_sgtable(attachment->dev, sgt, priv->vaddr, 
                         priv->dma_handle, CAN_FRAME_SIZE);
    if (ret < 0) {
        kfree(sgt);
        return ERR_PTR(ret);
    }

     // 确保sg_table有效
    if (!sgt->sgl) {
       sg_free_table(sgt);
       kfree(sgt);
       return ERR_PTR(-ENOMEM);
    }
    
    return sgt;
}

static void flexcan_dmabuf_unmap(
    struct dma_buf_attachment *attachment,
    struct sg_table *sgt,
    enum dma_data_direction dir)
{
    sg_free_table(sgt);
    kfree(sgt);
}

static int flexcan_dmabuf_mmap(struct dma_buf *dmabuf, 
                                struct vm_area_struct *vma)
{
    struct can_dma_buf *priv = dmabuf->priv;
    unsigned long pfn;
    int ret;
    
    pr_info("DMA-BUF mmap调用: dmabuf=%p, ops=%p\n", dmabuf, dmabuf->ops);
    
    // 添加设备有效性检查
    if (!priv->dev) {
        pr_err("错误：设备指针无效\n");
        return -EINVAL;
    }
    
    // 打印完整物理地址信息
    phys_addr_t phys = virt_to_phys(priv->vaddr);
    pr_info("vaddr=%p, dma_handle=%pad, phys=%pap, size=%zu\n",
           priv->vaddr, &priv->dma_handle, &phys, CAN_FRAME_SIZE);
    
    // 使用直接物理映射
    pfn = __phys_to_pfn(phys);
    ret = remap_pfn_range(vma, vma->vm_start, pfn,
                         CAN_FRAME_SIZE, vma->vm_page_prot);
    
    if (ret) {
        pr_err("remap_pfn_range失败: %d\n", ret);
        return ret;
    }
    
    // 设置缓存属性
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    
    pr_info("mmap成功: vma_start=0x%lx, size=%lu\n",
           vma->vm_start, vma->vm_end - vma->vm_start);
    
    return 0;
}

static void flexcan_dmabuf_release(struct dma_buf *dmabuf)
{
    struct can_dma_buf *priv = dmabuf->priv;
    struct flexcan_dma_ring *ring = priv->ring;
    unsigned long flags;
    
    if (!ring) {
        pr_warn("Release dma-buf %p without ring context\n", dmabuf);
        return;
    }
    
    spin_lock_irqsave(&ring->rx_lock, flags);
    
    // 重置缓冲区状态
    atomic_set(&priv->status, FRAME_FREE);
    
    spin_unlock_irqrestore(&ring->rx_lock, flags);
    
    pr_debug("Released dma-buf %p, index %d\n", dmabuf, priv->index);
}

static void flexcan_dma_buf_begin_cpu_access(struct dma_buf *dmabuf, size_t start,
				       size_t len,
				       enum dma_data_direction direction)
{
	return;
}


static void ion_dma_buf_end_cpu_access(struct dma_buf *dmabuf, size_t start,
				       size_t len,
				       enum dma_data_direction direction)
{
	return;
}


static const struct dma_buf_ops flexcan_dmabuf_ops = {
    .map_dma_buf = flexcan_dmabuf_map,
    .unmap_dma_buf = flexcan_dmabuf_unmap,
    .mmap = flexcan_dmabuf_mmap,
    .release = flexcan_dmabuf_release,
    .begin_cpu_access = flexcan_dma_buf_begin_cpu_access,
    .end_cpu_access = ion_dma_buf_end_cpu_access,
    .kmap = dma_buf_kmap,
    .kmap_atomic = dma_buf_kmap_atomic,
    .kunmap = dma_buf_kunmap,
    .kunmap_atomic = dma_buf_kunmap_atomic,
};





/* 字符设备操作 */

#define GET_TX_BUF  _IOR('F', 0, int)
#define GET_TX_DONE _IOR('F', 2, int)
#define GET_RX_BUF _IOR('F', 1, int)

#define GET_RX_DONE _IOR('F', 3, int)
static long flexcan_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct flexcan_dma_ring *ring = filp->private_data;
    int idx,  ret = 0;
    int __user *uarg = (int __user *)arg;
    unsigned long flags = 0;
    int found = -1;
    DEFINE_WAIT(wait); 
    u32 head, tail;

    switch (cmd) {

        case GET_TX_BUF:
            idx = atomic_inc_return(&ring->tx_prod) % DMA_POOL_SIZE;
            get_dma_buf(ring->tx_bufs[idx].dma_buf); // 增加引用计数
            ret = dma_buf_fd(ring->tx_bufs[idx].dma_buf, O_CLOEXEC);
            put_user(ret, uarg);
            break;

        case GET_RX_BUF: 
            spin_lock_irqsave(&ring->rx_lock, flags);
            
            while (1) {
                head = atomic_read(&ring->rx_head);
                tail = atomic_read(&ring->rx_tail);
                u32 avail = CIRC_CNT_ME(head, tail, DMA_POOL_SIZE);
                
                printk("GET_RX_BUF: head=%u, tail=%u, avail=%u\n", head, tail, avail);
                
                found = -1;

                if (avail > 0) {
                    // 找到下一个就绪缓冲区
                    int idx = tail % DMA_POOL_SIZE;  // 使用标准取模
                    
                    pr_debug("Checking buffer %d (status=%d)\n", 
                            idx, atomic_read(&ring->rx_bufs[idx].status));
                    
                    // 检查缓冲区状态
                    if (atomic_read(&ring->rx_bufs[idx].status) == FRAME_READY) {
                        found = idx;
                        atomic_set(&ring->rx_bufs[idx].status, FRAME_IN_USE);
                        
                        // 更新 tail 指针（标准递增和取模）
                        atomic_set(&ring->rx_tail, (tail + 1) % (2 * DMA_POOL_SIZE));
                        
                        pr_debug("Found buffer %d, new tail=%u\n", 
                                found, atomic_read(&ring->rx_tail));
                        
                        break;
                    } else {
                        // 状态不一致，跳过此缓冲区
                        printk("Buffer %d status %d != READY, skipping\n",
                            idx, atomic_read(&ring->rx_bufs[idx].status));
                        
                        // 更新 tail 指针
                        atomic_set(&ring->rx_tail, (tail + 1) % (2 * DMA_POOL_SIZE));
                    }
                } else {
                    // 没有可用数据
                    if (filp->f_flags & O_NONBLOCK) {
                        found = -1;
                        break;
                    }
                    
                    // 准备等待
                    prepare_to_wait(&ring->rx_wq, &wait, TASK_INTERRUPTIBLE);
                    spin_unlock_irqrestore(&ring->rx_lock, flags);
                    
                    // 让出 CPU
                    schedule();
                    
                    // 重新获取锁
                    spin_lock_irqsave(&ring->rx_lock, flags);
                    finish_wait(&ring->rx_wq, &wait);
                    
                    // 检查信号中断
                    if (signal_pending(current)) {
                        ret = -ERESTARTSYS;
                        break;
                    }
                }
            }
            
            
            spin_unlock_irqrestore(&ring->rx_lock, flags);
            
            if (ret) {
                return ret;
            }
            if (found < 0) {
                return -EAGAIN;
            }
            
            // 返回找到的缓冲区
            get_dma_buf(ring->rx_bufs[found].dma_buf);
            ret = dma_buf_fd(ring->rx_bufs[found].dma_buf, O_CLOEXEC);
            put_user(ret, uarg);
            break;

        case GET_RX_DONE: 

            if (copy_from_user(&idx, (void __user *)arg, sizeof(int)))
                return -EFAULT;
            
            if (idx < 0 || idx >= DMA_POOL_SIZE)
                return -EINVAL;
            
            spin_lock_irqsave(&ring->rx_lock, flags);
            
            if (atomic_read(&ring->rx_bufs[idx].status) != FRAME_IN_USE) {
                spin_unlock_irqrestore(&ring->rx_lock, flags);
                return -EINVAL;
            }
            
            // 只需更新状态，tail 指针已在 GET_RX_BUF 中更新
            atomic_set(&ring->rx_bufs[idx].status, FRAME_FREE);
            
            spin_unlock_irqrestore(&ring->rx_lock, flags);
            break;
        
    default:
        return -ENOTTY;
    }
    return 0;
}

static ssize_t flexcan_dma_write(struct file *filp, const char __user *buf,
                                    size_t count, loff_t *ppos)
{
    struct flexcan_dma_ring *ring = filp->private_data;
    int tx_fd;
    int i; // 将循环变量移到函数开头

    if(copy_from_user(&tx_fd, buf, sizeof(tx_fd))){
        return -EFAULT;
    }

    for (i = 0; i < DMA_POOL_SIZE; i++){
        struct file *buf_file = ring->tx_bufs[i].dma_buf->file;
        if (buf_file && file_count(buf_file) && 
            buf_file->f_inode->i_ino == fget(tx_fd)->f_inode->i_ino) {
            atomic_set(&ring->tx_bufs[i].status, FRAME_READY);
            schedule_delayed_work(&ring->tx_work, 0);
            return sizeof(tx_fd);
        }
    }
    return -EINVAL;
}

static unsigned int flexcan_dma_poll(struct file *filp, poll_table *wait)
{
    struct flexcan_dma_ring *ring = filp->private_data;
    unsigned int mask = 0;
    int i;
    u32 rx_avail;

    poll_wait(filp, &ring->rx_wq, wait);

    for (i = 0; i < DMA_POOL_SIZE; i++) {
            if (atomic_read(&ring->rx_bufs[i].status) == FRAME_READY) {
                rx_avail = 1;
            }
        }

    if (rx_avail > 0){
        mask |= POLLIN;
    }

    return mask;
}

static int flexcan_dma_mmap(struct file *filp, struct vm_area_struct *vma)
{
    pr_err("错误：请使用 DMA-BUF fd 进行 mmap，而不是字符设备 fd\n");
    return -EINVAL;
}

static int flexcan_dma_open(struct inode *inode, struct file *filp)
{
    filp->private_data = dma_ring;
    
    // 添加调试信息
    pr_info("字符设备打开: 进程=%s, pid=%d\n",
           current->comm, current->pid);
    return 0;
}
static const struct file_operations fops = {
    .open = flexcan_dma_open,
    .unlocked_ioctl = flexcan_ioctl,
    .compat_ioctl = flexcan_ioctl,
    .write = flexcan_dma_write,
    .poll = flexcan_dma_poll,
    .mmap = flexcan_dma_mmap,
    .owner = THIS_MODULE,
};

/* DMA缓冲区初始化 */
static int init_dma_pool(struct device *dev, struct can_dma_buf *pool)
{
    DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

    int i;

    size_t alloc_size = ALIGN(CAN_FRAME_SIZE, PAGE_SIZE);

    for (i = 0; i < DMA_POOL_SIZE; i++) {

        pool[i].vaddr = dma_alloc_coherent(dev, alloc_size,
                                         &pool[i].dma_handle, GFP_KERNEL);

        memset(pool[i].vaddr, 0, CAN_FRAME_SIZE);

        if (!pool[i].vaddr) {
            dev_err(dev, "DMA alloc failed for buffer %d\n", i);
            goto err_alloc;
        }

        dma_sync_single_for_device(dev, pool[i].dma_handle,
                                  alloc_size, DMA_BIDIRECTIONAL);

        pool[i].dev = dev;
        pool[i].ring = dma_ring;
        pool[i].index = i;
        pool[i].timestamp = jiffies;
        atomic_set(&pool[i].status, FRAME_FREE);
        
        

        exp_info.ops = &flexcan_dmabuf_ops,
        exp_info.size = alloc_size,
        exp_info.flags = O_RDWR,
        exp_info.priv = &pool[i],

        

        pool[i].dma_buf = dma_buf_export(&exp_info);
        if (IS_ERR(pool[i].dma_buf)) {
            dev_err(dev, "Failed to export DMA buffer: %ld\n", 
                   PTR_ERR(pool[i].dma_buf));
            goto err_export;
        }

        pr_info("导出 DMA-BUF: vaddr=%p, ops=%p\n", 
                    pool[i].vaddr, exp_info.ops);

        pr_info("dma_buf_ops:%p", pool[i].dma_buf->ops);
    }

    return 0;

    err_export:
        dma_free_coherent(dev, alloc_size, pool[i].vaddr, pool[i].dma_handle);
    err_alloc:
        while (--i >= 0) {
            dma_buf_put(pool[i].dma_buf);
            dma_free_coherent(dev, alloc_size, pool[i].vaddr, pool[i].dma_handle);
        }   
    return -ENOMEM;
}

/* 传输处理 */
static void process_tx_work(struct work_struct *work)
{
    struct flexcan_dma_ring *ring = container_of(work, struct flexcan_dma_ring, tx_work.work);
    unsigned long flags;
    u32 cons, prod;

    spin_lock_irqsave(&ring->tx_lock, flags);
    cons = atomic_read(&ring->tx_cons);
    prod = atomic_read(&ring->tx_prod);

    while (cons != prod) {
        int idx = cons % DMA_POOL_SIZE;
        struct can_dma_buf *buf = &ring->tx_bufs[idx];

        if (atomic_cmpxchg(&buf->status, FRAME_READY, FRAME_IN_USE) == FRAME_READY) {

            dma_sync_single_for_device(ring->dev, buf->dma_handle,
                              CAN_FRAME_SIZE, DMA_TO_DEVICE);
            // 触发硬件传输
            flexcan_hw_xmit(ring->ndev, buf->dma_handle, buf->vaddr);

            atomic_set(&buf->status, FRAME_SENT);
            cons++;  
            atomic_set(&ring->tx_cons, cons);
        } else {
            break;
        }
    }

    spin_unlock_irqrestore(&ring->tx_lock, flags);

    if (cons != prod)
        schedule_delayed_work(&ring->tx_work, 0);
}

static struct can_frame* get_dma_frame(int *index) {
    struct flexcan_dma_ring *ring = dma_ring;
    unsigned long flags;
    u32 head, tail;
    int idx;
    
    spin_lock_irqsave(&ring->rx_lock, flags);
    
    head = atomic_read(&ring->rx_head);
    tail = atomic_read(&ring->rx_tail);
    
    printk("get_dma_frame: head=%u, tail=%u, space=%u\n", 
            head, tail, CIRC_SPACE_ME(head, tail, DMA_POOL_SIZE));
    
    // 计算可用空间
    if (CIRC_SPACE_ME(head, tail, DMA_POOL_SIZE) == 0) {
        int i = 0;
        for (i = tail; i < head; i++){
            if (&ring->rx_bufs[i].status == FRAME_FREE) 
            {   
                printk("find &ring->rx_bufs[idx].status == FRAME_FREE\n\n");
                tail++;
            }
        }
        atomic_set(&ring->rx_tail, tail);
    }


    if (CIRC_SPACE_ME(head, tail, DMA_POOL_SIZE) == 0) {
        pr_warn_ratelimited("RX ring full! head=%u tail=%u\n", head, tail);
        spin_unlock_irqrestore(&ring->rx_lock, flags);
        return NULL;
    }
    
    // 计算缓冲区索引
    idx = head % DMA_POOL_SIZE;  // 使用标准取模运算
    
    // 确保缓冲区空闲
    if (atomic_read(&ring->rx_bufs[idx].status) != FRAME_FREE) {
        pr_warn("Buffer %d not free! Status=%d\n", 
               idx, atomic_read(&ring->rx_bufs[idx].status));
        spin_unlock_irqrestore(&ring->rx_lock, flags);
        return NULL;
    }
    
    // 成功获取缓冲区
    atomic_set(&ring->rx_bufs[idx].status, FRAME_PENDING);
    
    // 更新 head 指针（使用标准递增和取模）
    atomic_set(&ring->rx_head, (head + 1) % (2 * DMA_POOL_SIZE));
    
    ring->rx_bufs[idx].timestamp = jiffies;
    
    pr_debug("get_dma_frame: new head=%u, index=%d\n", 
    atomic_read(&ring->rx_head), idx);
    
    spin_unlock_irqrestore(&ring->rx_lock, flags);
    
    if (index) *index = idx;
    return (struct can_frame *)ring->rx_bufs[idx].vaddr;
}

static void rx_data_complete(int index)
{
    struct flexcan_dma_ring *ring = dma_ring;
    unsigned long flags;
    
    dma_sync_single_for_cpu(ring->dev, ring->rx_bufs[index].dma_handle,
                           CAN_FRAME_SIZE, DMA_FROM_DEVICE);
    
    spin_lock_irqsave(&ring->rx_lock, flags);
    
    if (atomic_read(&ring->rx_bufs[index].status) != FRAME_PENDING) {
        pr_warn("Unexpected status %d for buffer %d\n",
               atomic_read(&ring->rx_bufs[index].status), index);
    }
    
    atomic_set(&ring->rx_bufs[index].status, FRAME_READY);
    wake_up_interruptible(&ring->rx_wq);
    
    spin_unlock_irqrestore(&ring->rx_lock, flags);
}

// 使用旧版定时器API
static void frame_timeout_handler(unsigned long data)
{
    struct flexcan_dma_ring *ring = (struct flexcan_dma_ring *)data;
    unsigned long flags;
    u32 prod;
    int idx, count = 0;

    spin_lock_irqsave(&ring->rx_lock, flags);
        
    // 遍历所有缓冲区
    for (idx = 0; idx < DMA_POOL_SIZE; idx++) {
        if (atomic_read(&ring->rx_bufs[idx].status) == FRAME_PENDING || 
            atomic_read(&ring->rx_bufs[idx].status) == FRAME_READY) {
            // 检查是否超时
            if (time_after(jiffies, ring->rx_bufs[idx].timestamp + HZ)) {
                // 重置缓冲区状态
                atomic_set(&ring->rx_bufs[idx].status, FRAME_FREE);
                count++;
                pr_warn("Timeout reset buffer %d\n", idx);
            }
        }
    }
    
    spin_unlock_irqrestore(&ring->rx_lock, flags);
    
    if (count > 0) {
        pr_warn("Reset %d timed out buffers\n", count);
    }
    
    // 重新设置定时器
    mod_timer(&ring->timeout_timer, jiffies + HZ);
}

static int __init flexcan_dma_init(void)
{
    int ret;
    struct device *dev;
    int i = 0;
    struct net_device *net_dev = dev_get_by_name(&init_net, "can0");
    

    static struct flexcan_dma_ops ops = {
        .get_frame = get_dma_frame,
        .rx_data_complete = rx_data_complete,
    };
    flexcan_dma_register_ops(&ops);

    dma_ring = kzalloc(sizeof(*dma_ring), GFP_KERNEL);
    if (!dma_ring)
        return -ENOMEM;

    // 获取CAN设备
    if (!net_dev) {
        ret = -ENODEV;
        goto err_free;
    }
    dev = net_dev->dev.parent;
    dma_ring->dev = dev;
    dma_ring->ndev = net_dev;

    // 初始化DMA池
    ret = init_dma_pool(dev, dma_ring->tx_bufs);
    if (ret)
        goto err_put_dev;

    ret = init_dma_pool(dev, dma_ring->rx_bufs);
    if (ret)
        goto err_free_tx;

    // 初始化环形缓冲
    atomic_set(&dma_ring->tx_prod, 0);
    atomic_set(&dma_ring->tx_cons, 0);
    atomic_set(&dma_ring->rx_head, 0);
    atomic_set(&dma_ring->rx_tail, 0);
    spin_lock_init(&dma_ring->tx_lock);
    spin_lock_init(&dma_ring->rx_lock);
    init_waitqueue_head(&dma_ring->tx_wq);
    init_waitqueue_head(&dma_ring->rx_wq);
    INIT_DELAYED_WORK(&dma_ring->tx_work, process_tx_work);

    init_timer(&dma_ring->timeout_timer);
    dma_ring->timeout_timer.function = frame_timeout_handler;
    dma_ring->timeout_timer.data = (unsigned long)dma_ring;
    mod_timer(&dma_ring->timeout_timer, jiffies + HZ);

    // 注册字符设备
    ret = alloc_chrdev_region(&dma_ring->devno, 0, 1, "flexcan_dma");
    if (ret)
        goto err_free_rx;

    cdev_init(&dma_ring->cdev, &fops);
    ret = cdev_add(&dma_ring->cdev, dma_ring->devno, 1);
    if (ret)
        goto err_unreg;

    class = class_create(THIS_MODULE, "flexcan_dma");
    if (IS_ERR(class)) {
        ret = PTR_ERR(class);
        goto err_del_cdev;
    }

    device_create(class, NULL, dma_ring->devno, NULL, "flexcan_dma");

    schedule_delayed_work(&dma_ring->tx_work, 0);
    return 0;

// 错误处理
err_del_cdev:
    cdev_del(&dma_ring->cdev);
err_unreg:
    unregister_chrdev_region(dma_ring->devno, 1);
err_free_rx:
    for (i = 0; i < DMA_POOL_SIZE; i++) {
        dma_buf_put(dma_ring->rx_bufs[i].dma_buf);
        dma_free_coherent(dev, CAN_FRAME_SIZE, 
                         dma_ring->rx_bufs[i].vaddr,
                         dma_ring->rx_bufs[i].dma_handle);
    }
err_free_tx:
    for (i = 0; i < DMA_POOL_SIZE; i++) {
        dma_buf_put(dma_ring->tx_bufs[i].dma_buf);
        dma_free_coherent(dev, CAN_FRAME_SIZE,
                         dma_ring->tx_bufs[i].vaddr,
                         dma_ring->tx_bufs[i].dma_handle);
    }
err_put_dev:
    dev_put(net_dev);
err_free:
    kfree(dma_ring);
    return ret;
}

static void __exit flexcan_dma_exit(void)
{
    struct device *dev = dma_ring->dev;
    int i;

    flexcan_dma_unregister_ops();
    cancel_delayed_work_sync(&dma_ring->tx_work);
    del_timer_sync(&dma_ring->timeout_timer);
    
    // 销毁字符设备
    device_destroy(class, dma_ring->devno);
    class_destroy(class);
    cdev_del(&dma_ring->cdev);
    unregister_chrdev_region(dma_ring->devno, 1);

    // 释放DMA池
    for (i = 0; i < DMA_POOL_SIZE; i++) {
        dma_buf_put(dma_ring->tx_bufs[i].dma_buf);
        dma_free_coherent(dev, CAN_FRAME_SIZE,
                         dma_ring->tx_bufs[i].vaddr,
                         dma_ring->tx_bufs[i].dma_handle);
        
        dma_buf_put(dma_ring->rx_bufs[i].dma_buf);
        dma_free_coherent(dev, CAN_FRAME_SIZE,
                         dma_ring->rx_bufs[i].vaddr,
                         dma_ring->rx_bufs[i].dma_handle);
    }

    kfree(dma_ring);
}

module_init(flexcan_dma_init);
module_exit(flexcan_dma_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("xiao");
MODULE_DESCRIPTION("FlexCAN DMA Zero-Copy Driver");