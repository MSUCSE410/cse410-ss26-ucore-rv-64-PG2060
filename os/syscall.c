#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(TimeVal *val, int _tz) // TODO: implement sys_gettimeofday in pagetable. (VA to PA)
{
	// YOUR CODE
	struct proc *p = curr_proc();
	uint64 pa = useraddr(p->pagetable, (uint64)val);

	TimeVal *kval = (TimeVal *)pa;

	/* The code in `ch3` will leads to memory bugs*/

	uint64 cycle = get_cycle();
	kval->sec = cycle / CPU_FREQ;
	kval->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	return 0;
}

// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
/*
* LAB1: you may need to define sys_task_info here
*/

uint64 sys_task_info(TaskInfo * ti){

	struct proc *p = curr_proc();

	// need to translate ti bc virtual to pa so kernel can access
	uint64 pa = useraddr(p->pagetable, (uint64)ti);
	TaskInfo *kti = (TaskInfo *)pa;


	TaskStatus status;
	switch (p->state) {
		case UNUSED:
			status = UnInit;
			break;
		case RUNNABLE:
			status = Ready;
			break;
		case RUNNING:
			status = Running;
			break;
		case ZOMBIE:
			status = Exited;
			break;
		default:
			status = UnInit;
			break;
	}

	uint64 curr_cyc = get_cycle();
	uint64 elapsed_cyc = curr_cyc - p->start_cycle;
	int running_time = (elapsed_cyc * 1000) / CPU_FREQ;


	kti->status = status;
	for (int j = 0; j < MAX_SYSCALL_NUM; j++) {
			kti->syscall_times[j] = p->syscall_times[j];
	}
	kti->time = running_time;

	return 0;
}

/*
Parameters:
• start: start address of the virtual memory to be mapped.
• len: length of mapped byte, can be 0 (if yes, return directly), not too big
(upper limit 1GiB).
• port: bit 0 indicates whether it is readable, bit 1 indicates whether it is
writable, and bit 2 indicates whether it is executable. Other bits are invalid
(must be 0).
• flag: currently always 0, ignore this parameter.
• fd: always 0, ignore this parameter
*/
uint64 sys_mmap(uint64 start, uint64 len, int port, int flag, int fd){

	if (port & ~0x7)
		return -1;
	if ((port & 0x7) == 0)
		return -1;
	if (len > (1ULL << 30)) // 1gib
		return -1;
	if (start % PGSIZE != 0) // if address not page aligned
		return -1;


	struct proc *p = curr_proc();
	uint64 round_len = PGROUNDUP(len);

	// makes sure that [addr, sddr+len] page isnt mapped
	for(uint64 va = start; va < start + round_len; va += PGSIZE)
	{
		pte_t *pte = walk(p->pagetable, va, 0);
		if (pte !=0 && (*pte & PTE_V))
			return -1;
	}

	// creating page table entry perm flag
	int perm = PTE_U;
	if (port & 0x1)
		perm |= PTE_R;
	if (port & 0x2)
		perm |= PTE_W;
	if (port & 0x4)
		perm |= PTE_X;

	// mapping
	for(uint64 va = start; va < start+round_len; va += PGSIZE) {
		void *pa = kalloc();

		memset(pa, 0, PGSIZE);

		if (mappages(p->pagetable, va, PGSIZE, (uint64)pa, perm) !=0)
			return -1;
	}
	
	return 0;
}

uint64 sys_munmap(uint64 start, uint64 len){

	if (start % PGSIZE != 0) // if start not page aligned
		return -1;

	struct proc *p = curr_proc();
	uint64 round_len = PGROUNDUP(len);

	// makes sure that [addr, sddr+len] page is mapped
	for(uint64 va = start; va < start + round_len; va += PGSIZE)
	{
		pte_t *pte = walk(p->pagetable, va, 0);
		// means a page that is unmapped is found
		if (pte == 0 || !(*pte & PTE_V))
			return -1;
	}

	uvmunmap(p->pagetable, start, round_len / PGSIZE, 1);

	return 0;
}

extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	/*
	* LAB1: you may need to update syscall counter for task info here
	*/
	struct proc *p = curr_proc();
	if (id>= 0 && id < MAX_SYSCALL_NUM) {
		p->syscall_times[id]++;
	}
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((TimeVal *)args[0], args[1]);
		break;
	case SYS_task_info:
		ret = sys_task_info((TaskInfo *)args[0]);
		break;
	/*
	* LAB1: you may need to add SYS_taskinfo case here
	*/
	case SYS_mmap:
		ret = sys_mmap(args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_munmap:
		ret = sys_munmap(args[0], args[1]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
