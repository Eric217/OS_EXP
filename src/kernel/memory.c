#include "memory.h"
#include "stdint.h"
#include "print.h"
#include "bitmap.h"
#include "string.h"
#include "global.h"
#include "debug.h"
#include "interrupt.h"

#define PG_SIZE 4096

/***************  位图地址 ********************
 * 因为0xc009f000是内核主线程栈顶，0xc009e000是内核主线程的pcb.
 * 一个页框大小的位图32K bit * 4KB = 128M 可表示128M内存,
 * 位图安排在地址 0xc009a000,这样系统最大支持4个页框的位图,即512M */
#define MEM_BITMAP_BASE 0xc009a000

#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22) // high 10 bit
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12) // mid 10 bit

/* 内核从虚拟地址3GB(0xc0000000)起,0x100000意指跨过低端1M内存,使虚拟地址在逻辑上连续 */
#define K_HEAP_START 0xc0100000

/* 内存池结构,生成两个实例用于管理内核内存池和用户内存池 */
struct pool {
    struct bitmap pool_bitmap;  // 本内存池用到的位图结构,用于管理物理内存
    uint32_t phy_addr_start;    // 本内存池所管理物理内存的起始地址
    uint32_t pool_size;         // 本内存池字节容量
};  

struct pool kernel_pool, user_pool;     // 生成内核内存池和用户内存池
struct virtual_addr kernel_vaddr;       // 此结构是用来给内核分配虚拟地址

/* 在pf表示的虚拟内存池中申请pg_cnt个虚拟页,
 * 成功则返回虚拟页的起始地址, 失败则返回NULL */
static void* vaddr_get(enum pool_flags pf, uint32_t pg_cnt) {
    int vaddr_start = 0, bit_idx_start = -1;
    uint32_t cnt = 0;
    if (pf == PF_KERNEL) {
        enum intr_status old_status = intr_disable();

        bit_idx_start  = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1) {
            intr_set_status(old_status);
            return NULL;
        }
        while(cnt < pg_cnt) 
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
        intr_set_status(old_status);

        vaddr_start = kernel_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
    } else {
        // 用户内存池 


    }
    return (void*)vaddr_start;
}

/* 得到虚拟地址vaddr对应的pte指针*/
uint32_t* pte_ptr(uint32_t vaddr) {
    /* 先访问到页表自己 + \
     * 再用页目录项pde(页目录内页表的索引)做为pte的索引访问到页表 + \
     * 再用pte的索引做为页内偏移*/
    uint32_t* pte = (uint32_t*)(0xffc00000 + \
                                ((vaddr & 0xffc00000) >> 10) + \
                                PTE_IDX(vaddr) * 4);
    return pte;
}

/* 得到虚拟地址vaddr对应的pde的指针 */
uint32_t* pde_ptr(uint32_t vaddr) {
    /* 0xfffff是用来访问到页表本身所在的地址 */
    uint32_t* pde = (uint32_t*)((0xfffff000) + PDE_IDX(vaddr) * 4);
    return pde;
}

/* 在m_pool指向的物理内存池中分配1个物理页,
 * 成功则返回页框的物理地址,失败则返回NULL */
static void* palloc(struct pool* m_pool) {
    enum intr_status old_status = intr_disable();

    int bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1);    // 找一个物理页面
    if (bit_idx == -1 ) {
        intr_set_status(old_status);
        return NULL;
    }
    bitmap_set(&m_pool->pool_bitmap, bit_idx, 1);    // 将此位bit_idx置1
    intr_set_status(old_status);

    uint32_t page_phyaddr = ((bit_idx * PG_SIZE) + m_pool->phy_addr_start);
    return (void*)page_phyaddr;
}

/* 页表中添加虚拟地址_vaddr与物理地址_page_phyaddr的映射 */
static void page_table_add(void* _vaddr, void* _page_phyaddr) {
    uint32_t vaddr = (uint32_t)_vaddr, page_phyaddr = (uint32_t)_page_phyaddr;
    uint32_t* pde = pde_ptr(vaddr);
    uint32_t* pte = pte_ptr(vaddr);

    enum intr_status old_status = intr_disable();
    // 如果虚拟地址对应的页目录项不存在，需要先创建PDE
    if (!(*pde & 0x00000001)) {  // 页目录项和页表项的第0位为P,此处判断是否存在
 
        // 页表中用到的页框一律从内核空间分配；有可能两个线程都走到此，因此要关中断
        uint32_t pde_phyaddr = (uint32_t)palloc(&kernel_pool); // TODO: - ？？？
        
        *pde = (pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1);

        // 分配到的物理页（即将用作页表）清0
        // pte 低12位 置0 即该物理页起始地址 
        memset((void*)((int)pte & 0xfffff000), 0, PG_SIZE);
    }  
    intr_set_status(old_status);

    if (*pte & 0x00000001) { // 这里应该没有PTE
        PANIC("pte repeat\n");
    }
    *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);      // US=1,RW=1,P=1
   
}

/* 分配pg_cnt个页空间,成功则返回起始虚拟地址,失败时返回NULL */
void* malloc_page(enum pool_flags pf, uint32_t pg_cnt) {
    ASSERT(pg_cnt > 0 && pg_cnt < 3840); // 假设15MB可用，一页4K，则3840页
    /* 
     1 在虚拟内存池中申请虚拟地址
     2 在物理内存池中申请物理页
     3 虚拟地址和物理地址在页表中完成映射
    */
    void* vaddr_start = vaddr_get(pf, pg_cnt);
    if (vaddr_start == NULL) 
        return NULL;
    
    uint32_t vaddr = (uint32_t)vaddr_start, cnt = pg_cnt;
    struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
    
    /* 因为虚拟地址是连续的,但物理地址可以是不连续的,所以逐个做映射*/
    while (cnt-- > 0) {
        void* page_phyaddr = palloc(mem_pool);
        if (page_phyaddr == NULL) {  // 失败时要将已申请的虚拟地址和物理页 rollback
            // 1 虚拟池位图置0
            // 2 添加的页表项置0
            uint32_t c = 0;
            uint32_t bit_idx_start = ((uint32_t)vaddr_start - kernel_vaddr.vaddr_start)/PG_SIZE;

            enum intr_status old_status = intr_disable();
            while(c < pg_cnt) 
                bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + c++, 0);
            
            c = pg_cnt - cnt - 1; 
            for (c = pg_cnt - cnt - 1; c > 0; c--) {
                vaddr -= PG_SIZE;
                *pte_ptr(vaddr) = 0;
            }
            intr_set_status(old_status);
  
            return NULL;
        }
        page_table_add((void*)vaddr, page_phyaddr); // 在页表中做映射
        vaddr += PG_SIZE;      // 下一个虚拟页
    }
    return vaddr_start;
}

/* 从内核物理内存池中申请pg_cnt页内存,成功则返回其虚拟地址,失败则返回NULL */
void* get_kernel_pages(uint32_t pg_cnt) {
    void* vaddr =  malloc_page(PF_KERNEL, pg_cnt);
    if (vaddr != NULL) {    // 若分配的地址不为空,将页框清0后返回
        memset(vaddr, 0, pg_cnt * PG_SIZE);
    }
    return vaddr;
}

/* 初始化内存池 */
static void mem_pool_init(uint32_t all_mem) {
    put_str("   mem_pool_init start...\n");
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
    uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE; // User Pool start，用户内存池的起始地址
    
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
     
    put_str("      kernel_pool_bitmap_start: ");put_int((int)kernel_pool.pool_bitmap.bits);
    put_str(" kernel_pool_phy_addr_start: ");put_int(kernel_pool.phy_addr_start);
    put_str("\n");
    put_str("      user_pool_bitmap_start: ");put_int((int)user_pool.pool_bitmap.bits);
    put_str(" user_pool_phy_addr_start: ");put_int(user_pool.phy_addr_start);
    put_str("\n");
   
    bitmap_init(&kernel_pool.pool_bitmap);
    bitmap_init(&user_pool.pool_bitmap);
    
    // 初始化内核虚拟地址的位图，大小等于内核物理池 
    kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_length;     
    // 位图的数组指向一块未使用的内存,目前定位在内核内存池和用户内存池之外
    kernel_vaddr.vaddr_bitmap.bits = (void*)(MEM_BITMAP_BASE + kbm_length + ubm_length);
    
    kernel_vaddr.vaddr_start = K_HEAP_START;
    bitmap_init(&kernel_vaddr.vaddr_bitmap);
    put_str("   mem_pool_init done!\n");
}

/* 内存管理部分初始化入口 */
void mem_init() {
    put_str("mem_init start...\n");
    // 0xb08 就是 loader里获取内存容量后存放的位置
    uint32_t mem_bytes_total = (*(uint32_t*)(0xb08)); 
    // put_int(mem_bytes_total/1024);
    // put_str(" KB：当前32位系统，寄存器进位都丢失了所以显示这个结果。已重新设置系统内存为 4GB。\n");
    // mem_bytes_total = 0xffffffff;
    mem_pool_init(mem_bytes_total);      // 初始化内存池
    put_str("mem_init done!\n");
}


