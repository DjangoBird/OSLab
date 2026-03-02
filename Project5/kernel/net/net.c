#include "io.h"
#include <e1000.h>
#include <type.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/list.h>
#include <os/smp.h>
#include <os/net.h>
#include <os/time.h>
#include <printk.h>

// =========================================================================
// 宏定义与结构体 
// =========================================================================

#define NET_ETH_HDR_LEN 14u
#define NET_IP_HDR_LEN 20u
#define NET_TCP_HDR_LEN 20u
#define RTP_HDR_LEN 8u

#define STREAM_DATA_OFFSET (NET_ETH_HDR_LEN + NET_IP_HDR_LEN + NET_TCP_HDR_LEN)
#define STREAM_MAGIC 0x45

#ifndef ETH_ALEN
#define ETH_ALEN 6
#endif

#ifndef ETH_P_IP
#define ETH_P_IP 0x0800
#endif

// 自定义流协议头
typedef struct {
    uint8_t magic;
    uint8_t flags; // 0x01:DAT, 0x02:RSD, 0x04:ACK
    uint16_t len;
    uint32_t seq;
} __attribute__((packed)) stream_header_t;

// 接收端状态机
typedef struct {
    uint32_t buf_start;
    uint32_t buf_end;
    uint32_t expected_seq;
    uint32_t last_seq;      // 用于 do_rsd_longterm 检测进度
    uint32_t start_time;    // 用于 do_rsd_longterm 计时
    uint8_t peer_mac[ETH_ALEN];
    uint8_t local_mac[ETH_ALEN];
    uint32_t peer_ip;
    uint32_t local_ip;
    uint16_t peer_port;
    uint16_t local_port;
    int valid;
} stream_receiver_t;

// 全局变量
static stream_receiver_t stream_rx;
static uint8_t stream_packet_data[4096];
static LIST_HEAD(send_block_queue);
static LIST_HEAD(recv_block_queue);

// =========================================================================
// 辅助函数 (大小端转换等)
// =========================================================================

static uint16_t ntohs(uint16_t n) {
    return ((n & 0xFF) << 8) | ((n & 0xFF00) >> 8);
}

static uint32_t ntohl(uint32_t n) {
    return ((n & 0xFF) << 24) | ((n & 0xFF00) << 8) | ((n & 0xFF0000) >> 8) |
           ((n & 0xFF000000) >> 24);
}

static uint16_t htons(uint16_t n) { return ntohs(n); }
static uint32_t htonl(uint32_t n) { return ntohl(n); }

static uint16_t net_get_be16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static uint32_t net_get_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void net_put_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xff);
}

static void net_put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xff);
    p[2] = (uint8_t)((v >> 8) & 0xff);
    p[3] = (uint8_t)(v & 0xff);
}

// 声明后续用到的函数
int do_net_send(void *txpacket, int length);
int do_net_recv_re(void *rxbuffer, int pkt_num, int *pkt_lens);
static void send_stream_rsd(uint32_t seq);

// =========================================================================
// Task 4: 可靠传输实现
// =========================================================================

void stream_receiver_init() {
    memset(&stream_rx, 0, sizeof(stream_receiver_t));
    stream_rx.buf_start = 0;
    stream_rx.buf_end = 0;
    stream_rx.expected_seq = 0;
    for (int i = 0; i < 4096; i++) {
        stream_packet_data[i] = 0;
    }
    for (int i = 0; i < ETH_ALEN; i++) {
        stream_rx.peer_mac[i] = 0;
        stream_rx.local_mac[i] = 0;
    }
    stream_rx.peer_ip = 0;
    stream_rx.local_ip = 0;
    stream_rx.peer_port = 0;
    stream_rx.local_port = 0;
    stream_rx.valid = 0;
    return;
}

static void send_stream_ack(uint32_t seq) {
    uint8_t packet[62] = {0};
    struct ethhdr *eth = (struct ethhdr *)packet;
    
    // 填充以太网头
    memcpy(eth->ether_dmac, stream_rx.peer_mac, ETH_ALEN);
    memcpy(eth->ether_smac, stream_rx.local_mac, ETH_ALEN);
    net_put_be16((uint8_t *)&eth->ether_type, ETH_P_IP);
    
    // 填充IP头
    uint8_t *ip = packet + NET_ETH_HDR_LEN;
    ip[0] = 0x45;
    ip[1] = 0;
    net_put_be16(&ip[2], NET_IP_HDR_LEN + NET_TCP_HDR_LEN + RTP_HDR_LEN);
    net_put_be16(&ip[4], 0);
    net_put_be16(&ip[6], 0);
    ip[8] = 64;
    ip[9] = 6;
    net_put_be16(&ip[10], 0);
    net_put_be32(&ip[12], stream_rx.local_ip);
    net_put_be32(&ip[16], stream_rx.peer_ip);

    // 填充TCP头 (伪)
    uint8_t *tcp = ip + NET_IP_HDR_LEN;
    net_put_be16(&tcp[0], stream_rx.local_port);
    net_put_be16(&tcp[2], stream_rx.peer_port);
    net_put_be32(&tcp[4], 0);
    net_put_be32(&tcp[8], 0);
    tcp[12] = (NET_TCP_HDR_LEN / 4u) << 4;
    tcp[13] = 0x10;
    net_put_be16(&tcp[14], 65535);
    net_put_be16(&tcp[16], 0);
    net_put_be16(&tcp[18], 0);

    // 填充 Stream 协议头
    stream_header_t *header = (stream_header_t *)&packet[STREAM_DATA_OFFSET];
    header->magic = STREAM_MAGIC;
    header->flags = 0x04; // ACK
    header->len = 0;
    header->seq = htonl(seq);

    do_net_send(packet, sizeof(packet));
    return;
}

static void send_stream_rsd(uint32_t seq) {
    uint8_t packet[62] = {0};
    struct ethhdr *eth = (struct ethhdr *)packet;
    
    // 填充以太网头
    memcpy(eth->ether_dmac, stream_rx.peer_mac, ETH_ALEN);
    memcpy(eth->ether_smac, stream_rx.local_mac, ETH_ALEN);
    net_put_be16((uint8_t *)&eth->ether_type, ETH_P_IP);
    
    // 填充IP头
    uint8_t *ip = packet + NET_ETH_HDR_LEN;
    ip[0] = 0x45;
    ip[1] = 0;
    net_put_be16(&ip[2], NET_IP_HDR_LEN + NET_TCP_HDR_LEN + RTP_HDR_LEN);
    net_put_be16(&ip[4], 0);
    net_put_be16(&ip[6], 0);
    ip[8] = 64;
    ip[9] = 6;
    net_put_be16(&ip[10], 0);
    net_put_be32(&ip[12], stream_rx.local_ip);
    net_put_be32(&ip[16], stream_rx.peer_ip);

    // 填充TCP头 (伪)
    uint8_t *tcp = ip + NET_IP_HDR_LEN;
    net_put_be16(&tcp[0], stream_rx.local_port);
    net_put_be16(&tcp[2], stream_rx.peer_port);
    net_put_be32(&tcp[4], 0);
    net_put_be32(&tcp[8], 0);
    tcp[12] = (NET_TCP_HDR_LEN / 4u) << 4;
    tcp[13] = 0x10;
    net_put_be16(&tcp[14], 65535);
    net_put_be16(&tcp[16], 0);
    net_put_be16(&tcp[18], 0);

    // 填充 Stream 协议头
    stream_header_t *header = (stream_header_t *)&packet[STREAM_DATA_OFFSET];
    header->magic = STREAM_MAGIC;
    header->flags = 0x02; // RSD
    header->len = 0;
    header->seq = htonl(seq);

    do_net_send(packet, sizeof(packet));
    return;
}

static int handle_stream_packet(uint8_t *packet_data, uint32_t packet_len, void *user_buffer) {
    if (packet_len < STREAM_DATA_OFFSET + sizeof(stream_header_t)) {
        return -1;
    }
    
    stream_header_t *header = (stream_header_t *)&packet_data[STREAM_DATA_OFFSET];
    if (header->magic != STREAM_MAGIC) {
        return -2;
    }
    if (header->flags != 0x01) { // 只处理 DAT 包
        return -3;
    }

    // 更新远端信息
    memcpy(stream_rx.peer_mac, packet_data + 6, ETH_ALEN);
    memcpy(stream_rx.local_mac, packet_data, ETH_ALEN);

    const uint8_t *ip = packet_data + NET_ETH_HDR_LEN;
    stream_rx.peer_ip = net_get_be32(ip + 12);
    stream_rx.local_ip = net_get_be32(ip + 16);

    const uint8_t *tcp = ip + 20;
    stream_rx.peer_port = net_get_be16(tcp);
    stream_rx.local_port = net_get_be16(tcp + 2);

    uint16_t data_len = ntohs(header->len);
    uint32_t seq = ntohl(header->seq);

    if (packet_len < STREAM_DATA_OFFSET + sizeof(stream_header_t) + data_len) {
        return -4;
    }

    uint8_t *data = &packet_data[STREAM_DATA_OFFSET + sizeof(stream_header_t)];

    // 核心逻辑 1: 旧包 (Seq + Len <= Expected) -> 发送 ACK
    if (seq + data_len <= stream_rx.expected_seq) {
        send_stream_ack(stream_rx.expected_seq);
        return -5;
    }

    // 核心逻辑 2: 正确包 (Seq == Expected) -> 拷贝 + 更新 + ACK
    if (seq == stream_rx.expected_seq) {
        memcpy((uint8_t *)user_buffer + stream_rx.expected_seq, data, data_len);
        stream_rx.expected_seq += data_len;
        send_stream_ack(stream_rx.expected_seq);
        printk("get %d\n", stream_rx.expected_seq);
        return data_len;
    } 
    // 核心逻辑 3: 乱序包 (Seq > Expected) -> 发送 RSD
    else {
        send_stream_rsd(stream_rx.expected_seq);
        return -6;
    }
}

// 独立的超时重传检测函数
int do_rsd_longterm() {
    if (stream_rx.valid == 1) {
        uint32_t time_end = get_timer(); // 获取当前时间 tick
        if (stream_rx.last_seq == stream_rx.expected_seq) {
            // 如果 Expected Seq 很久(>=10 ticks)没有变化
            if ((time_end - stream_rx.start_time) >= 10) {
                send_stream_rsd(stream_rx.expected_seq);
                stream_rx.start_time = time_end;
            }
        } else {
            // 进度有更新，重置计时器
            stream_rx.last_seq = stream_rx.expected_seq;
            stream_rx.start_time = time_end;
        }
    }
    return 0;
}

int do_net_recv_stream(void *user_buffer, int *nbytes_ptr, int length) {
    stream_receiver_init(); // 初始化状态
    stream_rx.valid = 1;
    stream_rx.start_time = get_timer();
    
    if (!user_buffer || !nbytes_ptr) {
        printk("the pointer is NULL!\n");
        stream_rx.valid = 0;
        return -1;
    }
    
    nbytes_ptr[0] = 0;
    int packet_len = 0;
    uint32_t total_bytes = 0;

    while (total_bytes < length) {
        // [集成] 调用超时检测，确保尾部丢包能恢复
        do_rsd_longterm();

        do_net_recv_re(stream_packet_data, 1, &packet_len);
        
        if (packet_len > 4095) {
            printk("too big for one packet\n");
            stream_rx.valid = 0;
            return -1;
        } else {
            int status = 0;
            uint32_t packet_length = packet_len;
            if (packet_length > 0) {
                status = handle_stream_packet(stream_packet_data, packet_length, user_buffer);
                if (status > 0) {
                    total_bytes += status;
                } else if (status != -6 && status != -5) {
                    // 打印错误码 (可选)
                    // printk("Status: %d\n", status);
                }
            }
        }
    }
    stream_rx.valid = 0;
    // 传输结束，发送最后一个ACK并清空状态
    return total_bytes;
}

// =========================================================================
// Task 1, 2, 3: 基础功能实现
// =========================================================================

int do_net_send(void *txpacket, int length) {
    int trans_len;
    int sched_cpu_id = get_current_cpu_id();
    while (1) {
        trans_len = e1000_transmit(txpacket, length);
        if (trans_len == 0) {
            e1000_write_reg(e1000, E1000_IMS, E1000_IMS_TXQE);
            local_flush_dcache();
            do_block(&current_running[sched_cpu_id]->list, &send_block_queue);
        } else {
            break;
        }
    }
    return trans_len;
}

int do_net_recv(void *rxbuffer, int pkt_num, int *pkt_lens) {
    int total_bytes = 0;
    int sched_cpu_id = get_current_cpu_id();
    for (int i = 0; i < pkt_num;) {
        pkt_lens[i] = e1000_poll(rxbuffer);
        if (pkt_lens[i] == 0) {
            e1000_write_reg(e1000, E1000_IMS, E1000_IMS_RXDMT0);
            local_flush_dcache();
            do_block(&current_running[sched_cpu_id]->list, &recv_block_queue);
            continue;
        }
        rxbuffer += pkt_lens[i];
        total_bytes += pkt_lens[i];
        i++;
    }
    return total_bytes;
}

int do_net_recv_re(void *rxbuffer, int pkt_num, int *pkt_lens) {
    int total_bytes = 0;
    // 轮询模式的接收，带有 sleep 节流
    for (int i = 0; i < pkt_num;) {
        pkt_lens[i] = e1000_poll(rxbuffer);
        if (pkt_lens[i] == 0) {
            do_sleep(5); // 关键的节流，避免空转
            continue;
        }
        rxbuffer += pkt_lens[i];
        total_bytes += pkt_lens[i];
        i++;
    }
    return total_bytes;
}

// =========================================================================
// 中断处理函数
// =========================================================================

static void handle_e1000_txqe(void) {
    free_block_list(&send_block_queue); // 注意：这里使用了 free_wait_list 而非 free_block_list，根据你提供的代码风格
    e1000_write_reg(e1000, E1000_IMC, E1000_IMC_TXQE);
    local_flush_dcache();
}

static void handle_e1000_rxdmt0(void) {
    free_block_list(&recv_block_queue);
    e1000_write_reg(e1000, E1000_IMC, E1000_IMC_RXDMT0);
    local_flush_dcache();
}

void net_handle_irq(void) {
    local_flush_dcache();
    uint32_t icr = e1000_read_reg(e1000, E1000_ICR);
    if (icr & E1000_ICR_TXQE) {
        handle_e1000_txqe();
    }
    if (icr & E1000_ICR_RXDMT0) {
        handle_e1000_rxdmt0();
    }
}