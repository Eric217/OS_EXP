#include "inode.h"
#include "fs.h"
#include "file.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "interrupt.h"
#include "list.h"
#include "stdio-kernel.h"
#include "string.h"
#include "super_block.h"

/* 用来存储inode位置 */
struct inode_position {
    bool     two_sec;   // inode是否跨扇区
    uint32_t sec_lba;   // inode所在的扇区号
    uint32_t off_size;  // inode在扇区内的字节偏移量
};

/** 获取inode所在的扇区和扇区内的偏移量，写入到 arg3 中 */
static void inode_locate(struct partition* part, uint32_t inode_no, struct inode_position* inode_pos) {
    // inode_table在硬盘上是连续的
    ASSERT(inode_no < 4096);
    uint32_t inode_table_lba = part->sb->inode_table_lba;
    
    uint32_t inode_size = sizeof(struct inode);
    uint32_t off_size = inode_no * inode_size;      // 第inode_no号I结点相对于inode_table_lba的字节偏移量
    uint32_t off_sec  = off_size / 512;             // 第inode_no号I结点相对于inode_table_lba的扇区偏移量
    uint32_t off_size_in_sec = off_size % 512;      // 待查找的inode所在扇区中的起始地址
    
    // 判断此i结点是否跨越2个扇区
    inode_pos->two_sec = 512 - off_size_in_sec < inode_size; 
    inode_pos->sec_lba = inode_table_lba + off_sec;
    inode_pos->off_size = off_size_in_sec;
}

/** 将 inode 更新到硬盘中，io_buf 是用于硬盘io的缓冲区 */
void inode_sync(struct partition* part, struct inode* inode, void* io_buf) {     
    uint8_t inode_no = inode->i_no;
    struct inode_position inode_pos;
    inode_locate(part, inode_no, &inode_pos);  
    ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));
    
    struct inode pure_inode;
    memcpy(&pure_inode, inode, sizeof(struct inode));
    // inode 的成员 inode_tag 和 i_open_cnts 只存在于内存中，记录在链表中的位置 和 被多少进程共享
    // 存入硬盘时置它们为默认值    
    pure_inode.i_open_cnts = 0;
    pure_inode.write_deny = false;  // false，保证在硬盘中读出时为可写
    pure_inode.inode_tag.prev = pure_inode.inode_tag.next = NULL;
    
    char* inode_buf = (char*)io_buf;
    // 将待更新的 inode 写入扇区 (读写硬盘以扇区为单位)
    if (inode_pos.two_sec) {        // inode 跨两个扇区时 
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2); 
        memcpy((inode_buf + inode_pos.off_size), &pure_inode, sizeof(struct inode)); 
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    } else {               
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
        memcpy((inode_buf + inode_pos.off_size), &pure_inode, sizeof(struct inode));
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }
}

/** 根据 inode号 返回相应的 inode */
struct inode* inode_open(struct partition* part, uint32_t inode_no) {
    // 先在 inode 缓冲链表中找 
    struct list_elem* elem = part->open_inodes.head.next;
    struct inode* inode_found;
    while (elem != &part->open_inodes.tail) {
        inode_found = elem2entry(struct inode, inode_tag, elem);
        if (inode_found->i_no == inode_no) {
            inode_found->i_open_cnts++;
            return inode_found;
        }
        elem = elem->next;
    }
    // 链表中没有缓存，从硬盘上读入 并加到链表
    struct inode_position inode_pos;   
    inode_locate(part, inode_no, &inode_pos);
    
    // 为使 sys_malloc 新创建的 inode 被所有任务共享，需要在内核空间中分配
    // 因此将 cur_pbc->pgdir 临时置 NULL；这个过程中不可以任务切换（时钟中断） 
    struct task_struct* cur = running_thread();
    uint32_t* cur_pagedir_bak = cur->pgdir;
    enum intr_status old_status = intr_disable();
    cur->pgdir = NULL;
    inode_found = (struct inode*)sys_malloc(sizeof(struct inode));
    // 完成在内核空间分配后 恢复pgdir
    cur->pgdir = cur_pagedir_bak;
    intr_set_status(old_status);

    char* inode_buf;
    if (inode_pos.two_sec) {    // 跨扇区时
        inode_buf = (char*)sys_malloc(1024);
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    } else {     
        inode_buf = (char*)sys_malloc(512);
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }
    memcpy(inode_found, inode_buf + inode_pos.off_size, sizeof(struct inode));
    
    // 因为一会很可能要用到此inode，故将其插入到队首便于提前检索到 
    list_push(&part->open_inodes, &inode_found->inode_tag);
    inode_found->i_open_cnts = 1;
    
    sys_free(inode_buf);
    return inode_found;
}

/** 关闭或减少 inode 的打开数 */
void inode_close(struct inode* inode) {
    enum intr_status old_status = intr_disable();
    // 若没有进程打开此文件，释放此 inode 
    if (--inode->i_open_cnts == 0) {    
        list_remove(&inode->inode_tag);  
        struct task_struct* cur = running_thread();
        uint32_t* cur_pagedir_bak = cur->pgdir;
        cur->pgdir = NULL;
        sys_free(inode);
        cur->pgdir = cur_pagedir_bak;
    }
    intr_set_status(old_status);
}

/** 初始化new_inode */
void inode_init(uint32_t inode_no, struct inode* new_inode) {
    new_inode->i_no = inode_no;
    new_inode->i_size = 0;
    new_inode->i_open_cnts = 0;
    new_inode->write_deny = false;
     
    uint8_t sec_idx = 0;
    while (sec_idx < 13) { 
        new_inode->i_sectors[sec_idx] = 0;
        sec_idx++;
    }
}

