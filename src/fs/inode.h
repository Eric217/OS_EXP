#ifndef __FS_INODE_H
#define __FS_INODE_H
#include "stdint.h"
#include "list.h"

/** inode 结构 */
struct inode {

    uint32_t i_no;			// inode编号

    uint32_t i_size; 		// 当inode代表文件：指文件大小；目录：指该目录下所有目录项大小之和。（字节为单位）
    
    uint32_t i_open_cnts; 	// 记录此文件被打开的次数

    bool 	 write_deny;   	// 写文件不能并行，进程写文件前检查此标识
    
    // 本系统中块大小为 512 字节，即一个扇区；仅支持一级间接块索引表。（故最大文件大小为 140*512B = 70KB
    uint32_t i_sectors[13]; // 0～11 个是直接块, 最后一个用来存储一级间接块索引表指针

    struct list_elem inode_tag;
};

struct inode* inode_open(struct partition* part, uint32_t inode_no);
void inode_sync(struct partition* part, struct inode* inode, void* io_buf);
void inode_close(struct inode* inode);

void inode_init(uint32_t inode_no, struct inode* new_inode);

#endif

