/*
 *	NetBEUI data structures
 */
 
#ifndef __NET_NETBEUI_H
#define __NET_NETBEUI_H

/*
 *	Used to keep lists of netbeui sessions
 */
 
struct nb_ses
{
	struct nb_ses *next;
	struct nb_nam *name;
	struct nb_link *parent;	/* Owner link */
	struct sock *sk;
};

/*
 *	A netbeui link
 */
 
struct nb_link
{
	u8	mac[6];		/* Mac address of remote */
	struct device *dev;	/* Device we heard him on */
	struct llc *llc;	/* 802.2 link layer */
	struct nb_ses *sessions;/* Netbeui sessions on this LLC link */
	struct wait_queue *wait;/* Wait queue for this netbios LLC */
};


/*
 *	Netbios name defence list
 */

struct nb_name
{
	struct nb_name *next;	/*	Chain 		*/
	struct device *dev;	/*	Device 		*/
	char name[NB_NAME_LEN];	/* 	Object Name	*/
	int state;		/* 	Name State	*/
#define NB_NAME_ACQUIRE		1	/* We are trying to get a name */
#define NB_NAME_COLLIDE		2	/* Name collided - we failed */
#define NB_OURS			3	/* We own the name	*/
#define NB_NAME_OTHER		4	/* Name found - owned by other */
	int ours;			/* We own this name */
	int users;			/* Number of nb_ses's to this name */
	struct timer_list	timer;	/* Our timer */
	int timer_mode;			/* Timer mode */
#define NB_TIMER_ACQUIRE	1	/* Expiry means we got our name */
#define NB_TIMER_COLLIDE	2	/* Expire a collded record */
#define NB_TIMER_DROP		3	/* Drop a learned record */	
};


/*
 *	LLC link manager
 */
 
extern struct nb_link *netbeui_find_link(u8 macaddr);
extern struct nb_link *netbeui_create_link(u8 macaddr);
extern int netbeui_destroy_link(u8 macaddr);

/*
 *	Namespace manager
 */
 
extern struct nb_name *netbeui_find_name(char *name);
extern struct nb_name *netbeui_add_name(char *name, int ours);
extern struct nb_name *netbeui_lookup_name(char *name);
extern int nb_delete_name(struct nb_name *name);


#endif
