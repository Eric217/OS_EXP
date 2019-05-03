#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "dir.h"
#include "stdint.h"
#include "stdio-kernel.h"
#include "list.h"
#include "string.h"
#include "ide.h"
#include "global.h"
#include "debug.h"
#include "memory.h"

struct partition* cur_part;     // 默认情况下操作的是哪个分区

/** 在分区链表中找到名为part_name的分区,并将其指针赋值给cur_part */
static bool mount_partition(struct list_elem* pelem, int arg) {
    char* part_name = (char*)arg;
    struct partition* part = elem2entry(struct partition, part_tag, pelem);
    if (!strcmp(part->name, part_name)) {
        cur_part = part;
        struct disk* hd = cur_part->my_disk;
        
        // sb_buf用来存储从硬盘上读入的超级块  
        struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE); 
        // 在内存中创建分区cur_part的超级块  
        cur_part->sb = (struct super_block*)sys_malloc(sizeof(struct super_block));
        if (cur_part->sb == NULL || sb_buf == NULL)  
            PANIC("alloc memory failed!"); 
        
        // 读入超级块 
        memset(sb_buf, 0, SECTOR_SIZE);
        ide_read(hd, cur_part->start_lba + 1, sb_buf, 1); 
        // 把sb_buf中超级块的信息复制到分区的超级块sb中 
        memcpy(cur_part->sb, sb_buf, sizeof(struct super_block));
        
        // 将硬盘上的块位图读入到内存  
        cur_part->block_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->block_bitmap_sects * SECTOR_SIZE);
        if (cur_part->block_bitmap.bits == NULL) 
            PANIC("alloc memory failed!");
        cur_part->block_bitmap.btmp_bytes_len = sb_buf->block_bitmap_sects * SECTOR_SIZE;
        // 从硬盘上读入块位图到分区的block_bitmap.bits  
        ide_read(hd, sb_buf->block_bitmap_lba, cur_part->block_bitmap.bits, sb_buf->block_bitmap_sects);
        
        // 将硬盘上的inode位图读入到内存     
        cur_part->inode_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->inode_bitmap_sects * SECTOR_SIZE);
        if (cur_part->inode_bitmap.bits == NULL) 
            PANIC("alloc memory failed!");
        cur_part->inode_bitmap.btmp_bytes_len = sb_buf->inode_bitmap_sects * SECTOR_SIZE;
        // 从硬盘上读入inode位图到分区的inode_bitmap.bits 
        ide_read(hd, sb_buf->inode_bitmap_lba, cur_part->inode_bitmap.bits, sb_buf->inode_bitmap_sects);
         
        list_init(&cur_part->open_inodes);
        printk("MOUNT %s DONE!\n", part->name);   
 
        // 使 list_traversal 停止遍历 
        return true;
    }
    return false;   // 使 list_traversal 继续遍历
}

/** 格式化分区：初始化分区的元信息，创建文件系统 */
static void partition_format(struct partition* part) {

    uint32_t boot_sector_sects = 1; // OBR，直接固定 1 扇区
    uint32_t super_block_sects = 1; // SB
    
    uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);  
    uint32_t inode_table_sects = DIV_ROUND_UP(((sizeof(struct inode) * MAX_FILES_PER_PART)), SECTOR_SIZE);
    
    uint32_t used_sects = boot_sector_sects + super_block_sects + inode_bitmap_sects + inode_table_sects;
    uint32_t free_sects = part->sec_cnt - used_sects;
    
    // 块位图与空闲块共享 free sects，下面简单处理一下（大多情况下有冗余空间）
    uint32_t block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR);
    uint32_t block_bitmap_bit_len = free_sects - block_bitmap_sects;
    block_bitmap_sects = DIV_ROUND_UP(block_bitmap_bit_len, BITS_PER_SECTOR);
    
    // 超级块初始化
    struct super_block sb;
    sb.magic = 0x19590318;
    sb.sec_cnt = part->sec_cnt;
    sb.inode_cnt = MAX_FILES_PER_PART;
    sb.part_lba_base = part->start_lba;
    
    sb.block_bitmap_lba = sb.part_lba_base + 2; // 第0块是引导块，第1块是超级块
    sb.block_bitmap_sects = block_bitmap_sects;
    
    sb.inode_bitmap_lba = sb.block_bitmap_lba + sb.block_bitmap_sects;
    sb.inode_bitmap_sects = inode_bitmap_sects;
    
    sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_bitmap_sects;
    sb.inode_table_sects = inode_table_sects;
    
    sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects;
    sb.root_inode_no = 0;
    sb.dir_entry_size = sizeof(struct dir_entry);
    
    printk("%s info:\n", part->name);
    printk("   block_bitmap_lba:0x%x\n   block_bitmap_sectors:0x%x\n",
        sb.block_bitmap_lba, sb.block_bitmap_sects);
    printk("   inode_bitmap_lba:0x%x\n   inode_bitmap_sectors:0x%x\n",
        sb.inode_bitmap_lba, sb.inode_bitmap_sects);
    printk("   inode_table_lba:0x%x\n   inode_table_sectors:0x%x\n",
        sb.inode_table_lba, sb.inode_table_sects);
    printk("   data_start_lba:0x%x\n", sb.data_start_lba);

    struct disk* hd = part->my_disk;
    ide_write(hd, part->start_lba + 1, &sb, 1);
    printk("   super_block_lba:0x%x\n", part->start_lba + 1);
    
    // 找出数据量最大的元信息，其尺寸作为缓冲区的尺寸
    uint32_t buf_size = sb.block_bitmap_sects >= sb.inode_bitmap_sects ?
        sb.block_bitmap_sects : sb.inode_bitmap_sects;
    buf_size = (buf_size >= sb.inode_table_sects ? buf_size : sb.inode_table_sects) * SECTOR_SIZE;
    uint8_t* buf = (uint8_t*)sys_malloc(buf_size);  // 申请的内存由内存管理系统清0后返回
    if (buf == NULL)  
        PANIC("alloc memory failed!");
   
    // 将 块位图 初始化并写入 sb.block_bitmap_lba 
    // 因为挂载时 想直接通过 位图占用扇区数 * 扇区位数 得到 block_bitmap_bit_len，
    // 即使可以通过和前面相同步骤计算出这个值，或者直接存在 sb 中，但是不雅
    // 所以需要把 块位图最后一个扇区末尾多余的位置 1
    // 第0个块预留给根目录，位图中先占位
    buf[0] |= 1;      
    // 回想位图在内存中的形式、存取方式：一格内存，从右至左
    uint32_t block_bitmap_last_byte = block_bitmap_bit_len / 8;
    uint8_t  block_bitmap_last_bit  = block_bitmap_bit_len % 8;
    uint32_t last_size = SECTOR_SIZE - (block_bitmap_last_byte % SECTOR_SIZE);      
    // last_size是位图所在最后一个扇区中，不足一扇区的其余部分
    // 1 先将位图最后一字节到其所在的扇区的结束全置为1 
    memset(&buf[block_bitmap_last_byte], 0xff, last_size);
    // 2 再将上一步中覆盖的最后一字节内的有效位重新置0 
    uint8_t bit_idx = 0; 
    while (bit_idx < block_bitmap_last_bit) {
        buf[block_bitmap_last_byte] &= ~(1 << bit_idx++); 
    }
    ide_write(hd, sb.block_bitmap_lba, buf, sb.block_bitmap_sects);
    memset(buf, 0, buf_size);

    // 将 inode位图 初始化并写入 sb.inode_bitmap_lba  
    // 第0个inode分给根目录
    buf[0] |= 1;
    // inode_table 大小为 4096，故 inode_bitmap 恰好占用1扇区，没有多余无效位
    // 最好按上面的步骤走，但是本系统就固定最多 4096 文件，写死吧
    ide_write(hd, sb.inode_bitmap_lba, buf, sb.inode_bitmap_sects);
    memset(buf, 0, buf_size);

    // 将 inode数组 初始化并写入 sb.inode_table_lba 
    // 准备 根目录的 inode
    struct inode* i = (struct inode*)buf;
    i->i_size = sb.dir_entry_size * 2;   // .和..
    i->i_no = 0;
    i->i_sectors[0] = sb.data_start_lba; 
    ide_write(hd, sb.inode_table_lba, buf, sb.inode_table_sects);
    memset(buf, 0, buf_size);

    // 将 根目录 初始化，其目录项列表写入 sb.data_start_lba    
    // 准备根目录的两个目录项 . 和 .. 
    struct dir_entry* p_de = (struct dir_entry*)buf;
    // 初始化 "."
    memcpy(p_de->filename, ".", 1);
    p_de->i_no = 0;
    p_de->f_type = FT_DIRECTORY;
    p_de++;
    // 初始化 ".."
    memcpy(p_de->filename, "..", 2);
    p_de->i_no = 0;   // 根目录的父目录 是根目录
    p_de->f_type = FT_DIRECTORY;
    ide_write(hd, sb.data_start_lba, buf, 1);
    
    printk("   root_dir_lba:0x%x\n", sb.data_start_lba);
    printk("%s format done\n", part->name);
    sys_free(buf);
}

/* 在磁盘上搜索文件系统,若没有则格式化分区创建文件系统 */
void filesys_init() {
    uint8_t channel_no = 0, dev_no, part_idx = 0;
    
    // sb_buf 用来存储从硬盘上读入的超级块 
    struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE);
    if (sb_buf == NULL) 
        PANIC("alloc memory failed!");
    printk("\n   Searching filesystem...\n");

    while (channel_no < channel_cnt) {
        dev_no = 0;
        while(dev_no < 2) {
            if (dev_no == 0) {  // 跨过裸盘hd60M.img
                dev_no++;
                continue;
            }
            struct disk* hd = &channels[channel_no].devices[dev_no];
            struct partition* part = hd->prim_parts;
            while(part_idx < 12) {   // 4个主分区+8个逻辑
                if (part_idx == 4) {  // 开始处理逻辑分区
                    part = hd->logic_parts;
                }
                // 全局变量 默认值为0
                // 如果分区存在
                if (part->sec_cnt) {  
                    memset(sb_buf, 0, SECTOR_SIZE);
                    // 读出分区的超级块，根据魔数判断是否存在文件系统
                    ide_read(hd, part->start_lba + 1, sb_buf, 1);
                    
                    // 现在只支持自己的文件系统
                    // 否则，一律格式化
                    if (sb_buf->magic == 0x19590318) {
                        printk("     %s found valid filesystem!\n", part->name);
                    } else {           // 其它文件系统不支持,一律按无文件系统处理
                        printk("  formatting %s's partition %s...\n", hd->name, part->name);
                        partition_format(part);
                    }
                }
                part_idx++;
                part++; // 下一分区
            }
            dev_no++;  // 下一磁盘
        }
        channel_no++;  // 下一通道
    }
    sys_free(sb_buf);

    // 挂载默认分区 sdb1
    char *default_part = channels[0].devices[1].prim_parts[0].name; 
    list_traversal(&partition_list, mount_partition, (int)default_part);
}


