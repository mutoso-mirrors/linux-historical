/* i2c-core.c - a device driver for the iic-bus interface		     */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 1995-99 Simon G. Vogl

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.		     */
/* ------------------------------------------------------------------------- */
#define RCSID "$Id: i2c-core.c,v 1.42 1999/11/30 20:06:42 frodo Exp $"
/* ------------------------------------------------------------------------- */

/* With some changes from Ky�sti M�lkki <kmalkki@cc.hut.fi>.
   All SMBus-related things are written by Frodo Looijaard <frodol@dds.nl> */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/proc_fs.h>
#include <linux/config.h>

#include <linux/i2c.h>

/* ----- compatibility stuff ----------------------------------------------- */

/* 2.0.0 kernel compatibility */
#if LINUX_VERSION_CODE < 0x020100
#define MODULE_AUTHOR(noone)
#define MODULE_DESCRIPTION(none)
#define MODULE_PARM(no,param)
#define MODULE_PARM_DESC(no,description)
#define EXPORT_SYMBOL(noexport)
#define EXPORT_NO_SYMBOLS
#endif

#include <linux/version.h>
#ifndef KERNEL_VERSION
#define KERNEL_VERSION(a,b,c) (((a) << 16) | ((b) << 8) | (c))
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,53)
#include <linux/init.h>
#else
#define __init 
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
#define init_MUTEX(s) do { *(s) = MUTEX; } while(0)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,1,4))
#define copy_from_user memcpy_fromfs
#define copy_to_user memcpy_tofs
#else
#include <asm/uaccess.h>
#endif

/* ----- global defines ---------------------------------------------------- */

/* exclusive access to the bus */
#define I2C_LOCK(adap) down(&adap->lock)
#define I2C_UNLOCK(adap) up(&adap->lock) 

#define ADAP_LOCK()	down(&adap_lock)
#define ADAP_UNLOCK()	up(&adap_lock)

#define DRV_LOCK()	down(&driver_lock)
#define DRV_UNLOCK()	up(&driver_lock)

#define DEB(x) if (i2c_debug>=1) x;
#define DEB2(x) if (i2c_debug>=2) x;

/* ----- global variables -------------------------------------------------- */

/**** lock for writing to global variables: the adapter & driver list */
struct semaphore adap_lock;  
struct semaphore driver_lock;  

/**** adapter list */
static struct i2c_adapter *adapters[I2C_ADAP_MAX];
static int adap_count;

/**** drivers list */
static struct i2c_driver *drivers[I2C_DRIVER_MAX];
static int driver_count;

/**** debug level */
static int i2c_debug=1;
static void i2c_dummy_adapter(struct i2c_adapter *adapter);
static void i2c_dummy_client(struct i2c_client *client);

/* ---------------------------------------------------
 * /proc entry declarations
 *----------------------------------------------------
 */

/* Note that quite some things changed within the 2.1 kernel series.
   Some things below are somewhat difficult to read because of this. */

#ifdef CONFIG_PROC_FS

static int i2cproc_init(void);
static int i2cproc_cleanup(void);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,58)) && \
    (LINUX_VERSION_CODE <= KERNEL_VERSION(2,3,27))
static void monitor_bus_i2c(struct inode *inode, int fill);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,58)) */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,29))

static ssize_t i2cproc_bus_read(struct file * file, char * buf,size_t count, 
                                loff_t *ppos);
static int read_bus_i2c(char *buf, char **start, off_t offset, int len,
                           int *eof , void *private);

#else /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,1,29)) */

static int i2cproc_bus_read(struct inode * inode, struct file * file,
                            char * buf, int count);
static int read_bus_i2c(char *buf, char **start, off_t offset, int len,
                        int unused);

static struct proc_dir_entry proc_bus_dir =
  {
    /* low_ino */       0,     /* Set by proc_register_dynamic */
    /* namelen */       3, 
    /* name */          "bus",
    /* mode */          S_IRUGO | S_IXUGO | S_IFDIR,
    /* nlink */         2,     /* Corrected by proc_register[_dynamic] */
    /* uid */           0,
    /* gid */           0,
    /* size */          0,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,0,36))
    /* ops */           &proc_dir_inode_operations, 
#endif
  };

static struct proc_dir_entry proc_bus_i2c_dir =
  {
    /* low_ino */       0,     /* Set by proc_register_dynamic */
    /* namelen */       3, 
    /* name */          "i2c",
    /* mode */          S_IRUGO | S_IFREG,
    /* nlink */         1,     
    /* uid */           0,
    /* gid */           0,
    /* size */          0,
    /* ops */           NULL,
    /* get_info */      &read_bus_i2c
  };

#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,29)) */

/* To implement the dynamic /proc/bus/i2c-? files, we need our own 
   implementation of the read hook */
static struct file_operations i2cproc_operations = {
        NULL,
        i2cproc_bus_read,
};

static struct inode_operations i2cproc_inode_operations = {
        &i2cproc_operations
};

static int i2cproc_initialized = 0;

#else /* undef CONFIG_PROC_FS */

#define i2cproc_init()
#define i2cproc_cleanup()

#endif /* CONFIG_PROC_FS */


/* ---------------------------------------------------    
 * registering functions 
 * --------------------------------------------------- 
 */

/* -----
 * i2c_add_adapter is called from within the algorithm layer,
 * when a new hw adapter registers. A new device is register to be
 * available for clients.
 */
int i2c_add_adapter(struct i2c_adapter *adap)
{
	int i,j;

	ADAP_LOCK();
	for (i = 0; i < I2C_ADAP_MAX; i++)
		if (NULL == adapters[i])
			break;
	if (I2C_ADAP_MAX == i) {
		printk(KERN_WARNING 
		       " i2c-core.o: register_adapter(%s) - enlarge I2C_ADAP_MAX.\n",
			adap->name);
		ADAP_UNLOCK();
		return -ENOMEM;
	}

	adapters[i] = adap;
	adap_count++;
	ADAP_UNLOCK();
	
	/* init data types */
	init_MUTEX(&adap->lock);

	i2c_dummy_adapter(adap);  /* actually i2c_dummy->add_adapter */
#ifdef CONFIG_PROC_FS

	if (i2cproc_initialized) {
		char name[8];
		struct proc_dir_entry *proc_entry;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,1,29))
		int res;
#endif

		sprintf(name,"i2c-%d", i);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,29))
		proc_entry = create_proc_entry(name,0,proc_bus);
		if (! proc_entry) {
			printk("i2c-core.o: Could not create /proc/bus/%s\n",
                               name);
			return -ENOENT;
  		}
		proc_entry->ops = &i2cproc_inode_operations;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,27))
		proc_entry->owner = THIS_MODULE;
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,58))
		proc_entry->fill_inode = &monitor_bus_i2c;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,58)) */
#else /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,1,29)) */
		adap->proc_entry = NULL;
		if (!(proc_entry = kmalloc(sizeof(struct proc_dir_entry)+
                                           strlen(name)+1, GFP_KERNEL))) {
			printk("i2c-core.o: Out of memory!\n");
			return -ENOMEM;
		}
		memset(proc_entry,0,sizeof(struct proc_dir_entry));
		proc_entry->namelen = strlen(name);
		proc_entry->name = (char *) (proc_entry + 1);
		proc_entry->mode = S_IRUGO | S_IFREG;
		proc_entry->nlink = 1;
		proc_entry->ops = &i2cproc_inode_operations;
	
		/* Nasty stuff to keep GCC satisfied */
		{ 
			char *procname;
			(const char *) procname  = proc_entry->name;
			strcpy (procname,name);
		}
	
		if ((res = proc_register_dynamic(&proc_bus_dir, proc_entry))) {
			printk("i2c-core.o: Could not create %s.\n",name);
			kfree(proc_entry);
			return res;
		}
	
		adap->proc_entry = proc_entry;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,29)) */

		adap->inode = proc_entry->low_ino;
	}

#endif /* def CONFIG_PROC_FS */

	/* inform drivers of new adapters */
	DRV_LOCK();	
	for (j=0;j<I2C_DRIVER_MAX;j++)
		if (drivers[j]!=NULL && drivers[j]->flags&I2C_DF_NOTIFY)
			drivers[j]->attach_adapter(adap);
	DRV_UNLOCK();
	
	DEB(printk("i2c-core.o: adapter %s registered as adapter %d.\n",adap->name,i));

	return 0;	
}


int i2c_del_adapter(struct i2c_adapter *adap)
{
	int i,j;
	ADAP_LOCK();
	for (i = 0; i < I2C_ADAP_MAX; i++)
		if (adap == adapters[i])
			break;
	if (I2C_ADAP_MAX == i) {
		printk( " i2c-core.o: unregister_adapter adap [%s] not found.\n",
			adap->name);
		ADAP_UNLOCK();
		return -ENODEV;
	}
	
	i2c_dummy_adapter(adap);  /* actually i2c_dummy->del_adapter */
#ifdef CONFIG_PROC_FS
	if (i2cproc_initialized) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,29))
		char name[8];
		sprintf(name,"i2c-%d", i);
		remove_proc_entry(name,proc_bus);
#else /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,1,29)) */
		int res;
		if (adapters[i]->proc_entry) {
			if ((res = proc_unregister(&proc_bus_dir,
			                  adapters[i]->proc_entry->low_ino))) {
				printk("i2c-core.o: Deregistration of /proc "
				       "entry failed\n");
				ADAP_UNLOCK();
				return res;
			}
			kfree(adapters[i]->proc_entry);
		}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,29)) */
	}
#endif /* def CONFIG_PROC_FS */

	/* detach any active clients */
	for (j=0;j<I2C_CLIENT_MAX;j++) {
		struct i2c_client *client = adap->clients[j];
		if ( (client!=NULL) 
		     /* && (client->driver->flags & I2C_DF_NOTIFY) */ )
			/* detaching devices is unconditional of the set notify
			 * flag, as _all_ clients that reside on the adapter
			 * must be deleted, as this would cause invalid states.
			 */
			client->driver->detach_client(client);
			/* i2c_detach_client(client); --- frodo */
	}
	/* all done, now unregister */
	adapters[i] = NULL;
	adap_count--;
	
	ADAP_UNLOCK();	
	DEB(printk("i2c-core.o: adapter unregistered: %s\n",adap->name));
	return 0;    
}


/* -----
 * What follows is the "upwards" interface: commands for talking to clients,
 * which implement the functions to access the physical information of the
 * chips.
 */

int i2c_add_driver(struct i2c_driver *driver)
{
	int i,j;
	DRV_LOCK();
	for (i = 0; i < I2C_DRIVER_MAX; i++)
		if (NULL == drivers[i])
			break;
	if (I2C_DRIVER_MAX == i) {
		printk(KERN_WARNING 
		       " i2c-core.o: register_driver(%s) - enlarge I2C_DRIVER_MAX.\n",
			driver->name);
		DRV_UNLOCK();
		return -ENOMEM;
	}

	drivers[i] = driver;
	driver_count++;
	
	DRV_UNLOCK();	/* driver was successfully added */
	
	DEB(printk("i2c-core.o: driver %s registered.\n",driver->name));
	
	/* Notify all existing adapters and clients to dummy driver */
	ADAP_LOCK();
	if (driver->flags&I2C_DF_DUMMY) { 
		for (i=0; i<I2C_ADAP_MAX; i++) {
			if (adapters[i]) {
				driver->attach_adapter(adapters[i]);
		 		for (j=0; j<I2C_CLIENT_MAX; j++)
		 			if (adapters[i]->clients[j])
	 				driver->detach_client(adapters[i]->clients[j]);
			}
		}
		ADAP_UNLOCK();
		return 0;
	}

	/* now look for instances of driver on our adapters
	 */
	if ( driver->flags&I2C_DF_NOTIFY ) {
		for (i=0;i<I2C_ADAP_MAX;i++)
			if (adapters[i]!=NULL)
				driver->attach_adapter(adapters[i]);
	}
	ADAP_UNLOCK();
	return 0;
}

int i2c_del_driver(struct i2c_driver *driver)
{
	int i,j,k;

	DRV_LOCK();
	for (i = 0; i < I2C_DRIVER_MAX; i++)
		if (driver == drivers[i])
			break;
	if (I2C_DRIVER_MAX == i) {
		printk(KERN_WARNING " i2c-core.o: unregister_driver: [%s] not found\n",
			driver->name);
		DRV_UNLOCK();
		return -ENODEV;
	}
	/* Have a look at each adapter, if clients of this driver are still
	 * attached. If so, detach them to be able to kill the driver 
         * afterwards.
	 */
	DEB2(printk("i2c-core.o: unregister_driver - looking for clients.\n"));
	/* removing clients does not depend on the notify flag, else 
	 * invalid operation might (will!) result, when using stale client
	 * pointers.
	 */
	ADAP_LOCK(); /* should be moved inside the if statement... */
	if ((driver->flags&I2C_DF_DUMMY)==0) 
	for (k=0;k<I2C_ADAP_MAX;k++) {
		struct i2c_adapter *adap = adapters[k];
		if (adap == NULL) /* skip empty entries. */
			continue;
		DEB2(printk("i2c-core.o: examining adapter %s:\n",adap->name));
		for (j=0;j<I2C_CLIENT_MAX;j++) { 
			struct i2c_client *client = adap->clients[j];
			if (client != NULL && client->driver == driver) {
				DEB2(printk("i2c-core.o:   detaching client %s:\n",
					client->name));
				/*i2c_detach_client(client);*/
				driver->detach_client(client);
			}
		}
	}
	ADAP_UNLOCK();
	drivers[i] = NULL;
	driver_count--;
	DRV_UNLOCK();
	
	DEB(printk("i2c-core.o: driver unregistered: %s\n",driver->name));
	return 0;
}


int i2c_attach_client(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	int i;

	for (i = 0; i < I2C_CLIENT_MAX; i++)
		if (NULL == adapter->clients[i])
			break;
	if (I2C_CLIENT_MAX == i) {
		printk(KERN_WARNING 
		       " i2c-core.o: attach_client(%s) - enlarge I2C_CLIENT_MAX.\n",
			client->name);
		return -ENOMEM;
	}

	adapter->clients[i] = client;
	adapter->client_count++;
	i2c_dummy_client(client);
	
	if (adapter->client_register != NULL) 
		adapter->client_register(client);
	DEB(printk("i2c-core.o: client [%s] registered to adapter [%s](pos. %d).\n",
		client->name, adapter->name,i));
	return 0;
}


int i2c_detach_client(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	int i;

	for (i = 0; i < I2C_CLIENT_MAX; i++)
		if (client == adapter->clients[i])
			break;
	if (I2C_CLIENT_MAX == i) {
		printk(KERN_WARNING " i2c-core.o: unregister_client [%s] not found\n",
			client->name);
		return -ENODEV;
	}

	if (adapter->client_unregister != NULL) 
		adapter->client_unregister(client);
	/*	client->driver->detach_client(client);*/

	adapter->clients[i] = NULL;
	adapter->client_count--;
	i2c_dummy_client(client);

	DEB(printk("i2c-core.o: client [%s] unregistered.\n",client->name));
	return 0;    
}

void i2c_inc_use_client(struct i2c_client *client)
{

	if (client->driver->inc_use != NULL)
		client->driver->inc_use(client);

	if (client->adapter->inc_use != NULL)
		client->adapter->inc_use(client->adapter);
}

void i2c_dec_use_client(struct i2c_client *client)
{

	if (client->driver->dec_use != NULL)
		client->driver->dec_use(client);

	if (client->adapter->dec_use != NULL)
		client->adapter->dec_use(client->adapter);
}

/* ----------------------------------------------------
 * The /proc functions
 * ----------------------------------------------------
 */

#ifdef CONFIG_PROC_FS

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,58)) && \
    (LINUX_VERSION_CODE <= KERNEL_VERSION(2,3,27))
/* Monitor access to /proc/bus/i2c*; make unloading i2c-proc impossible
   if some process still uses it or some file in it */
void monitor_bus_i2c(struct inode *inode, int fill)
{
  if (fill)
    MOD_INC_USE_COUNT;
  else
    MOD_DEC_USE_COUNT;
}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,58)) */

/* This function generates the output for /proc/bus/i2c */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,29))
int read_bus_i2c(char *buf, char **start, off_t offset, int len, int *eof, 
                 void *private)
#else /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,1,29)) */
int read_bus_i2c(char *buf, char **start, off_t offset, int len, int unused)
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,29)) */
{
	int i;
	int nr = 0;
	/* Note that it is safe to write a `little' beyond len. Yes, really. */
	for (i = 0; (i < I2C_ADAP_MAX) && (nr < len); i++)
		if (adapters[i]) {
			nr += sprintf(buf+nr, "i2c-%d\t", i);
			if (adapters[i]->algo->smbus_xfer) {
			        if (adapters[i]->algo->master_xfer)
					nr += sprintf(buf+nr,"smbus/i2c");
				else
					nr += sprintf(buf+nr,"smbus    ");
			} else if (adapters[i]->algo->master_xfer)
				nr += sprintf(buf+nr,"i2c       ");
			else
				nr += sprintf(buf+nr,"dummy     ");
			nr += sprintf(buf+nr,"\t%-32s\t%-32s\n",
			              adapters[i]->name,
			              adapters[i]->algo->name);
		}
	return nr;
}

/* This function generates the output for /proc/bus/i2c-? */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,29))
ssize_t i2cproc_bus_read(struct file * file, char * buf,size_t count, 
                         loff_t *ppos)
{
  struct inode * inode = file->f_dentry->d_inode;
#else (LINUX_VERSION_CODE < KERNEL_VERSION(2,1,29))
int i2cproc_bus_read(struct inode * inode, struct file * file,char * buf,
                     int count)
{
#endif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,29))
  char *kbuf;
  struct i2c_client *client;
  int i,j,len=0;

  if (count < 0)
    return -EINVAL;
  if (count > 4000)
    count = 4000;
  for (i = 0; i < I2C_ADAP_MAX; i++)
    if (adapters[i]->inode == inode->i_ino) {
      /* We need a bit of slack in the kernel buffer; this makes the
         sprintf safe. */
      if (! (kbuf = kmalloc(count + 80,GFP_KERNEL)))
        return -ENOMEM;
      for (j = 0; j < I2C_CLIENT_MAX; j++)
        if ((client = adapters[i]->clients[j]))
          /* Filter out dummy clients */
          if (client->driver->id != I2C_DRIVERID_I2CDEV)
            len += sprintf(kbuf+len,"%02x\t%-32s\t%-32s\n",
                           client->addr,
                           client->name,client->driver->name);
      if (file->f_pos+len > count)
        len = count - file->f_pos;
      len = len - file->f_pos;
      if (len < 0) 
        len = 0;
      copy_to_user (buf,kbuf+file->f_pos,len);
      file->f_pos += len;
      kfree(kbuf);
      return len;
    }
  return -ENOENT;
}

int i2cproc_init(void)
{

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,29))
	struct proc_dir_entry *proc_bus_i2c;
#else
	int res;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,29)) */

	i2cproc_initialized = 0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,29))
	if (! proc_bus) {
		printk("i2c-core.o: /proc/bus/ does not exist");
		i2cproc_cleanup();
		return -ENOENT;
 	} 
	proc_bus_i2c = create_proc_entry("i2c",0,proc_bus);
	if (!proc_bus_i2c) {
		printk("i2c-core.o: Could not create /proc/bus/i2c");
		i2cproc_cleanup();
		return -ENOENT;
 	}
	proc_bus_i2c->read_proc = &read_bus_i2c;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,27))
	proc_bus_i2c->owner = THIS_MODULE;
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,58))
	proc_bus_i2c->fill_inode = &monitor_bus_i2c;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,29)) */
	i2cproc_initialized += 2;
#else /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,1,29)) */
	/* In Linux 2.0.x, there is no /proc/bus! But I hope no other module
	   introduced it, or we are fucked. And 2.0.35 and earlier does not
	   export proc_dir_inode_operations, so we grab it from proc_net,
	   which also uses it. Not nice. */
	proc_bus_dir.ops = proc_net.ops;
	if ((res = proc_register_dynamic(&proc_root, &proc_bus_dir))) {
		printk("i2c-core.o: Could not create /proc/bus/");
		i2cproc_cleanup();
		return res;
	}
	i2cproc_initialized ++;
	if ((res = proc_register_dynamic(&proc_bus_dir, &proc_bus_i2c_dir))) {
		printk("i2c-core.o: Could not create /proc/bus/i2c\n");
		i2cproc_cleanup();
		return res;
	}
	i2cproc_initialized ++;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,29)) */
	return 0;
}

int i2cproc_cleanup(void)
{

	if (i2cproc_initialized >= 1) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,29))
		remove_proc_entry("i2c",proc_bus);
		i2cproc_initialized -= 2;
#else /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,1,29)) */
		int res;
		if (i2cproc_initialized >= 2) {
			if ((res = proc_unregister(&proc_bus_dir,
			                          proc_bus_i2c_dir.low_ino))) {
				printk("i2c-core.o: could not delete "
				       "/proc/bus/i2c, module not removed.");
				return res;
			}    
			i2cproc_initialized --;
		}
		if ((res = proc_unregister(&proc_root,proc_bus_dir.low_ino))) {
			printk("i2c-core.o: could not delete /proc/bus/, "
			       "module not removed.");
			return res;
		}    
		i2cproc_initialized --;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,29)) */
	}
	return 0;
}


#endif /* def CONFIG_PROC_FS */

/* ---------------------------------------------------    
 * dummy driver notification
 * --------------------------------------------------- 
 */

static void i2c_dummy_adapter(struct i2c_adapter *adap)
{
	int i;	
	for (i=0; i<I2C_DRIVER_MAX; i++)
	  if (drivers[i] && (drivers[i]->flags & I2C_DF_DUMMY))
	    drivers[i]->attach_adapter(adap);
}

static void i2c_dummy_client(struct i2c_client *client)
{
	int i;
	for (i=0; i<I2C_DRIVER_MAX; i++)
	  if (drivers[i] && (drivers[i]->flags & I2C_DF_DUMMY))
	    drivers[i]->detach_client(client);
}


/* ----------------------------------------------------
 * the functional interface to the i2c busses.
 * ----------------------------------------------------
 */

int i2c_transfer(struct i2c_adapter * adap, struct i2c_msg msgs[],int num)
{
	int ret;

	if (adap->algo->master_xfer) {
 	 	DEB2(printk("i2c-core.o: master_xfer: %s with %d msgs.\n",adap->name,num));

		I2C_LOCK(adap);
		ret = adap->algo->master_xfer(adap,msgs,num);
		I2C_UNLOCK(adap);

		return ret;
	} else {
		printk("i2c-core.o: I2C adapter %04x: I2C level transfers not supported\n",
                       adap->id);
		return -ENOSYS;
	}
}

int i2c_master_send(struct i2c_client *client,const char *buf ,int count)
{
	int ret;
	struct i2c_adapter *adap=client->adapter;
	struct i2c_msg msg;

	if (client->adapter->algo->master_xfer) {
		msg.addr   = client->addr;
		msg.flags = client->flags & I2C_M_TEN;
		msg.len = count;
		(const char *)msg.buf = buf;
	
		DEB2(printk("i2c-core.o: master_send: writing %d bytes on %s.\n",
			count,client->adapter->name));
	
		I2C_LOCK(adap);
		ret = adap->algo->master_xfer(adap,&msg,1);
		I2C_UNLOCK(adap);

		/* if everything went ok (i.e. 1 msg transmitted), return #bytes
		 * transmitted, else error code.
		 */
		return (ret == 1 )? count : ret;
	} else {
		printk("i2c-core.o: I2C adapter %04x: I2C level transfers not supported\n",
                       client->adapter->id);
		return -ENOSYS;
	}
}

int i2c_master_recv(struct i2c_client *client, char *buf ,int count)
{
	struct i2c_adapter *adap=client->adapter;
	struct i2c_msg msg;
	int ret;
	if (client->adapter->algo->master_xfer) {
		msg.addr   = client->addr;
		msg.flags = client->flags & I2C_M_TEN;
		msg.flags |= I2C_M_RD;
		msg.len = count;
		msg.buf = buf;

		DEB2(printk("i2c-core.o: master_recv: reading %d bytes on %s.\n",
			count,client->adapter->name));
	
		I2C_LOCK(adap);
		ret = adap->algo->master_xfer(adap,&msg,1);
		I2C_UNLOCK(adap);
	
		DEB2(printk("i2c-core.o: master_recv: return:%d (count:%d, addr:0x%02x)\n",
			ret, count, client->addr));
	
		/* if everything went ok (i.e. 1 msg transmitted), return #bytes
	 	* transmitted, else error code.
	 	*/
		return (ret == 1 )? count : ret;
	} else {
		printk("i2c-core.o: I2C adapter %04x: I2C level transfers not supported\n",
                       client->adapter->id);
		return -ENOSYS;
	}
}


int i2c_control(struct i2c_client *client,
	unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct i2c_adapter *adap = client->adapter;

	DEB2(printk("i2c-core.o: i2c ioctl, cmd: 0x%x, arg: %#lx\n", cmd, arg));
	switch ( cmd ) {
		case I2C_RETRIES:
			adap->retries = arg;
			break;
		case I2C_TIMEOUT:
			adap->timeout = arg;
			break;
		default:
			if (adap->algo->algo_control!=NULL)
				ret = adap->algo->algo_control(adap,cmd,arg);
	}
	return ret;
}

/* ----------------------------------------------------
 * the i2c address scanning function
 * Will not work for 10-bit addresses!
 * ----------------------------------------------------
 */
int i2c_probe(struct i2c_adapter *adapter,
                   struct i2c_client_address_data *address_data,
                   i2c_client_found_addr_proc *found_proc)
{
  int addr,i,found,err;
  int adap_id = i2c_adapter_id(adapter);

  /* Forget it if we can't probe using SMBUS_QUICK */
  if (! i2c_check_functionality(adapter,I2C_FUNC_SMBUS_QUICK))
    return -1;

  for (addr = 0x00; 
       addr <= 0x7f;
       addr++) {

    /* If it is in one of the force entries, we don't do any detection
       at all */
    found = 0;

    for (i = 0;
         !found && (address_data->force[i] != I2C_CLIENT_END);
         i += 3) {
      if (((adap_id == address_data->force[i]) || 
           (address_data->force[i] == ANY_I2C_BUS)) &&
           (addr == address_data->force[i+1])) {
	DEB2(printk("i2c-core.o: found force parameter for adapter %d, addr %04x\n",
               adap_id,addr));
        if ((err = found_proc(adapter,addr,0,0)))
           return err;
        found = 1;
      }
    }
    if (found) 
      continue;

    /* If this address is in one of the ignores, we can forget about it
       right now */
    for (i = 0;
         !found && (address_data->ignore[i] != I2C_CLIENT_END); 
         i += 2) {
      if (((adap_id == address_data->ignore[i]) || 
           ((address_data->ignore[i] == ANY_I2C_BUS))) &&
          (addr == address_data->ignore[i+1])) {
          DEB2(printk("i2c-core.o: found ignore parameter for adapter %d, "
                 "addr %04x\n", adap_id ,addr));
        found = 1;
      }
    }
    for (i = 0;
         !found && (address_data->ignore_range[i] != I2C_CLIENT_END);
         i += 3) {
      if (((adap_id == address_data->ignore_range[i]) ||
           ((address_data->ignore_range[i]==ANY_I2C_BUS))) &&
          (addr >= address_data->ignore_range[i+1]) &&
          (addr <= address_data->ignore_range[i+2])) {
        DEB2(printk("i2c-core.o: found ignore_range parameter for adapter %d, "
                 "addr %04x\n", adap_id,addr));
        found = 1;
      }
    }
    if (found) 
      continue;

    /* Now, we will do a detection, but only if it is in the normal or 
       probe entries */  
    for (i = 0;
         !found && (address_data->normal_i2c[i] != I2C_CLIENT_END);
         i += 1) {
      if (addr == address_data->normal_i2c[i]) {
        found = 1;
        DEB2(printk("i2c-core.o: found normal i2c entry for adapter %d, "
               "addr %02x", adap_id,addr));
        }
    }

    for (i = 0;
         !found && (address_data->normal_i2c_range[i] != I2C_CLIENT_END);
         i += 2) {
      if ((addr >= address_data->normal_i2c_range[i]) &&
          (addr <= address_data->normal_i2c_range[i+1])) {
        found = 1;
        DEB2(printk("i2c-core.o: found normal i2c_range entry for adapter %d, "
               "addr %04x\n", adap_id,addr));
      }
    }

    for (i = 0;
         !found && (address_data->probe[i] != I2C_CLIENT_END);
         i += 2) {
      if (((adap_id == address_data->probe[i]) ||
           ((address_data->probe[i] == ANY_I2C_BUS))) &&
          (addr == address_data->probe[i+1])) {
        found = 1;
	DEB2(printk("i2c-core.o: found probe parameter for adapter %d, "
                 "addr %04x\n", adap_id,addr));
      }
    }
    for (i = 0;
         !found && (address_data->probe_range[i] != I2C_CLIENT_END);
         i += 3) {
      if (((adap_id == address_data->probe_range[i]) ||
           (address_data->probe_range[i] == ANY_I2C_BUS)) &&
          (addr >= address_data->probe_range[i+1]) &&
          (addr <= address_data->probe_range[i+2])) {
        found = 1;
	DEB2(printk("i2c-core.o: found probe_range parameter for adapter %d, "
                 "addr %04x\n", adap_id,addr));
      }
    }
    if (!found) 
      continue;

    /* OK, so we really should examine this address. First check
       whether there is some client here at all! */
    if (i2c_smbus_xfer(adapter,addr,0,0,0,I2C_SMBUS_QUICK,NULL) >= 0)
      if ((err = found_proc(adapter,addr,0,-1)))
        return err;
  }
  return 0;
}

/* +++ frodo
 * return id number for a specific adapter
 */
int i2c_adapter_id(struct i2c_adapter *adap)
{
	int i;
	for (i = 0; i < I2C_ADAP_MAX; i++)
		if (adap == adapters[i])
			return i;
	return -1;
}

/* The SMBus parts */

extern s32 i2c_smbus_write_quick(struct i2c_client * client, u8 value)
{
	return i2c_smbus_xfer(client->adapter,client->addr,client->flags,
                              value,0,I2C_SMBUS_QUICK,NULL);
}

extern s32 i2c_smbus_read_byte(struct i2c_client * client)
{
	union i2c_smbus_data data;
	if (i2c_smbus_xfer(client->adapter,client->addr,client->flags,
                           I2C_SMBUS_READ,0,I2C_SMBUS_BYTE, &data))
		return -1;
	else
		return 0x0FF & data.byte;
}

extern s32 i2c_smbus_write_byte(struct i2c_client * client, u8 value)
{
	return i2c_smbus_xfer(client->adapter,client->addr,client->flags,
                              I2C_SMBUS_WRITE,value, I2C_SMBUS_BYTE,NULL);
}

extern s32 i2c_smbus_read_byte_data(struct i2c_client * client, u8 command)
{
	union i2c_smbus_data data;
	if (i2c_smbus_xfer(client->adapter,client->addr,client->flags,
                           I2C_SMBUS_READ,command, I2C_SMBUS_BYTE_DATA,&data))
		return -1;
	else
		return 0x0FF & data.byte;
}

extern s32 i2c_smbus_write_byte_data(struct i2c_client * client, 
                                     u8 command, u8 value)
{
	union i2c_smbus_data data;
	data.byte = value;
	return i2c_smbus_xfer(client->adapter,client->addr,client->flags,
                              I2C_SMBUS_WRITE,command,
	                      I2C_SMBUS_BYTE_DATA,&data);
}

extern s32 i2c_smbus_read_word_data(struct i2c_client * client, u8 command)
{
	union i2c_smbus_data data;
	if (i2c_smbus_xfer(client->adapter,client->addr,client->flags,
                           I2C_SMBUS_READ,command, I2C_SMBUS_WORD_DATA, &data))
		return -1;
	else
		return 0x0FFFF & data.word;
}

extern s32 i2c_smbus_write_word_data(struct i2c_client * client,
                                     u8 command, u16 value)
{
	union i2c_smbus_data data;
	data.word = value;
	return i2c_smbus_xfer(client->adapter,client->addr,client->flags,
                              I2C_SMBUS_WRITE,command,
                              I2C_SMBUS_WORD_DATA,&data);
}

extern s32 i2c_smbus_process_call(struct i2c_client * client,
                                  u8 command, u16 value)
{
	union i2c_smbus_data data;
	data.word = value;
	if (i2c_smbus_xfer(client->adapter,client->addr,client->flags,
                           I2C_SMBUS_WRITE,command,
                           I2C_SMBUS_PROC_CALL, &data))
		return -1;
	else
		return 0x0FFFF & data.word;
}

/* Returns the number of read bytes */
extern s32 i2c_smbus_read_block_data(struct i2c_client * client,
                                     u8 command, u8 *values)
{
	union i2c_smbus_data data;
	int i;
	if (i2c_smbus_xfer(client->adapter,client->addr,client->flags,
                           I2C_SMBUS_READ,command,
	                   I2C_SMBUS_BLOCK_DATA,&data))
		return -1;
	else {
		for (i = 1; i <= data.block[0]; i++)
			values[i-1] = data.block[i];
		return data.block[0];
	}
}

extern s32 i2c_smbus_write_block_data(struct i2c_client * client,
                                      u8 command, u8 length, u8 *values)
{
	union i2c_smbus_data data;
	int i;
	if (length > 32)
		length = 32;
	for (i = 1; i <= length; i++)
		data.block[i] = values[i-1];
	data.block[0] = length;
	return i2c_smbus_xfer(client->adapter,client->addr,client->flags,
                              I2C_SMBUS_WRITE,command,
	                      I2C_SMBUS_BLOCK_DATA,&data);
}

/* Simulate a SMBus command using the i2c protocol 
   No checking of parameters is done!  */
static s32 i2c_smbus_xfer_emulated(struct i2c_adapter * adapter, u16 addr, 
                                   unsigned short flags,
                                   char read_write, u8 command, int size, 
                                   union i2c_smbus_data * data)
{
	/* So we need to generate a series of msgs. In the case of writing, we
	  need to use only one message; when reading, we need two. We initialize
	  most things with sane defaults, to keep the code below somewhat
	  simpler. */
	unsigned char msgbuf0[33];
	unsigned char msgbuf1[33];
	int num = read_write == I2C_SMBUS_READ?2:1;
	struct i2c_msg msg[2] = { { addr, flags, 1, msgbuf0 }, 
	                          { addr, flags | I2C_M_RD, 0, msgbuf1 }
	                        };
	int i;

	msgbuf0[0] = command;
	switch(size) {
	case I2C_SMBUS_QUICK:
		msg[0].len = 0;
		/* Special case: The read/write field is used as data */
		msg[0].flags = flags | (read_write==I2C_SMBUS_READ)?I2C_M_RD:0;
		num = 1;
		break;
	case I2C_SMBUS_BYTE:
		if (read_write == I2C_SMBUS_READ) {
			/* Special case: only a read! */
			msg[0].flags = I2C_M_RD | flags;
			num = 1;
		}
		break;
	case I2C_SMBUS_BYTE_DATA:
		if (read_write == I2C_SMBUS_READ)
			msg[1].len = 1;
		else {
			msg[0].len = 2;
			msgbuf0[1] = data->byte;
		}
		break;
	case I2C_SMBUS_WORD_DATA:
		if (read_write == I2C_SMBUS_READ)
			msg[1].len = 2;
		else {
			msg[0].len=3;
			msgbuf0[1] = data->word & 0xff;
			msgbuf0[2] = (data->word >> 8) & 0xff;
		}
		break;
	case I2C_SMBUS_PROC_CALL:
		num = 2; /* Special case */
		msg[0].len = 3;
		msg[1].len = 2;
		msgbuf0[1] = data->word & 0xff;
		msgbuf0[2] = (data->word >> 8) & 0xff;
		break;
	case I2C_SMBUS_BLOCK_DATA:
		if (read_write == I2C_SMBUS_READ) {
			printk("i2c-core.o: Block read not supported under "
			       "I2C emulation!\n");
		return -1;
		} else {
			msg[1].len = data->block[0] + 1;
			if (msg[1].len > 32) {
				printk("i2c-core.o: smbus_access called with "
				       "invalid block write size (%d)\n",
				       msg[1].len);
				return -1;
			}
			for (i = 1; i <= msg[1].len; i++)
				msgbuf0[i] = data->block[i];
		}
		break;
	default:
		printk("i2c-core.o: smbus_access called with invalid size (%d)\n",
		       size);
		return -1;
	}
    
	if (i2c_transfer(adapter, msg, num) < 0)
		return -1;

	if (read_write == I2C_SMBUS_READ)
		switch(size) {
			case I2C_SMBUS_BYTE:
				data->byte = msgbuf0[0];
				break;
			case I2C_SMBUS_BYTE_DATA:
				data->byte = msgbuf1[0];
				break;
			case I2C_SMBUS_WORD_DATA: 
			case I2C_SMBUS_PROC_CALL:
				data->word = msgbuf1[0] | (msgbuf1[1] << 8);
				break;
  		}
  	return 0;
}


s32 i2c_smbus_xfer(struct i2c_adapter * adapter, u16 addr, unsigned short flags,
                   char read_write, u8 command, int size, 
                   union i2c_smbus_data * data)
{
	s32 res;
        flags = flags & I2C_M_TEN;
	if (adapter->algo->smbus_xfer) {
		I2C_LOCK(adapter);
		res = adapter->algo->smbus_xfer(adapter,addr,flags,read_write,
		                                command,size,data);
		I2C_UNLOCK(adapter);
	} else
		res = i2c_smbus_xfer_emulated(adapter,addr,flags,read_write,
                                              command,size,data);
	return res;
}


/* You should always define `functionality'; the 'else' is just for
   backward compatibility. */ 
u32 i2c_get_functionality (struct i2c_adapter *adap)
{
  if (adap->algo->functionality)
    return adap->algo->functionality(adap);
  else
    return 0xffffffff;
}

int i2c_check_functionality (struct i2c_adapter *adap, u32 func)
{
  u32 adap_func = i2c_get_functionality (adap);
  return (func & adap_func) == func;
}


static int __init i2c_init(void)
{
        printk("i2c-core.o: i2c core module\n");
	memset(adapters,0,sizeof(adapters));
	memset(drivers,0,sizeof(drivers));
	adap_count=0;
	driver_count=0;

	init_MUTEX(&adap_lock);
	init_MUTEX(&driver_lock);
	
	i2cproc_init();
	
	return 0;
}

#ifndef MODULE
#ifdef CONFIG_I2C_CHARDEV
	extern int i2c_dev_init(void);
#endif
#ifdef CONFIG_I2C_ALGOBIT
	extern int algo_bit_init(void);
#endif
#ifdef CONFIG_I2C_BITLP
	extern int bitlp_init(void);
#endif
#ifdef CONFIG_I2C_BITELV
	extern int bitelv_init(void);
#endif
#ifdef CONFIG_I2C_BITVELLE
	extern int bitvelle_init(void);
#endif
#ifdef CONFIG_I2C_BITVIA
	extern int bitvia_init(void);
#endif

#ifdef CONFIG_I2C_ALGOPCF
	extern int algo_pcf_init(void);	
#endif
#ifdef CONFIG_I2C_PCFISA
	extern int pcfisa_init(void);
#endif

/* This is needed for automatic patch generation: sensors code starts here */
/* This is needed for automatic patch generation: sensors code ends here   */

int __init i2c_init_all(void)
{
	/* --------------------- global ----- */
	i2c_init();

#ifdef CONFIG_I2C_CHARDEV
	i2c_dev_init();
#endif
	/* --------------------- bit -------- */
#ifdef CONFIG_I2C_ALGOBIT
	i2c_algo_bit_init();
#endif
#ifdef CONFIG_I2C_PHILIPSPAR
	i2c_bitlp_init();
#endif
#ifdef CONFIG_I2C_ELV
	i2c_bitelv_init();
#endif
#ifdef CONFIG_I2C_VELLEMAN
	i2c_bitvelle_init();
#endif

	/* --------------------- pcf -------- */
#ifdef CONFIG_I2C_ALGOPCF
	i2c_algo_pcf_init();	
#endif
#ifdef CONFIG_I2C_ELEKTOR
	i2c_pcfisa_init();
#endif
/* This is needed for automatic patch generation: sensors code starts here */
/* This is needed for automatic patch generation: sensors code ends here */

	return 0;
}

#endif



EXPORT_SYMBOL(i2c_add_adapter);
EXPORT_SYMBOL(i2c_del_adapter);
EXPORT_SYMBOL(i2c_add_driver);
EXPORT_SYMBOL(i2c_del_driver);
EXPORT_SYMBOL(i2c_attach_client);
EXPORT_SYMBOL(i2c_detach_client);
EXPORT_SYMBOL(i2c_inc_use_client);
EXPORT_SYMBOL(i2c_dec_use_client);


EXPORT_SYMBOL(i2c_master_send);
EXPORT_SYMBOL(i2c_master_recv);
EXPORT_SYMBOL(i2c_control);
EXPORT_SYMBOL(i2c_transfer);
EXPORT_SYMBOL(i2c_adapter_id);
EXPORT_SYMBOL(i2c_probe);

EXPORT_SYMBOL(i2c_smbus_xfer);
EXPORT_SYMBOL(i2c_smbus_write_quick);
EXPORT_SYMBOL(i2c_smbus_read_byte);
EXPORT_SYMBOL(i2c_smbus_write_byte);
EXPORT_SYMBOL(i2c_smbus_read_byte_data);
EXPORT_SYMBOL(i2c_smbus_write_byte_data);
EXPORT_SYMBOL(i2c_smbus_read_word_data);
EXPORT_SYMBOL(i2c_smbus_write_word_data);
EXPORT_SYMBOL(i2c_smbus_process_call);
EXPORT_SYMBOL(i2c_smbus_read_block_data);
EXPORT_SYMBOL(i2c_smbus_write_block_data);

EXPORT_SYMBOL(i2c_get_functionality);
EXPORT_SYMBOL(i2c_check_functionality);

#ifdef MODULE
MODULE_AUTHOR("Simon G. Vogl <simon@tk.uni-linz.ac.at>");
MODULE_DESCRIPTION("I2C-Bus main module");
MODULE_PARM(i2c_debug, "i");
MODULE_PARM_DESC(i2c_debug,"debug level");

int init_module(void) 
{
	return i2c_init();
}

void cleanup_module(void) 
{
	i2cproc_cleanup();
}
#endif
