/*
 *	Generic address resultion entity
 *
 *	Authors:
 *	net_random Alan Cox
 *	net_ratelimit Andy Kleen
 *
 *	Created by Alexey Kuznetsov <kuznet@ms2.inr.ac.ru>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>

static unsigned long net_rand_seed = 152L;

unsigned long net_random(void)
{
	net_rand_seed=net_rand_seed*69069L+1;
        return net_rand_seed^jiffies;
}

void net_srandom(unsigned long entropy)
{
	net_rand_seed ^= entropy;
	net_random();
}


/* 
 * This enforces a rate limit: not more than one kernel message
 * every 5secs to make a denial-of-service attack impossible.
 *
 * All warning printk()s should be guarded by this function. 
 */ 
int net_ratelimit(void)
{
	static unsigned long last_msg; 
	static int missed;
	
	if ((jiffies - last_msg) >= 5*HZ) {
		if (missed)	
			printk(KERN_WARNING "ipv4: (%d messages suppressed. Flood?)\n", missed);
		missed = 0; 
		last_msg = jiffies;
		return 1;
	}
	missed++; 
	return 0; 
}
