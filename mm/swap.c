/*
 *  linux/mm/swap.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * This file should contain most things doing the swapping from/to disk.
 * Started 18.12.91
 */

#include <string.h>

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>

#define SWAP_BITS (4096<<3)


#define bitop(name,op) \
static inline int name(char * addr,unsigned int nr) \
{ \
int __res; \
__asm__ __volatile__("bt" op " %1,%2; adcl $0,%0" \
:"=g" (__res) \
:"r" (nr),"m" (*(addr)),"0" (0)); \
return __res; \
}

//定义几个函数 bit, setbit, clrbit用来设置和清除标志位
bitop(bit,"")
bitop(setbit,"s")
bitop(clrbit,"r")

//swap标志位， 1表示该swap没被使用，0表示该swap已被使用(对应的硬盘上值有效)
static char * swap_bitmap = NULL;
int SWAP_DEV = 0;

/*
 * We never page the pages in task[0] - kernel memory.
 * We page all other pages.
 */
#define FIRST_VM_PAGE (TASK_SIZE>>12)//16K
#define LAST_VM_PAGE (1024*1024) //1024K
#define VM_PAGES (LAST_VM_PAGE - FIRST_VM_PAGE)

//产生一个页号 取值为 1-32K(2^15)
static int get_swap_page(void)
{
	int nr;

	if (!swap_bitmap)
		return 0;
	for (nr = 1; nr < 32768 ; nr++)
		if (clrbit(swap_bitmap,nr))
			return nr;
	return 0;
}

void swap_free(int swap_nr)
{
	if (!swap_nr)
		return;
	if (swap_bitmap && swap_nr < SWAP_BITS)
		if (!setbit(swap_bitmap,swap_nr))
			return;
	printk("Swap-space bad (swap_free())\n\r");
	return;
}

//将 table_ptr对应的swap_nr中得数据读入内存，并重新生成内存页表项
void swap_in(unsigned long *table_ptr)
{
	int swap_nr;
	unsigned long page;

	if (!swap_bitmap) {
		printk("Trying to swap in without swap bit-map");
		return;
	}
	if (1 & *table_ptr) {
		printk("trying to swap in present page\n\r");
		return;
	}
	//取得swap_nr，用来指定读取数据的位置
	swap_nr = *table_ptr >> 1;
	if (!swap_nr) {
		printk("No swap page in swap_in\n\r");
		return;
	}

	//分配一页物理内存
	if (!(page = get_free_page()))
		oom();
	//将数据从硬盘刷到内存中
	read_swap_page(swap_nr, (char *) page);
	//swap标志位设为1,未使用
	if (setbit(swap_bitmap,swap_nr))
		printk("swapping in multiply from same page\n\r");
	//权限操作，并且该页内存下次一定要存回到swap分区
	*table_ptr = page | (PAGE_DIRTY | 7);
}

//将 table_ptr指定的页面写到swap中
int try_to_swap_out(unsigned long * table_ptr)
{
	unsigned long page;
	unsigned long swap_nr;

	page = *table_ptr;
	//一定是在内存中
	if (!(PAGE_PRESENT & page))
		return 0;
	if (page - LOW_MEM > PAGING_MEMORY)
		return 0;

	//如果该页数据修改过，需要刷到硬盘上
	if (PAGE_DIRTY & page) {
		page &= 0xfffff000;
		//该页内存应该在mem_map中，如果不在则返回出错
		if (mem_map[MAP_NR(page)] != 1)
			return 0;
		//分配一个swap页号
		if (!(swap_nr = get_swap_page()))
			return 0;
		//将页号存储到页表项中，同时设置该页表为free(不在内存中),这等于是对内存数据做了个镜像
		//下次访问该线性地址时可以直接从swap分区加载
		*table_ptr = swap_nr<<1;
		invalidate();
		//写到磁盘中
		write_swap_page(swap_nr, (char *) page);
		//释放该页物理内存，mem_map[MAP_RN(page)]--
		free_page(page);
		return 1;
	}
	//这里不对数据进行镜像操作，那么该线性地址下次必须重新读取
	*table_ptr = 0;
	invalidate();
	free_page(page);
	return 1;
}

/*
 * Ok, this has a rather intricate logic - the idea is to make good
 * and fast machine code. If we didn't worry about that, things would
 * be easier.
 */
int swap_out(void)
{
	//这里遍历所有任务对应的虚拟地址空间页，从最小的页开始，找到一个当前在内存中的页，就可以swap_out出去。
	//任务的虚拟地址空间从64M-4G，对应的页从64M>>12>>10(16) 一共1024*1024个
	//FIXME: dir_entry: 16-1023是如何写入pg_dir的？
	static int dir_entry = FIRST_VM_PAGE>>10;
	static int page_entry = -1;
	int counter = VM_PAGES;
	int pg_table;

	while (counter>0) {
		pg_table = pg_dir[dir_entry];
		if (pg_table & 1)
			break;
		counter -= 1024;//一个dir_entry对应1024个page
		dir_entry++;
		if (dir_entry >= 1024)
			dir_entry = FIRST_VM_PAGE>>10;
	}
	pg_table &= 0xfffff000;
	while (counter-- > 0) {
		page_entry++;
		//如果该pg_table都不能满足在内存中的需求，换下一个pg_table
		if (page_entry >= 1024) {
			page_entry = 0;
		repeat:
			dir_entry++;
			if (dir_entry >= 1024)
				dir_entry = FIRST_VM_PAGE>>10;
			pg_table = pg_dir[dir_entry];
			if (!(pg_table&1))
				if ((counter -= 1024) > 0)
					goto repeat;
				else
					break;
			pg_table &= 0xfffff000;
		}
		//如果成功swap_out一页，则成功退出，否则继续尝试
		if (try_to_swap_out(page_entry + (unsigned long *) pg_table))
			return 1;
	}
	printk("Out of swap-memory\n\r");
	return 0;
}

/*
 * Get physical address of first (actually last :-) free page, and mark it
 * used. If no free pages left, return 0.
 */
unsigned long get_free_page(void)
{
register unsigned long __res asm("ax");

repeat:
	__asm__("std ; repne ; scasb\n\t" //找到DI中最后的一个标志位为0的page  ECX-- AX=0 DI=mem_map+PAGING_PAGES-1
		"jne 1f\n\t"
		//OK，找到可以用的页，标志位置为1，注意scasb的伪代码为:
		//%%AL-%%DI （compare byte in AL and des)
		//%%DI=%%DI-1 (std and byte operation)
		//所以等到repne判断%%DI=0的时候，%%DI对应的值已经减1了，故这里需要用1(%%edi)补偿回来
		"movb $1,1(%%edi)\n\t"
		//ECX执向 mem_map里面可以用的索引值，注意repne循环的时候使用ECX作为计数器自减
		//ECX=(valid_page)>>12+LOW_MEM，注意在repne和scasb过程中，ECX作为计数器一直递减(std)
		"sall $12,%%ecx\n\t"
		"addl %2,%%ecx\n\t"  
		"movl %%ecx,%%edx\n\t"
		"movl $1024,%%ecx\n\t"
		"leal 4092(%%edx),%%edi\n\t" //注意不是4096，因为是从低字节到高字节的 4 byte拷贝，第一个是4092-4096,最后一个是0-4
		"rep ; stosl\n\t"//copy 1024(ECX--) from EAX to EDI(--)  (从高到低) 
		"movl %%edx,%%eax\n"
		"1:"
		:"=a" (__res)
		:"0" (0),"i" (LOW_MEM),"c" (PAGING_PAGES),
		"D" (mem_map+PAGING_PAGES-1)
		:"di","cx","dx");
	if (__res >= HIGH_MEMORY)
		goto repeat;
	if (!__res && swap_out())
		goto repeat;
	return __res;
}

void init_swapping(void)
{
	extern int *blk_size[];
	int swap_size,i,j;

	if (!SWAP_DEV)
		return;
	if (!blk_size[MAJOR(SWAP_DEV)]) {
		printk("Unable to get size of swap device\n\r");
		return;
	}
	swap_size = blk_size[MAJOR(SWAP_DEV)][MINOR(SWAP_DEV)];
	if (!swap_size)
		return;
	if (swap_size < 100) {
		printk("Swap device too small (%d blocks)\n\r",swap_size);
		return;
	}
	swap_size >>= 2;
	//swap 最大是32K个pages
	if (swap_size > SWAP_BITS)
		swap_size = SWAP_BITS;
	//swap_bitmap 每个bit位都用来作为一个标志位，一共有4K*8=32K个  支持32K*4K=128M  swap大小
	swap_bitmap = (char *) get_free_page();
	if (!swap_bitmap) {
		printk("Unable to start swapping: out of memory :-)\n\r");
		return;
	}
	//swap page id: 0 
	read_swap_page(0,swap_bitmap);
	if (strncmp("SWAP-SPACE",swap_bitmap+4086,10)) {
		printk("Unable to find swap-space signature\n\r");
		free_page((long) swap_bitmap);
		swap_bitmap = NULL;
		return;
	}
	memset(swap_bitmap+4086,0,10);
	//实际上只测试 0  swap_size-SWAP_BITS之间的bit位
	for (i = 0 ; i < SWAP_BITS ; i++) {
		if (i == 1)
			i = swap_size;
		if (bit(swap_bitmap,i)) {
			printk("Bad swap-space bit-map\n\r");
			free_page((long) swap_bitmap);
			swap_bitmap = NULL;
			return;
		}
	}
	//标志位为1的是未使用的swap空间
	j = 0;
	for (i = 1 ; i < swap_size ; i++)
		if (bit(swap_bitmap,i))
			j++;
	if (!j) {
		free_page((long) swap_bitmap);
		swap_bitmap = NULL;
		return;
	}
	printk("Swap device ok: %d pages (%d bytes) swap-space\n\r",j,j*4096);
}
