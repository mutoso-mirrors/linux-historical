/*!***************************************************************************
*!
*! FILE NAME  : i2c.c
*!
*! DESCRIPTION: implements an interface for IIC/I2C, both directly from other
*!              kernel modules (i2c_writereg/readreg) and from userspace using
*!              ioctl()'s
*!
*! Nov 30 1998  Torbjorn Eliasson  Initial version.
*!              Bjorn Wesen        Elinux kernel version.
*! Jan 14 2000  Johan Adolfsson    Fixed PB shadow register stuff - 
*!                                 don't use PB_I2C if DS1302 uses same bits,
*!                                 use PB.
*! $Log: i2c.c,v $
*! Revision 1.7  2001/04/04 13:11:36  markusl
*! Updated according to review remarks
*!
*! Revision 1.6  2001/03/19 12:43:00  markusl
*! Made some symbols unstatic (used by the eeprom driver)
*!
*! Revision 1.5  2001/02/27 13:52:48  bjornw
*! malloc.h -> slab.h
*!
*! Revision 1.4  2001/02/15 07:17:40  starvik
*! Corrected usage if port_pb_i2c_shadow
*!
*! Revision 1.3  2001/01/26 17:55:13  bjornw
*! * Made I2C_USES_PB_NOT_PB_I2C a CONFIG option instead of assigning it
*!   magically. Config.in needs to set it for the options that need it, like
*!   Dallas 1302 support. Actually, it should be default since it screws up
*!   the PB bits even if you don't use I2C..
*! * Include linux/config.h to get the above
*!
*! Revision 1.2  2001/01/18 15:49:30  bjornw
*! 2.4 port of I2C including some cleanups (untested of course)
*!
*! Revision 1.1  2001/01/18 15:35:25  bjornw
*! Verbatim copy of the Etrax i2c driver, 2.0 elinux version
*!
*!
*! ---------------------------------------------------------------------------
*!
*! (C) Copyright 1999, 2000, 2001 Axis Communications AB, LUND, SWEDEN
*!
*!***************************************************************************/
/* $Id: i2c.c,v 1.7 2001/04/04 13:11:36 markusl Exp $ */
/****************** INCLUDE FILES SECTION ***********************************/

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/config.h>

#include <asm/etraxi2c.h>

#include <asm/system.h>
#include <asm/svinto.h>
#include <asm/io.h>
#include <asm/delay.h>

#include "i2c.h"

/****************** I2C DEFINITION SECTION *************************/

#define D(x)

#define I2C_MAJOR 123  /* LOCAL/EXPERIMENTAL */
static const char i2c_name[] = "i2c";

#define CLOCK_LOW_TIME            8
#define CLOCK_HIGH_TIME           8
#define START_CONDITION_HOLD_TIME 8
#define STOP_CONDITION_HOLD_TIME  8
#define ENABLE_OUTPUT 0x01
#define ENABLE_INPUT 0x00
#define I2C_CLOCK_HIGH 1
#define I2C_CLOCK_LOW 0
#define I2C_DATA_HIGH 1
#define I2C_DATA_LOW 0

#if 0
/* TODO: fix this so the CONFIG_ETRAX_I2C_USES... is set in Config.in instead */
#if defined(CONFIG_DS1302) && (CONFIG_DS1302_SDABIT==0) && \
           (CONFIG_DS1302_SCLBIT == 1)
#define CONFIG_ETRAX_I2C_USES_PB_NOT_PB_I2C
#endif
#endif

#ifdef CONFIG_ETRAX_I2C_USES_PB_NOT_PB_I2C
/* Use PB and not PB_I2C */
#define SDABIT 0
#define SCLBIT 1
#define i2c_enable() 
#define i2c_disable() 

/* enable or disable output-enable, to select output or input on the i2c bus */

#define i2c_dir_out() \
  REG_SHADOW_SET(R_PORT_PB_DIR, port_pb_dir_shadow, SDABIT, 1)
#define i2c_dir_in()  \
  REG_SHADOW_SET(R_PORT_PB_DIR, port_pb_dir_shadow, SDABIT, 0)

/* control the i2c clock and data signals */

#define i2c_clk(x) \
  REG_SHADOW_SET(R_PORT_PB_DATA, port_pb_data_shadow, SCLBIT, x)
#define i2c_data(x) \
  REG_SHADOW_SET(R_PORT_PB_DATA, port_pb_data_shadow, SDABIT, x)

/* read a bit from the i2c interface */

#define i2c_getbit() (*R_PORT_PB_READ & (1 << SDABIT))

#else
/* enable or disable the i2c interface */

#define i2c_enable() *R_PORT_PB_I2C = (port_pb_i2c_shadow |= IO_MASK(R_PORT_PB_I2C, i2c_en))
#define i2c_disable() *R_PORT_PB_I2C = (port_pb_i2c_shadow &= ~IO_MASK(R_PORT_PB_I2C, i2c_en))

/* enable or disable output-enable, to select output or input on the i2c bus */

#define i2c_dir_out() *R_PORT_PB_I2C = (port_pb_i2c_shadow &= ~IO_MASK(R_PORT_PB_I2C, i2c_oe_))
#define i2c_dir_in() *R_PORT_PB_I2C = (port_pb_i2c_shadow |= IO_MASK(R_PORT_PB_I2C, i2c_oe_))

/* control the i2c clock and data signals */

#define i2c_clk(x) *R_PORT_PB_I2C = (port_pb_i2c_shadow = (port_pb_i2c_shadow & \
       ~IO_MASK(R_PORT_PB_I2C, i2c_clk)) | IO_FIELD(R_PORT_PB_I2C, i2c_clk, (x)))

#define i2c_data(x) *R_PORT_PB_I2C = (port_pb_i2c_shadow = (port_pb_i2c_shadow & \
       ~IO_MASK(R_PORT_PB_I2C, i2c_d)) | IO_FIELD(R_PORT_PB_I2C, i2c_d, (x)))

/* read a bit from the i2c interface */

#define i2c_getbit() (*R_PORT_PB_READ & 0x1)
#endif

/* use the kernels delay routine */

#define i2c_delay(usecs) udelay(usecs)


/****************** FUNCTION DEFINITION SECTION *************************/


/* generate i2c start condition */

void
i2c_start(void)
{
	/*
	 * SCL=1 SDA=1
	 */
	i2c_dir_out();
	i2c_delay(CLOCK_HIGH_TIME/6);
	i2c_data(I2C_DATA_HIGH);
	i2c_clk(I2C_CLOCK_HIGH);
	i2c_delay(CLOCK_HIGH_TIME);
	/*
	 * SCL=1 SDA=0
	 */
	i2c_data(I2C_DATA_LOW);
	i2c_delay(START_CONDITION_HOLD_TIME);
	/*
	 * SCL=0 SDA=0
	 */
	i2c_clk(I2C_CLOCK_LOW);
	i2c_delay(CLOCK_LOW_TIME);
}

/* generate i2c stop condition */

void
i2c_stop(void)
{
	i2c_dir_out();

	/*
	 * SCL=0 SDA=0
	 */
	i2c_clk(I2C_CLOCK_LOW);
	i2c_data(I2C_DATA_LOW);
	i2c_delay(CLOCK_LOW_TIME*2);
	/*
	 * SCL=1 SDA=0
	 */
	i2c_clk(I2C_CLOCK_HIGH);
	i2c_delay(CLOCK_HIGH_TIME*2);
	/*
	 * SCL=1 SDA=1
	 */
	i2c_data(I2C_DATA_HIGH);
	i2c_delay(STOP_CONDITION_HOLD_TIME);

	i2c_dir_in();
}

/* write a byte to the i2c interface */

void
i2c_outbyte(unsigned char x)
{
	int i;
	
	i2c_dir_out();

	for (i = 0; i < 8; i++) {
		if (x & 0x80)
			i2c_data(I2C_DATA_HIGH);
		else
			i2c_data(I2C_DATA_LOW);
		
		i2c_delay(CLOCK_LOW_TIME/2);
		i2c_clk(I2C_CLOCK_HIGH);
		i2c_delay(CLOCK_HIGH_TIME);
		i2c_clk(I2C_CLOCK_LOW);
		i2c_delay(CLOCK_LOW_TIME/2);
		x <<= 1;
	}
	i2c_data(I2C_DATA_LOW);
	i2c_delay(CLOCK_LOW_TIME/2);

	/*
	 * enable input
	 */
	i2c_dir_in();
}

/* read a byte from the i2c interface */

unsigned char
i2c_inbyte(void)
{
	unsigned char aBitByte = 0;
	int i;
	int iaa;

	/*
	 * enable output
	 */
	i2c_dir_out();
	/*
	 * Release data bus by setting
	 * data high
	 */
	i2c_data(I2C_DATA_HIGH);
	/*
	 * enable input
	 */
	i2c_dir_in();
	/*
	 * Use PORT PB instead of I2C
	 * for input. (I2C not working)
	 */
	i2c_clk(1);
	i2c_data(1);
	/*
	 * get bits
	 */
	for (i = 0; i < 8; i++) {
		i2c_delay(CLOCK_LOW_TIME/2);
		/*
		 * low clock period
		 */
		i2c_clk(I2C_CLOCK_HIGH);
		/*
		 * switch off I2C
		 */
		i2c_data(1);
		i2c_disable();
		i2c_dir_in();
		/*
		 * wait before getting bit
		 */
		i2c_delay(CLOCK_HIGH_TIME/2);
		aBitByte = (aBitByte << 1);
		iaa = i2c_getbit();
		aBitByte = aBitByte | iaa ;
		/*
		 * wait
		 */
		i2c_delay(CLOCK_HIGH_TIME/2);
		/*
		 * end clock puls
		 */
		i2c_enable();
		i2c_dir_out();
		i2c_clk(I2C_CLOCK_LOW);
		/*
		 * low clock period
		 */
		i2c_delay(CLOCK_LOW_TIME/2);
	}
	i2c_dir_out();
	return aBitByte;
}

/*#---------------------------------------------------------------------------
*#
*# FUNCTION NAME: i2c_getack
*#
*# DESCRIPTION  : checks if ack was received from ic2
*#
*#--------------------------------------------------------------------------*/

int
i2c_getack(void)
{
	int ack = 1;
	/*
	 * enable output
	 */
	i2c_dir_out();
	/*
	 * Release data bus by setting
	 * data high
	 */
	i2c_data(I2C_DATA_HIGH);
	/*
	 * enable input
	 */
	i2c_dir_in();
	i2c_delay(CLOCK_HIGH_TIME/4);
	/*
	 * generate ACK clock pulse
	 */
	i2c_clk(I2C_CLOCK_HIGH);
	/*
	 * Use PORT PB instead of I2C
	 * for input. (I2C not working)
	 */
	i2c_clk(1);
	i2c_data(1);
	/*
	 * switch off I2C
	 */
	i2c_data(1);
	i2c_disable();
	i2c_dir_in();
	/*
	 * now wait for ack
	 */
	i2c_delay(CLOCK_HIGH_TIME/2);
	/*
	 * check for ack
	 */
	if(i2c_getbit())
		ack = 0;
	i2c_delay(CLOCK_HIGH_TIME/2);
	if(!ack){
		if(!i2c_getbit()) /* receiver pulld SDA low */
			ack = 1;
		i2c_delay(CLOCK_HIGH_TIME/2);
	}

	/*
	 * end clock pulse
	 */
	i2c_enable();
	i2c_dir_out();
	i2c_clk(I2C_CLOCK_LOW);
	i2c_delay(CLOCK_HIGH_TIME/4);
	/*
	 * enable output
	 */
	i2c_dir_out();
	/*
	 * remove ACK clock pulse
	 */
	i2c_data(I2C_DATA_HIGH);
	i2c_delay(CLOCK_LOW_TIME/2);
	return ack;
}

/*#---------------------------------------------------------------------------
*#
*# FUNCTION NAME: I2C::sendAck
*#
*# DESCRIPTION  : Send ACK on received data
*#
*#--------------------------------------------------------------------------*/
void
i2c_sendack(void)
{
	/*
	 * enable output
	 */
	i2c_delay(CLOCK_LOW_TIME);
	i2c_dir_out();
	/*
	 * set ack pulse high
	 */
	i2c_data(I2C_DATA_LOW);
	/*
	 * generate clock pulse
	 */
	i2c_delay(CLOCK_HIGH_TIME/6);
	i2c_clk(I2C_CLOCK_HIGH);
	i2c_delay(CLOCK_HIGH_TIME);
	i2c_clk(I2C_CLOCK_LOW);
	i2c_delay(CLOCK_LOW_TIME/6);
	/*
	 * reset data out
	 */
	i2c_data(I2C_DATA_HIGH);
	i2c_delay(CLOCK_LOW_TIME);
	
	i2c_dir_in();
}

/*#---------------------------------------------------------------------------
*#
*# FUNCTION NAME: i2c_writereg
*#
*# DESCRIPTION  : Writes a value to an I2C device
*#
*#--------------------------------------------------------------------------*/
int
i2c_writereg(unsigned char theSlave, unsigned char theReg,
	     unsigned char theValue)
{
	int error, cntr = 3;
	unsigned long flags;
		
	do {
		error = 0;
		/*
		 * we don't like to be interrupted
		 */
		save_flags(flags);
		cli();
		/*
		 * generate start condition
		 */
		i2c_start();
		/*
		 * dummy preamble
		 */
		i2c_outbyte(0x01);
		i2c_data(I2C_DATA_HIGH);
		i2c_clk(I2C_CLOCK_HIGH);
		i2c_delay(CLOCK_HIGH_TIME); /* Dummy Acknowledge */
		i2c_clk(I2C_CLOCK_LOW);
		i2c_delay(CLOCK_LOW_TIME);
		i2c_clk(I2C_CLOCK_HIGH);
		i2c_delay(CLOCK_LOW_TIME); /* Repeated Start Condition */
		i2c_data(I2C_DATA_LOW);
		i2c_delay(CLOCK_HIGH_TIME);
		i2c_clk(I2C_CLOCK_LOW);
		i2c_delay(CLOCK_LOW_TIME);

		i2c_start();
		/*
		 * send slave address
		 */
		i2c_outbyte(theSlave);
		/*
		 * wait for ack
		 */
		if(!i2c_getack())
			error = 1;
		/*
		 * now select register
		 */
		i2c_dir_out();
		i2c_outbyte(theReg);
		/*
		 * now it's time to wait for ack
		 */
		if(!i2c_getack())
			error |= 2;
		/*
		 * send register register data
		 */
		i2c_outbyte(theValue);
		/*
		 * now it's time to wait for ack
		 */
		if(!i2c_getack())
			error |= 4;
		/*
		 * end byte stream
		 */
		i2c_stop();
		/*
		 * enable interrupt again
		 */
		restore_flags(flags);
		
	} while(error && cntr--);
	
	i2c_delay(CLOCK_LOW_TIME);
	
	return -error;
}

/*#---------------------------------------------------------------------------
*#
*# FUNCTION NAME: i2c_readreg
*#
*# DESCRIPTION  : Reads a value from the decoder registers.
*#
*#--------------------------------------------------------------------------*/
unsigned char
i2c_readreg(unsigned char theSlave, unsigned char theReg)
{
	unsigned char b = 0;
	int error, cntr = 3;
	unsigned long flags;
		
	do {
		error = 0;
		/*
		 * we don't like to be interrupted
		 */
		save_flags(flags);
		cli();
		/*
		 * generate start condition
		 */
		i2c_start();
		/*
		 * dummy preamble
		 */
		i2c_outbyte(0x01);
		i2c_data(I2C_DATA_HIGH);
		i2c_clk(I2C_CLOCK_HIGH);
		i2c_delay(CLOCK_HIGH_TIME); /* Dummy Acknowledge */
		i2c_clk(I2C_CLOCK_LOW);
		i2c_delay(CLOCK_LOW_TIME);
		i2c_clk(I2C_CLOCK_HIGH);
		i2c_delay(CLOCK_LOW_TIME); /* Repeated Start Condition */
		i2c_data(I2C_DATA_LOW);
		i2c_delay(CLOCK_HIGH_TIME);
		i2c_clk(I2C_CLOCK_LOW);
		i2c_delay(CLOCK_LOW_TIME);
    
		i2c_start();
    
		/*
		 * send slave address
		 */
		i2c_outbyte(theSlave);
		/*
		 * wait for ack
		 */
		if(!i2c_getack())
			error = 1;
		/*
		 * now select register
		 */
		i2c_dir_out();
		i2c_outbyte(theReg);
		/*
		 * now it's time to wait for ack
		 */
		if(!i2c_getack())
			error = 1;
		/*
		 * repeat start condition
		 */
		i2c_delay(CLOCK_LOW_TIME);
		i2c_start();
		/*
		 * send slave address
		 */
		i2c_outbyte(theSlave | 0x01);
		/*
		 * wait for ack
		 */
		if(!i2c_getack())
			error = 1;
		/*
		 * fetch register
		 */
		b = i2c_inbyte();
		/*
		 * send Ack
		 */
		i2c_sendack();
		/*
		 * end sequence
		 */
		i2c_stop();
		/*
		 * enable interrupt again
		 */
		restore_flags(flags);
		
	} while(error && cntr--);

	return b;
}

static int
i2c_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int
i2c_release(struct inode *inode, struct file *filp)
{
	return 0;
}

/* Main device API. ioctl's to write or read to/from i2c registers.
 */

static int
i2c_ioctl(struct inode *inode, struct file *file,
	  unsigned int cmd, unsigned long arg)
{
	if(_IOC_TYPE(cmd) != ETRAXI2C_IOCTYPE) {
		return -EINVAL;
	}

	switch (_IOC_NR(cmd)) {
		case I2C_WRITEREG:
			/* write to an i2c slave */
			D(printk("i2cw %d %d %d\n", 
				 I2C_ARGSLAVE(arg),
				 I2C_ARGREG(arg),
				 I2C_ARGVALUE(arg)));

			return i2c_writereg(I2C_ARGSLAVE(arg),
					    I2C_ARGREG(arg),
					    I2C_ARGVALUE(arg));
		case I2C_READREG:
		{
			unsigned char val;
			/* read from an i2c slave */
			D(printk("i2cr %d %d ", 
				I2C_ARGSLAVE(arg),
				I2C_ARGREG(arg)));
			val = i2c_readreg(I2C_ARGSLAVE(arg), I2C_ARGREG(arg));
			D(printk("= %d\n", val));
			return val;
		}					    
		default:
			return -EINVAL;

	}
	
	return 0;
}

static struct file_operations i2c_fops = {
	owner:    THIS_MODULE,
	ioctl:    i2c_ioctl,
	open:     i2c_open,
	release:  i2c_release,
};

static int __init
i2c_init(void)
{
	int res;

	/* Setup and enable the Port B I2C interface */

#ifndef CONFIG_ETRAX_I2C_USES_PB_NOT_PB_I2C
	*R_PORT_PB_I2C = port_pb_i2c_shadow |= 
		IO_STATE(R_PORT_PB_I2C, i2c_en,  on) |
		IO_FIELD(R_PORT_PB_I2C, i2c_d,   1)  |
		IO_FIELD(R_PORT_PB_I2C, i2c_clk, 1)  |
		IO_STATE(R_PORT_PB_I2C, i2c_oe_, enable);
#endif

	port_pb_dir_shadow &= ~IO_MASK(R_PORT_PB_DIR, dir0);
	port_pb_dir_shadow &= ~IO_MASK(R_PORT_PB_DIR, dir1);

	*R_PORT_PB_DIR = (port_pb_dir_shadow |=
			  IO_STATE(R_PORT_PB_DIR, dir0, input)  |
			  IO_STATE(R_PORT_PB_DIR, dir1, output));

	/* register char device */

	res = register_chrdev(I2C_MAJOR, i2c_name, &i2c_fops);
	if(res < 0) {
		printk(KERN_ERR "i2c: couldn't get a major number.\n");
		return res;
	}

	printk("I2C driver v2.2, (c) 1999-2001 Axis Communications AB\n");
	
	return 0;
}

/* this makes sure that i2c_init is called during boot */

module_init(i2c_init);

/****************** END OF FILE i2c.c ********************************/
