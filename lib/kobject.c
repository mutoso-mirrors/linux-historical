/*
 * kobject.c - library routines for handling generic kernel objects
 */

#undef DEBUG

#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/stat.h>

static spinlock_t kobj_lock = SPIN_LOCK_UNLOCKED;

/**
 *	populate_dir - populate directory with attributes.
 *	@kobj:	object we're working on.
 *
 *	Most subsystems have a set of default attributes that 
 *	are associated with an object that registers with them.
 *	This is a helper called during object registration that 
 *	loops through the default attributes of the subsystem 
 *	and creates attributes files for them in sysfs.
 *
 */

static int populate_dir(struct kobject * kobj)
{
	struct kobj_type * t = get_ktype(kobj);
	struct attribute * attr;
	int error = 0;
	int i;
	
	if (t && t->default_attrs) {
		for (i = 0; (attr = t->default_attrs[i]); i++) {
			if ((error = sysfs_create_file(kobj,attr)))
				break;
		}
	}
	return error;
}

static int create_dir(struct kobject * kobj)
{
	int error = 0;
	if (strlen(kobj->name)) {
		error = sysfs_create_dir(kobj);
		if (!error) {
			if ((error = populate_dir(kobj)))
				sysfs_remove_dir(kobj);
		}
	}
	return error;
}


static inline struct kobject * to_kobj(struct list_head * entry)
{
	return container_of(entry,struct kobject,entry);
}


#ifdef CONFIG_HOTPLUG
static int get_kobj_path_length(struct kset *kset, struct kobject *kobj)
{
	int length = 1;
	struct kobject * parent = kobj;

	/* walk up the ancestors until we hit the one pointing to the 
	 * root.
	 * Add 1 to strlen for leading '/' of each level.
	 */
	do {
		length += strlen (parent->name) + 1;
		parent = parent->parent;
	} while (parent);
	return length;
}

static void fill_kobj_path(struct kset *kset, struct kobject *kobj, char *path, int length)
{
	struct kobject * parent;

	--length;
	for (parent = kobj; parent; parent = parent->parent) {
		int cur = strlen (parent->name);
		/* back up enough to print this name with '/' */
		length -= cur;
		strncpy (path + length, parent->name, cur);
		*(path + --length) = '/';
	}

	pr_debug("%s: path = '%s'\n",__FUNCTION__,path);
}

#define BUFFER_SIZE	1024	/* should be enough memory for the env */
#define NUM_ENVP	32	/* number of env pointers */
static void kset_hotplug(const char *action, struct kset *kset,
			 struct kobject *kobj)
{
	char *argv [3];
	char **envp = NULL;
	char *buffer = NULL;
	char *scratch;
	int i = 0;
	int retval;
	int kobj_path_length;
	char *kobj_path = NULL;
	char *name = NULL;

	/* If the kset has a filter operation, call it. If it returns
	   failure, no hotplug event is required. */
	if (kset->hotplug_ops->filter) {
		if (!kset->hotplug_ops->filter(kset, kobj))
			return;
	}

	pr_debug ("%s\n", __FUNCTION__);

	if (!hotplug_path[0])
		return;

	envp = kmalloc(NUM_ENVP * sizeof (char *), GFP_KERNEL);
	if (!envp)
		return;
	memset (envp, 0x00, NUM_ENVP * sizeof (char *));

	buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
	if (!buffer)
		goto exit;

	if (kset->hotplug_ops->name)
		name = kset->hotplug_ops->name(kset, kobj);
	if (name == NULL)
		name = kset->kobj.name;

	argv [0] = hotplug_path;
	argv [1] = name;
	argv [2] = 0;

	/* minimal command environment */
	envp [i++] = "HOME=/";
	envp [i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";

	scratch = buffer;

	envp [i++] = scratch;
	scratch += sprintf(scratch, "ACTION=%s", action) + 1;

	kobj_path_length = get_kobj_path_length (kset, kobj);
	kobj_path = kmalloc (kobj_path_length, GFP_KERNEL);
	if (!kobj_path)
		goto exit;
	memset (kobj_path, 0x00, kobj_path_length);
	fill_kobj_path (kset, kobj, kobj_path, kobj_path_length);

	envp [i++] = scratch;
	scratch += sprintf (scratch, "DEVPATH=%s", kobj_path) + 1;

	if (kset->hotplug_ops->hotplug) {
		/* have the kset specific function add its stuff */
		retval = kset->hotplug_ops->hotplug (kset, kobj,
				  &envp[i], NUM_ENVP - i, scratch,
				  BUFFER_SIZE - (scratch - buffer));
		if (retval) {
			pr_debug ("%s - hotplug() returned %d\n",
				  __FUNCTION__, retval);
			goto exit;
		}
	}

	pr_debug ("%s: %s %s %s %s %s %s\n", __FUNCTION__, argv[0], argv[1],
		  envp[0], envp[1], envp[2], envp[3]);
	retval = call_usermodehelper (argv[0], argv, envp, 0);
	if (retval)
		pr_debug ("%s - call_usermodehelper returned %d\n",
			  __FUNCTION__, retval);

exit:
	kfree(kobj_path);
	kfree(buffer);
	kfree(envp);
	return;
}
#else
static void kset_hotplug(const char *action, struct kset *kset,
			 struct kobject *kobj)
{
	return;
}
#endif	/* CONFIG_HOTPLUG */

/**
 *	kobject_init - initialize object.
 *	@kobj:	object in question.
 */

void kobject_init(struct kobject * kobj)
{
	atomic_set(&kobj->refcount,1);
	INIT_LIST_HEAD(&kobj->entry);
	kobj->kset = kset_get(kobj->kset);
}


/**
 *	unlink - remove kobject from kset list.
 *	@kobj:	kobject.
 *
 *	Remove the kobject from the kset list and decrement
 *	its parent's refcount.
 *	This is separated out, so we can use it in both 
 *	kobject_del() and kobject_add() on error.
 */

static void unlink(struct kobject * kobj)
{
	if (kobj->kset) {
		down_write(&kobj->kset->subsys->rwsem);
		list_del_init(&kobj->entry);
		up_write(&kobj->kset->subsys->rwsem);
	}
	if (kobj->parent) 
		kobject_put(kobj->parent);
	kobject_put(kobj);
}

/**
 *	kobject_add - add an object to the hierarchy.
 *	@kobj:	object.
 */

int kobject_add(struct kobject * kobj)
{
	int error = 0;
	struct kobject * parent;
	struct kobject * top_kobj;

	if (!(kobj = kobject_get(kobj)))
		return -ENOENT;

	parent = kobject_get(kobj->parent);

	pr_debug("kobject %s: registering. parent: %s, set: %s\n",
		 kobj->name, parent ? parent->name : "<NULL>", 
		 kobj->kset ? kobj->kset->kobj.name : "<NULL>" );

	if (kobj->kset) {
		down_write(&kobj->kset->subsys->rwsem);
		if (parent) 
			list_add_tail(&kobj->entry,&parent->entry);
		else {
			list_add_tail(&kobj->entry,&kobj->kset->list);
			kobj->parent = kobject_get(&kobj->kset->kobj);
		}
		up_write(&kobj->kset->subsys->rwsem);
	}
	error = create_dir(kobj);
	if (error)
		unlink(kobj);
	else {
		/* If this kobj does not belong to a kset,
		   try to find a parent that does. */
		top_kobj = kobj;
		if (!top_kobj->kset && top_kobj->parent) {
			do {
				top_kobj = top_kobj->parent;
			} while (!top_kobj->kset && top_kobj->parent);
		}
	
		if (top_kobj->kset && top_kobj->kset->hotplug_ops)
			kset_hotplug("add", top_kobj->kset, kobj);
	}
	return error;
}


/**
 *	kobject_register - initialize and add an object.
 *	@kobj:	object in question.
 */

int kobject_register(struct kobject * kobj)
{
	int error = 0;
	if (kobj) {
		kobject_init(kobj);
		error = kobject_add(kobj);
		WARN_ON(error);
	} else
		error = -EINVAL;
	return error;
}

/**
 *	kobject_del - unlink kobject from hierarchy.
 * 	@kobj:	object.
 */

void kobject_del(struct kobject * kobj)
{
	struct kobject * top_kobj;

	/* If this kobj does not belong to a kset,
	   try to find a parent that does. */
	top_kobj = kobj;
	if (!top_kobj->kset && top_kobj->parent) {
		do {
			top_kobj = top_kobj->parent;
		} while (!top_kobj->kset && top_kobj->parent);
	}

	if (top_kobj->kset && top_kobj->kset->hotplug_ops)
		kset_hotplug("remove", top_kobj->kset, kobj);

	sysfs_remove_dir(kobj);
	unlink(kobj);
}

/**
 *	kobject_unregister - remove object from hierarchy and decrement refcount.
 *	@kobj:	object going away.
 */

void kobject_unregister(struct kobject * kobj)
{
	pr_debug("kobject %s: unregistering\n",kobj->name);
	kobject_del(kobj);
	kobject_put(kobj);
}

/**
 *	kobject_get - increment refcount for object.
 *	@kobj:	object.
 */

struct kobject * kobject_get(struct kobject * kobj)
{
	struct kobject * ret = kobj;
	spin_lock(&kobj_lock);
	if (kobj && atomic_read(&kobj->refcount) > 0)
		atomic_inc(&kobj->refcount);
	else
		ret = NULL;
	spin_unlock(&kobj_lock);
	return ret;
}

/**
 *	kobject_cleanup - free kobject resources. 
 *	@kobj:	object.
 */

void kobject_cleanup(struct kobject * kobj)
{
	struct kobj_type * t = get_ktype(kobj);
	struct kset * s = kobj->kset;

	pr_debug("kobject %s: cleaning up\n",kobj->name);
	if (t && t->release)
		t->release(kobj);
	if (s)
		kset_put(s);
}

/**
 *	kobject_put - decrement refcount for object.
 *	@kobj:	object.
 *
 *	Decrement the refcount, and if 0, call kobject_cleanup().
 */

void kobject_put(struct kobject * kobj)
{
	if (!atomic_dec_and_lock(&kobj->refcount, &kobj_lock))
		return;
	spin_unlock(&kobj_lock);
	kobject_cleanup(kobj);
}


/**
 *	kset_init - initialize a kset for use
 *	@k:	kset 
 */

void kset_init(struct kset * k)
{
	kobject_init(&k->kobj);
	INIT_LIST_HEAD(&k->list);
}


/**
 *	kset_add - add a kset object to the hierarchy.
 *	@k:	kset.
 *
 *	Simply, this adds the kset's embedded kobject to the 
 *	hierarchy. 
 *	We also try to make sure that the kset's embedded kobject
 *	has a parent before it is added. We only care if the embedded
 *	kobject is not part of a kset itself, since kobject_add()
 *	assigns a parent in that case. 
 *	If that is the case, and the kset has a controlling subsystem,
 *	then we set the kset's parent to be said subsystem. 
 */

int kset_add(struct kset * k)
{
	if (!k->kobj.parent && !k->kobj.kset && k->subsys)
		k->kobj.parent = &k->subsys->kset.kobj;

	return kobject_add(&k->kobj);
}


/**
 *	kset_register - initialize and add a kset.
 *	@k:	kset.
 */

int kset_register(struct kset * k)
{
	kset_init(k);
	return kset_add(k);
}


/**
 *	kset_unregister - remove a kset.
 *	@k:	kset.
 */

void kset_unregister(struct kset * k)
{
	kobject_unregister(&k->kobj);
}


/**
 *	kset_find_obj - search for object in kset.
 *	@kset:	kset we're looking in.
 *	@name:	object's name.
 *
 *	Lock kset via @kset->subsys, and iterate over @kset->list,
 *	looking for a matching kobject. Return object if found.
 */

struct kobject * kset_find_obj(struct kset * kset, const char * name)
{
	struct list_head * entry;
	struct kobject * ret = NULL;

	down_read(&kset->subsys->rwsem);
	list_for_each(entry,&kset->list) {
		struct kobject * k = to_kobj(entry);
		if (!strcmp(k->name,name)) {
			ret = k;
			break;
		}
	}
	up_read(&kset->subsys->rwsem);
	return ret;
}


void subsystem_init(struct subsystem * s)
{
	init_rwsem(&s->rwsem);
	kset_init(&s->kset);
}

/**
 *	subsystem_register - register a subsystem.
 *	@s:	the subsystem we're registering.
 *
 *	Once we register the subsystem, we want to make sure that 
 *	the kset points back to this subsystem for correct usage of 
 *	the rwsem. 
 */

int subsystem_register(struct subsystem * s)
{
	int error;

	subsystem_init(s);
	pr_debug("subsystem %s: registering\n",s->kset.kobj.name);

	if (!(error = kset_add(&s->kset))) {
		if (!s->kset.subsys)
			s->kset.subsys = s;
	}
	return error;
}

void subsystem_unregister(struct subsystem * s)
{
	pr_debug("subsystem %s: unregistering\n",s->kset.kobj.name);
	kset_unregister(&s->kset);
}


/**
 *	subsystem_create_file - export sysfs attribute file.
 *	@s:	subsystem.
 *	@a:	subsystem attribute descriptor.
 */

int subsys_create_file(struct subsystem * s, struct subsys_attribute * a)
{
	int error = 0;
	if (subsys_get(s)) {
		error = sysfs_create_file(&s->kset.kobj,&a->attr);
		subsys_put(s);
	}
	return error;
}


/**
 *	subsystem_remove_file - remove sysfs attribute file.
 *	@s:	subsystem.
 *	@a:	attribute desciptor.
 */

void subsys_remove_file(struct subsystem * s, struct subsys_attribute * a)
{
	if (subsys_get(s)) {
		sysfs_remove_file(&s->kset.kobj,&a->attr);
		subsys_put(s);
	}
}


EXPORT_SYMBOL(kobject_init);
EXPORT_SYMBOL(kobject_register);
EXPORT_SYMBOL(kobject_unregister);
EXPORT_SYMBOL(kobject_get);
EXPORT_SYMBOL(kobject_put);

EXPORT_SYMBOL(kset_find_obj);

EXPORT_SYMBOL(subsystem_init);
EXPORT_SYMBOL(subsystem_register);
EXPORT_SYMBOL(subsystem_unregister);
EXPORT_SYMBOL(subsys_create_file);
EXPORT_SYMBOL(subsys_remove_file);
