#ifndef _HEAD_H
#define _HEAD_H

//desc_struct为描述符，例如GDT/IDT/LDT描述符，8byte, 256个条目
typedef struct desc_struct {
	unsigned long a,b;
} desc_table[256];

//内存dir条目 1024个
extern unsigned long pg_dir[1024];
//idt和gdt链接
extern desc_table idt,gdt;

#define GDT_NUL 0
#define GDT_CODE 1
#define GDT_DATA 2
#define GDT_TMP 3

#define LDT_NUL 0
#define LDT_CODE 1
#define LDT_DATA 2

#endif
