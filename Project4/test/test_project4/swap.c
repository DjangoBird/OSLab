#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

const long MSG_BYTES = 16 * 4096;

static void fill_payload(char *buf, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    buf[i] = (char)(i & 0xff);
  }
}

static char *alloc_payload_buffer(void) { return (char *)(1ul << 30); }

int main() {
  char *buf = alloc_payload_buffer();
  fill_payload(buf, MSG_BYTES);
  for (size_t i = 0; i < MSG_BYTES; ++i) {
    if (buf[i] != (char)(i & 0xff)) {
      sys_move_cursor(0, 2);
      printf("错误位置: i=%lu, 期望=0x%02x, 实际=0x%02x\n", i, (i & 0xff),
             (unsigned char)buf[i]);
      // printf("[pipe recv] data mismatch at %d\n", (int)i);
      return -1;
    }
  }
  printf("swap success\n");
  return 0;
}
