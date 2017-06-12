#define move_to_user_mode() \
__asm__ ("movl %%esp,%%eax\n\t" \
	"pushl $0x17\n\t" \
	"pushl %%eax\n\t" \
	"pushfl\n\t" \
	"pushl $0x0f\n\t" \
	"pushl $1f\n\t" \
	"iret\n" \
	"1:\tmovl $0x17,%%eax\n\t" \
	"mov %%ax,%%ds\n\t" \
	"mov %%ax,%%es\n\t" \
	"mov %%ax,%%fs\n\t" \
	"mov %%ax,%%gs" \
	:::"ax")

#define sti() __asm__ ("sti"::)
#define cli() __asm__ ("cli"::)
#define nop() __asm__ ("nop"::)

#define iret() __asm__ ("iret"::)


//设置后的门描述符为 （中断门或陷阱门)
//offset 31.16: addr的高16bit  P:1 (来自0x8000) 后跟dpl(2 bit) type (5 bits)
//0008: 代码段(GDT第1项) offfset 15.0: addr的低16bit(即为中断门处理函数handler的地址)
#define _set_gate(gate_addr,type,dpl,addr) \
__asm__ ("movw %%dx,%%ax\n\t" \
	"movw %0,%%dx\n\t" \
	"movl %%eax,%1\n\t" \
	"movl %%edx,%2" \
	: \
	: "i" ((short) (0x8000+(dpl<<13)+(type<<8))), \
	"o" (*((char *) (gate_addr))), \
	"o" (*(4+(char *) (gate_addr))), \
	"d" ((char *) (addr)),"a" (0x00080000))

//idt这里是一个数组，每个元素长度为8 byte，idt[n]每次跳过8 byte
//dpl:00 type:01110 (interrupt gate)
#define set_intr_gate(n,addr) \
	_set_gate(&idt[n],14,0,addr)

//dpl:00 type:01111 (trap gate) 仅仅对内核态开放
#define set_trap_gate(n,addr) \
	_set_gate(&idt[n],15,0,addr)

//dpl:11 type:01111 (trap gate) 用户态开放
#define set_system_gate(n,addr) \
	_set_gate(&idt[n],15,3,addr)

#define _set_seg_desc(gate_addr,type,dpl,base,limit) {\
	*(gate_addr) = ((base) & 0xff000000) | \
		(((base) & 0x00ff0000)>>16) | \
		((limit) & 0xf0000) | \
		((dpl)<<13) | \
		(0x00408000) | \
		((type)<<8); \
	*((gate_addr)+1) = (((base) & 0x0000ffff)<<16) | \
		((limit) & 0x0ffff); }

//设置到gdt数组中,tss/ldt, 每个数组元素为8 byte，其实是一个gdt描述符
//n为gdt中的位置， addr为ldt/tss的线性地址，两者均在task_struct中。由于16M以内的线性地址已经和16M物理地址做了一一映射，所以此处也是实际的物理地址
#define _set_tssldt_desc(n,addr,type) \
__asm__ ("movw $104,%1\n\t" \       //segment limit (0-15 bit)
	"movw %%ax,%2\n\t" \            //base address (0-15 bit)
	"rorl $16,%%eax\n\t" \          
	"movb %%al,%3\n\t" \            //base address (16-23 bit)
	"movb $" type ",%4\n\t" \       //P-DPL-S-Type (8 bits)
	"movb $0x00,%5\n\t" \           //G-DB-A: 0000 limit (16-19 bit)
	"movb %%ah,%6\n\t" \            //base address (24-31 bit)
	"rorl $16,%%eax" \
	::"a" (addr), "m" (*(n)), "m" (*(n+2)), "m" (*(n+4)), \
	 "m" (*(n+5)), "m" (*(n+6)), "m" (*(n+7)) \
	)

//P-DPL-S-Type:10001001 32bit TSS
#define set_tss_desc(n,addr) _set_tssldt_desc(((char *) (n)),addr,"0x89")
//P-DPL-S-Type:10000010 LDT
#define set_ldt_desc(n,addr) _set_tssldt_desc(((char *) (n)),addr,"0x82")
