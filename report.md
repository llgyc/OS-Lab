# Report for lab5, Yuchen Gu

完成的challenge如下：

```
Challenge! The block cache has no eviction policy. Once a block gets faulted in to it, it never gets removed and will remain in memory forevermore. Add eviction to the buffer cache. Using the PTE_A "accessed" bits in the page tables, which the hardware sets on any access to a page, you can track approximate usage of disk blocks without the need to modify every place in the code that accesses the disk map region. Be careful with dirty blocks.
```

即确立一种eviction机制，我们的简单策略是每当cache中个数超过`BLKCACHESIZE`时，找到第一个近期内没被访问过但存在的block将其驱逐出去，并且将其余的block的PTE_A清空。

只需在bc_pgfault中加入如下代码即可：

```
	// Optional Eviction	
	if (addr < diskaddr(max_reserved_number()))
		return;
	static int cached_num = 0;
	assert(cached_num >= 0);
	cached_num++;
	if (cached_num <= BLKCACHESIZE)
		return;
	uint32_t an_option = 0;
	uint32_t i;
	void *iaddr;
	for (i = max_reserved_number(); i < super->s_nblocks; i++) {
		iaddr = diskaddr(i);
		if (iaddr == addr)
			continue;
		if (!(uvpt[PGNUM(iaddr)] & PTE_P))
			continue;
		an_option = i;
		if (!(uvpt[PGNUM(iaddr)] & PTE_A))
			break;
	}
	if (i == super->s_nblocks)
		i = an_option;
	if (i == 0)
		panic("bc_pgfault: eviction failed");
	iaddr = diskaddr(i);
	if (uvpt[PGNUM(iaddr)] & PTE_D)
		flush_block(iaddr);
	if ((r = sys_page_unmap(0, iaddr)) < 0)
		panic("in bc_pgfault, sys_page_unmap: %e", r);
	cached_num--;
	
	// Clean PTE_A bits
	for (i = max_reserved_number(); i < super->s_nblocks; i++) {
		iaddr = diskaddr(i);
		if (!(uvpt[PGNUM(iaddr)] & PTE_A))
			continue;
		if (uvpt[PGNUM(iaddr)] & PTE_D)
			flush_block(iaddr);
		if ((r = sys_page_map(0, addr, 0, addr, uvpt[PGNUM(iaddr)] & PTE_SYSCALL)) < 0)
			panic("in bc_pgfault, sys_page_map: %e", r);
	}
	assert(cached_num <= BLKCACHESIZE);
```

并且在`fs/fs.h`中加入

```
#define BLKCACHESIZE	2
```

宏定义

我们这里设置为2是为了保证会发生eviction来检查正确性

再次跑了一遍`make grade`以后可以看到

```
internal FS tests [fs/test.c]: OK (2.2s) 
  fs i/o: OK 
  check_bc: OK 
  check_super: OK 
  check_bitmap: OK 
  alloc_block: OK 
  file_open: OK 
  file_get_block: OK 
  file_flush/file_truncate/file rewrite: OK 
testfile: OK (7.7s) 
  serve_open/file_stat/file_close: OK 
  file_read: OK 
  file_write: OK 
  file_read after file_write: OK 
  open: OK 
  large file: OK 
spawn via spawnhello: OK (1.7s) 
Protection I/O space: OK (1.8s) 
PTE_SHARE [testpteshare]: OK (1.8s) 
PTE_SHARE [testfdsharing]: OK (2.6s) 
start the shell [icode]: Timeout! OK (30.4s) 
testshell: OK (24.2s) 
primespipe: OK (10.2s) 
Score: 150/150
```

应该是没有什么问题的

## Environment Configuration

```
Hardware Environment:
Memory:         2GB
Processor:      Intel(R) Core(TM) i7-8750H CPU @ 2.20GHz
OS Type:        32 bit
Disk:           20GB

Software Environment:
OS:             Ubuntu 18.04.5 LTS
Gcc:            gcc (Ubuntu 7.5.0-3ubuntu1~18.04) 7.5.0
Make:           GNU Make 4.1
Gdb:            GNU gdb (Ubuntu 8.1-0ubuntu3.2) 8.1.0.20180409-git

```

## File system preliminaries

我们实现的文件系统比真实使用的文件系统简单得多，但提供基础功能：创建、读取、写入以及删除以目录结构组织的文件。我们的操作系统只有一个用户，因此提供的保护不足以使得能够让多个用户之间保证安全。我们的文件系统不支持hard link, symbolic links, time stamps或者特殊的设备文件。

### On-Disk File System Structure

大多数UNIX文件系统将磁盘空间分成两部分区域：inode区域和数据区域。UNIX文件系统给文件系统中的每个文件分配一个inode；一个文件的inode包含关于文件关键的元数据，例如它的stat属性，以及指向data region的指针。data region被分成更大（通常8KB或者更大）的data block，在其中文件系统存储文件数据以及目录元数据。目录项包括文件名和指向inode的指针，当文件系统中多个目录项指向一个文件的inode时，这个文件被称做hard-linked。因为我们的文件系统不支持hard link，所以我们不需要这种间接访问，因此可以进行一个方便的简化：我们的文件系统完全不使用inode，作为替代单纯地把一个文件（或子目录的）所有元信息存在描述该文件的目录项之中。

文件和目录逻辑上都时由一串data block组成，可能被分散在磁盘各处，就像是一个进程虚拟地址空间的页可能被分散在物理内存中的各处一样。文件系统环境隐藏block布局的细节，提供用来在文件任意offset读写一串数据的接口。文件系统环境在内部把处所有对目录的修改当作执行文件创建和删除的过程。我们的文件系统允许用户进程之间读取目录元数据（使用read），这意味着用户进程可以自己执行目录扫描操作（实现ls程序）而不是必须依赖文件系统特殊的调用。这个目录扫描方法的确定，以及大多数现代UNIX变种不推荐它的原因是这使得用户程序依赖目录元数据的格式，使得改变文件系统内部布局而不改变或者至少不重新编译用户程序变得非常困难。

#### Sectors and Blocks

大多数磁盘不能进行字节粒度的读写，而只能进行以扇区为单位的读写。JOS中，扇区是512字节。文件系统实际上以block为单位分配和使用磁盘存储。Sector size是磁盘硬件的属性，然而block size是操作系统使用磁盘的方式。文件系统的block size必须是所使用的磁盘sector size的整数倍。

UNIX xv6文件系统使用512字节大小的block size，和磁盘的sector size一致。然而，大多数现代文件系统使用一个更大的block size，因为存储空间变得相当便宜，所有使用更大粒度管理更加efficient。我们的文件系统使用4096byte大小的block size，与处理器页的大小保持一致。

#### Superblocks

文件系统通常在磁盘“好找的”位置保留若干磁盘block（例如最前面或者最后面）用来存放描述整个文件系统属性的元数据，例如block size，磁盘大小，任何需要用来找到根目录的元数据，文件系统上次挂载的实际，文件系统上次查错的时间，等等。这些特殊的块被称作superblock。

我们的文件系统会包含恰好一个superblock，它总是被放在磁盘的1号block处。它的布局由`inc/fs.h`中的struct Super定义，0号block通常保留来存放bootloader以及分区表，因此文件系统通常不使用第一个block。许多“真实”的文件系统维护多个superblock，复制于磁盘中多个间隔大的区域中，这样如果其中一个损坏或者磁盘在某一块中发送介质错误，其它superblock还可以被找到用来访问文件系统。

![Disk layout](https://pdos.csail.mit.edu/6.828/2018/labs/lab5/disk.png)

#### File Meta-data

描述一个文件的元数据的布局由`inc/fs.h`中的struct File来定义。这个元数据包括文件的名字、大小、类型（regular file或者目录）以及指向组成这个文件的块的指针。正如上面提到的，我们没有inode，所以这些数据是存在磁盘上一个目录项中的。与大多“真实”文件系统不同，我们为了简单使用这一个File结构来表示，因为它同时出现在磁盘和内存之中。

struct File中的f_direct数组含有用来存放文件前十个block的block number的空间，我们把这些称作文件的direct block。对于不超过10*4096 = 40KB的小文件来说，这意味着该文件所有block的block number可以很好的填进File结构中。然而，对于更大的文件来说，我们需要空间来存放文件剩余部分的block number。因此，对于任何大于40KB的文件，我们分配一个额外的disk block，被称作indirect block，来存放4096/4 = 1024个额外的block number。我们的文件系统因此支持至多1034个block，或者说刚刚比4M大一点的空间。为了支持更大的文件，“真实”文件系统通常支持两级以及三级indirect block。

![File structure](https://pdos.csail.mit.edu/6.828/2018/labs/lab5/file.png)

#### Directories versus Regular Files

在我们的文件系统中，一个File结构可以代表要么一个regular文件或者一个目录；这两种类型的“文件”通过File结构中的type域来区分，文件系统以同样的方式来管理regular file和directory-files，除了它完全不会解释与regular file相关联的data block中的内容，然而可以把directory-file中的内容以一串描述文件和子目录的File结构的方式解读。

我们文件系统中的superblock包含一个存放文件系统根目录元数据的File结构（struct Super中的root成员）。这个目录文件的内容是一系列File结构描述着文件系统根目录下的文件以及目录。任何根目录下的子目录也可能包含更多的File结构代表子-子目录，依次类推。

## The File System

这个lab中，我们不需要实现整个文件系统，只要负责实现从磁盘中读取block到block cache中以及把他们写回磁盘；分配磁盘块；将文件偏移映射到磁盘块上；并且实现IPC接口中的read，write以及open函数。

### Disk Access

我们操作系统中的文件系统环境需要能够访问磁盘，然而我们还没有在内核中实现任何磁盘访问的功能。与传统“monolithic”操作系统添加一个IDE磁盘驱动器以及必需的系统调用来允许文件系统访问它不同，我们这里把IDE磁盘驱动当作用户文件系统环境的一部分来实现。我们将仍然需要稍微修改内核，为了使得文件系统环境拥有相应的特权来自己实现磁盘访问。

只要我们依赖轮询，基于可编程IO的磁盘访问并且不使用磁盘中断，就很容易在用户空间中以这种方式实现磁盘访问。在用户态实现中断驱动的设备驱动器也是可能的（例如L3和L4内核会实现这个），但是这会更加困难，因为内核必须要即时反应设备中断并且将他们分配到正确的用户态进程中。

x86处理器使用EFLAGS寄存器中的IOPL位来决定是否保护模式下的代码被允许进行特殊的设备IO指令例如IN和OUT指令。因为所有我们需要访问的IDE磁盘寄存器都位于x86的IO空间而非内存映射的，因此将IO特权交给文件系统环境是我们唯一需要做的使得文件系统能访问这些寄存器。事实上，EFLAGS中的IOPL位给内核提供了一个”all-or-nothing"的方法来控制用户态代码是否可以访问IO空间。在我们的情况下，我们希望文件系统能够访问IO，但我们不希望其它进程能够访问IO空间。

#### Exercise 1

只需在env_create中添加如下内容：

```
	if (type == ENV_TYPE_FS) {
		e->env_tf.tf_eflags |= FL_IOPL_3;
	}
```

#### Question 1

**Do you have to do anything else to ensure that this I/O privilege setting is saved and restored properly when you subsequently switch from one environment to another? Why?**

不需要，因为当进程切换的时候会被自动保存在环境的Trapframe中，IO权限会随着eflags寄存器可以自动被保存和恢复。

------

注意到GNUmakefile文件设置QEMU把文件`obj/kern/kernel.img`当作盘0（也就是DOS/Windows下的C盘）的映像，并且把`obj/fs/fs.img`当作盘1（即D盘）的映像。在这个lab中，我们的文件系统只应该会用到盘1，盘0只被用来启动内核。

### The Block Cache

在我们的文件系统中，我们会利用处理器的虚拟内存系统实现一个简单的“buffer cache”（实际上就是一个block cache），相关的代码在`fs/bc.c`中。

我们的文件系统被限制为能处理3GB或者更小的磁盘空间。我们保留了一个很大的3GB固定区域，从0x10000000(DISKMAP)直到0xD0000000(DISKMAP+DISKMAX)作为磁盘的内存映射区域。例如，磁盘block 0被映射在虚拟地址0x10000000，磁盘block 1被映射在虚拟地址0x10001000处，以此类推。`fs/bc.c`中的diskaddr函数实现了这个从磁盘block号到虚拟地址的转换（同时进行了安全检查）。

因为我们的文件系统进程拥有与其它进程虚拟地址空间相对独立的，自己的虚拟地址空间。并且文件系统进程唯一需要做的事情是实现文件访问，将文件系统进程的地址空间如此保留是合理的。对32位机器上真实的文件系统来说这种方式就不太合适，因为现代的磁盘通常都比3GB大得多。这样一个buffer cache管理方式在64位机器上可能还是合理的。

当然，将整个磁盘读进内存的时间还是很长的，因此我们会实现一种demand paging的方式，这种方式我们只需要分配这部分磁盘映射的页，通过产生page fault的时候再来读取对于的block。这样我们可以假装整个磁盘都在内存中。

#### Exercise 2

bc_pgfault中需要填写的部分是分配一个物理页并从磁盘中读出对应的内容，flush_block是检测到如果dirty以后就将内容写回磁盘并清空dirty bit。

```
static void
bc_pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t blockno = ((uint32_t)addr - DISKMAP) / BLKSIZE;
	int r;

	if (addr < (void*)DISKMAP || addr >= (void*)(DISKMAP + DISKSIZE))
		panic("page fault in FS: eip %08x, va %08x, err %04x",
		      utf->utf_eip, addr, utf->utf_err);

	if (super && blockno >= super->s_nblocks)
		panic("reading non-existent block %08x\n", blockno);

	addr = ROUNDDOWN(addr, PGSIZE);
	if ((r = sys_page_alloc(0, addr, PTE_U|PTE_P|PTE_W)) < 0)
		panic("in bc_pgfault, sys_page_alloc: %e", r);
	if ((r = ide_read(blockno * BLKSECTS, addr, BLKSECTS)) < 0)
		panic("in bc_pgfault, ide_read: %e", r);

	if ((r = sys_page_map(0, addr, 0, addr, uvpt[PGNUM(addr)] & PTE_SYSCALL)) < 0)
		panic("in bc_pgfault, sys_page_map: %e", r);

	if (bitmap && block_is_free(blockno))
		panic("reading free block %08x\n", blockno);
}

void
flush_block(void *addr)
{
	uint32_t blockno = ((uint32_t)addr - DISKMAP) / BLKSIZE;

	if (addr < (void*)DISKMAP || addr >= (void*)(DISKMAP + DISKSIZE))
		panic("flush_block of bad va %08x", addr);

	addr = ROUNDDOWN(addr, PGSIZE);
	if (!va_is_mapped(addr) || !va_is_dirty(addr))
		return;
	int r;
	if ((r = ide_write(blockno * BLKSECTS, addr, BLKSECTS)) < 0)
		panic("in flush_block, ide_write: %e", r);
	if ((r = sys_page_map(0, addr, 0, addr, uvpt[PGNUM(addr)] & PTE_SYSCALL)) < 0)
		panic("in flush_block, sys_page_map: %e", r);
}
```

`fs/fs.c`中的fs_init函数是一个基础的例子用来展示如何使用block cache。再初始化block cache之后，它仅仅把指向磁盘映射区域的指针存放在全局变量super中。在这之后我们可以通过简单地通过super结构读取，就像是他们在内存中一样，我们的page fault处理程序会将他们从磁盘中按需读入。

#### Special Question 1

在`fs/bc.c`中有这样一个问题：

```
	// Check that the block we read was allocated. (exercise for
	// the reader: why do we do this *after* reading the block
	// in?)
	if (bitmap && block_is_free(blockno))
		panic("reading free block %08x\n", blockno);
```

原因是如果在读取之前判断，可能发送我们读的是属于bitmap的部分，然而block_is_free会访问bitmap从而导致又一次触发page fault再次进入该函数进入同样的判断陷入死循环，因此需要在读取以后再判断bitmap的内容。

### The Block Bitmap

在fs_init设置好bitmap指针之后，我们可以把bitmap当作一个位的压缩数组，每一位代表磁盘上的一个block。例如，可以看到，block_is_free就是来检测一个给定的block是否在bitmap中被标记为free。

#### Exercise 3

注意按照注释说的在修改bitmap后及时flush_block即可

```
int
alloc_block(void)
{
	uint32_t i;
	for (i = 0; i < super->s_nblocks; i++) {
		if (!block_is_free(i))
			continue;
		bitmap[i/32] &= ~(1<<(i%32));
		flush_block(&bitmap[i/32]);
		return i;
	}
	return -E_NO_DISK;
}
```

###  File Operations

我们已经提供了一系列函数在`fs/fs.c`中，它们可以用来实现为了解释和管理File结构，扫描并管理目录文件的表项以及从文件系统根目录走到某个绝对路径需要的基本功能。

#### Exercise 4

只要对照之前File结构对应完成即可

```
static int
file_block_walk(struct File *f, uint32_t filebno, uint32_t **ppdiskbno, bool alloc)
{
	if (filebno >= NDIRECT + NINDIRECT)
		return -E_INVAL;
	if (filebno < NDIRECT) {
		if (ppdiskbno)
			*ppdiskbno = &(f->f_direct[filebno]);
		return 0;
	}
	if (f->f_indirect) {
		*ppdiskbno = ((uint32_t *)diskaddr(f->f_indirect)) + (filebno - NDIRECT);
		return 0;
	} else if (alloc) {
		int r = alloc_block();
		if (r < 0)
			return -E_NO_DISK;
		f->f_indirect = r;
		memset(diskaddr(r), 0, BLKSIZE);
		*ppdiskbno = ((uint32_t *)diskaddr(f->f_indirect)) + (filebno - NDIRECT);
		return 0;
	} else
		return -E_NOT_FOUND;
}

int
file_get_block(struct File *f, uint32_t filebno, char **blk)
{
	int r;
	uint32_t *pdiskbno;
	if ((r = file_block_walk(f, filebno, &pdiskbno, 1)) < 0)
		return r;
	if (*pdiskbno) {
		if (blk)
			*blk = diskaddr(*pdiskbno);
		return 0;
	}
	if ((r = alloc_block()) < 0)
		return r;
	*pdiskbno = r;
	if (blk)
		*blk = diskaddr(r);
	return 0;
}
```

`file_block_walk` 以及 `file_get_block` 是文件系统的关键部分。例如， `file_read` 和 `file_write`不过是在 `file_get_block`基础上增加一些从分散的block和一个连续的buffer直接拷贝的工作。

### The file system interface

现在我们已经在文件系统环境内有了必要的功能，我们必须使得它能够被其它希望使用文件系统的进程访问到。因为其它进程不能直接调用文件系统环境中的函数，我们会利用建立在JOS的IPC机制上的RPC(Remote Procedure Call)抽象来将对文件系统的访问暴露出来。我们可以用下面的图示来表示对文件系统的调用（例如read）：

```
      Regular env           FS env
   +---------------+   +---------------+
   |      read     |   |   file_read   |
   |   (lib/fd.c)  |   |   (fs/fs.c)   |
...|.......|.......|...|.......^.......|...............
   |       v       |   |       |       | RPC mechanism
   |  devfile_read |   |  serve_read   |
   |  (lib/file.c) |   |  (fs/serv.c)  |
   |       |       |   |       ^       |
   |       v       |   |       |       |
   |     fsipc     |   |     serve     |
   |  (lib/file.c) |   |  (fs/serv.c)  |
   |       |       |   |       ^       |
   |       v       |   |       |       |
   |   ipc_send    |   |   ipc_recv    |
   |       |       |   |       ^       |
   +-------|-------+   +-------|-------+
           |                   |
           +-------------------+
```

所有点线以下的部分都是一个普通进程向文件系统进程发起一个读请求的机制。一开始，read可以在任何文件描述符上工作，并且简单将其分配到合适的设备读函数上，在这个例子中是devfile_read（我们可以有更多种设备类型，例如管道）。devfile_read为磁盘上文件专门实现来了read。在`lib/file.c`中，这个和其它的形如devfile_*的函数实现了客户端的文件系统操作，并且大致以同样的方式工作，把参数都打包到一个request结构中，调用fsipc去发送IPC请求，解包并且返回结果。fsipc函数仅仅处理通常的向服务器发送请求以及接受回复的事情。

文件系统服务器端的代码位于`fs/serv.c`中，它在`serve`函数中无限循环，接受通过IPC的请求，将其分配到对应的处理函数上，并且将结果通过IPC送回。在read的例子中，serve会将其分配到serve_read上，它会处理关于读请求的IPC细节，例如解开请求结构并最终调用file_read去真正执行文件读取。

回忆一下JOS的IPC机制使得一个环境发送一个32位数字并且共享一个可选的页。为了将一个请求从客户端发送至服务器端，我们使用32位数字作为请求类型（文件系统服务器RPC就像syscall一样编号）并且将参数存在共享页的一个union Fsipc中。在客户端，我们总是共享fsipcbuf所在的页；在服务器端，我们总是把请求页映射到fsreq处(0x0ffff000)。

服务器还需要把回复通过IPC送回去。我们利用32位数作为函数的返回值。对于大多数RPC来说，这是它们所需要返回的全部。FSREQ_READ和FSREQ_STAT还会返回数据，它们就简单地将其写在客户端发送请求所用的页中。没有必要在回复IPC中再次发送这个页，因为客户端一开始已经将其与文件系统服务器端共享了该页。另外，在回复中，FSREQ_OPEN会与客户端分享一个新的"Fd页"。我们不久后将会重新提到这个文件描述符页。

#### Exercise 5

仿照其它serve_*函数完成即可

```
int
serve_read(envid_t envid, union Fsipc *ipc)
{
	struct Fsreq_read *req = &ipc->read;
	struct Fsret_read *ret = &ipc->readRet;

	if (debug)
		cprintf("serve_read %08x %08x %08x\n", envid, req->req_fileid, req->req_n);

	struct OpenFile *o;
	int r;
	if  ((r = openfile_lookup(envid, req->req_fileid, &o)) < 0)
		return r;
		
	if ((r = file_read(o->o_file, ret->ret_buf, req->req_n, o->o_fd->fd_offset)) < 0)
		return r;
		
	o->o_fd->fd_offset += r;
	
	return r;
}
```

#### Exercise 6

serve_write函数基本上和serve_read函数完全相同

```
int
serve_write(envid_t envid, struct Fsreq_write *req)
{
	if (debug)
		cprintf("serve_write %08x %08x %08x\n", envid, req->req_fileid, req->req_n);

	struct OpenFile *o;
	int r;
	if ((r = openfile_lookup(envid, req->req_fileid, &o)) < 0)
		return r;
		
	if ((r = file_write(o->o_file, req->req_buf, req->req_n, o->o_fd->fd_offset)) < 0)
		return r;
		
	o->o_fd->fd_offset += r;
	
	return r;
}
```

devfile_write函数注意按照注释说的控制req_n不要超过req_buf的大小，否则容易内存泄漏

```
static ssize_t
devfile_write(struct Fd *fd, const void *buf, size_t n)
{
	fsipcbuf.write.req_fileid = fd->fd_file.id;
	fsipcbuf.write.req_n = MIN(PGSIZE - (sizeof(int) + sizeof(size_t)), n);
	memmove(fsipcbuf.write.req_buf, buf, fsipcbuf.write.req_n);
	return fsipc(FSREQ_WRITE, NULL);
}
```

## Spawning Processes

我们已经为你提供了spawn函数的代码，它创建一个新进程，从文件系统加载一个程序映像进去，然后让子进程开始运行该程序。父进程则继续独立于子进程运行。spawn函数实质上相当于UNIX中fork后立即在子进程中执行exec的效果。

我们实现了spawn而非UNIX风格的exec，因为从用户空间以"exokernel"风格实现spawn更简单，不需要内核特殊的帮助。想想看为了在用户空间实现exec需要做什么，弄明白为什么这更困难。

#### Exercise 7

按照注释完成即可，注意利用user_mem_assert检查用户传过来的参数

```
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	user_mem_assert(curenv, tf, sizeof(struct Trapframe), PTE_W);
	struct Env *e;
	int ret = envid2env(envid, &e, 1);
	if (ret < 0)
		return ret;
	tf->tf_cs = GD_UT | 0x3;
	tf->tf_eflags |= FL_IF;
	tf->tf_eflags &= ~FL_IOPL_MASK;
	e->env_tf = *tf;
	return 0;
}
```

### Sharing library state across fork and spawn

UNIX文件描述符是一个通用的概念，还包括管道，控制台IO等等。JOS中，每个这样的设备类型都有一个对应的`struct Dev`结构，其中有指针指向实现了对该种设备read/write等功能的函数。`lib/fd.c`在此基础上实现了通用的类UNIX的文件描述符接口。每个`struct Fd`都指出了它的设备类型，大多数`lib/fd.c`中的函数简单地将操作分配至`strcut Dev`中合适的函数。

`lib/fd.c`还维护了每个应用程序地址空间中的文件描述符表区域（从FDTABLE开始）。这个区域为一个应用程序能同时打开的每个文件描述符（至多MAXFD，目前值为32）保留了一页的地址空间。每个文件描述符也有一个可选的”数据页”在FILEDATA开始的区域中，设备如果想要的话可以使用。

我们想要通过fork和spawn依然可以共享文件描述符的状态，但是文件描述符状态被保存在用户空间的内存中。目前，在fork时，内存会被标记COW，所以状态会被复制一份而非共享一份。（这意味着进程不能seek不由它们自己打开的文件并且pipe不能跨越fork工作。）在spawn时，内存完全被丢弃，一点都不复制。（实际上，spawn出来的进程一开始没有打开的文件描述符。）

我们会改变fork使其知道内存的一部分是被用作库操作系统的，因此需要被共享。我们会设置页表项中一个没有使用的项（就像是我们在fork中用的PTE_COW一样）而不是在哪里硬编码一系列区域。

我们已经定义了一个新的PTE_SHARE位在`inc/lib.h`中。这一位是三个在Intel和AMD手册中被标记为“可被软件使用”的位之一。我们会建立一个惯例，如果一个页表项该位set了，那么在fork和spawn时PTE应该被从父亲直接拷贝至孩子。注意这和标记成COW不一样：就像是第一段中说的，我们想要保证分享对页的更新。

#### Exercise 8

duppage中加上

```
	if (uvpt[pn] & PTE_SHARE) {
		if ((r = sys_page_map(0, addr, envid, addr, perm)) < 0)
			panic("sys_page_map: %e", r);
	}
```

copy_shared_pages函数和fork中的遍历很像

```
static int
copy_shared_pages(envid_t child)
{
	uint8_t *addr;
	
	for (addr = 0; addr < (uint8_t *)UTOP; addr += PGSIZE)
		if ((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P)
			&& (uvpt[PGNUM(addr)] & PTE_U) && (uvpt[PGNUM(addr)] & PTE_SHARE))
			sys_page_map(0, addr, child, addr, uvpt[PGNUM(addr)] & PTE_SYSCALL);
	
	return 0;
}
```

## The keyboard interface

为了能够让shell工作，我们需要一个方式向其输入。QEMU已经一直在把我们写的显示在CGA显示和串行端口上，但至今为止我们只在kernel monitor中处理输入。在QEMU中，向graphical window的输入会作为键盘向JOS的输入，然而向console的的输入会被认为是串行端口的字符。`kern/console.c`已经包含了键盘和串行驱动，从lab1以来一直被kernel monitor使用，但现在你需要将这些和剩下的系统连在一起。

#### Exercise 9

```

```

我们为你在`lib/console.c`中实现了console的输入输出文件类型。`kbd_intr`和`serial_intr`在console文件类型耗尽buffer的时候将buffer用最新的读入填充（console文件类型默认是为了stdin/stdout使用的除非用户重定向它们）。

## The Shell

运行`make run-icode`或者`make run-icode-nox`。这会运行你的kernel并且启动`user/icode`。`icode`运行(exec)`init`，会设置console为文件描述符0和1（标准输入输出）。它会接着spawn`sh`即shell。你现在可以执行下面的指令：

```
	echo hello world | cat
	cat lorem |cat
	cat lorem |num
	cat lorem |num |num |num |num |num
	lsfd
```

注意到用户库例程cprintf直接向console输出，不使用文件描述符代码。这对debug非常好但不适合用来和其它程序pipe起来。为了输出到某个特定的文件描述符（例如，标准输出，1），使用`fprintf(1, "...", ...)`。`printf("...", ...)`是一个向文件描述符1输出的简写。`user/lsfd.c`中可以看到更多例子。

#### Exercise 10

只需要在shell的parse过程中把<的处理加入即可，仿照>的写法及注释的提示。

```
		case '<':	// Input redirection
			// Grab the filename from the argument list
			if (gettoken(0, &t) != 'w') {
				cprintf("syntax error: < not followed by word\n");
				exit();
			}
			if ((fd = open(t, O_RDONLY)) < 0) {
				cprintf("open %s for read: %e", t, fd);
				exit();
			}
			if (fd != 0) {
				dup(fd, 0);
				close(fd);
			}
			break;
```

最后结果如下：

```
internal FS tests [fs/test.c]: OK (1.3s) 
  fs i/o: OK 
  check_bc: OK 
  check_super: OK 
  check_bitmap: OK 
  alloc_block: OK 
  file_open: OK 
  file_get_block: OK 
  file_flush/file_truncate/file rewrite: OK 
testfile: OK (1.5s) 
  serve_open/file_stat/file_close: OK 
  file_read: OK 
  file_write: OK 
  file_read after file_write: OK 
  open: OK 
  large file: OK 
spawn via spawnhello: OK (1.1s) 
Protection I/O space: OK (1.0s) 
PTE_SHARE [testpteshare]: OK (2.2s) 
PTE_SHARE [testfdsharing]: OK (2.0s) 
start the shell [icode]: Timeout! OK (31.3s) 
testshell: OK (1.6s) 
    (Old jos.out.testshell failure log removed)
primespipe: OK (7.2s) 
Score: 150/150
```

## This Complete The Lab.

