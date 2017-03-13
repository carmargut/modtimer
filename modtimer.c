#include <linux/vmalloc.h>
#include <asm-generic/uaccess.h>
#include <asm-generic/errno.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <asm/smp.h>
#include "cbuffer.h"

#define MAX_SIZE_CBUFF 10
#define MAX_CHARS_KBUF 100

LIST_HEAD(my_list);
DEFINE_SPINLOCK(sp_cbuff);

static struct timer_list my_timer;
static struct work_struct my_work;
static struct proc_dir_entry *proc_entry_modtimer, *proc_entry_modconfig;
typedef struct {
	int data;
	struct list_head links;
} list_item_t;

static struct semaphore mtx;
static struct semaphore cola;

static char consumidor = false;

static int timer_period = HZ/2;
static int max_random = 100;
static int emergency_treshold = 80;

static cbuffer_t *cbuff;

static void clear_list(struct list_head *list);
static void fire_timer(unsigned long data);

/*
 * Modtimer Open Callback
 */
static int modtimer_open(struct inode *inode, struct file *file) {
	if (down_interruptible(&mtx))
		return -EINTR;
	
	if (consumidor) {
		up(&mtx);
		return -EPERM;
	}
	
	consumidor = true;
	up(&mtx);
	
	init_timer(&my_timer);
	my_timer.expires = jiffies + timer_period;
	my_timer.data = 0;
	my_timer.function = fire_timer;
	add_timer(&my_timer);
	
	try_module_get(THIS_MODULE);
	
	return 0;
}

/*
 * Modtimer Release Callback
 */
static int modtimer_release(struct inode *inode, struct file *file) {
	del_timer_sync(&my_timer);
	flush_scheduled_work();
	clear_cbuffer_t(cbuff);
	clear_list(&my_list);
	
	consumidor = false;
	
	module_put(THIS_MODULE);
	
	return 0;
}

/*
 * Modtimer Read Callback
 */
static ssize_t modtimer_read(struct file *filp, char __user *buff, size_t len, loff_t *off) {
	list_item_t *item, *tmp = NULL;
	char number[5];
	int nr_bytes = len, leidos = 0;
	
	if (down_interruptible(&mtx))
		return -EINTR;
	
	while (list_empty(&my_list)) {
		up(&mtx);
		
		if (down_interruptible(&cola))
			return -EINTR;
		
		if (down_interruptible(&mtx))
			return -EINTR;
	}
	
	up(&mtx);
	
	if (down_interruptible(&mtx))
		return -EINTR;
	
	list_for_each_entry_safe(item, tmp, &my_list, links) {
		nr_bytes = sprintf(number, "%d\n", item->data);
		list_del(&item->links);
		vfree(item);
		
		copy_to_user(buff, number, nr_bytes);
		
		buff += nr_bytes;
		leidos += nr_bytes;
	}
	
	up(&mtx);
	
	return leidos;
}

/*
 * Modtimer operations
 */
static const struct file_operations modtimer_operations = {
	.open = modtimer_open,
    .release = modtimer_release,
	.read = modtimer_read,
};

/*
 * Modconfig Write callback
 */
static ssize_t modconfig_write(struct file *filp, const char __user *buff, size_t len, loff_t *off) {
	char kbuff[MAX_CHARS_KBUF];
	int val;
	
	if ((*off) > 0)
		return 0;
	
	if (len > MAX_CHARS_KBUF)
		return -ENOSPC;
	
	if (copy_from_user(kbuff, buff, len))
		return -EFAULT;
	
	if (sscanf(kbuff, "timer_period_ms %d", &val)) {
		timer_period = val;
	} else if(sscanf(kbuff, "max_random %d", &val)) {
		max_random = val;
	} else if(sscanf(kbuff, "emergency_treshold %d", &val)) {
		emergency_treshold = val;
	} else {
		return -EINVAL;
	}
	
	(*off) += len;
	return len;
}

/*
 * Modconfig Read callback
 */
static ssize_t modconfig_read(struct file *filp,char __user *buff, size_t len, loff_t *off) {
	char kbuff[MAX_CHARS_KBUF];
	int nr_bytes;
	
	if ((*off) > 0)
		return 0;
	
	nr_bytes = sprintf(kbuff, "timer_period = %d\nmax_random = %d\nemergency_treshold = %d\n", timer_period, max_random, emergency_treshold);
	
	if(copy_to_user(buff, kbuff, nr_bytes))
		return -EFAULT;
	
	(*off) += len;
	return len;
}

/*
 * Modconfig operations
 */
static struct file_operations modconfig_operations = {
    .read = modconfig_read,
    .write = modconfig_write,
};

/*
 * Function invoked when timer expires
 */
static void fire_timer(unsigned long data) {
	unsigned int rand_num = (get_random_int() % max_random);
	int actual_cpu = smp_processor_id();
	unsigned long flags;
	printk(KERN_INFO "Generated number: %d\n", rand_num);
	
	spin_lock_irqsave(&sp_cbuff, flags);
	insert_cbuffer_t(cbuff, rand_num);
	if (size_cbuffer_t(cbuff)*100/MAX_SIZE_CBUFF >= emergency_treshold) {
		if (actual_cpu == 0) {
			schedule_work_on(1, &my_work);
		} else {
			schedule_work_on(0, &my_work);
		}
	}
	spin_unlock_irqrestore(&sp_cbuff, flags);
	
	mod_timer(&(my_timer), jiffies + timer_period);
}

/*
 * Work's handler function 
 */
static void copy_items_into_list(struct work_struct *my_work) {
	int items_cbuff, number, i;
	unsigned long flags;
	list_item_t *item;
	
	spin_lock_irqsave(&sp_cbuff, flags);
	items_cbuff = size_cbuffer_t(cbuff);
	spin_unlock_irqrestore(&sp_cbuff, flags);
	
	for(i = 0; i < items_cbuff; i++) {
		spin_lock_irqsave(&sp_cbuff, flags);
		number = remove_cbuffer_t(cbuff);
		spin_unlock_irqrestore(&sp_cbuff, flags);
		
		item = vmalloc(sizeof(list_item_t));
		item->data = number;
		list_add_tail(&item->links, &my_list);
	}
	
	printk(KERN_INFO "%d elements moved from the buffer to the list\n", items_cbuff);
	
	down(&mtx);
	up(&cola);
	up(&mtx);
}

/*
 * Clear the list
 */
static void clear_list(struct list_head *list) {
	list_item_t *tmp, *item = NULL;
	
	list_for_each_entry_safe(item, tmp, list, links) {
		list_del(&(item->links));
		vfree(item);
	}
}

int init_modtimer(void) {
	sema_init(&mtx, 1);
	sema_init(&cola, 0);
	
	proc_entry_modtimer = proc_create_data("modtimer", 0666, NULL, &modtimer_operations, NULL);
	if (!proc_entry_modtimer) {
		printk(KERN_INFO "modtimer: Couldn't create entry in /proc.\n");
		return -ENOMEM;
	}
	
	proc_entry_modconfig = proc_create_data("modconfig", 0666, NULL, &modconfig_operations, NULL);
	if (!proc_entry_modconfig) {
		printk(KERN_INFO "modconfig: Couldn't create entry in /proc.\n");
		remove_proc_entry("modtimer", NULL);
		return -ENOMEM;
	}
	
	cbuff = create_cbuffer_t(MAX_SIZE_CBUFF);
	
	if (!cbuff)
		return -ENOMEM;
	
	INIT_WORK(&my_work, copy_items_into_list);
	
	return 0;
}

void cleanup_modtimer(void) {
	flush_scheduled_work();
	
	destroy_cbuffer_t(cbuff);
	del_timer_sync(&my_timer);
	
	remove_proc_entry("modtimer", NULL);
	remove_proc_entry("modconfig", NULL);
}

module_init(init_modtimer);
module_exit(cleanup_modtimer);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("");
MODULE_AUTHOR("Carlos Martinez & Sergio Pino");
