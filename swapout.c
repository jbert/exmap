/*
 * Create a proc file. If a pid is written to the file (in one
 * write(), in decimal, trailing newline permitted) then this module attempts
 * to swapout the pages belonging to that pid.
 *
 * (c) John Berthels 2005 <jjberthels@gmail.com>. See COPYING for license
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/rmap.h>
#include <linux/pagemap.h>
#include <linux/version.h>
#include <linux/vmalloc.h>

#define PROCFS_NAME "swapout"
MODULE_LICENSE ("GPL");
MODULE_AUTHOR ("John Berthels <jjberthels@gmail.com>");
MODULE_DESCRIPTION ("Allow swapping out of a process");


static int swap_out_pid(pid_t pid)
{
	printk(KERN_INFO "/proc/%s: swapping out %d\n", PROCFS_NAME, pid);
	/* Doesn't actually do anything.
	 * It looks like we need to loop along the pages, a pagevec-size
	 * at a time, forcing writeback of any dirty pages and then calling
	 * pagevec_release().
	 *
	 * If you're reading this and know anything about this area, please
	 * feel free to educate me :-)
	 */
	return 0;
}


/* Copied (and modded) from user_atoi in arch/frv somewhere */
static unsigned long user_atoul (const char __user * ubuf, size_t len)
{
	char buf[16];
	unsigned long ret;

	if (len > 15) {
		printk (KERN_ALERT "/proc/%s : user_atoul bad length %d\n",
			PROCFS_NAME, len);
		return -EINVAL;
	}

	if (copy_from_user (buf, ubuf, len)) {
		printk (KERN_ALERT "/proc/%s : user_atoul cfu failed\n",
			PROCFS_NAME);
		return -EFAULT;
	}

	buf[len] = 0;

/*	printk(KERN_INFO "user_atoul: examining buffer [%s]\n", buf); */

	ret = simple_strtoul (buf, NULL, 0);
	return ret;
}


/*
 * Writes to the procfile should be of the form:
 * pid:0xdeadbeef\n
 * where deadbeef is the hex addr of the vma to examine
 * and pid is the (decimal) pid of the process to examine
 */
static int procfile_write (struct file *file,
			   const char __user *buffer,
			   unsigned long count,
			   void *data)
{
	pid_t pid;
	int errcode = -EINVAL;

	pid = user_atoul(buffer, count);
	if (pid <= 0) {
		printk (KERN_ALERT
			"/proc/%s:can't parse buffer to read pid\n",
			PROCFS_NAME);
		return errcode;
	}

	if ((errcode = swap_out_pid(pid)) < 0) {
		printk (KERN_ALERT
			"/proc/%s: failed save info for pid %d [%d]\n",
			PROCFS_NAME, pid, errcode);
		return errcode;
	}
	
	return count;
}


int init_module ()
{
	struct proc_dir_entry *swapout_proc_file;
	printk (KERN_INFO "/proc/%s: insert\n", PROCFS_NAME);
	
	swapout_proc_file = create_proc_entry (PROCFS_NAME,
							0644,
							NULL);

	if (swapout_proc_file == NULL) {
		remove_proc_entry (PROCFS_NAME, &proc_root);
		printk (KERN_ALERT "/proc/%s: could not initialize\n",
			PROCFS_NAME);
		return -ENOMEM;
	}
	
	swapout_proc_file->write_proc = procfile_write;
	swapout_proc_file->owner = THIS_MODULE;
	
	/*     swapout_proc_file->mode         = S_IFREG | S_IRUGO; */
	/* TODO - this is quite probably a security problem */
	swapout_proc_file->mode = 0666;
	
	swapout_proc_file->uid = 0;
	swapout_proc_file->gid = 0;
	swapout_proc_file->size = 0;

	return 0;
}

void cleanup_module ()
{
	printk (KERN_INFO "/proc/%s: remove\n", PROCFS_NAME);
	remove_proc_entry (PROCFS_NAME, &proc_root);
}


