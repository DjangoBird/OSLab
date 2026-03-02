#ifndef __INCLUDE_OS_FS_H__
#define __INCLUDE_OS_FS_H__

#include <type.h>

#define CEIL_DIV(a,n) ((a)/(n)+(((a)%(n)==0)?0:1))

/* macros of file system */
#define SUPERBLOCK_MAGIC 0xDF4C4459
#define NUM_FDESCS 16

/* Disk Layout Constants */
#define SECTOR_SIZE      512
#define BLOCK_SIZE       4096        // 4KB per block

/* File Types & Modes */
#define T_DIR            1           // Directory
#define T_FILE           2           // File

/* Sizes & Limits */
#define INODE_NUM           512
#define DATA_BLOCK_NUM      (1<<20)       //4GB
#define MAX_NAME_LEN        24
#define INODE_MAP_SEC_NUM   CEIL_DIV(INODE_NUM,SECTOR_SIZE*8)
#define BLOCK_MAP_SEC_NUM   CEIL_DIV(DATA_BLOCK_NUM ,SECTOR_SIZE*8)
#define INODE_SEC_NUM       CEIL_DIV(sizeof(inode_t)*INODE_NUM, SECTOR_SIZE)
#define DATA_SEC_NUM        (DATA_BLOCK_NUM*(BLOCK_SIZE/SECTOR_SIZE)) 

/* Layout Offsets */
#define SUPERBLOCK_OFFSET   0
#define BLOCK_MAP_OFFSET    1
#define INODE_MAP_OFFSET    (BLOCK_MAP_OFFSET + BLOCK_MAP_SEC_NUM)
#define INODE_OFFSET        (INODE_MAP_OFFSET + INODE_MAP_SEC_NUM)
#define DATA_OFFSET         (INODE_OFFSET + INODE_SEC_NUM)

#define FS_START_SEC        ((1<<29)/SECTOR_SIZE)        // 512MB
#define FS_SIZE             (DATA_OFFSET + DATA_SEC_NUM)

// 辅助宏
#define IPSEC               (SECTOR_SIZE / sizeof(inode_t))
#define DPSEC               (SECTOR_SIZE / sizeof(dentry_t))
#define DPBLK               (BLOCK_SIZE / sizeof(dentry_t))

#define IA_PER_BLK          (BLOCK_SIZE / sizeof(uint32_t))
#define IA_PER_SEC          (SECTOR_SIZE/ sizeof(uint32_t))

#define NDIRECT             12
#define DIRECT_SIZE         (NDIRECT*BLOCK_SIZE)
#define INDIRECT_1ST_SIZE   (3*BLOCK_SIZE*IA_PER_BLK)
#define INDIRECT_2ND_SIZE   (2*BLOCK_SIZE*IA_PER_BLK*IA_PER_BLK)
#define INDIRECT_3RD_SIZE   (1*BLOCK_SIZE*IA_PER_BLK*IA_PER_BLK*IA_PER_BLK)
#define MAX_FILE_SIZE       (DIRECT_SIZE + INDIRECT_1ST_SIZE + INDIRECT_2ND_SIZE + INDIRECT_3RD_SIZE)

/* data structures of file system */
typedef struct superblock {
    // TODO [P6-task1]: Implement the data structure of superblock
    uint32_t magic_number;
    uint32_t fs_size;           /* Total size (sectors) */
    uint32_t start_sector;
    
    uint32_t inode_map_offset;  /* Offset of inode bitmap */
    uint32_t block_map_offset;  /* Offset of block bitmap */
    uint32_t inode_offset;      /* Offset of inode table */
    uint32_t data_offset;       /* Offset of data blocks */

    uint32_t inode_num;         /* Total inodes */
    uint32_t data_block_num;    /* Total data blocks */

    uint32_t pad[20];           /* Padding to align/reserve space */
} superblock_t;

typedef struct dentry {
    // TODO [P6-task1]: Implement the data structure of directory entry
    char name[MAX_NAME_LEN];    /* File name */
    uint32_t ino;               /* Inode number */
    uint32_t pad;
} dentry_t;

typedef struct inode { 
    // TODO [P6-task1]: Implement the data structure of inode
    char type;      // 文件类型 (T_DIR 目录 / T_FILE 文件)
    char mode;      // 读写权限 (O_RDONLY 等)
    short nlink;    // 硬链接数
    
    uint32_t ino;   // 自身的编号
    uint32_t size;  // 文件大小 (字节数)
    
    // 时间戳
    uint32_t ctime; // 创建时间
    uint32_t atime; // 访问时间
    uint32_t mtime; // 修改时间

    // 数据索引
    uint32_t direct_addrs[NDIRECT];      // 直接索引 (12*4KB)
    uint32_t indirect_addrs_1st[3];      // 一级间接索引 (1024*4KB)
    uint32_t indirect_addrs_2nd[2];      // 二级间接索引
    uint32_t indirect_addrs_3rd;         // 三级间接索引
} inode_t;

typedef struct fdesc {
    // TODO [P6-task2]: Implement the data structure of file descriptor
    uint8_t valid;      // 该描述符是否被占用
    uint8_t mode;       // 打开模式 (只读/只写)
    short ref;          // 引用计数 (fork时子进程会继承，ref+1)
    int ino;            // 打开的是哪个文件的 Inode
    
    uint32_t write_ptr;
    uint32_t read_ptr;
} fdesc_t;

/* modes of do_open */
#define O_RDONLY 1  /* read only open */
#define O_WRONLY 2  /* write only open */
#define O_RDWR   3  /* read/write open */

/* whence of do_lseek */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* fs function declarations */
extern int do_mkfs(int force_flag);
extern int do_statfs(void);
extern int do_cd(char *path);
extern int do_mkdir(char *path);
extern int do_rmdir(char *path);
extern int do_ls(char *path, int option);
extern int do_open(char *path, int mode);
extern int do_read(int fd, char *buff, int length);
extern int do_write(int fd, char *buff, int length);
extern int do_close(int fd);
extern int do_ln(char *src_path, char *dst_path);
extern int do_rm(char *path);
extern int do_lseek(int fd, int offset, int whence);

extern int do_touch(char *path);
extern int do_cat(char *path);

#endif