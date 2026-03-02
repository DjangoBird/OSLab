/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 *               Author : Han Shukai (email : hanshukai@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                  The shell acts as a task running in user mode.
 *       The main function is to make system calls through the user's output.
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define SHELL_BEGIN 20
#define MAX_INS_LENGTH 128
#define MAX_ARG_NUM 16
#define MAX_ARG_LEN 32

char buff[MAX_INS_LENGTH];
int ins_pos = 0;
int end;
int argc;
char argv[MAX_ARG_NUM][MAX_ARG_LEN];

int parse_args(const char *buff);
int xtoi(char *str);

void shell_clear_display(){
    sys_clear();
    sys_move_cursor(0, SHELL_BEGIN);
    printf("------------------- COMMAND -------------------\n");
}

int main(void)
{
    sys_move_cursor(0, SHELL_BEGIN);
    printf("------------------- COMMAND -------------------\n");
    printf("> Django@UCAS_OS: ");
    int input_char;

    while (1)
    {
        // TODO [P3-task1]: call syscall to read UART port
        end = 0;

        while((input_char = sys_getchar()) == -1);
        if(input_char == '\b' || input_char == 127){
            if(ins_pos > 0){
                sys_write_ch('\b');
                sys_reflush();
                buff[--ins_pos] = '\0';
            }
        }
        else if(input_char == '\n' || input_char == '\r'){
            sys_write_ch('\n');
            sys_reflush();
            end = 1;
        }
        else{
            if(ins_pos < MAX_INS_LENGTH - 1){
                sys_write_ch(input_char);
                sys_reflush();
                buff[ins_pos++] = input_char;
            }
        }

        if(end == 0) continue;

        buff[ins_pos] ='\0';
        ins_pos = 0;

        argc = parse_args(buff);

        if(argc == 0){
            printf("> Django@UCAS_OS: ");
            continue;
        }
        else if(strcmp("exec", argv[0]) == 0){
            if(argc < 2){
                printf("ERROR: exec requires a program name!\n");
            }else{
                int is_wait = strcmp(argv[argc-1],"&");
                int exec_argc = argc - 1 - (is_wait ? 0 : 1);

                char *exec_argv[MAX_ARG_NUM];
                for(int i = 0; i < exec_argc; i++){
                    exec_argv[i] = argv[i+1];
                }

                int pid = sys_exec(argv[1], exec_argc, exec_argv);

                if(pid <= 0){
                    printf("ERROR: exec failed for program: ");
                    printf("%s", argv[1]);
                    printf("!\n");
                }
                else{
                    printf("Info: execute ");
                    printf("%s",argv[1]);
                    printf(" successfully, pid = ");
                    printf("%d",pid);
                    printf(" ...\n");

                    if(is_wait){
                        sys_waitpid(pid);
                    }
                }
            }
        }
        else if(strcmp("kill", argv[0]) == 0){
            if(argc != 2){
                printf("ERROR: kill requires only one pid!\n");
            }else{
                int pid = atoi(argv[1]);
                if(sys_kill(pid) == 0){
                    printf("ERROR: kill %d failed!\n", pid);
                }else{
                    printf("Info: kill %d successfully!\n", pid);
                }
            }
        }
        else if(strcmp("wait", argv[0]) == 0){
            if(argc != 2){
                printf("ERROR: wait requires only one pid!\n");
            }else{
                int pid = atoi(argv[1]);
                if(sys_waitpid(pid) <= 0){
                    printf("ERROR: Cannot find process %d or wait failed ...\n", pid);
                }else{
                    printf("Info: Execute waitpid successfully, process %d exited\n", pid);
                }
            }
        }
        else if(strcmp("exit", argv[0]) == 0){
            sys_exit();
        }
        else if(strcmp("ps", argv[0]) == 0){
            sys_ps();
        }
        else if(strcmp("clear", argv[0]) == 0){
            shell_clear_display();
        }
        else if (strcmp("taskset", argv[0]) == 0) {
            if (argc == 4 && strcmp(argv[1], "-p") == 0) {
                int mask = xtoi(argv[2]);
                int target_pid = atoi(argv[3]);
                int ret = sys_taskset(1, mask, (void *)(long)target_pid);
                if (ret >= 0)
                    printf("Info: Set pid %d affinity mask to 0x%x\n", target_pid, mask);
                else
                    printf("Error: PID %d not found.\n", target_pid);
            }
            else if (argc == 3) {
                int mask = xtoi(argv[1]); // 使用 hex 解析
                char *cmd_name = argv[2];
                
                int pid = sys_taskset(0, mask, (void *)cmd_name);
                
                if (pid > 0)
                    printf("Info: Launched %s with affinity mask 0x%x, pid = %d\n", cmd_name, mask, pid);
                else
                    printf("Error: Failed to launch %s\n", cmd_name);
            }
            else {
                printf("Usage: taskset <mask> <cmd> OR taskset -p <mask> <pid>\n");
                printf("Note: mask is hex (e.g., 0x3)\n");
            }
        }
        else if (strcmp("free", argv[0]) == 0) {
            size_t free_mem = sys_free_mem();
            int human = (argc > 1 && strcmp(argv[1], "-h") == 0);

            if (human) {
                if (free_mem < 1024) {
                    printf("Free Memory: %d B\n", (int)free_mem);
                } else if (free_mem < 1024 * 1024) {
                    printf("Free Memory: %d KB\n", (int)(free_mem / 1024));
                } else {
                    printf("Free Memory: %d MB\n", (int)(free_mem / 1024 / 1024));
                }
            } else {
                printf("Free Memory: %d bytes\n", (int)free_mem);
            }
        }
        else{
            printf("ERROR: unknown command: %s\n", argv[0]);
        }

        printf("> Django@UCAS_OS: ");
        memset(buff, 0, MAX_INS_LENGTH);

        /************************************************************/
        /* Do not touch this comment. Reserved for future projects. */
        /************************************************************/    
    }

    return 0;
}

int parse_args(const char *buff){
    int local_argc = 0;
    int i;
    for(int j = 0; j < MAX_ARG_NUM; j++){
        argv[j][0] = '\0';
    }

    while(*buff){
        while(*buff && isspace(*buff)) buff++;
        if(*buff == '\0') break;
        if(local_argc >= MAX_ARG_NUM) break;

        i = 0;
        while(*buff && ! isspace(*buff) && i < MAX_ARG_LEN - 1){
            argv[local_argc][i++] = *buff++;
        }
        argv[local_argc][i] = '\0';
        local_argc++;
    }
    return local_argc;
}

int xtoi(char *str) {
    int res = 0;
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str += 2; // 跳过 0x
    }
    while (*str) {
        char c = *str;
        int val;
        if (c >= '0' && c <= '9') val = c - '0';
        else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
        else break;
        res = res * 16 + val;
        str++;
    }
    return res;
}
