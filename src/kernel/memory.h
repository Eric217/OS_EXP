#ifndef __KERNEL_MEMORY_H
#define __KERNEL_MEMORY_H
#include "stdint.h"
#include "bitmap.h"
#include "list.h"

#define PG_P_1  1    // 页表项或页目录项存在属性位
#define PG_P_0  0    // 页表项或页目录项存在属性位
#define PG_RW_R 0    // R/W 属性位值, 读/执行
#define PG_RW_W 2    // R/W 属性位值, 读/写/执行
#define PG_US_S 0    // U/S 属性位值, 系统级
#define PG_US_U 4    // U/S 属性位值, 用户级

#define DESC_CNT 7

/* 内存池标记,用于判断用哪个内存池 */
enum pool_flags {
    PF_KERNEL = 1,    // 内核内存池
    PF_USER = 2         // 用户内存池
};

/// 虚拟地址池，用于虚拟地址管理
struct virtual_addr {
    struct bitmap vaddr_bitmap; // 虚拟地址用到的位图结构
    uint32_t vaddr_start;       // 虚拟地址 起始地址
};

/* 内存块 */
struct mem_block {
    struct list_elem free_elem; // 此结构暂存在空闲块中，当空闲块被分配，即从队列中移除时，这块内存被清0
};

/* 内存块描述符 */
struct mem_block_desc {
    uint32_t block_size;        // 内存块大小
    uint32_t blocks_per_arena;  // 本arena中可容纳此mem_block的数量.
    struct list free_list;      // 目前可用的mem_block链表
};

/* 内存仓库arena元信息 */
struct arena {
    struct mem_block_desc* desc;     // 此 arena 关联的 mem_block_desc
    uint32_t cnt; // large 为 ture 表示页框数量；否则表示空闲 mem_block 数量
    bool large;
};

extern struct pool kernel_pool, user_pool;

void mem_init(void);
void malloc_init(void);
void block_desc_init(struct mem_block_desc* desc_array);

void* get_pages(uint32_t pg_cnt, enum pool_flags flag);
void* get_one_page(enum pool_flags flag, uint32_t vaddr);
void* malloc_page(enum pool_flags pf, uint32_t pg_cnt);

uint32_t* pte_ptr(uint32_t vaddr);
uint32_t* pde_ptr(uint32_t vaddr);
uint32_t addr_v2p(uint32_t vaddr);

void mfree_page(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt);
void pfree(uint32_t pg_phy_addr);

void* sys_malloc(uint32_t size);
void  sys_free(void* ptr);


#endif

