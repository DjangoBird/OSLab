
内存空间：

0x50200000：bootloader 
0x50201000：kernel 
0x50500000：kernel_stack 
0x52000000：用户程序0 ··· 0x520f0000 ：用户程序15（最多16个用户程序） 
0x52500000：用户程序info数组地址 
0x53000000：用户程序栈
0x54000000：批处理内存 

### 任务1：制作第一个引导块

```asm
	la a0, msg
	li a7, BIOS_PUTSTR
	jal bios_func_entry
```

调用BIOS_PUTSTR函数
### 任务2：加载和初始化内存

调用BIOS_SDREAD函数

```asm
	li a7, BIOS_SDREAD
	la a0, kernel
	la t0, os_size_loc
	lh a1, 0(t0)
	li a2, 1
	jal bios_func_entry
```

一个需要注意的问题是内核占了几个扇区。这个在createimage 的时候会显示，我们提供的 createimage 文件会把扇区的数目写在了头一个扇区的倒数第 4 个字节的位置(0x502001fc)，长度为 2 字节。用 lh指令可以载入两字节到寄存器。（之前没注意到然后爆栈了）

清空BSS段，往BSS段中写入0
```asm
la t0 , __bss_start
la t1 , __BSS_END__

clear_bss:
  bge t0 , t1 , bss_done
  sw zero , 0(t0)
  addi t0 , t0 , 4
  j clear_bss
```

按要求完成回显

```
	//读取终端输入并回显
    int tmp;
    while(1){
        while((tmp=bios_getchar())==-1);
        bios_putchar(tmp);
    }
```

### 任务3：加载并选择启动多个用户程序之一

```
    int nsec_kern = NBYTES2SEC(nbytes_kernel);
    fseek(img, OS_SIZE_LOC, SEEK_SET);  
    fwrite(&nsec_kern, 2, 1, img);      
    printf("Kernel size: %d sectors\n", nsec_kern);
```

我们使用这段函数将bootloader以及kernel和应用程序放在镜像的固定位置上，具体就是bootloader固定占据第一个扇区，后面kernel以及应用程序都占据15个扇区，所以我们需要使用write_padding函数来填充空闲的扇区。

```
if (strcmp(*files, "bootblock") == 0) {
            off+=1;
            write_padding(img, &phyaddr, SECTOR_SIZE);
}
else {
      off+=15;
      write_padding(img, &phyaddr, off*SECTOR_SIZE);
}
fclose(fp);
files++;
```

load_task_img函数细节如下： 因为之前的应用程序在disk的位置已经是固定的了，所以只需要根据taskid来确定应用程序的位置即可。

```
uint64_t entry_addr;
    char info[] = "Loading task _ ...\n\r";
    for(int i=0;i<strlen(info);i++){
        if(info[i]!='_') bios_putchar(info[i]);
        else bios_putchar(taskid +'0');
    }
    entry_addr = TASK_MEM_BASE + TASK_SIZE * (taskid - 1);  
    bios_sd_read(entry_addr, 15, 1 + taskid * 15);
    return entry_addr;
```

最后我们需要完善crt0.S文件，crt0.s是每个用户程序都需要的初始化自身C语言环境功能。
>（哎没完善导致batch回不去栈帧被bios调用疯狂刷掉）

至此S-core任务已完成。

### 任务4: 镜像文件的紧密排列

这里我们需要修改createimage.c文件，在main中除了bootblock需要padding第0个扇区，kernel以及其他应用程序以及要新添的taskinfo都不进行padding了。

```
typedef struct {
    char task_name[16];
    int start_addr;
    int block_nums;
} task_info_t;
```

然后我们将task_info_t数组放在image文件的末尾，具体位置及大小将储存在bootloader的倒数第三个字和倒数第二个字当中：

```
    int info_size = sizeof(task_info_t) * tasknum;
    // 将定位信息写进bootloader的末尾几个字节
    fseek(img, APP_INFO_ADDR_LOC, SEEK_SET);
    fwrite(taskinfo_addr, 4, 1, img);
    printf("Address for task info: %x.\n", *taskinfo_addr);
    fwrite(&info_size, 4, 1, img);    
    printf("Size of task info array: %d bytes.\n", info_size);
    fseek(img, *taskinfo_addr, SEEK_SET);  
    fwrite(taskinfo, sizeof(task_info_t), tasknum, img);
    printf("Write %d tasks into image.\n",  tasknum);
    *taskinfo_addr+=info_size;
```

同时我们需要填写appinfo中的信息：

```
else if(tasknum>=0){
            taskinfo[taskidx].task_name[0]= '\0';
            strcat(taskinfo[taskidx].task_name, *files);
            taskinfo[taskidx].start_addr = start_addr;
            taskinfo[taskidx].block_nums  = NBYTES2SEC(phyaddr) - start_addr / SECTOR_SIZE;
            printf("current phyaddr:%x\n", phyaddr);
            printf("%s: start_addr is %x, blocknums is %d\n",\
            taskinfo[taskidx].task_name, taskinfo[taskidx].start_addr,taskinfo[taskidx].block_nums);
        }
```

我们在bootblock.S中需要添加以下代码，将appinfo的位置和大小作为输入，输入到kernel当中：

```
la t1, app_info_addr_loc	
	lw a0, (t1)		// pass the location for task info as parameter 1
	lw a1, 4(t1)	// pass the size as parameter 2
```

在main中，我们使用init_task_info函数将appinfo的信息初始化到tasks数组中。这里我们把appinfo放置到内存的地址是0x52500000。

```
static void init_task_info(int app_info_loc, int app_info_size)
{
    int start_sec, blocknums;
    start_sec = app_info_loc / SECTOR_SIZE;
    blocknums = NBYTES2SEC(app_info_loc + app_info_size) - start_sec;
    int task_info_addr = TASK_INFO_MEM;
    bios_sd_read(task_info_addr, blocknums, start_sec);
    int start_addr = (TASK_INFO_MEM + app_info_loc - start_sec * SECTOR_SIZE);
    uint8_t *tmp = (uint8_t *)(start_addr);
    memcpy((uint8_t *)tasks, tmp, app_info_size);
}
```

接下来我们修改load_task_img函数，首先我们将输入改为字符串，这样我们比较字符串与每一个task的name，如果有匹配，就将该task加载到指定位置的内存当中。

```
uint64_t load_task_img(char *taskname){
    int i;
    int entry_addr;
    int start_sec;
    for(i=0;i<TASK_MAXNUM;i++){
        if(strcmp(taskname, tasks[i].task_name)==0){
            entry_addr = TASK_MEM_BASE + TASK_SIZE * i;
            start_sec = tasks[i].start_addr / 512;     // 起始扇区：向下取整
            bios_sd_read(entry_addr, tasks[i].block_nums, start_sec);  
            return entry_addr + (tasks[i].start_addr - start_sec*512);  
            // 返回程序存储的起始位置
        }
    }
    // 匹配失败，提醒重新输入
    char *output_str = "Fail to find the task! Please try again!";
    for(i=0; i<strlen(output_str); i++){
        bios_putchar(output_str[i]);
    }
    bios_putchar('\n');
    return 0;
}
```

### 任务5：批处理运行多个用户程序和管道输入



```csharp
跳转表

┌───────────────────────────┐
│     内核代码 main()        │
│  bios_putstr("Hello OS!") │
└─────────────┬─────────────┘
              │
              │ 调用封装函数
              ▼
┌───────────────────────────┐
│      call_jmptab()         │
│  which = CONSOLE_PUTSTR    │
│  计算地址 = KERNEL_JMPTAB_BASE + 0*8 │
│  取出函数地址 → func       │
│  func(arg0, arg1, ...)     │
└─────────────┬─────────────┘
              │
              ▼
┌───────────────────────────┐
│      jump table (0x51ffff00) │
│  0x51ffff00 → port_write()  │
│  0x51ffff08 → port_write_ch()│
│  0x51ffff10 → port_read_ch()│
│  0x51ffff18 → sd_read()     │
│  0x51ffff20 → sd_write()    │
└─────────────┬─────────────┘
              │
              ▼
┌───────────────────────────┐
│       port_write()         │
│ 调用 call_bios(BIOS_PUTSTR)│
└─────────────┬─────────────┘
              │
              ▼
┌───────────────────────────┐
│      call_bios()           │
│  BIOS入口 0x50150000       │
│  根据 which 参数选择功能    │
│  执行真实 I/O              │
└───────────────────────────┘

```
