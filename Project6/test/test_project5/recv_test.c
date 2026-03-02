#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE (8 * 1024) 

// 放在全局/静态区，避免爆栈
static uint8_t buffer[BUF_SIZE];

uint16_t fletcher16(uint8_t *data, int count) {
    uint16_t sum1 = 0;
    uint16_t sum2 = 0;

    for (int i = 0; i < count; ++i) {
        sum1 = (sum1 + data[i]) % 255;
        sum2 = (sum2 + sum1) % 255;
    }

    return (sum2 << 8) | sum1;
}

int main() {
    int nbytes = 0;
    uint32_t file_size = 0;
    
    // 清空 buffer
    memset(buffer, 0, BUF_SIZE);

    printf("=== Reliable Stream Receiver (Single-Shot Mode) ===\n");
    printf("Waiting for data (Requesting up to %d bytes)...\n", BUF_SIZE);

    // [关键修改] 只调用一次接收函数，请求最大缓冲区大小
    // 你的 net.c 会一直循环接收，直到发送端停止或填满长度
    int ret = sys_net_recv_stream(buffer, &nbytes, BUF_SIZE);

    if (ret < 0) {
        printf("Error receiving data: %d\n", ret);
        return 0;
    }

    printf("Receive call returned. Total bytes: %d\n", ret);

    if (ret < 4) {
        printf("Error: Received data too small to contain size header.\n");
        return 0;
    }

    // 解析头部大小 (前4字节)
    memcpy((uint8_t *)&file_size, buffer, 4);
    printf("File size declared in header: %d bytes\n", file_size);

    int actual_data_len = ret - 4;
    
    // 简单的完整性检查
    if (actual_data_len != file_size) {
        printf("Warning: Declared size (%d) != Received payload size (%d)\n", 
               file_size, actual_data_len);
        // 注意：有些测试可能发送 padding，只要收到的大于等于 file_size 就算成功
        if (actual_data_len < file_size) {
             printf("Error: Data incomplete!\n");
        }
    }

    // 计算 Checksum (跳过前4字节的 size)
    // 注意：这里计算的是实际接收到的数据的校验和
    uint16_t checksum = fletcher16(buffer + 4, actual_data_len);

    printf("\n=== Transfer Complete ===\n");
    printf("Total received payload: %d bytes\n", actual_data_len);
    printf("Checksum: 0x%04x\n", checksum);

    return 0;
}