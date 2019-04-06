#include "ide.h"
#include "stdio.h"
#include "stdio-kernel.h"
#include "interrupt.h"
#include "memory.h"
#include "debug.h"
#include "string.h"
#include "io.h"
#include "timer.h"

/* 定义硬盘各寄存器的端口号 */
#define reg_data(channel)       (channel->port_base + 0)
#define reg_error(channel)      (channel->port_base + 1)
#define reg_sect_cnt(channel)   (channel->port_base + 2)
#define reg_lba_l(channel)      (channel->port_base + 3)
#define reg_lba_m(channel)      (channel->port_base + 4)
#define reg_lba_h(channel)      (channel->port_base + 5)
#define reg_dev(channel)        (channel->port_base + 6)
#define reg_status(channel)     (channel->port_base + 7)
#define reg_cmd(channel)        (reg_status(channel))
#define reg_alt_status(channel) (channel->port_base + 0x206)
#define reg_ctl(channel)        reg_alt_status(channel)

/* reg_alt_status寄存器的一些关键位 */
#define BIT_STAT_BSY    0x80        // 硬盘忙
#define BIT_STAT_DRDY   0x40        // 驱动器准备好
#define BIT_STAT_DRQ    0x8         // 数据传输准备好了

/* device寄存器的一些关键位 */
#define BIT_DEV_MBS     0xa0        // 第7位和第5位固定为1
#define BIT_DEV_LBA     0x40
#define BIT_DEV_DEV     0x10

/* 一些硬盘操作的指令 */
#define CMD_IDENTIFY        0xec    // identify指令
#define CMD_READ_SECTOR     0x20    // 读扇区指令
#define CMD_WRITE_SECTOR    0x30    // 写扇区指令

uint8_t channel_cnt;                // 按硬盘数计算的通道数
struct ide_channel channels[2];     // 有两个ide通道。假设主板上只有一对主次通道

int32_t ext_lba_base = 0;       // 用于记录总扩展分区的起始lba；标记是否第一次扫描
uint8_t p_no = 0, l_no = 0;     // 用来记录硬盘主分区和逻辑分区的下标
struct list partition_list;     // 所有分区队列

/** 分区表项结构，16字节大小 */
struct partition_table_entry {
    uint8_t  bootable;      // 是否可引导
    uint8_t  start_head;    // 起始磁头号
    uint8_t  start_sec;     // 起始扇区号
    uint8_t  start_chs;     // 起始柱面号
    uint8_t  fs_type;       // 分区类型
    uint8_t  end_head;      // 结束磁头号
    uint8_t  end_sec;       // 结束扇区号
    uint8_t  end_chs;       // 结束柱面号
    
    uint32_t start_lba;     // 本分区起始扇区的lba地址
    uint32_t sec_cnt;       // 本分区的扇区数目
} __attribute__ ((packed)); // 保证此结构 16字节 

/** 引导扇区，mbr或ebr所在的扇区 */
struct boot_sector {
    uint8_t  other[446];    // 引导代码
    struct   partition_table_entry partition_table[4];  // 分区表，4个表项
    uint16_t signature;     // 引导扇区结束标志 0x55,0xaa
} __attribute__ ((packed));


/** 1.0 向硬盘控制器写入 主/从 盘 */
static void select_disk(struct disk* hd) {
    uint8_t reg_device = BIT_DEV_MBS | BIT_DEV_LBA;
    if (hd->dev_no == 1) {          // 从盘 DEV 位为1
        reg_device |= BIT_DEV_DEV;
    }
    outb(reg_dev(hd->my_channel), reg_device);
}

/** 1.1 向硬盘控制器写入 起始扇区地址 及要读写的 扇区数(0 for 256) */
static void select_sector(struct disk* hd, uint32_t lba, uint8_t sec_cnt) {    
    struct ide_channel* channel = hd->my_channel;
    // sec_cnt为0则表示写入256个扇区
    outb(reg_sect_cnt(channel), sec_cnt);
    // lba地址低8位(不需单独取出低8位，指令 outb %b0, %w1 只用al)
    outb(reg_lba_l(channel), lba);
    outb(reg_lba_m(channel), lba >> 8);      // 8~15位
    outb(reg_lba_h(channel), lba >> 16);     // 16~23位
    // lba地址24~27位存储在device寄存器的0～3位
    outb(reg_dev(channel), BIT_DEV_MBS | BIT_DEV_LBA | (hd->dev_no == 1 ? BIT_DEV_DEV : 0) | lba >> 24);
}

/** 2 发出命令（磁盘开始工作）*/
static void cmd_out(struct ide_channel* channel, uint8_t cmd) {
    // 只要向硬盘发出了命令 便将此标记置为true，硬盘中断处理程序需要根据它来判断
    channel->expecting_intr = true;
    outb(reg_cmd(channel), cmd);
}

/** 3
 a 读：执行 P操作，等待硬盘读数据到硬盘缓冲区，完成后产生中断，中断返回后等待硬盘响应，最多31秒，然后搬数据到内存 
 b 写：等待硬盘响应，把数据写到硬盘缓冲区，P操作，等待硬盘把缓冲区的数据写到磁盘，完成后中断、返回
 */
static bool busy_wait(struct disk* hd) {
    struct ide_channel* channel = hd->my_channel;
    uint16_t time_limit = 30 * 1000;      // 可以等待30000毫秒
    while (time_limit -= 10 >= 0) {
        if (!(inb(reg_status(channel)) & BIT_STAT_BSY)) {
            return (inb(reg_status(channel)) & BIT_STAT_DRQ);
        } else {
            msleep(10);            // 睡眠10毫秒
        }
    }
    return false;
}

/** 缓冲区 -> 内存，sec_cnt 个扇区 */
static void read_from_sector(struct disk* hd, void* buf, uint8_t sec_cnt) {
    uint32_t size_in_byte;
    if (sec_cnt == 0) {
        size_in_byte = 256 * 512; // 8位，256会丢最高位1变为0 
    } else {
        size_in_byte = sec_cnt * 512;
    }
    // 字长 16
    insw(reg_data(hd->my_channel), buf, size_in_byte / 2);
}

/** 内存 -> 缓冲区，sec_cnt 个扇区 */
static void write2sector(struct disk* hd, void* buf, uint8_t sec_cnt) {
    uint32_t size_in_byte;
    if (sec_cnt == 0) {
        size_in_byte = 256 * 512;
    } else {
        size_in_byte = sec_cnt * 512;
    }
    outsw(reg_data(hd->my_channel), buf, size_in_byte / 2);
}
 
/* 从硬盘读取sec_cnt个扇区到buf */
void ide_read(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt) {
   
    ASSERT(sec_cnt > 0);
    mutex_lock(&hd->my_channel->lock);
    
    select_disk(hd); // 1.0
    
    uint32_t secs_op;         // 每次操作的扇区数
    uint32_t secs_done = 0;   // 已完成的扇区数
    while(secs_done < sec_cnt) {  // 硬盘一次最多操作256个扇区，可能分多次读
        if ((secs_done + 256) <= sec_cnt) {
            secs_op = 256;
        } else {
            secs_op = sec_cnt - secs_done;
        }
    
        select_sector(hd, lba + secs_done, secs_op);    // 1.1
        cmd_out(hd->my_channel, CMD_READ_SECTOR);       // 2 
        sema_p(&hd->my_channel->disk_done);             // 3
        if (!busy_wait(hd)) {  // 没有响应，一般是修不好的，如硬盘炸了，PANIC
            char error[64]; sprintf(error, "%s read sector %d failed!!!!!!\n", hd->name, lba);
            PANIC(error);
        }
        // 4.a 从驱动器缓冲区读到内存
        read_from_sector(hd, (void*)((uint32_t)buf + secs_done * 512), secs_op);
        secs_done += secs_op;
    }
    mutex_unlock(&hd->my_channel->lock);
}

/* 将buf中sec_cnt扇区数据写入硬盘 */
void ide_write(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt) {
   
    ASSERT(sec_cnt > 0);
    mutex_lock(&hd->my_channel->lock);
    
    select_disk(hd);
    
    uint32_t secs_op;         // 每次操作的扇区数
    uint32_t secs_done = 0;   // 已完成的扇区数
    while(secs_done < sec_cnt) {
        if ((secs_done + 256) <= sec_cnt) {
            secs_op = 256;
        } else {
            secs_op = sec_cnt - secs_done;
        }
         
        select_sector(hd, lba + secs_done, secs_op); 
        cmd_out(hd->my_channel, CMD_WRITE_SECTOR);    
        if (!busy_wait(hd)) { 
            char error[64];
            sprintf(error, "%s write sector %d failed!!!!!!\n", hd->name, lba);
            PANIC(error);
        }
         
        write2sector(hd, (void*)((uint32_t)buf + secs_done * 512), secs_op);
        // 硬盘开始工作
        sema_down(&hd->my_channel->disk_done);
        secs_done += secs_op;
    }
    /* 醒来后开始释放锁*/
    mutex_unlock(&hd->my_channel->lock);
}

/** 将dst中len个相邻字节交换位置后存入buf。读数据时字长为单位，而相邻字节位置是互换的 */
static void swap_pairs_bytes(const char* dst, char* buf, uint32_t len) {
    uint8_t idx;
    for (idx = 0; idx < len; idx += 2) {
        /* buf中存储dst中两相邻元素交换位置后的字符串*/
        buf[idx + 1] = *dst++;
        buf[idx]     = *dst++;
    }
    buf[idx] = '\0';
}

/** 获得硬盘参数信息 */
static void identify_disk(struct disk* hd) {
    char id_info[512];
    select_disk(hd);
    cmd_out(hd->my_channel, CMD_IDENTIFY);
    sema_p(&hd->my_channel->disk_done);
    
    if (!busy_wait(hd)) {     //  若失败
        char error[64];
        sprintf(error, "%s identify failed!!!!!!\n", hd->name);
        PANIC(error);
    }
    read_from_sector(hd, id_info, 1);
    
    char buf[64];
    uint8_t sn_start = 10 * 2, sn_len = 20, md_start = 27 * 2, md_len = 40;
    swap_pairs_bytes(id_info + sn_start, buf, sn_len);
    printk("       disk %s info:\n       SN: %s\n", hd->name, buf);
    memset(buf, 0, sizeof(buf));
    swap_pairs_bytes(id_info + md_start, buf, md_len);
    printk("       MODULE: %s\n", buf);
    uint32_t sectors = *(uint32_t*)(id_info + 60*2);
    printk("       SECTORS: %d\n", sectors);
    printk("       CAPACITY: %dMB\n", sectors * 512 / 1024 / 1024);
}

/** 扫描硬盘中 地址为ext_lba的扇区中 的所有分区 */
static void partition_scan(struct disk* hd, uint32_t ext_lba) {
    struct boot_sector* bs = sys_malloc(sizeof(struct boot_sector));
    ide_read(hd, ext_lba, bs, 1);
    uint8_t part_idx = 0;
    struct partition_table_entry* p = bs->partition_table;
    
    /* 遍历分区表4个分区表项 */
    while (part_idx++ < 4) {
        if (p->fs_type == 0x5) {     // 若为扩展分区
            if (ext_lba_base != 0) {
                /* 子扩展分区的start_lba是相对于主引导扇区中的总扩展分区地址 */
                partition_scan(hd, p->start_lba + ext_lba_base);
            } else { // ext_lba_base为0表示是第一次读取引导块,也就是主引导记录所在的扇区
                /* 记录下扩展分区的起始lba地址,后面所有的扩展分区地址都相对于此 */
                ext_lba_base = p->start_lba;
                partition_scan(hd, p->start_lba);
            }
        } else if (p->fs_type != 0) { // 若是有效的分区类型
            if (ext_lba == 0) {     // 此时全是主分区
                hd->prim_parts[p_no].start_lba = ext_lba + p->start_lba;
                hd->prim_parts[p_no].sec_cnt = p->sec_cnt;
                hd->prim_parts[p_no].my_disk = hd;
                list_append(&partition_list, &hd->prim_parts[p_no].part_tag);
                sprintf(hd->prim_parts[p_no].name, "%s%d", hd->name, p_no + 1);
                p_no++;
                ASSERT(p_no < 4);        // 0,1,2,3
            } else {
                hd->logic_parts[l_no].start_lba = ext_lba + p->start_lba;
                hd->logic_parts[l_no].sec_cnt = p->sec_cnt;
                hd->logic_parts[l_no].my_disk = hd;
                list_append(&partition_list, &hd->logic_parts[l_no].part_tag);
                sprintf(hd->logic_parts[l_no].name, "%s%d", hd->name, l_no + 5);     // 逻辑分区数字是从5开始,主分区是1～4.
                l_no++;
                if (l_no >= 8)    // 只支持8个逻辑分区,避免数组越界
                    return;
            }
        }
        p++;
    }
    sys_free(bs);
}

/* 打印分区信息 */
static bool partition_info(struct list_elem* pelem, int arg UNUSED) {
    struct partition* part = elem2entry(struct partition, part_tag, pelem);
    printk("   %s start_lba:0x%x, sec_cnt:0x%x\n",part->name, part->start_lba, part->sec_cnt);
    
    /* 在此处return false与函数本身功能无关,
     * 只是为了让主调函数list_traversal继续向下遍历元素 */
    return false;
}

/** 硬盘中断处理程序 */
void intr_hd_handler(uint8_t irq_no) {
    ASSERT(irq_no == 0x2e || irq_no == 0x2f);
    uint8_t ch_no = irq_no - 0x2e;
    struct ide_channel* channel = channels + ch_no;
    ASSERT(channel->irq_no == irq_no);
    // 每次读写硬盘时会申请锁，从而保证了同步一致性，此中断对应的就是这一次的 expecting_intr
    if (channel->expecting_intr) {
        channel->expecting_intr = false;
        sema_v(&channel->disk_done);
        // 读取状态寄存器使硬盘控制器认为此次的中断已被处理,从而硬盘可以继续执行新的读写
        inb(reg_status(channel));
    }
}

/** 硬盘数据结构初始化 仅支持 PATA 硬盘 */
void ide_init() {
    printk("   ide_init start...\n");
    uint8_t hd_cnt = *((uint8_t*)(0x475));  // 获取硬盘的数量
    ASSERT(hd_cnt > 0);
    channel_cnt = DIV_ROUND_UP(hd_cnt, 2);  // 一个ide通道上有两个硬盘,根据硬盘数量反推有几个ide通道
    struct ide_channel* channel;
    uint8_t channel_no = 0, dev_no = 0;
    
    /* 处理每个通道上的硬盘 */
    while (channel_no < channel_cnt) {
        channel = channels + channel_no;
        sprintf(channel->name, "ide%d", channel_no);
        
        // 为每个ide通道初始化端口基址及中断向量
        switch (channel_no) {
            case 0:
                channel->port_base  = 0x1f0;    // ide0通道的起始端口号是0x1f0
                channel->irq_no     = 0x20 + 14;// 从片8259a上倒数第二的中断引脚,温盘,也就是ide0通道的的中断向量号
                break;
            case 1:
                channel->port_base  = 0x170;    // ide1通道的起始端口号是0x170
                channel->irq_no     = 0x20 + 15;// 从8259A上的最后一个中断引脚,我们用来响应ide1通道上的硬盘中断
                break;
        }
        
        channel->expecting_intr = false;        // 未向硬盘写入指令时不期待硬盘的中断
        mutex_init(&channel->lock);
        
        // 初始化为0 是向硬盘控制器请求数据后 P阻塞线程
        // 直到硬盘完成后发中断，处理程序 V唤醒线程
        sema_init(&channel->disk_done, 0);
        
        register_handler(channel->irq_no, intr_hd_handler);

        // 分别获取两个硬盘的参数及分区信息
        while (dev_no < 2) {
            struct disk* hd = channel->devices + dev_no;
            hd->my_channel = channel;
            hd->dev_no = dev_no;
            sprintf(hd->name, "sd%c", 'a' + channel_no * 2 + dev_no);
            identify_disk(hd);     // 获取硬盘参数
            if (dev_no != 0) {     // 内核本身的裸硬盘(hd60M.img)不处理
                partition_scan(hd, 0);  // 扫描该硬盘上的分区
            }
            p_no = 0, l_no = 0;
            dev_no++;
        }
        dev_no = 0;    // 将硬盘驱动器号置0,为下一个channel的两个硬盘初始化。
        channel_no++;
    }
    printk("     all partition info:\n");
    list_traversal(&partition_list, partition_info, (int)NULL);
    printk("   ide_init done!\n");
}


