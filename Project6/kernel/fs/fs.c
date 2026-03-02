#include <os/string.h>
#include <os/fs.h>
#include <os/kernel.h>
#include <os/mm.h>
#include <os/time.h>
#include <pgtable.h>
#include <os/smp.h>
#include <assert.h>
#include <os/sched.h>
#include <screen.h>


static superblock_t superblock;
static inode_t current_inode;
static fdesc_t fdesc_array[NUM_FDESCS];

// 大容量缓存区
// 添加 __attribute__((aligned(4)))
static uint8_t imap[INODE_MAP_SEC_NUM * SECTOR_SIZE] __attribute__((aligned(4)));
static uint8_t bmap[BLOCK_MAP_SEC_NUM * SECTOR_SIZE] __attribute__((aligned(4)));
static uint8_t disk_buffer[BLOCK_SIZE] __attribute__((aligned(4)));
static uint8_t index_buffer[BLOCK_SIZE] __attribute__((aligned(4)));
static uint8_t level_buffer[3][BLOCK_SIZE];

static inline int fs_exist(){
    if(superblock.magic_number == SUPERBLOCK_MAGIC) return 1;
    //read FS from disk
    bios_sd_read(kva2pa((uintptr_t)&superblock), 1, FS_START_SEC);
    if(superblock.magic_number == SUPERBLOCK_MAGIC){
        //imap&bmap
        bios_sd_read(kva2pa((uintptr_t)imap), INODE_MAP_SEC_NUM, FS_START_SEC + INODE_MAP_OFFSET);
        bios_sd_read(kva2pa((uintptr_t)bmap), BLOCK_MAP_SEC_NUM, FS_START_SEC + BLOCK_MAP_OFFSET);
        //current_inode
        bios_sd_read(kva2pa((uintptr_t)disk_buffer), 1, FS_START_SEC + INODE_OFFSET);
        current_inode = *(inode_t *)disk_buffer;
        return 1;
    }
    return 0;
}

static inline int alloc_block(uint32_t *addr_array, int num){
    int i, j, mask, cnt=0;
    // 遍历整个块位图
    for(i=0; i<BLOCK_MAP_SEC_NUM*SECTOR_SIZE; i++){
        for(j=0, mask=1; j<8; j++, mask<<=1){
            if((bmap[i] & mask) == 0){ // 找到空闲位
                bmap[i] |= mask;       // 在内存中标记为占用
                
                int blk_index = i*8+j;
                uint32_t data_blk_addr = blk_index*BLOCK_SIZE/SECTOR_SIZE + FS_START_SEC + DATA_OFFSET;
                
                // 清空新块的内容 (防止读取到脏数据)
                bzero(index_buffer, BLOCK_SIZE);
                bios_sd_write(kva2pa((uintptr_t)index_buffer), BLOCK_SIZE/SECTOR_SIZE, data_blk_addr);
                
                addr_array[cnt] = data_blk_addr;
                cnt++;
                
                if(cnt==num){
                    // 只写回 bmap 中发生变化的那个扇区！
                    // i 是字节索引。计算这是 bmap 数组中的第几个扇区。
                    int dirty_sec_offset = i / SECTOR_SIZE;
                    
                    // 计算内存地址：bmap 起始地址 + 偏移
                    uintptr_t dirty_mem_addr = (uintptr_t)bmap + dirty_sec_offset * SECTOR_SIZE;
                    
                    // 计算磁盘扇区号：BlockMap起始扇区 + 偏移
                    uint32_t dirty_disk_sec = FS_START_SEC + BLOCK_MAP_OFFSET + dirty_sec_offset;

                    // 只写 1 个扇区
                    bios_sd_write(kva2pa(dirty_mem_addr), 1, dirty_disk_sec);
                    
                    return 1; // 分配成功
                }
            }
        }
    }
    printk("[ALLOC_BLOCK] Warning: data block has been used up!\n");
    return 0;
}

static int alloc_inode() {
    // 遍历 inode 位图
    for (int i = 0; i < INODE_MAP_SEC_NUM * SECTOR_SIZE; i++) {
        for (int j = 0; j < 8; j++) {
            if (!((imap[i] >> j) & 1)) {
                imap[i] |= (1 << j); // 标记占用

                // 只写回 imap 中发生变化的那个扇区
                int dirty_sec_offset = i / SECTOR_SIZE;
                uintptr_t dirty_mem_addr = (uintptr_t)imap + dirty_sec_offset * SECTOR_SIZE;
                uint32_t dirty_disk_sec = FS_START_SEC + INODE_MAP_OFFSET + dirty_sec_offset;

                // 只写 1 个扇区
                bios_sd_write(kva2pa(dirty_mem_addr), 1, dirty_disk_sec);
                
                return i * 8 + j; // 返回 inode 号
            }
        }
    }
    return -1; // 无空闲 Inode
}

// inode初始化
static inline inode_t set_inode(short type, int mode, int ino){
    inode_t node;
    bzero(&node, sizeof(inode_t));
    node.type = type;
    node.mode = mode;
    node.ino = ino;
    node.size = 0;
    node.nlink = 1;
    node.atime = node.mtime = node.ctime =get_timer();
    return node;
}

// 根据ino获取inode
static inline inode_t* ino2inode(int ino){
    assert(ino>=0 && ino<INODE_NUM*SECTOR_SIZE/sizeof(inode_t));
    int offset = ino/IPSEC;
    bios_sd_read(kva2pa((uintptr_t)disk_buffer), 1, FS_START_SEC + INODE_OFFSET + offset);
    return ((inode_t *)disk_buffer)+ ino - offset * IPSEC;
}


// 根据name查找对应inode（在node指示的目录下）,查找到了返回1，并将inode存于res
static inline int get_inode_from_name(inode_t node, char* name, inode_t* res){
    // 基准inode非目录类型
    if(node.type != T_DIR)
        return 0;
    // 读入对应目录页
    bios_sd_read(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, node.direct_addrs[0]);
    dentry_t* de = (dentry_t*) disk_buffer;
    for(int i=0; i<DPBLK; i++){
        //跳过空的无效项
        if(de[i].name[0]==0)
            continue;
        else if(strcmp(de[i].name, name)==0){
            if(res!=NULL)
                *res = *ino2inode(de[i].ino);
            return 1;
        }
    }
    return 0;
}

// 以current_node为基准解析路径，找到返回1，并将对应inode存储于res
static inline int parse_path(inode_t node, char* path, inode_t* res){
    if(path==NULL || path[0]==0){
        if(res!=NULL)
            *res = node;
        return 1;
    }

    int len;
    for(len=0; len<strlen(path); len++){
        if(path[len]=='/')
            break;
    }

    char name[len+1];
    memcpy(name, path, len);
    name[len] = '\0';
    
    inode_t tmp;
    if(get_inode_from_name(node, name, &tmp)==0)
        return 0;

    if(path[len] == '/'){
        return parse_path(tmp, path+len+1, res);
    } else {
        if(res != NULL)
            *res = tmp;
        return 1;
    }
}

void recursive_recycle(uint32_t data_blk_addr, int level, char*zero_buff){
    if(level){
        uint32_t* addr_array = (uint32_t*)level_buffer[level-1];
        bios_sd_read(kva2pa((uintptr_t)level_buffer[level-1]), BLOCK_SIZE/SECTOR_SIZE, data_blk_addr);
        for(int i=0; i<IA_PER_BLK; i++){
            if(addr_array[i] != 0){
                recursive_recycle(addr_array[i], level-1, zero_buff);
            }
        }
    }
    // 清空数据块
    bios_sd_write(kva2pa((uintptr_t)zero_buff), BLOCK_SIZE/SECTOR_SIZE, data_blk_addr);
    // 修改bmap
    int bno = (data_blk_addr - FS_START_SEC - DATA_OFFSET) *SECTOR_SIZE / BLOCK_SIZE;
    bmap[bno / 8] &= ~(1 << (bno % 8));
}

void recycle_level_index(uint32_t data_blk_addr, int level){
    bzero(disk_buffer, BLOCK_SIZE);
    recursive_recycle(data_blk_addr, level, disk_buffer);
    // 写回bmap
    bios_sd_write(kva2pa((uintptr_t)bmap), BLOCK_MAP_SEC_NUM, FS_START_SEC + BLOCK_MAP_OFFSET);
}

// 辅助宏
#ifndef IA_PER_BLK
#define IA_PER_BLK (BLOCK_SIZE / sizeof(uint32_t))
#endif

// 获取数据块物理地址 (修复了溢出Bug和三级索引逻辑)
uint32_t get_data_block_addr(inode_t *node, int size){
    uint32_t blk_addr;
    uint32_t *table;

    // ============================================
    // 1. Direct Blocks (直接索引) [0 ~ 48KB]
    // ============================================
    if(size < DIRECT_SIZE){
        int index = size / BLOCK_SIZE;
        if(node->direct_addrs[index] == 0){
            if(!alloc_block(&blk_addr, 1)) return 0;
            node->direct_addrs[index] = blk_addr;
            
            inode_t* node_ptr = ino2inode(node->ino);
            node_ptr->direct_addrs[index] = blk_addr;
            int offset = node->ino / IPSEC;
            bios_sd_write(kva2pa((uintptr_t)disk_buffer), 1, FS_START_SEC + INODE_OFFSET + offset);
        }
        return node->direct_addrs[index];
    }
    size -= DIRECT_SIZE;
    
    // ============================================
    // 2. Indirect 1st Level (一级间接) [48KB ~ 4MB]
    // ============================================
    if(size < INDIRECT_1ST_SIZE){
        int index1 = size / BLOCK_SIZE / IA_PER_BLK; // Inode slot
        int index2 = (size / BLOCK_SIZE) % IA_PER_BLK; // Table slot

        if(node->indirect_addrs_1st[index1] == 0){
            if(!alloc_block(&blk_addr, 1)) return 0;
            node->indirect_addrs_1st[index1] = blk_addr;
            inode_t* node_ptr = ino2inode(node->ino);
            node_ptr->indirect_addrs_1st[index1] = blk_addr;
            int offset = node->ino / IPSEC;
            bios_sd_write(kva2pa((uintptr_t)disk_buffer), 1, FS_START_SEC + INODE_OFFSET + offset);
        }
        
        uint32_t table_addr = node->indirect_addrs_1st[index1];
        bios_sd_read(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, table_addr);
        table = (uint32_t*)disk_buffer;
        
        if(table[index2] == 0){
            if(!alloc_block(&blk_addr, 1)) return 0;
            // Reload table (alloc_block might use disk_buffer implicitly or via interrupt)
            bios_sd_read(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, table_addr);
            table = (uint32_t*)disk_buffer;
            
            table[index2] = blk_addr;
            bios_sd_write(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, table_addr);
        }
        return table[index2];
    }
    size -= INDIRECT_1ST_SIZE;

    // ============================================
    // 3. Indirect 2nd Level (二级间接) [4MB ~ 4GB]
    // ============================================
    if(size < INDIRECT_2ND_SIZE){ // 这里注意 size 是 int，实际上只能支持到 2GB
        // 使用分步除法避免溢出
        // index1 = blocks / (IA * IA)
        int blocks = size / BLOCK_SIZE;
        int index1 = blocks / IA_PER_BLK / IA_PER_BLK; 
        int index2 = (blocks / IA_PER_BLK) % IA_PER_BLK;
        int index3 = blocks % IA_PER_BLK;
        
        // 3.1 确保二级索引表存在
        if(node->indirect_addrs_2nd[index1] == 0){
            if(!alloc_block(&blk_addr, 1)) return 0;
            node->indirect_addrs_2nd[index1] = blk_addr;
            inode_t* node_ptr = ino2inode(node->ino);
            node_ptr->indirect_addrs_2nd[index1] = blk_addr;
            int offset = node->ino / IPSEC;
            bios_sd_write(kva2pa((uintptr_t)disk_buffer), 1, FS_START_SEC + INODE_OFFSET + offset);
        }
        uint32_t l2_addr = node->indirect_addrs_2nd[index1];

        // 3.2 读取二级表 -> 获取一级表地址
        bios_sd_read(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, l2_addr);
        table = (uint32_t*)disk_buffer;
        
        if(table[index2] == 0){
            if(!alloc_block(&blk_addr, 1)) return 0;
            bios_sd_read(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, l2_addr);
            table = (uint32_t*)disk_buffer;
            table[index2] = blk_addr;
            bios_sd_write(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, l2_addr);
        }
        uint32_t l1_addr = table[index2];

        // 3.3 读取一级表 -> 获取数据块地址
        bios_sd_read(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, l1_addr);
        table = (uint32_t*)disk_buffer;
        
        if(table[index3] == 0){
            if(!alloc_block(&blk_addr, 1)) return 0;
            bios_sd_read(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, l1_addr);
            table = (uint32_t*)disk_buffer;
            table[index3] = blk_addr;
            bios_sd_write(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, l1_addr);
        }
        return table[index3];
    }
    
    printk("[Error] size out of bound or not supported!\n");
    return 0;
}



int do_mkfs(int force_flag)
{
    // TODO [P6-task1]: Implement do_mkfs
    uint64_t cpu_id = get_current_cpu_id();
    if(force_flag){
        // 只需要清理元数据
        int blk_num =CEIL_DIV( DATA_OFFSET * SECTOR_SIZE, BLOCK_SIZE);
        // 清除内存中缓存
        bzero((char*)&superblock, sizeof(superblock_t));
        // 清除磁盘内容
        bzero(disk_buffer, BLOCK_SIZE);
        int cur_y = current_running[cpu_id]->cursor_y;
        for(int i=0; i<blk_num; i+=BLOCK_SIZE/SECTOR_SIZE){
            bios_sd_write(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE ,FS_START_SEC+i);
            screen_move_cursor(0, cur_y);
            printk("[FS] Cleaning blocks %d/%d\n", i, blk_num);
        }
        printk("[FS] Cleanning finished! Reseting...\n");
    }
    // 检查是否已经建立文件系统
    else if(fs_exist()){
        printk("[FS] Warning: filesystem has already been set up!\n");
        printk("[FS] Info: use 'mkfs -f' to reset filesystem.\n");
        return 1;   // do_mkfs failed
    }
    printk("[FS] Start initialize filesystem!\n");
    printk("[FS] Setting superblock...\n");
    // 初始化相关参数
    superblock.magic_number = SUPERBLOCK_MAGIC;
    superblock.start_sector = FS_START_SEC;
    superblock.fs_size = FS_SIZE;
    superblock.block_map_offset = BLOCK_MAP_OFFSET;
    superblock.inode_map_offset = INODE_MAP_OFFSET;
    superblock.inode_offset = INODE_OFFSET;
    superblock.inode_num = INODE_NUM;
    superblock.data_offset = DATA_OFFSET;
    superblock.data_block_num = DATA_BLOCK_NUM;
    // 打印相关信息
    printk("\t magic: 0x%x\n", superblock.magic_number);
    printk("\t num sector: %d, start sector: %d\n", superblock.fs_size, superblock.start_sector);
    printk("\t block map offset: %d(%d)\n", superblock.block_map_offset, BLOCK_MAP_SEC_NUM);
    printk("\t inode map offset: %d(%d)\n", superblock.inode_map_offset, INODE_MAP_SEC_NUM);
    printk("\t inode offset: %d(%d), inode num: %d\n", superblock.inode_offset, INODE_SEC_NUM, superblock.inode_num);
    printk("\t data offset: %d(%d), data block num: %d\n", superblock.data_offset, DATA_SEC_NUM, superblock.data_block_num);
    // 将superblock写入扇区
    bios_sd_write(kva2pa((uintptr_t)&superblock), 1, FS_START_SEC);
    // 初始化inode map
    printk("[FS] Setting inode-map...\n");
    bzero(imap, INODE_MAP_SEC_NUM*SECTOR_SIZE);   // 全部置为0，表示为未使用
    bios_sd_write(kva2pa((uintptr_t)imap), INODE_MAP_SEC_NUM, FS_START_SEC + INODE_MAP_OFFSET);
    // 初始化block map
    printk("[FS] Setting sector-map...\n");
    bzero(bmap, BLOCK_MAP_SEC_NUM*SECTOR_SIZE);
    bios_sd_write(kva2pa((uintptr_t)bmap), BLOCK_MAP_SEC_NUM, FS_START_SEC + BLOCK_MAP_OFFSET);
    // 创建根目录
    // 1. 创建inode
    printk("[FS] Setting inode...\n");
    int root_index = alloc_inode();
    // 2.创建dentry，并写回数据块
    bzero(disk_buffer, 512);
    dentry_t * de = (dentry_t*) disk_buffer;
    strcpy(de[0].name, ".");
    strcpy(de[1].name, "..");
    de[0].ino = de[1].ino = root_index;
    uint32_t data_blk_addr;
    alloc_block(&data_blk_addr, 1);
    bios_sd_write(kva2pa((uintptr_t)disk_buffer), 1,  data_blk_addr);
    // 3.写回inode
    inode_t *root_inode = ino2inode(root_index);
    *root_inode = set_inode(T_DIR, O_RDWR, root_index);
    root_inode->direct_addrs[0] = data_blk_addr;
    root_inode->size = 2*sizeof(dentry_t);
    current_inode = *root_inode; // 更新当前inode
    int offset = root_index / IPSEC;
    bios_sd_write(kva2pa((uintptr_t)disk_buffer), 1, FS_START_SEC + INODE_OFFSET + offset);
    // 初始化文件描述符
    bzero(fdesc_array, sizeof(fdesc_t)*NUM_FDESCS);
    printk("[FS] Initialize filesystem finished!\n");
    return 0;  // do_mkfs succeeds
}

int do_statfs(void)
{
    // TODO [P6-task1]: Implement do_statfs
    if(!fs_exist()){
        printk("[FS] Warning: filesystem has not been set up!\n");
        return 1;
    }
    int iused_cnt=0, bused_cnt=0, i, j, mask;
    for(i=0; i<INODE_MAP_SEC_NUM*SECTOR_SIZE; i++){
        for(j=0, mask=1; j<8; j++, mask<<=1){
            if(imap[i] & mask)
                iused_cnt++;
        }
    }
    for(i=0; i<BLOCK_MAP_SEC_NUM*SECTOR_SIZE; i++){
        for(j=0, mask=1; j<8; j++, mask<<=1){
            if(bmap[i] & mask)
                bused_cnt++;
        }
    }
    printk("[FS] state:\n");
    printk("\t magic: 0x%x\n", superblock.magic_number);
    printk("\t total sector: %d, start sector: %d(%08x)\n", superblock.fs_size, superblock.start_sector, superblock.start_sector);
    printk("\t block map offset: %d, occupied sector: %d\n", superblock.block_map_offset, BLOCK_MAP_SEC_NUM);
    printk("\t inode map offset: %d, occupied sector: %d\n", superblock.inode_map_offset, INODE_MAP_SEC_NUM);
    printk("\t inode block offset: %d, usage %d/%d\n", superblock.inode_offset, iused_cnt, superblock.inode_num);
    printk("\t data block offset: %d, usage %d/%d\n", superblock.data_offset, bused_cnt, superblock.data_block_num);
    printk("\t inode size: %dB, dir entry size: %dB\n", sizeof(inode_t), sizeof(dentry_t));
    return 0;  // do_statfs succeeds
}

int do_cd(char *path)
{
    // TODO [P6-task1]: Implement do_cd
    if(!fs_exist()){
        printk("[CD] Warning: filesystem has not been set up!\n");
        return 1;
    }
    inode_t tmp;
    // 不存在该目录
    if(parse_path(current_inode, path, &tmp)==0)
        return 2;
    if(tmp.type == T_FILE){
        printk("[CD] Error: Cannot enter a file!\n");
        return 3;
    }
    current_inode = tmp;
    return 0;  // do_cd succeeds
}


int do_mkdir(char *path)
{
    if(!fs_exist()){
        printk("[MKDIR] Warning: filesystem has not been set up!\n");
        return 1;
    }
    // 同名文件/目录已经存在
    if(get_inode_from_name(current_inode, path, NULL))
        return 1;

    // ==========================================
    // 1. 准备资源 (分配 inode 和 数据块)
    // ==========================================
    int ino = alloc_inode();
    uint32_t new_data_blk_addr;
    alloc_block(&new_data_blk_addr, 1);

    // ==========================================
    // 2. 初始化新目录的数据块 (. 和 ..)
    // ==========================================
    // 清空缓冲区，准备写入新目录的初始内容
    bzero(disk_buffer, BLOCK_SIZE); 
    dentry_t *de = (dentry_t*) disk_buffer;
    
    // 设置 . 指向自己
    strcpy(de[0].name, ".");
    de[0].ino = ino;
    
    // 设置 .. 指向父目录
    strcpy(de[1].name, "..");
    de[1].ino = current_inode.ino;
    
    // 写回新分配的数据块
    bios_sd_write(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, new_data_blk_addr);

    // ==========================================
    // 3. 初始化并写回新目录的 Inode
    // ==========================================
    inode_t *node = ino2inode(ino);
    
    *node = set_inode(T_DIR, O_RDWR, ino);
    node->direct_addrs[0] = new_data_blk_addr;
    node->size = 2 * sizeof(dentry_t); // 初始大小为两个目录项
    
    // 立即写回，防止 disk_buffer 被污染
    int offset = ino / IPSEC;
    bios_sd_write(kva2pa((uintptr_t)disk_buffer), 1, FS_START_SEC + INODE_OFFSET + offset);

    // ==========================================
    // 4. 修改父目录：添加新目录项 (关键修复点)
    // ==========================================
    uint32_t parent_blk_addr = get_data_block_addr(&current_inode, current_inode.size);

    // 读入父目录的目标数据块
    bios_sd_read(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, parent_blk_addr);
    de = (dentry_t*) disk_buffer;

    // 计算在块内的偏移量
    int entry_idx = (current_inode.size % BLOCK_SIZE) / sizeof(dentry_t);

    // 写入新目录项
    strcpy(de[entry_idx].name, path);
    de[entry_idx].ino = ino;

    // 写回父目录数据块
    bios_sd_write(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, parent_blk_addr);

    // ==========================================
    // 5. 更新父目录 Inode (Size)
    // ==========================================
    current_inode.size += sizeof(dentry_t);

    // 更新磁盘上的父目录 inode
    inode_t* parent_node_ptr = ino2inode(current_inode.ino);
    parent_node_ptr->size = current_inode.size; // 同步大小
    parent_node_ptr->mtime = get_timer();

    offset = current_inode.ino / IPSEC;
    bios_sd_write(kva2pa((uintptr_t)disk_buffer), 1, FS_START_SEC + INODE_OFFSET + offset);

    return 0;  // do_mkdir succeeds
}

int do_rmdir(char *path)
{
    // TODO [P6-task1]: Implement do_rmdir
    if(!fs_exist()){
        printk("[RMDIR] Warning: filesystem has not been set up!\n");
        return 1;
    }
    inode_t node;
    // 未找到该目录
    if(get_inode_from_name(current_inode, path, &node)==0){
        printk("[RMDIR] Error: No such directory!\n");
        return 1;
    }
    // 判断是否为目录
    if(node.type!=T_DIR){
        printk("[RMDIR] Failed!It is not a directory!\n");
        return 1;
    }
    // 判断是否含有子目录
    if(node.size>sizeof(dentry_t)*2){
        printk("[RMDIR] Failed!The diretory contains sub-dirs!\n");
        return 1;
    }
    // 删除目录
    node.nlink--;
    if(node.nlink==0){
        // 1.待删除目录的inode处理
        int offset = node.ino/IPSEC;
        inode_t* tmp = ino2inode(node.ino);
        // bios_sdread(kva2pa(buffer), 1, FS_START_SEC + INODE_OFFSET + offset);    // 调用get_inode_from_name后buffer里已经为该内容
        bzero(tmp, sizeof(inode_t));
        bios_sd_write(kva2pa((uintptr_t)disk_buffer), 1, FS_START_SEC + INODE_OFFSET + offset);
        // 2.处理inode bitmap
        imap[node.ino/8] &= ~(1 << (node.ino%8));
        bios_sd_write(kva2pa((uintptr_t)imap), INODE_MAP_SEC_NUM, FS_START_SEC + INODE_MAP_OFFSET);
        // 3.待删除目录分配的数据块处理
        uint32_t data_blk_addr = node.direct_addrs[0];
        bzero(disk_buffer, BLOCK_SIZE);
        bios_sd_write(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, data_blk_addr);
        // 4.处理数据块的bitmap
        int bno = (data_blk_addr - FS_START_SEC - DATA_OFFSET) *SECTOR_SIZE / BLOCK_SIZE;
        bmap[bno/8] &= ~(1 << (bno%8));
        bios_sd_write(kva2pa((uintptr_t)bmap), BLOCK_MAP_SEC_NUM, FS_START_SEC + BLOCK_MAP_OFFSET);
        // 5.父目录的间址块处理
        bios_sd_read(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, current_inode.direct_addrs[0]);
        dentry_t* de = (dentry_t*) disk_buffer;
        int cur;
        for(cur=0; cur<BLOCK_SIZE/sizeof(dentry_t); cur++)
            if(de[cur].ino == node.ino)
                break;
        bzero(&de[cur], sizeof(dentry_t));
        bios_sd_write(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, current_inode.direct_addrs[0]);
        // 6.处理父目录inode的size域
        inode_t* node_ptr = ino2inode(current_inode.ino);
        node_ptr->size -= sizeof(dentry_t);
        offset = current_inode.ino / IPSEC;
        bios_sd_write(kva2pa((uintptr_t)disk_buffer), 1, FS_START_SEC + INODE_OFFSET + offset);
    }
    else{
        // 只需写回nlink域
        int offset = node.ino/IPSEC;
        inode_t* node_ptr = ino2inode(node.ino);
        node_ptr->nlink = node.nlink;
        bios_sd_write(kva2pa((uintptr_t)disk_buffer), 1, FS_START_SEC + INODE_OFFSET + offset);
    }
    return 0;  // do_rmdir succeeds
}

int do_ls(char *path, int option)
{
    // TODO [P6-task1]: Implement do_ls
    if(!fs_exist()){
        printk("[MKDIR] Warning: filesystem has not been set up!\n");
        return 1;
    }
    // Note: argument 'option' serves for 'ls -l' in A-core
    inode_t node;
    if(path[0]==0)
        node = current_inode;
    else if(parse_path(current_inode, path, &node)==0)
        return 1;   // 找不到该路径
    // 这里要用index_buffer是因为后续in2inode时会覆盖buffer中的内容
    bios_sd_read(kva2pa((uintptr_t)index_buffer), BLOCK_SIZE/SECTOR_SIZE, node.direct_addrs[0]);
    dentry_t* de = (dentry_t*)index_buffer;

    for(int i=2; i<DPSEC; i++){
        if(de[i].name[0]==0)
            continue;
        if(option){
            printk("ino: %d ", de[i].ino);
            inode_t tmp = *ino2inode(de[i].ino);
            printk("%c%c%c nlink:%d ctime:%d atime:%d mtime:%d size:%d %s\n", 
                    tmp.type == T_DIR ? 'd' : '-',
                    (tmp.mode & O_RDONLY) ? 'r' : '-',
                    (tmp.mode & O_WRONLY) ? 'w' : '-',
                    tmp.nlink, tmp.ctime, tmp.atime, tmp.mtime, tmp.size
                    ,de[i].name
                    );
        }
        else
            printk("\t%s", de[i].name);
    }
    if(option==0)
        printk("\n");
    return 0;  // do_ls succeeds
}


int do_touch(char *path)
{
    if(!fs_exist()){
        printk("[TOUCH] Warning: filesystem has not been set up!\n");
        return 1;
    }
    // 同名文件/目录已经存在
    if(get_inode_from_name(current_inode, path, NULL))
        return 1;

    // ==========================================
    // 1. 准备资源
    // ==========================================
    int ino = alloc_inode();
    uint32_t new_data_blk_addr;
    alloc_block(&new_data_blk_addr, 1);

    // ==========================================
    // 2. 初始化并写回新文件的 Inode
    // ==========================================
    inode_t *node = ino2inode(ino);
    
    *node = set_inode(T_FILE, O_RDWR, ino);
    node->direct_addrs[0] = new_data_blk_addr;
    node->size = 0; // touch 创建的文件初始大小为 0
    
    // 立即写回，防止 disk_buffer 被后续操作污染
    int offset = ino / IPSEC;
    bios_sd_write(kva2pa((uintptr_t)disk_buffer), 1, FS_START_SEC + INODE_OFFSET + offset);

    // ==========================================
    // 3. 修改父目录：增加目录项
    // ==========================================
    // 使用 get_data_block_addr 获取正确的物理块地址
    // &current_inode 确保如果需要分配新块，内存中的 inode 也会更新
    uint32_t parent_blk_addr = get_data_block_addr(&current_inode, current_inode.size);

    // 读入父目录的目标数据块
    bios_sd_read(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, parent_blk_addr);
    dentry_t *de = (dentry_t*) disk_buffer;

    // 计算在块内的偏移量 (current_inode.size 指向的是下一个空闲字节的位置)
    int entry_idx = (current_inode.size % BLOCK_SIZE) / sizeof(dentry_t);

    // 安全检查
    if (entry_idx >= DPBLK) {
        printk("[TOUCH] Error: Entry index out of bound!\n");
        return 1;
    }

    // 写入新目录项
    strcpy(de[entry_idx].name, path);
    de[entry_idx].ino = ino;

    // 写回父目录数据块
    bios_sd_write(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, parent_blk_addr);

    // ==========================================
    // 4. 更新父目录 Inode (Size)
    // ==========================================
    // 必须先更新内存中的 current_inode
    current_inode.size += sizeof(dentry_t);

    // 更新磁盘上的父目录 inode
    inode_t* parent_node_ptr = ino2inode(current_inode.ino);
    parent_node_ptr->size = current_inode.size; // 同步大小
    parent_node_ptr->mtime = get_timer();       // 更新修改时间

    offset = current_inode.ino / IPSEC;
    bios_sd_write(kva2pa((uintptr_t)disk_buffer), 1, FS_START_SEC + INODE_OFFSET + offset);

    return 0;  // do_touch succeeds
}


int do_open(char *path, int mode)
{
    // TODO [P6-task2]: Implement do_open
    if(!fs_exist()){
        printk("[MKDIR] Warning: filesystem has not been set up!\n");
        return 1;
    }
    // 1.查找对应文件结点
    inode_t node;
    // 未找到该文件
    if(get_inode_from_name(current_inode, path, &node)==0){
        printk("[FOPEN] Fail to find the file!\n");
        return -1;
    }
    // 判断是否为文件类型
    if(node.type!=T_FILE){
        printk("[FOPEN] Failed!It is not a file!\n");
        return -1;
    }
    if(((node.mode & O_RDONLY)>(mode & O_RDONLY))||(node.mode & O_WRONLY)>(mode & O_WRONLY)){
        printk("[FOPEN] Failed to access file! Mode info: %d vs %d\n", node.mode, mode);
        return -1;
    }
    // 2.分配描述符
    int fd;
    for(fd=0; fd<NUM_FDESCS; fd++){
        if(fdesc_array[fd].valid==0)
            break;
    }
    fdesc_array[fd].valid = 1;
    fdesc_array[fd].mode = mode;
    fdesc_array[fd].ref++;
    fdesc_array[fd].read_ptr = 0;
    fdesc_array[fd].write_ptr = 0;
    fdesc_array[fd].ino = node.ino;
    return fd;  // return the fd of file descriptor
}

int do_read(int fd, char *buff, int length)
{
    // TODO [P6-task2]: Implement do_read
    if(!fs_exist()){
        printk("[MKDIR] Warning: filesystem has not been set up!\n");
        return 1;
    }
    if(fd>=NUM_FDESCS || fd<0 || fdesc_array[fd].valid==0){
        printk("[FREAD] Warning: invalid file descriptor!\n");
        return -1;
    }
    // 判断文件是否可读
    if((fdesc_array[fd].mode & O_RDONLY)==0){
        printk("[FREAD] No right to read file!\n");
        return -1;
    }
    inode_t node = *ino2inode(fdesc_array[fd].ino);
    int len = length> MAX_FILE_SIZE - fdesc_array[fd].read_ptr ? MAX_FILE_SIZE - fdesc_array[fd].read_ptr : length;
    // 以block为单位读取
    for(int read_ptr = fdesc_array[fd].read_ptr; read_ptr<fdesc_array[fd].read_ptr + len;){
        int partial_len = read_ptr % BLOCK_SIZE ? (BLOCK_SIZE - (read_ptr % BLOCK_SIZE)) : BLOCK_SIZE;
        int tmp = fdesc_array[fd].read_ptr + len - read_ptr;
        partial_len = partial_len > tmp ? tmp : partial_len;
        uint32_t read_addr =  get_data_block_addr(&node, read_ptr);   
        bios_sd_read(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, read_addr);
        memcpy(buff, disk_buffer+(read_ptr%BLOCK_SIZE), partial_len);
        read_ptr += partial_len;
        buff += partial_len;
    }
    fdesc_array[fd].read_ptr += len;
    // 修改inode的atime
    inode_t *node_ptr = ino2inode(node.ino);
    node_ptr->atime = get_timer();
    int offset = node.ino / IPSEC;
    bios_sd_write(kva2pa((uintptr_t)disk_buffer), 1, FS_START_SEC + INODE_OFFSET + offset);

    return len;  // return the length of trully read data
}

int do_write(int fd, char *buff, int length)
{
    // TODO [P6-task2]: Implement do_write
    if(!fs_exist()){
        printk("[MKDIR] Warning: filesystem has not been set up!\n");
        return 1;
    }
    if(fd>=NUM_FDESCS || fd<0 || fdesc_array[fd].valid==0){
        printk("[FWRITE] Warning: invalid file descriptor!\n");
        return -1;
    }
    if((fdesc_array[fd].mode & O_WRONLY)==0){
        printk("[FWRITE] No right to write file!\n");
        return -1;
    }
    inode_t node = *ino2inode(fdesc_array[fd].ino);
    int len = (length> (MAX_FILE_SIZE - fdesc_array[fd].write_ptr)) ? (MAX_FILE_SIZE - fdesc_array[fd].write_ptr) : length;
    // 以block为单位写
    for(int write_ptr = fdesc_array[fd].write_ptr; write_ptr<fdesc_array[fd].write_ptr + len;){
        int partial_len = write_ptr % BLOCK_SIZE ? (BLOCK_SIZE - (write_ptr % BLOCK_SIZE)) : BLOCK_SIZE;
        int tmp = fdesc_array[fd].write_ptr + len - write_ptr;
        partial_len = partial_len > tmp ? tmp : partial_len;
        uint32_t write_addr =  get_data_block_addr(&node, write_ptr);  
        // printk("return from get_data_block_addr\n"); 
        if(write_ptr % BLOCK_SIZE || partial_len<BLOCK_SIZE)
            bios_sd_read(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, write_addr);
        memcpy(disk_buffer + (write_ptr%BLOCK_SIZE), buff, partial_len);
        bios_sd_write(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, write_addr);
        write_ptr += partial_len;
        buff += partial_len;
    }
    fdesc_array[fd].write_ptr += len;
    // 需要更新inode的size信息：size和mtime
    inode_t *node_ptr = ino2inode(node.ino);
    node_ptr->mtime = get_timer();
    if(fdesc_array[fd].write_ptr>node.size)
        node_ptr->size = fdesc_array[fd].write_ptr;
    int offset = node.ino / IPSEC;
    bios_sd_write(kva2pa((uintptr_t)disk_buffer), 1, FS_START_SEC + INODE_OFFSET + offset);
    
    return len;  // return the length of trully written data
}

int do_close(int fd)
{
    // TODO [P6-task2]: Implement do_close
    // 检查fd是否越界
    if(fd>=NUM_FDESCS || fd<0){
        printk("[FCLOSE] Warning: invalid file descriptor!\n");
        return -1;
    }
    fdesc_array[fd].ref--;
    if(fdesc_array[fd].ref==0){
        bzero(&fdesc_array[fd], sizeof(fdesc_t));
    }
    return 0;  // do_fclose succeeds
}


int do_ln(char *src_path, char *dst_path)
{
    // 0. 基础检查
    if(!fs_exist()){
        printk("[LN] Warning: filesystem has not been set up!\n");
        return 1;
    }

    inode_t node; // src 的 inode 副本
    inode_t tmp;  // 用于检查 dst 是否存在的临时变量

    // 1. 判断源文件是否存在
    if(parse_path(current_inode, src_path, &node) == 0){
        printk("[LN] Error: cannot find file %s!\n", src_path);
        return 2;
    }

    // 2. 禁止对目录建立硬链接 (防止环路)
    if(node.type == T_DIR){
        printk("[LN] Error: cannot link a directory!\n");
        return 3;
    }

    // 3. 判断目标路径是否已经被占用
    if(get_inode_from_name(current_inode, dst_path, &tmp)){
        printk("[LN] Error: file %s has existed!\n", dst_path);
        return 4;
    }

    // ==========================================
    // 4. 修改源文件 Inode (增加 nlink)
    // ==========================================
    inode_t *src_node_ptr = ino2inode(node.ino);
    src_node_ptr->nlink++; // 链接数 +1

    // 立即写回源文件 inode
    int offset = node.ino / IPSEC;
    bios_sd_write(kva2pa((uintptr_t)disk_buffer), 1, FS_START_SEC + INODE_OFFSET + offset);

    // ==========================================
    // 5. 修改父目录：增加目录项 (dentry)
    // ==========================================
    // [修复] 使用 get_data_block_addr 获取正确的物理块地址
    // 传入 &current_inode 是为了万一目录满了需要分配新块时，能更新内存中的 inode
    uint32_t parent_blk_addr = get_data_block_addr(&current_inode, current_inode.size);

    // 读入父目录的目标数据块
    bios_sd_read(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, parent_blk_addr);
    dentry_t *de = (dentry_t*)disk_buffer;

    // 计算在块内的偏移量 (Append 模式)
    int entry_idx = (current_inode.size % BLOCK_SIZE) / sizeof(dentry_t);

    // 写入新目录项 (指向源文件的 inode 编号)
    strcpy(de[entry_idx].name, dst_path);
    de[entry_idx].ino = node.ino; 

    // 写回父目录数据块
    bios_sd_write(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, parent_blk_addr);

    // ==========================================
    // 6. 更新父目录 Inode (Size)
    // ==========================================
    // 必须先更新内存中的 current_inode
    current_inode.size += sizeof(dentry_t);

    // 更新磁盘上的父目录 inode
    inode_t* parent_node_ptr = ino2inode(current_inode.ino);
    parent_node_ptr->size = current_inode.size; // 同步大小
    parent_node_ptr->mtime = get_timer();       // 更新修改时间

    offset = current_inode.ino / IPSEC;
    bios_sd_write(kva2pa((uintptr_t)disk_buffer), 1, FS_START_SEC + INODE_OFFSET + offset);

    return 0;  // do_ln succeeds
}

int do_rm(char *path)
{
    // TODO [P6-task2]: Implement do_rm
    // 1. 判断文件系统是否存在
    if(!fs_exist()){
        printk("[RM] Warning: filesystem has not been set up!\n");
        return 1;
    }
    // 2. 判断是否存在文件
    inode_t node;
    if(get_inode_from_name(current_inode, path, &node)==0){
        printk("[RM] Error: No such file!\n");
        return 2;
    }
    if(node.type==T_DIR){
        printk("[RM] Error: cannot rm a directory!\n");
        return 3;
    }

    // 3. 判断是否需要释放对应inode结点
    node.nlink--;
    if(node.nlink==0){
        // 3.1 修改imap
        imap[node.ino/8] &= ~(1 << (node.ino%8));
        bios_sd_write(kva2pa((uintptr_t)imap), INODE_MAP_SEC_NUM, FS_START_SEC + INODE_MAP_OFFSET);
        // 3.2 删除结点内容
        inode_t *node_ptr = ino2inode(node.ino);
        bzero(node_ptr, sizeof(inode_t));
        int offset = node.ino / IPSEC;
        bios_sd_write(kva2pa((uintptr_t)disk_buffer), 1, FS_START_SEC + INODE_OFFSET + offset);
        // 3.3 回收数据块
        // 3.3.1.直接索引
        for(int i=0; i<NDIRECT; i++){
            if(node.direct_addrs[i]!=0){
                // 清空数据块
                bzero(disk_buffer, BLOCK_SIZE);
                bios_sd_write(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, node.direct_addrs[i]);
                // 修改bmap
                int bno = (node.direct_addrs[i] - FS_START_SEC - DATA_OFFSET) *SECTOR_SIZE / BLOCK_SIZE;
                bmap[bno/8] &= ~(1 << (bno%8));
                bios_sd_write(kva2pa((uintptr_t)bmap), BLOCK_MAP_SEC_NUM, FS_START_SEC + BLOCK_MAP_OFFSET);
            }
        }
        // 3.3.2.一级索引
        for(int i=0; i<3; i++){
            if(node.indirect_addrs_1st[i]!=0){
                recycle_level_index(node.indirect_addrs_1st[i], 1);
            }
        }
        // 3.3.3.二级索引
        for(int i=0; i<2; i++){
            if(node.indirect_addrs_2nd[i]!=0){
                recycle_level_index(node.indirect_addrs_2nd[i], 2);
            }
        }
    }
    else{
        // 只需写回nlink域
        int offset = node.ino/IPSEC;
        inode_t* node_ptr = ino2inode(node.ino);
        node_ptr->nlink = node.nlink;
        bios_sd_write(kva2pa((uintptr_t)disk_buffer), 1, FS_START_SEC + INODE_OFFSET + offset);
    }
    // 4. 在父目录下删除对应目录项
    bios_sd_read(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, current_inode.direct_addrs[0]);
    dentry_t* de = (dentry_t*) disk_buffer;
    int cur;
    for(cur=0; cur<BLOCK_SIZE/sizeof(dentry_t); cur++)
        if(de[cur].ino == node.ino)
            break;
    bzero(&de[cur], sizeof(dentry_t));
    bios_sd_write(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, current_inode.direct_addrs[0]);
    // 5. 处理父目录inode的size域
    inode_t* node_ptr = ino2inode(current_inode.ino);
    node_ptr->size -= sizeof(dentry_t);
    int offset = current_inode.ino / IPSEC;
    bios_sd_write(kva2pa((uintptr_t)disk_buffer), 1, FS_START_SEC + INODE_OFFSET + offset);
    return 0;  // do_rm succeeds 
}

int do_lseek(int fd, int offset, int whence)
{
    // TODO [P6-task2]: Implement do_lseek
    if(!fs_exist()){
        printk("[MKDIR] Warning: filesystem has not been set up!\n");
        return -1;
    }
    if(fdesc_array[fd].valid==0)
        return -1;
    if(whence==SEEK_SET){
        fdesc_array[fd].write_ptr = fdesc_array[fd].read_ptr = offset;
        return offset;
    }
    else if(whence==SEEK_CUR){
        fdesc_array[fd].write_ptr += offset;
        fdesc_array[fd].read_ptr += offset;
        return fdesc_array[fd].read_ptr;    // [TODO] read和write指针不一致？
    }
    else if(whence==SEEK_END){
        inode_t node = *ino2inode(fdesc_array[fd].ino);
        fdesc_array[fd].write_ptr = fdesc_array[fd].read_ptr = node.size + offset;
        return fdesc_array[fd].write_ptr;
    }
    printk("[LSEEK] Warning: unknown mode!\n");
    return -1;  // the resulting offset location from the beginning of the file
}


int do_cat(char *path) {
    if(!fs_exist()){
        printk("[CAT] Warning: filesystem has not been set up!\n");
        return 1;
    }

    // 1. 查找对应文件结点
    inode_t node;
    // 未找到该文件
    if(parse_path(current_inode, path, &node) == 0){
        printk("[CAT] Fail to find the file!\n");
        return -1;
    }

    // 判断是否为文件类型
    if(node.type != T_FILE){
        printk("[CAT] Failed! It is not a file!\n");
        return -1;
    }

    // 栈上缓冲区，多 1 字节用于存放 \0
    char buf[BLOCK_SIZE + 1];

    // 以 block 为单位循环读取并打印
    for(int read_ptr = 0; read_ptr < node.size; read_ptr += BLOCK_SIZE){
        // 1. 获取物理块地址
        // 注意：这里传入 &node 是对的，虽然 cat 不会分配新块，但保持接口一致
        uint32_t read_addr = get_data_block_addr(&node, read_ptr);   
        
        // 2. 读取磁盘块
        bios_sd_read(kva2pa((uintptr_t)disk_buffer), BLOCK_SIZE/SECTOR_SIZE, read_addr);

        // 3. 计算当前块内的有效数据长度
        // 如果是最后一块，长度可能小于 BLOCK_SIZE
        int len = (node.size - read_ptr) > BLOCK_SIZE ? BLOCK_SIZE : (node.size - read_ptr);

        // 4. 安全拷贝并添加结束符
        memcpy(buf, disk_buffer, len);
        buf[len] = '\0'; // 强制截断，防止 printk 越界

        // 5. 打印
        printk("%s", buf);
    }
    
    // 最后换个行，美观一点
    printk("\n");
    return 0;  // do_cat succeeds
}