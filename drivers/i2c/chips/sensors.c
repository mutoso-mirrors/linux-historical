/*
    sensors.c - Part of lm_sensors, Linux kernel modules for hardware
                monitoring
    Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl> 

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
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* Not configurable as a module */

#include <linux/init.h>

extern int sensors_adm1021_init(void);
extern int sensors_lm75_init(void);

int __init sensors_init_all(void)
{
#ifdef CONFIG_SENSORS_ADM1021
	sensors_adm1021_init();
#endif
#ifdef CONFIG_SENSORS_LM75
	sensors_lm75_init();
#endif
	return 0;
}
