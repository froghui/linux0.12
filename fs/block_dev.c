/*
 *  linux/fs/block_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>

extern int *blk_size[];

//将buf处的count字节数 写到dev设备的pos位置
//数据 physical memory of process -->buffer-->disk, 这里完成process memory-->buffer
//buffer-->disk由磁盘中断完成。第一个触发磁盘读写操作后，磁盘中断会不断的处理磁盘读写请求。
int block_write(int dev, long * pos, char * buf, int count)
{
	int block = *pos >> BLOCK_SIZE_BITS;
	int offset = *pos & (BLOCK_SIZE-1);
	int chars;
	int written = 0;
	int size;
	struct buffer_head * bh;
	register char * p;

	if (blk_size[MAJOR(dev)])
		size = blk_size[MAJOR(dev)][MINOR(dev)];
	else
		size = 0x7fffffff;
	while (count>0) {
		//block_nr阈值检查
		if (block >= size)
			return written?written:-EIO;
		//需要写多少字节
		chars = BLOCK_SIZE - offset;
		if (chars > count)
			chars=count;
		//如果是读整个block
		if (chars == BLOCK_SIZE)
			bh = getblk(dev,block);
		else
			bh = breada(dev,block,block+1,block+2,-1);
		block++;
		if (!bh)
			return written?written:-EIO;
		//开始写入，计数器包括: 当前位置指针*pos，一共需要写的count，已经写入的witten， 同时offset也置0，下一次从block头开始写
		p = offset + bh->b_data;
		offset = 0;
		*pos += chars;
		written += chars;
		count -= chars;
		//循环写入chars个 byte, 注意这只是写到内存中
		while (chars-->0)
			*(p++) = get_fs_byte(buf++);
		//标志为dirt，需要同步到硬盘中
		bh->b_dirt = 1;
		brelse(bh);
	}
	return written;
}

//将设备dev pos处的count数据读到buf处
//数据 disk-->buffer-->pyhsical memory of process
int block_read(int dev, unsigned long * pos, char * buf, int count)
{
	int block = *pos >> BLOCK_SIZE_BITS;
	int offset = *pos & (BLOCK_SIZE-1);
	int chars;
	int size;
	int read = 0;
	struct buffer_head * bh;
	register char * p;

    //blk_size是个什么鬼？？二维数组，谁初始化？
	if (blk_size[MAJOR(dev)])
		size = blk_size[MAJOR(dev)][MINOR(dev)];
	else
		size = 0x7fffffff;//4M最大，这里是不是有问题 应该最大为 0x3fff (4M>>10)
	while (count>0) {
		if (block >= size)
			return read?read:-EIO;
		chars = BLOCK_SIZE-offset;
		//截断，不需要整个block这么多，只需要一小部分(count这么多)
		if (chars > count)
			chars = count;
		//预读2个block一共读3个block数据
		if (!(bh = breada(dev,block,block+1,block+2,-1)))
			return read?read:-EIO;
		block++;
		//开始读取，计数器包括: 当前位置指针*pos，一共需要读的count，已经读的read， 同时offset也置0，下一次从block头开始读
		p = offset + bh->b_data;
		offset = 0;
		*pos += chars;
		read += chars;
		count -= chars;
		//循环写入chars个 byte, 注意这只是写到内存中
		while (chars-->0)
			put_fs_byte(*(p++),buf++);
		//当前block可以释放掉
		brelse(bh);
	}
	return read;
}
