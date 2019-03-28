#include "memory.h"
#include "stdint.h"
#include "print.h"
#include "bitmap.h"
#include "string.h"
#include "global.h"
#include "debug.h"
#include "interrupt.h"
#include "thread.h"
#include "sync.h"

/***************  位图地址 ********************
 * 因为0xc009f000是内核主线程栈顶，0xc009e000是内核主线程的pcb.
 * 一个页框大小的位图32K bit * 4KB = 128M 可表示128M内存,
 * 位图安排在地址 0xc009a000,这样系统最大支持4个页框的位图,即512M */
#define MEM_BITMAP_BASE 0xc009a000

#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22) // high 10 bit
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12) // mid 10 bit

/* 内核从虚拟地址3GB(0xc0000000)起,0x100000意指跨过低端1M内存,使虚拟地址在逻辑上连续 */
#define K_HEAP_START (KERNEL_SPACE + 0x100000)

 /* 物理池 生成两个实例用于管理内核内存池和用户内存池 */
struct pool {
    struct bitmap pool_bitmap;  // 本内存池用到的位图结构,用于管理物理内存
    uint32_t phy_addr_start;    // 本内存池所管理物理内存的起始地址
    uint32_t pool_size;         // 本内存池字节容量
    struct mutex_t lock;
};

struct pool kernel_pool;
struct pool user_pool;     // 生成内核内存池和用户内存池

struct virtual_addr kernel_vaddr;       // 此结构是用来给内核分配虚拟地址

struct mem_block_desc k_block_descs[DESC_CNT];    // 内核内存块描述符数组

/** 在pf表示的虚拟内存池中申请pg_cnt个虚拟页,
 ** 成功则返回虚拟页的起始地址, 失败则返回NULL */
static void* vaddr_get(enum pool_flags pf, uint32_t pg_cnt) {
    int vaddr_start = 0, bit_idx_start = -1;
    uint32_t cnt = 0;

    if (pf == PF_KERNEL) {

        bit_idx_start  = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1) {
            return NULL;
        }
        while(cnt < pg_cnt) 
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
        
        vaddr_start = kernel_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
    } else {
        // 用户内存池 
        struct task_struct* cur = running_thread();
        bit_idx_start = bitmap_scan(&cur->userprog_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1) {
            return NULL;
        }
        while(cnt < pg_cnt) 
            bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
        vaddr_start = cur->userprog_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
        ASSERT(vaddr_start < (int)(KERNEL_SPACE - PG_SIZE));
    }
    return (void*)vaddr_start;
}

/** 在虚拟地址池中释放以 _vaddr 起始的连续 pg_cnt 个虚拟页地址。另外，释放资源好像没必要担心中断 - - */
static void vaddr_remove(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt) {
    uint32_t bit_idx_start = 0, vaddr = (uint32_t)_vaddr, cnt = 0;
    
    if (pf == PF_KERNEL) {  // 内核虚拟内存池
        bit_idx_start = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        while(cnt < pg_cnt) {
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 0);
        }
    } else {  // 用户虚拟内存池
        struct task_struct* cur_thread = running_thread();
        bit_idx_start = (vaddr - cur_thread->userprog_vaddr.vaddr_start) / PG_SIZE;
        while(cnt < pg_cnt) {
            bitmap_set(&cur_thread->userprog_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 0);
        }
    }
}

/** 得到虚拟地址vaddr对应的pte指针*/
uint32_t* pte_ptr(uint32_t vaddr) {
    /* 先访问到页表自己 + \
     * 再用页目录项pde(页目录内页表的索引)做为pte的索引访问到页表 + \
     * 再用pte的索引做为页内偏移*/
    uint32_t* pte = (uint32_t*)(0xffc00000 + \
                                ((vaddr & 0xffc00000) >> 10) + \
                                PTE_IDX(vaddr) * 4);
    return pte;
}

/** 得到虚拟地址vaddr对应的pde的指针 */
uint32_t* pde_ptr(uint32_t vaddr) {
    /* 0xfffff是用来访问到页表本身所在的地址 */
    uint32_t* pde = (uint32_t*)((0xfffff000) + PDE_IDX(vaddr) * 4);
    return pde;
}

/** 在m_pool指向的物理内存池中分配1个物理页,
 ** 成功则返回页框的物理地址,失败则返回NULL */
static void* palloc(struct pool* m_pool) {
 

    int bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1); // 找一个物理页面
    if (bit_idx == -1 ) {
     
        return NULL;
    }
    bitmap_set(&m_pool->pool_bitmap, bit_idx, 1); // 更新位图
     

    uint32_t page_phyaddr = ((bit_idx * PG_SIZE) + m_pool->phy_addr_start);
    return (void*)page_phyaddr;
}

/** 将物理地址 pg_phy_addr 回收到物理内存池 */
void pfree(uint32_t pg_phy_addr) {
    struct pool* mem_pool = pg_phy_addr >= user_pool.phy_addr_start ? &user_pool : &kernel_pool;
    uint32_t bit_idx = (pg_phy_addr - mem_pool->phy_addr_start) / PG_SIZE;
    bitmap_set(&mem_pool->pool_bitmap, bit_idx, 0); // 更新位图
}

/** 页表中添加虚拟地址_vaddr与物理地址_page_phyaddr的映射 */
static bool page_table_add(void* _vaddr, void* _page_phyaddr) {
    if (!_vaddr || !_page_phyaddr) 
        return false;
    uint32_t vaddr = (uint32_t)_vaddr, page_phyaddr = (uint32_t)_page_phyaddr;
    uint32_t* pde = pde_ptr(vaddr);
    uint32_t* pte = pte_ptr(vaddr);
    
    // 如果虚拟地址对应的页目录项不存在，需要先创建PDE
    if (!(*pde & 0x00000001)) {  // 页目录项和页表项的第0位为P,此处判断是否存在
 
        // 页表中用到的页框一律从内核空间分配；有可能两个线程都走到此，因此要关中断
        uint32_t pde_phyaddr = (uint32_t)palloc(&kernel_pool);  
        if (!pde_phyaddr) {
            return false;
        }
        
        *pde = (pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1);

        // 分配到的物理页（即将用作页表）清0
        // pte 低12位 置0 即该物理页起始地址 
        memset((void*)((int)pte & 0xfffff000), 0, PG_SIZE);
    }  

    if (*pte & 0x00000001) { // 这里应该没有PTE
        PANIC("pte repeat\n");
    }
    *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);      // US=1,RW=1,P=1
    return true;
}

/** 去掉页表中虚拟地址 vaddr 的映射，只去掉 vaddr 对应的 pte */
static void page_table_pte_remove(uint32_t vaddr) {
    uint32_t* pte = pte_ptr(vaddr);
    *pte &= ~PG_P_1; // 将页表项pte的P位置0
    asm volatile ("invlpg %0"::"m" (vaddr):"memory"); // 更新 TLB 单个条目
}

/** 分配 pg_cnt 个页空间,成功则返回起始虚拟地址,失败时返回 NULL 
  * 1 在虚拟内存池中申请虚拟地址
  * 2 在物理内存池中申请物理页
  * 3 虚拟地址和物理地址在页表中完成映射
  */
void* malloc_page(enum pool_flags pf, uint32_t pg_cnt) {
    ASSERT(pg_cnt > 0 && pg_cnt < 3840); // 假设15MB可用，一页4K，则3840页

    void* vaddr_start = vaddr_get(pf, pg_cnt);
    if (!vaddr_start)
        return NULL;
    
    uint32_t vaddr = (uint32_t)vaddr_start, cnt = pg_cnt;
    struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
    
    // 因为虚拟地址是连续的,但物理地址可以是不连续的,所以逐个做映射
    while (cnt-- > 0) {
        void* page_phyaddr = palloc(mem_pool);
        // OOM 时要将已申请的虚拟地址和物理页 rollback
        if (!page_phyaddr || !page_table_add((void*)vaddr, page_phyaddr)) {
            // 1 虚拟池位图置0 (未分情况)
            // 2 内核池位图置0 未实现
            // 2 添加的页表项置0
            uint32_t loop_cnt = pg_cnt - cnt;
            while (loop_cnt-- > 0) {
                vaddr -= PG_SIZE;
                uint32_t phy_addr = addr_v2p(vaddr);            
                ASSERT(phy_addr % PG_SIZE == 0);
                // 先物理页框
                pfree(phy_addr);
                page_table_pte_remove(vaddr);
            }
            vaddr_remove(pf, vaddr_start, pg_cnt);
            return NULL;
        } 
        vaddr += PG_SIZE;      // 下一个虚拟页
    }
    return vaddr_start;
}

/** 释放以虚拟地址 vaddr 为起始的 cnt 个物理页框 */
void mfree_page(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt) {
    uint32_t pg_phy_addr;
    uint32_t vaddr = (int32_t)_vaddr, page_cnt = 0;
    ASSERT(pg_cnt >=1 && vaddr % PG_SIZE == 0);
    pg_phy_addr = addr_v2p(vaddr);  // 获取虚拟地址vaddr对应的物理地址
    
    // 确保待释放的物理内存在（低端1M+页目录1k+页表地址1k）外
    ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= 0x102000);
    
    // 判断pg_phy_addr属于用户物理内存池还是内核物理内存池
    if (pg_phy_addr >= user_pool.phy_addr_start) {   // 位于user_pool内存池
        vaddr -= PG_SIZE;
        while (page_cnt < pg_cnt) {
            vaddr += PG_SIZE;
            pg_phy_addr = addr_v2p(vaddr);
            
            // 确保物理地址属于用户物理内存池
            ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= user_pool.phy_addr_start);
            // 先物理页框
            pfree(pg_phy_addr);
            // 再从页表中清除此虚拟地址所在的页表项pte
            page_table_pte_remove(vaddr);
            
            page_cnt++;
        }
        // 清空虚拟地址的位图中的相应位 
        vaddr_remove(pf, _vaddr, pg_cnt);
        
    } else {         // 位于kernel_pool内存池
        vaddr -= PG_SIZE;
        while (page_cnt < pg_cnt) {
            vaddr += PG_SIZE;
            pg_phy_addr = addr_v2p(vaddr);
            // 确保待释放的物理内存只属于内核物理内存池 
            ASSERT((pg_phy_addr % PG_SIZE) == 0 && \
                   pg_phy_addr >= kernel_pool.phy_addr_start && \
                   pg_phy_addr < user_pool.phy_addr_start);
             
            pfree(pg_phy_addr); 
            page_table_pte_remove(vaddr); 
            page_cnt++;
        } 
        vaddr_remove(pf, _vaddr, pg_cnt);
    }
}

/** 从物理内存池中申请 pg_cnt 页内存,成功则清零后返回其虚拟地址，失败返回 NULL */
void* get_pages(uint32_t pg_cnt, enum pool_flags flag) {
    struct pool* mem_pool = flag & PF_KERNEL ? &kernel_pool : &user_pool;
    mutex_lock(&mem_pool->lock);
    void* vaddr =  malloc_page(flag, pg_cnt);
     
    if (vaddr != NULL)    // 页框清0后返回
        memset(vaddr, 0, pg_cnt * PG_SIZE);
    mutex_unlock(&mem_pool->lock);
    return vaddr;
}

void* get_one_page(enum pool_flags flag, uint32_t vaddr) {

    struct pool* mem_pool = flag & PF_KERNEL ? &kernel_pool : &user_pool;
    mutex_lock(&mem_pool->lock);

    /* 先将虚拟地址对应的位图置1 */
    struct task_struct* cur = running_thread();
    int32_t bit_idx = -1;

    if (cur->pgdir != NULL && flag == PF_USER) {
        /* 若当前是用户进程申请用户内存,就修改用户进程自己的虚拟地址位图 */
        bit_idx = (vaddr - cur->userprog_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx, 1);
        
    } else if (cur->pgdir == NULL && flag == PF_KERNEL){
        /* 如果是内核线程申请内核内存,就修改kernel_vaddr. */
        bit_idx = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx, 1);
    } else {
        PANIC("not allow kernel alloc userspace or user alloc kernelspace");
    }
    
    void* page_phyaddr = palloc(mem_pool);
    if (page_phyaddr == NULL) { 
        mutex_unlock(&mem_pool->lock);
        return NULL;
    }
    page_table_add((void*)vaddr, page_phyaddr);

    mutex_unlock(&mem_pool->lock);
    return (void*)vaddr;
}

/* 得到虚拟地址映射到的物理地址 */
uint32_t addr_v2p(uint32_t vaddr) {
    uint32_t* pte = pte_ptr(vaddr);
    /* (*pte)的值是页表所在的物理页框地址,
     * 去掉其低12位的页表项属性+虚拟地址vaddr的低12位 */
    return ((*pte & 0xfffff000) + (vaddr & 0x00000fff));
}

/** 返回 arena 中第 idx 个内存块的地址 */
static struct mem_block* arena2block(struct arena* a, uint32_t idx) {
    return (struct mem_block*)((uint32_t)a + sizeof(struct arena) + idx * a->desc->block_size);
}

/** 返回内存块b所在的 arena 地址 
  * 申请大小小于等于 1024 时，一个 arena 占一页 4KB，低12位置0即 arena 地址 */
static struct arena* block2arena(struct mem_block* b) {
    return (struct arena*)((uint32_t)b & 0xfffff000); 
}

/* 在堆中申请size字节内存 */
void* sys_malloc(uint32_t size) {
  
    enum pool_flags PF;
    uint32_t pool_size;
    struct pool* mem_pool;
    struct mem_block_desc* descs;
    struct task_struct* cur_thread = running_thread(); 

    /* 判断用哪个内存池*/
    if (cur_thread->pgdir == NULL) {     // 若为内核线程
        PF = PF_KERNEL;       
        descs = k_block_descs;
        mem_pool = &kernel_pool;
        pool_size = kernel_pool.pool_size;
    } else {                      // 用户进程pcb中的pgdir会在为其分配页表时创建
        PF = PF_USER;
        mem_pool = &user_pool;
        pool_size = user_pool.pool_size;
        descs = cur_thread->u_block_desc;
    }
 
    /* 若申请的内存不在内存池容量范围内则直接返回NULL */
    if (!(size > 0 && size < pool_size)) {
        return NULL;
    }
    mutex_lock(&mem_pool->lock);
    struct arena* a;
    struct mem_block* b;

    // 超过最大内存块1024, 就分配页框
    if (size > 1024) {
        uint32_t page_cnt = DIV_ROUND_UP(size + sizeof(struct arena), PG_SIZE); // 向上取整需要的页框数

        a = get_pages(page_cnt, PF);        
        if (!a) {
            mutex_unlock(&mem_pool->lock);
            return NULL; 
        }

        // 一整块/多块页框，desc 置 NULL，cnt 为页框数，large 置 true
        a->desc = NULL;
        a->cnt = page_cnt;
        a->large = true;
        mutex_unlock(&mem_pool->lock);
        return (void*)(a + 1); // 跨过arena大小，返回剩下内存

    } else {    // 若申请的内存小于等于1024,可在各种规格的 mem_block_desc 中去适配
        uint8_t desc_idx;
        // 匹配合适的内存块规格
        for (desc_idx = 0; size > descs[desc_idx].block_size; desc_idx++);         
 
        // 若 mem_block_desc 中 free_list 没有可用的 mem_block，创建新 arena
        if (list_empty(&descs[desc_idx].free_list)) {
            a = get_pages(1, PF);   // 普通小内存分配，arena 为1页框
            if (!a) {
                mutex_unlock(&mem_pool->lock);
                return NULL;           
            }
            
            // 分配小块内存的 arena，desc 置为相应内存块描述符；
            // cnt 为可用的内存块数，large 置 false
            a->desc = &descs[desc_idx];
            a->large = false;
            a->cnt = descs[desc_idx].blocks_per_arena;
             
            enum intr_status old_status = intr_disable();
            // 将 arena 拆分成内存块，并添加到描述符的 free_list
            for (uint32_t block_idx = 0; block_idx < descs[desc_idx].blocks_per_arena; block_idx++) {
                b = arena2block(a, block_idx);
                ASSERT(!elem_find(&a->desc->free_list, &b->free_elem));
                list_append(&a->desc->free_list, &b->free_elem);
            }
            intr_set_status(old_status);
        }
        
        // 得到一个空闲块的指针
        b = elem2entry(struct mem_block, free_elem, list_pop(&(descs[desc_idx].free_list)));
        // 把该空闲块上的暂存队列元素写0
        memset(b, 0, descs[desc_idx].block_size);
        // 得到所在 arena 的指针
        a = block2arena(b);
        // 此 arena 中空闲内存块数减1；
        a->cnt--; 

        mutex_unlock(&mem_pool->lock);
        return (void*)b;
    }

}

/* 回收内存ptr */
void sys_free(void* ptr) {
    ASSERT(ptr);
    if (!ptr)
        return; 
    enum pool_flags PF; 
    struct pool* mem_pool;
    // 判断是线程还是进程
    if (running_thread()->pgdir == NULL) {
        ASSERT((uint32_t)ptr >= K_HEAP_START);
        PF = PF_KERNEL; 
        mem_pool = &kernel_pool;
    } else {
        PF = PF_USER; 
        mem_pool = &user_pool;
    }
   
    struct mem_block* b = ptr;
    struct arena* a = block2arena(b);        
    ASSERT(a->large == 0 || a->large == 1);

    mutex_lock(&mem_pool->lock);

    if (a->desc == NULL && a->large == true) { // 大于1024的内存，释放页框
        mfree_page(PF, a, a->cnt);
    } else { // 小于等于1024的内存块

        // 先将内存块回收到free_list 
        list_append(&a->desc->free_list, &b->free_elem);
        
        /* 再判断此arena中的内存块是否都是空闲,如果是就释放arena */
        if (++a->cnt == a->desc->blocks_per_arena) { 
            for (uint32_t block_idx = 0; block_idx < a->desc->blocks_per_arena; block_idx++) {
                struct mem_block* b = arena2block(a, block_idx);
                ASSERT(elem_find(&a->desc->free_list, &b->free_elem));
                list_remove(&b->free_elem);
            }
            mfree_page(PF, a, 1);
        }       
    } 
    mutex_unlock(&mem_pool->lock);
}
 
/** 初始化内存池 */
static void mem_pool_init(uint32_t all_mem) {
    put_str("     mem_pool_init start...\n");
    uint32_t page_table_size = PG_SIZE * 256;            
    // 为实现内核共享，loader里内核空间的1GB对应的页表已经初始化了
    // 1页目录表+第0和第768个页目录项指向同一个页表+
    // 769~1022页目录项共指向254个页表,1023指向PDT本身不算，共256个已用的页表。
    uint32_t used_mem = page_table_size + 0x100000;     // 0x100000为低端1M内存
    uint32_t free_mem = all_mem - used_mem;             // 内存管理系统已安排在1MB内

    uint16_t all_free_pages    = free_mem / PG_SIZE;    // 以页为单位的分配,free不需要是4k的倍数
    uint16_t kernel_free_pages = all_free_pages / 2;    // 目前是内核和用户一人一半
    uint16_t user_free_pages   = all_free_pages - kernel_free_pages;
    
    // 为简化位图操作，余数不处理，坏处是这样做会丢内存。
    // 好处是不用做内存的越界检查,因为位图表示的内存少于实际物理内存
    // 一位表示一页，以字节为单位，除以8
    uint32_t kbm_length = kernel_free_pages / 8;        // Kernel BitMap的长度
    uint32_t ubm_length = user_free_pages   / 8;        // User BitMap的长度.
    
    uint32_t kp_start = used_mem;                       // Kernel Pool start,内核内存池的起始地址
    uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE; // 用户池起始地址
    
    kernel_pool.phy_addr_start = kp_start;
    user_pool.phy_addr_start   = up_start;
    
    kernel_pool.pool_size = kernel_free_pages * PG_SIZE;
    user_pool.pool_size   = user_free_pages   * PG_SIZE;
    
    kernel_pool.pool_bitmap.btmp_bytes_len    = kbm_length;
    user_pool.pool_bitmap.btmp_bytes_len      = ubm_length;
    
    // 位图是全局的数据，长度不固定，用数组表示的话需要在编译时知道其长度
    // 而我们需要根据总内存大小算出需要多少字节，所以改为指定一块内存来生成位图
    // bits：uint8* ，所有位所在内存首地址
    kernel_pool.pool_bitmap.bits = (void*)MEM_BITMAP_BASE;
    
    // 用户内存池的位图紧跟在内核内存池位图之后 
    user_pool.pool_bitmap.bits = (void*)(MEM_BITMAP_BASE + kbm_length);
     
    put_str("       kernel_pool_bitmap_start: ");
        put_int((int)kernel_pool.pool_bitmap.bits);
    put_str("\n       kernel_pool_phy_addr_start: ");
        put_int(kernel_pool.phy_addr_start);
    put_str("\n       user_pool_bitmap_start: ");
        put_int((int)user_pool.pool_bitmap.bits);
    put_str("\n       user_pool_phy_addr_start: ");
        put_int(user_pool.phy_addr_start);
   
    bitmap_init(&kernel_pool.pool_bitmap);
    bitmap_init(&user_pool.pool_bitmap);
    
    mutex_init(&kernel_pool.lock);
    mutex_init(&user_pool.lock);

    // 初始化内核虚拟地址的位图，大小等于内核物理池 
    kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_length;     
    // 位图的数组指向一块未使用的内存,目前定位在内核内存池和用户内存池之外
    kernel_vaddr.vaddr_bitmap.bits = (void*)(MEM_BITMAP_BASE + kbm_length + ubm_length);
    
    kernel_vaddr.vaddr_start = K_HEAP_START;
    bitmap_init(&kernel_vaddr.vaddr_bitmap);
    put_str("\n     mem_pool_init done!\n");
}

/** 为malloc做准备，初始化各种规格的 mem_block_desc */
void block_desc_init(struct mem_block_desc* desc_array) {
    uint16_t desc_idx, block_size = 16;
    
    for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx++) {
        desc_array[desc_idx].block_size = block_size;
        
        desc_array[desc_idx].blocks_per_arena = (PG_SIZE - sizeof(struct arena)) / block_size;
        
        list_init(&desc_array[desc_idx].free_list);
        
        block_size <<= 1;         // 更新为下一个规格内存块
    }
}

/** 内存管理部分初始化入口 */
void mem_init() {
    put_str("   mem_init start...\n");
    // 0xb08 就是 loader里获取内存容量后存放的位置
    uint32_t mem_bytes_total = (*(uint32_t*)(GDT_BASE_ADDR + GDT_TOTAL_SIZE)); 
    // put_int(mem_bytes_total/1024);
    // put_str(" KB：当前32位系统，寄存器CF进位,所以显示这个结果。已重新设置系统内存为 4GB。\n");
    // mem_bytes_total = 0xffffffff;
    mem_pool_init(mem_bytes_total);      // 初始化内存池
    block_desc_init(k_block_descs);
    put_str("   mem_init done!\n");
}


