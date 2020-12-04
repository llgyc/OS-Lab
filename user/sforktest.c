#include <inc/lib.h>

int shared_value = 0;

void
umain(int argc, char **argv)
{
	envid_t who;

	if ((who = sfork()) != 0) {
		cprintf("read shared_value=%d from %x\n", shared_value, sys_getenvid());
		shared_value = 1;
		ipc_send(who, 0, 0, 0);
	} else {
		uint32_t i = ipc_recv(&who, 0, 0);
		cprintf("read shared_value=%d from %x\n", shared_value, sys_getenvid());
	}

}
