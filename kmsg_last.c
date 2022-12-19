// **************************************************************************
//
// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2018-2022 Tomas Paukrt
//
// /proc/kmsg.last support
//
// **************************************************************************

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/console.h>
#include <linux/proc_fs.h>
#include <linux/version.h>

// **************************************************************************

// buffer size
#define BUF_SIZE	8192

// magic number for checking buffer content validity
#define BUF_MAGIC	0x4B4D5347

// **************************************************************************

// ring buffer for storing current kernel messages
static struct {
	char		data[BUF_SIZE];
	char		*head;
	char		*tail;
	int		magic;
} *rbuf;

// linear buffer for storing previous kernel messages
static struct {
	char		data[BUF_SIZE];
	int		count;
} *lbuf;

// **************************************************************************
// write a message to the ring buffer
static void ring_buffer_write(struct console *console,
                              const char *str, unsigned int len)
{
	unsigned long	flags;
	int		found;
	char		*start;
	char		*stop;
	char		*head;
	char		*tail;

	// disable interrupts
	local_irq_save(flags);

	// initialize local variables
	start = rbuf->data;
	stop  = rbuf->data + BUF_SIZE;
	head  = rbuf->head;
	tail  = rbuf->tail;

	// write a message to the ring buffer
	while (len--) {
		*head++ = *str++;
		if (head == stop)
			head = start;
		if (head == tail) {
			found = 0;
			while (!found) {
				found = *tail++ == '\n';
				if (tail == stop)
					tail = start;
				if (tail == head)
					break;
			}
		}
	}

	// update pointers
	rbuf->head = head;
	rbuf->tail = tail;

	// restore interrupts
	local_irq_restore(flags);
}

// **************************************************************************
// copy content of the ring buffer to the linear buffer
static void ring_buffer_copy(void)
{
	char		*start;
	char		*stop;
	char		*head;
	char		*tail;
	char		*dst;
	char		data;

	// exit if content of the ring buffer is not valid
	if (rbuf->magic != BUF_MAGIC ||
	    (uintptr_t)(rbuf->head - rbuf->data) >= BUF_SIZE ||
	    (uintptr_t)(rbuf->tail - rbuf->data) >= BUF_SIZE) {
		lbuf->count = 0;
		return;
	}

	// initialize local variables
	start = rbuf->data;
	stop  = rbuf->data + BUF_SIZE;
	head  = rbuf->head;
	tail  = rbuf->tail;
	dst   = lbuf->data;

	// calculate the number of bytes in the ring buffer
	lbuf->count = (tail > head ? BUF_SIZE : 0) + head - tail;

	// copy data from the ring buffer to the linear buffer
	while (head != tail) {
		data = *tail++;
		if (tail == stop)
			tail = start;
		if (data & 0x80)
			data &= 0x7F;
		if (data < 0x20 && data != '\n')
			data |= 0x20;
		*dst++ = data;
	}
}

// **************************************************************************
// reinitialize the ring buffer
static void ring_buffer_init(void)
{
	// initialize pointers
	rbuf->head = rbuf->data;
	rbuf->tail = rbuf->data;

	// validate content of the ring buffer
	rbuf->magic = BUF_MAGIC;
}

// **************************************************************************
// read data from the linear buffer
static ssize_t linear_buffer_read(struct file *file, char __user *buf,
                                  size_t count, loff_t *ppos)
{
	// check and fix position
	if (*ppos >= lbuf->count)
		*ppos = lbuf->count;

	// check and fix count
	if (*ppos + count >= lbuf->count)
		count = lbuf->count - *ppos;

	// copy data from the linear buffer
	if (copy_to_user(buf, lbuf->data + *ppos, count))
		return -EFAULT;

	// update position
	*ppos += count;

	// return the number of read bytes
	return count;
}

// **************************************************************************
// write data to the linear buffer
static ssize_t linear_buffer_write(struct file *file, const char __user *buf,
                                   size_t count, loff_t *ppos)
{
	// erase the linear buffer
	lbuf->count = 0;

	// return the number of written bytes
	return count;
}

// **************************************************************************

// virtual console for recording kernel messages
static struct console ring_buffer_console = {
	.name  = "ram",
	.write = ring_buffer_write,
	.flags = CON_ENABLED | CON_ANYTIME,
	.index = -1,
};

// file operations for accesssing stored kernel messages
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,6,0)
static const struct file_operations linear_buffer_fops = {
	.read   = linear_buffer_read,
	.write  = linear_buffer_write,
	.llseek = generic_file_llseek,
};
#else
static const struct proc_ops linear_buffer_fops = {
	.proc_read  = linear_buffer_read,
	.proc_write = linear_buffer_write,
	.proc_lseek = generic_file_llseek,
};
#endif

// **************************************************************************
// module early init function
static int __init proc_kmsg_last_early_init(void)
{
	struct page	**pages;
	int		count;
	int		i;

	// calculate the number of pages for the ring buffer
	count = DIV_ROUND_UP(sizeof(*rbuf), PAGE_SIZE);

	// allocate memory for an array of page pointers
	if (!(pages = kmalloc(count * sizeof(pages[0]), GFP_KERNEL)))
		return -ENOMEM;

	// allocate memory for the ring buffer
	for (i = 0; i < count; i++)
		if (!(pages[i] = alloc_page(GFP_KERNEL)))
			return -ENOMEM;

	// map the allocated memory into virtually contiguous space
	if (!(rbuf = vmap(pages, count, VM_MAP,
	                  pgprot_writecombine(PAGE_KERNEL))))
		return -ENOMEM;

	// allocate memory for the linear buffer
	if (!(lbuf = kmalloc(sizeof(*lbuf), GFP_KERNEL)))
		return -ENOMEM;

	// success
	return 0;
}

// **************************************************************************
// module late init function
static int __init proc_kmsg_last_late_init(void)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
	struct proc_dir_entry *entry;
#endif

	// exit if early init failed
	if (!lbuf || !rbuf)
		return -ENOMEM;

	// copy content of the ring buffer to the linear buffer
	ring_buffer_copy();

	// reinitialize the ring buffer
	ring_buffer_init();

	// create the entry in the /proc directory
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
	if ((entry = create_proc_entry("kmsg.last", S_IRUSR, NULL))) {
		entry->proc_fops = &linear_buffer_fops;
	}
#else
	proc_create("kmsg.last", S_IRUSR, NULL, &linear_buffer_fops);
#endif

	// register the virtual console
	register_console(&ring_buffer_console);

	// success
	return 0;
}

// **************************************************************************

early_initcall(proc_kmsg_last_early_init);
late_initcall(proc_kmsg_last_late_init);
