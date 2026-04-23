#include "syscall.h"
#include "console.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"

uint64 console_write(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	tracef("write size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return len;
}

uint64 console_read(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	tracef("read size = %d", len);
	for (int i = 0; i < len; ++i) {
		int c = consgetc();
		str[i] = c;
	}
	copyout(p->pagetable, va, str, len);
	return len;
}

uint64 sys_write(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_write(va, len);
	case FD_INODE:
		return inodewrite(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
}

uint64 sys_read(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_read(va, len);
	case FD_INODE:
		return inoderead(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
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

uint64 sys_gettimeofday(uint64 val, int _tz)
{
	struct proc *p = curr_proc();
	uint64 cycle = get_cycle();
	TimeVal t;
	t.sec = cycle / CPU_FREQ;
	t.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	copyout(p->pagetable, val, (char *)&t, sizeof(TimeVal));
	return 0;
}

uint64 sys_getpid()
{
	return curr_proc()->pid;
}

uint64 sys_getppid()
{
	struct proc *p = curr_proc();
	return p->parent == NULL ? IDLE_PID : p->parent->pid;
}

uint64 sys_clone()
{
	debugf("fork!");
	return fork();
}

static inline uint64 fetchaddr(pagetable_t pagetable, uint64 va)
{
	uint64 *addr = (uint64 *)useraddr(pagetable, va);
	return *addr;
}

uint64 sys_exec(uint64 path, uint64 uargv)
{
	struct proc *p = curr_proc();
	char name[MAX_STR_LEN];
	copyinstr(p->pagetable, name, path, MAX_STR_LEN);
	uint64 arg;
	static char strpool[MAX_ARG_NUM][MAX_STR_LEN];
	char *argv[MAX_ARG_NUM];
	int i;
	for (i = 0; uargv && (arg = fetchaddr(p->pagetable, uargv));
	     uargv += sizeof(char *), i++) {
		copyinstr(p->pagetable, (char *)strpool[i], arg, MAX_STR_LEN);
		argv[i] = (char *)strpool[i];
	}
	argv[i] = NULL;
	return exec(name, (char **)argv);
}

uint64 sys_wait(int pid, uint64 va)
{
	struct proc *p = curr_proc();
	int *code = (int *)useraddr(p->pagetable, va);
	return wait(pid, code);
}

uint64 sys_spawn(uint64 va)
{
	// TODO: your job is to complete the sys call
	struct proc *p = curr_proc();
	char name[200]; // need to translate since this is va
	copyinstr(p->pagetable, name, va, 200);
	
	return spawn(name);
}

uint64 sys_set_priority(long long prio)
{
	// TODO: your job is to complete the sys call
	if (prio < 2)
		return -1;

	struct proc *p = curr_proc();
	p->priority = prio;
	p->pass = BIG_STRIDE / prio;

	return prio;
}

uint64 sys_openat(uint64 va, uint64 omode, uint64 _flags)
{
	struct proc *p = curr_proc();
	char path[200];
	copyinstr(p->pagetable, path, va, 200);
	return fileopen(path, omode);
}

uint64 sys_close(int fd)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d", fd);
		return -1;
	}
	fileclose(f);
	p->files[fd] = 0;
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
requests anon pm of len bytes and map to vm starting at addr, with memory page
attribute of port
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

	if (port & ~0x7) // other bits of port must be 0
		return -1;
	if ((port & 0x7) == 0) // unreadble memory = useless
		return -1;
	if (len > (1ULL << 30)) // 1gib
		return -1;
	if (start % PGSIZE != 0) // if address not page aligned
		return -1;


	struct proc *p = curr_proc();
	uint64 round_len = PGROUNDUP(len); // rounding len up page bound

	// makes sure that [addr, sddr+len] page isnt mapped
	for(uint64 va = start; va < start + round_len; va += PGSIZE)
	{
		pte_t *pte = walk(p->pagetable, va, 0);
		if (pte !=0 && (*pte & PTE_V)) // in use/mapped
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
		// so it doesnt read another proc old mem
		memset(pa, 0, PGSIZE);
		// writing physical page to usr pt
		if (mappages(p->pagetable, va, PGSIZE, (uint64)pa, perm) !=0)
			return -1;
	}
	
	return 0;
}

uint64 sys_munmap(uint64 start, uint64 len){
	// unmap vm
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


int sys_fstat(int fd,uint64 stat){
	//TODO: your job is to complete the syscall
	if(fd < 0 || fd >= FD_BUFFER_SIZE)
		return -1;

	struct proc *p = curr_proc();
	struct file *f = p->files[fd];

	if (f == NULL)
		return -1;

	if (f->type != FD_INODE)
		return -1;

	Stat *kst = (Stat *)useraddr(p->pagetable, stat);
	if (kst == 0)
		return -1;

	ivalid(f->ip);
	kst->dev = f->ip->dev;
	kst->ino = f->ip->inum;
	kst->nlink = f->ip->nlink;
	kst->mode = (f->ip->type == T_DIR) ? DIR : FILE_STAT;

	return 0;
}

int sys_linkat(int olddirfd, uint64 oldpath, int newdirfd, uint64 newpath, uint64 flags){
	//TODO: your job is to complete the syscall
	struct proc *p = curr_proc();
	char old[200];
	char new[200];

	copyinstr(p->pagetable, old, oldpath, 200);
	copyinstr(p->pagetable, new, newpath, 200);

	if (strncmp(old, new, 200) == 0)
		return -1;

	struct inode *ip = namei(old);

	if (ip == 0)
		return -1;
	ivalid(ip);
	if (ip->type == T_DIR) {
		iput(ip);
		return -1;
	}

	struct inode *dp = root_dir();
	ivalid(dp);
	if (dirlink(dp, new, ip->inum) < 0){
		iput(dp);
		iput(ip);
		return -1;
	}

	ip->nlink++;
	iupdate(ip);

	iput(dp);
	iput(ip);
	return 0;

}

int sys_unlinkat(int dirfd, uint64 name, uint64 flags){
	//TODO: your job is to complete the syscall
	struct proc *p = curr_proc();
	char path[200];

	copyinstr(p->pagetable, path, name, 200);
	

	struct inode *ip = namei(path);

	if (ip == 0)
		return -1;

	ivalid(ip);

	
	struct inode *dp = root_dir();
	ivalid(dp);

	// removes directory entru
	if (dirunlink(dp, path) < 0){
		iput(dp);
		iput(ip);
		return -1;
	}

	// decrementing the link cnt
	ip->nlink--;
	iupdate(ip);

	iput(dp);
	iput(ip);
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

	struct proc *p = curr_proc();
	if (id>= 0 && id < MAX_SYSCALL_NUM) {
		p->syscall_times[id]++;
	}

	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_read:
		ret = sys_read(args[0], args[1], args[2]);
		break;
	case SYS_openat:
		ret = sys_openat(args[0], args[1], args[2]);
		break;
	case SYS_close:
		ret = sys_close(args[0]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday(args[0], args[1]);
		break;
	case SYS_getpid:
		ret = sys_getpid();
		break;
	case SYS_getppid:
		ret = sys_getppid();
		break;
	case SYS_clone: // SYS_fork
		ret = sys_clone();
		break;
	case SYS_execve:
		ret = sys_exec(args[0], args[1]);
		break;
	case SYS_wait4:
		ret = sys_wait(args[0], args[1]);
		break;
	case SYS_fstat:
	    ret = sys_fstat(args[0],args[1]);
		break;
	case SYS_linkat:
	    ret = sys_linkat(args[0],args[1],args[2],args[3],args[4]);
		break;
	case SYS_unlinkat:
	    ret = sys_unlinkat(args[0],args[1],args[2]);
		break;
	case SYS_spawn:
		ret = sys_spawn(args[0]);
		break;
	case SYS_setpriority:
		ret = sys_set_priority((long long)args[0]);
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
