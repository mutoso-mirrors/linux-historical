/*
 *  evdev.c  Version 0.1
 *
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  Event char devices, giving access to raw input device events.
 *
 *  Sponsored by SuSE
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * 
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#define EVDEV_MINOR_BASE	64
#define EVDEV_MINORS		32
#define EVDEV_BUFFER_SIZE	64

#include <linux/poll.h>
#include <linux/malloc.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>

struct evdev {
	int used;
	int minor;
	struct input_handle handle;
	wait_queue_head_t wait;
	devfs_handle_t devfs;
	struct evdev_list *list;
};

struct evdev_list {
	struct input_event buffer[EVDEV_BUFFER_SIZE];
	int head;
	int tail;
	struct fasync_struct *fasync;
	struct evdev *evdev;
	struct evdev_list *next;
};

static struct evdev *evdev_table[BITS_PER_LONG] = { NULL, /* ... */ };

static void evdev_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	struct evdev *evdev = handle->private;
	struct evdev_list *list = evdev->list;

	while (list) {

		get_fast_time(&list->buffer[list->head].time);
		list->buffer[list->head].type = type;
		list->buffer[list->head].code = code;
		list->buffer[list->head].value = value;
		list->head = (list->head + 1) & (EVDEV_BUFFER_SIZE - 1);
		
		if (list->fasync)
			kill_fasync(list->fasync, SIGIO, POLL_IN);

		list = list->next;
	}

	wake_up_interruptible(&evdev->wait);
}

static int evdev_fasync(int fd, struct file *file, int on)
{
	int retval;
	struct evdev_list *list = file->private_data;
	retval = fasync_helper(fd, file, on, &list->fasync);
	return retval < 0 ? retval : 0;
}

static int evdev_release(struct inode * inode, struct file * file)
{
	struct evdev_list *list = file->private_data;
	struct evdev_list **listptr = &list->evdev->list;

	evdev_fasync(-1, file, 0);

	while (*listptr && (*listptr != list))
		listptr = &((*listptr)->next);
	*listptr = (*listptr)->next;
	
	if (!--list->evdev->used) {
		input_unregister_minor(list->evdev->devfs);
		evdev_table[list->evdev->minor] = NULL;
		kfree(list->evdev);
	}

	kfree(list);

	MOD_DEC_USE_COUNT;
	return 0;
}

static int evdev_open(struct inode * inode, struct file * file)
{
	struct evdev_list *list;
	int i = MINOR(inode->i_rdev) - EVDEV_MINOR_BASE;

	if (i > EVDEV_MINORS || !evdev_table[i])
		return -ENODEV;

	if (!(list = kmalloc(sizeof(struct evdev_list), GFP_KERNEL)))
		return -ENOMEM;

	memset(list, 0, sizeof(struct evdev_list));

	list->evdev = evdev_table[i];
	list->next = evdev_table[i]->list;
	evdev_table[i]->list = list;

	file->private_data = list;

	list->evdev->used++;

	MOD_INC_USE_COUNT;
	return 0;
}

static ssize_t evdev_write(struct file * file, const char * buffer, size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static ssize_t evdev_read(struct file * file, char * buffer, size_t count, loff_t *ppos)
{
	DECLARE_WAITQUEUE(wait, current);
	struct evdev_list *list = file->private_data;
	int retval = 0;

	if (list->head == list->tail) {

		add_wait_queue(&list->evdev->wait, &wait);
		current->state = TASK_INTERRUPTIBLE;

		while (list->head == list->tail) {

			if (file->f_flags & O_NONBLOCK) {
				retval = -EAGAIN;
				break;
			}
			if (signal_pending(current)) {
				retval = -ERESTARTSYS;
				break;
			}

			schedule();
		}

		current->state = TASK_RUNNING;
		remove_wait_queue(&list->evdev->wait, &wait);
	}

	if (retval)
		return retval;

	while (list->head != list->tail && retval + sizeof(struct input_event) <= count) {
		if (copy_to_user(buffer + retval, list->buffer + list->tail,
			 sizeof(struct input_event))) return -EFAULT;
		list->tail = (list->tail + 1) & (EVDEV_BUFFER_SIZE - 1);
		retval += sizeof(struct input_event);
	}

	return retval;	
}

static unsigned int evdev_poll(struct file *file, poll_table *wait)
{
	struct evdev_list *list = file->private_data;
	poll_wait(file, &list->evdev->wait, wait);
	if (list->head != list->tail)
		return POLLIN | POLLRDNORM;
	return 0;
}

static int evdev_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct evdev_list *list = file->private_data;
	struct evdev *evdev = list->evdev;
	struct input_dev *dev = evdev->handle.dev;

	switch (cmd) {

		case EVIOCGVERSION:
			return put_user(EV_VERSION, (__u32 *) arg);
		case EVIOCGID:
			return copy_to_user(&dev->id, (void *) arg,
						sizeof(struct input_id)) ? -EFAULT : 0;
		default:

			if (_IOC_TYPE(cmd) != 'E' || _IOC_DIR(cmd) != _IOC_READ)
				return -EINVAL;

			if ((_IOC_NR(cmd) & ~EV_MAX) == _IOC_NR(EVIOCGBIT(0,0))) {

				long *bits = NULL;
				int len = 0;

				switch (_IOC_NR(cmd) & EV_MAX) {
					case      0: bits = dev->evbit;  len = EV_MAX;  break;
					case EV_KEY: bits = dev->keybit; len = KEY_MAX; break;
					case EV_REL: bits = dev->relbit; len = REL_MAX; break;
					case EV_ABS: bits = dev->absbit; len = ABS_MAX; break;
					case EV_LED: bits = dev->ledbit; len = LED_MAX; break;
					case EV_SND: bits = dev->sndbit; len = SND_MAX; break;
					default: return -EINVAL;
				}
				len = NBITS(len) * sizeof(long);
				if (len > _IOC_SIZE(cmd)) len = _IOC_SIZE(cmd);
				return copy_to_user((void *) arg, bits, len) ? -EFAULT : len;
			}

			if (_IOC_NR(cmd) == _IOC_NR(EVIOCGNAME(0))) {
				int len = strlen(dev->name) + 1;
				if (len > _IOC_SIZE(cmd)) len = _IOC_SIZE(cmd);
				return copy_to_user((char *) arg, dev->name, len) ? -EFAULT : len;
			}
	}
	return -EINVAL;
}

static struct file_operations evdev_fops = {
	read:		evdev_read,
	write:		evdev_write,
	poll:		evdev_poll,
	open:		evdev_open,
	release:	evdev_release,
	ioctl:		evdev_ioctl,
	fasync:		evdev_fasync,
};

static int evdev_connect(struct input_handler *handler, struct input_dev *dev)
{
	struct evdev *evdev;
	int minor;

	for (minor = 0; minor < EVDEV_MINORS && evdev_table[minor]; minor++);
	if (evdev_table[minor]) {
		printk(KERN_ERR "evdev: no more free evdev devices\n");
		return -1;
	}

	if (!(evdev = kmalloc(sizeof(struct evdev), GFP_KERNEL)))
		return -1;
	memset(evdev, 0, sizeof(struct evdev));

	init_waitqueue_head(&evdev->wait);

	evdev->minor = minor;
	evdev_table[minor] = evdev;

	evdev->handle.dev = dev;
	evdev->handle.handler = handler;
	evdev->handle.private = evdev;

	evdev->used = 1;

	input_open_device(&evdev->handle);
	evdev->devfs = input_register_minor("event%d", minor, EVDEV_MINOR_BASE);

	printk("event%d: Event device for input%d\n", minor, dev->number);

	return 0;
}

static void evdev_disconnect(struct input_handle *handle)
{
	struct evdev *evdev = handle->private;

	input_close_device(handle);

	if (!--evdev->used) {
		input_unregister_minor(evdev->devfs);
		evdev_table[evdev->minor] = NULL;
		kfree(evdev);
	}
}
	
static struct input_handler evdev_handler = {
	event:		evdev_event,
	connect:	evdev_connect,
	disconnect:	evdev_disconnect,
	fops:		&evdev_fops,
	minor:		EVDEV_MINOR_BASE,
};

static int __init evdev_init(void)
{
	input_register_handler(&evdev_handler);
	return 0;
}

static void __exit evdev_exit(void)
{
	input_unregister_handler(&evdev_handler);
}

module_init(evdev_init);
module_exit(evdev_exit);
