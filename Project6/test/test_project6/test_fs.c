#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

// 假设你的系统调用封装在这个头文件
#include "syscall.h" 

#define TEST_FILE "hugefile.txt"

// 定义 128MB 大小
#define TARGET_SIZE (128 * 1024 * 1024) 
// 头部写入的数据量 (1KB)
#define HEAD_LEN    1024 

static char buf[HEAD_LEN];

int main(void)
{
    int i, fd, ret;
    printf("[TEST] Starting File System Large File Test (Sparse)...\n");

    // 1. 准备头部数据
    for(i=0; i<HEAD_LEN; i++) {
        buf[i] = (i % 95) + 32; // 可打印字符
    }

    // 2. 创建文件
    printf("[TEST] Creating large file %s (Target Size: 128MB)...\n", TEST_FILE);
    sys_touch(TEST_FILE);
    
    fd = sys_open(TEST_FILE, O_RDWR);
    if(fd < 0) { printf("[TEST] Error: Open failed\n"); return 0; }

    // --- 步骤 A: 写头部 ---
    printf("[TEST] Writing head (%d bytes)...\n", HEAD_LEN);
    ret = sys_write(fd, buf, HEAD_LEN);
    if(ret != HEAD_LEN) {
        printf("[TEST] Error: Head write failed. Wrote %d\n", ret);
        sys_close(fd);
        return 0;
    }

    // --- 步骤 B: Lseek 到尾部并写一个字节 ---
    // 定位到 128MB - 1 的位置
    printf("[TEST] Lseeking to offset %d...\n", TARGET_SIZE - 1);
    ret = sys_lseek(fd, TARGET_SIZE - 1, SEEK_SET);
    
    if(ret < 0) {
        printf("[TEST] Error: Lseek failed\n");
        sys_close(fd);
        return 0;
    }

    // 写入尾部标记字符 'X'
    char tail_char = 'X';
    ret = sys_write(fd, &tail_char, 1);
    if(ret != 1) {
        printf("[TEST] Error: Tail write failed.\n");
    } else {
        printf("[TEST] Tail write success. File should now be 128MB.\n");
    }

    // 3. 关闭文件以刷新元数据
    sys_close(fd);

    // 4. 重新打开并校验
    printf("[TEST] Re-opening for verification...\n");
    fd = sys_open(TEST_FILE, O_RDWR);
    if(fd < 0) { printf("[TEST] Error: Re-open failed\n"); return 0; }

    // --- 校验头部 ---
    char check_buf[HEAD_LEN];
    sys_read(fd, check_buf, HEAD_LEN);
    for(i=0; i<HEAD_LEN; i++) {
        if(check_buf[i] != buf[i]) {
            printf("[TEST] HEAD Data Mismatch at byte %d! Got %c, Expect %c\n", i, check_buf[i], buf[i]);
            sys_close(fd);
            return 0;
        }
    }
    printf("[TEST] Head data verified.\n");

    // --- 校验尾部 ---
    // 再次定位到最后
    sys_lseek(fd, TARGET_SIZE - 1, SEEK_SET);
    char check_tail;
    ret = sys_read(fd, &check_tail, 1);
    
    if(check_tail == tail_char) {
        printf("[TEST] TAIL Data verified PASS! (Got '%c')\n", check_tail);
    } else {
        printf("[TEST] TAIL Data Mismatch! (Got %d, Expect '%c')\n", check_tail, tail_char);
    }

    sys_close(fd);
    
    // 5. 清理
    sys_rm(TEST_FILE);
    printf("[TEST] All tests finished.\n");
    return 0;
}