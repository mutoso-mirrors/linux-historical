/*
 * budget-patch.c: driver for Budget Patch,
 * hardware modification of DVB-S cards enabling full TS
 *
 * Written by Emard <emard@softhome.net>
 *
 * Original idea by Roberto Deza <rdeza@unav.es>
 *
 * Special thanks to Holger Waechtler, Michael Hunold, Marian Durkovic
 * and Metzlerbros
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 * 
 *
 * the project's page is at http://www.linuxtv.org/dvb/
 */

#include "av7110.h"
#include "av7110_hw.h"
#include "budget.h"
#include "stv0299.h"
#include "ves1x93.h"
#include "tda8083.h"

#define budget_patch budget

static struct saa7146_extension budget_extension;

MAKE_BUDGET_INFO(fs_1_3,"Siemens/Technotrend/Hauppauge PCI rev1.3+Budget_Patch", BUDGET_PATCH);

static struct pci_device_id pci_tbl[] = {
        MAKE_EXTENSION_PCI(fs_1_3,0x13c2, 0x0000),
        {
                .vendor    = 0,
        }
};

static int budget_wdebi(struct budget_patch *budget, u32 config, int addr, u32 val, int count)
{
        struct saa7146_dev *dev=budget->dev;

        dprintk(2, "budget: %p\n", budget);

        if (count <= 0 || count > 4)
                return -1;

        saa7146_write(dev, DEBI_CONFIG, config);

        saa7146_write(dev, DEBI_AD, val );
        saa7146_write(dev, DEBI_COMMAND, (count << 17) | (addr & 0xffff));
        saa7146_write(dev, MC2, (2 << 16) | 2);
        mdelay(5);

        return 0;
}


static int budget_av7110_send_fw_cmd(struct budget_patch *budget, u16* buf, int length)
{
        int i;

        dprintk(2, "budget: %p\n", budget);

        for (i = 2; i < length; i++)
                budget_wdebi(budget, DEBINOSWAP, COMMAND + 2*i, (u32) buf[i], 2);

        if (length)
                budget_wdebi(budget, DEBINOSWAP, COMMAND + 2, (u32) buf[1], 2);
        else
                budget_wdebi(budget, DEBINOSWAP, COMMAND + 2, 0, 2);

        budget_wdebi(budget, DEBINOSWAP, COMMAND, (u32) buf[0], 2);
        return 0;
}


static void av7110_set22k(struct budget_patch *budget, int state)
{
        u16 buf[2] = {( COMTYPE_AUDIODAC << 8) | (state ? ON22K : OFF22K), 0};
        
        dprintk(2, "budget: %p\n", budget);
        budget_av7110_send_fw_cmd(budget, buf, 2);
}


static int av7110_send_diseqc_msg(struct budget_patch *budget, int len, u8 *msg, int burst)
{
        int i;
        u16 buf[18] = { ((COMTYPE_AUDIODAC << 8) | SendDiSEqC),
                16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

        dprintk(2, "budget: %p\n", budget);

        if (len>10)
                len=10;

        buf[1] = len+2;
        buf[2] = len;

        if (burst != -1)
                buf[3]=burst ? 0x01 : 0x00;
        else
                buf[3]=0xffff;
                
        for (i=0; i<len; i++)
                buf[i+4]=msg[i];

        budget_av7110_send_fw_cmd(budget, buf, 18);
        return 0;
}

static int budget_patch_set_tone(struct dvb_frontend* fe, fe_sec_tone_mode_t tone)
{
	struct budget_patch* budget = (struct budget_patch*) fe->dvb->priv;

	switch (tone) {
                case SEC_TONE_ON:
                        av7110_set22k (budget, 1);
                        break;
                case SEC_TONE_OFF:
                        av7110_set22k (budget, 0);
                        break;
                default:
                        return -EINVAL;
                }

	return 0;
}

static int budget_patch_diseqc_send_master_cmd(struct dvb_frontend* fe, struct dvb_diseqc_master_cmd* cmd)
        {
	struct budget_patch* budget = (struct budget_patch*) fe->dvb->priv;

                av7110_send_diseqc_msg (budget, cmd->msg_len, cmd->msg, 0);

	return 0;
        }

static int budget_patch_diseqc_send_burst(struct dvb_frontend* fe, fe_sec_mini_cmd_t minicmd)
{
	struct budget_patch* budget = (struct budget_patch*) fe->dvb->priv;

	av7110_send_diseqc_msg (budget, 0, NULL, minicmd);

	return 0;
        }

static int alps_bsrv2_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct budget_patch* budget = (struct budget_patch*) fe->dvb->priv;
	u8 pwr = 0;
	u8 buf[4];
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = buf, .len = sizeof(buf) };
	u32 div = (params->frequency + 479500) / 125;

	if (params->frequency > 2000000) pwr = 3;
	else if (params->frequency > 1800000) pwr = 2;
	else if (params->frequency > 1600000) pwr = 1;
	else if (params->frequency > 1200000) pwr = 0;
	else if (params->frequency >= 1100000) pwr = 1;
	else pwr = 2;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = ((div & 0x18000) >> 10) | 0x95;
	buf[3] = (pwr << 6) | 0x30;

        // NOTE: since we're using a prescaler of 2, we set the
	// divisor frequency to 62.5kHz and divide by 125 above

	if (i2c_transfer (&budget->i2c_adap, &msg, 1) != 1) return -EIO;
	return 0;
}

static struct ves1x93_config alps_bsrv2_config = {
	.demod_address = 0x08,
	.xin = 90100000UL,
	.invert_pwm = 0,
	.pll_set = alps_bsrv2_pll_set,
};

static u8 alps_bsru6_inittab[] = {
	0x01, 0x15,
	0x02, 0x00,
	0x03, 0x00,
        0x04, 0x7d,   /* F22FR = 0x7d, F22 = f_VCO / 128 / 0x7d = 22 kHz */
	0x05, 0x35,   /* I2CT = 0, SCLT = 1, SDAT = 1 */
	0x06, 0x40,   /* DAC not used, set to high impendance mode */
	0x07, 0x00,   /* DAC LSB */
	0x08, 0x40,   /* DiSEqC off, LNB power on OP2/LOCK pin on */
	0x09, 0x00,   /* FIFO */
	0x0c, 0x51,   /* OP1 ctl = Normal, OP1 val = 1 (LNB Power ON) */
	0x0d, 0x82,   /* DC offset compensation = ON, beta_agc1 = 2 */
	0x0e, 0x23,   /* alpha_tmg = 2, beta_tmg = 3 */
	0x10, 0x3f,   // AGC2  0x3d
	0x11, 0x84,
	0x12, 0xb5,   // Lock detect: -64  Carrier freq detect:on
	0x15, 0xc9,   // lock detector threshold
	0x16, 0x00,
	0x17, 0x00,
	0x18, 0x00,
	0x19, 0x00,
	0x1a, 0x00,
	0x1f, 0x50,
	0x20, 0x00,
	0x21, 0x00,
	0x22, 0x00,
	0x23, 0x00,
	0x28, 0x00,  // out imp: normal  out type: parallel FEC mode:0
	0x29, 0x1e,  // 1/2 threshold
	0x2a, 0x14,  // 2/3 threshold
	0x2b, 0x0f,  // 3/4 threshold
	0x2c, 0x09,  // 5/6 threshold
	0x2d, 0x05,  // 7/8 threshold
	0x2e, 0x01,
	0x31, 0x1f,  // test all FECs
	0x32, 0x19,  // viterbi and synchro search
	0x33, 0xfc,  // rs control
	0x34, 0x93,  // error control
	0x0f, 0x52,
	0xff, 0xff
};

static int alps_bsru6_set_symbol_rate(struct dvb_frontend* fe, u32 srate, u32 ratio)
{
	u8 aclk = 0;
	u8 bclk = 0;

	if (srate < 1500000) { aclk = 0xb7; bclk = 0x47; }
	else if (srate < 3000000) { aclk = 0xb7; bclk = 0x4b; }
	else if (srate < 7000000) { aclk = 0xb7; bclk = 0x4f; }
	else if (srate < 14000000) { aclk = 0xb7; bclk = 0x53; }
	else if (srate < 30000000) { aclk = 0xb6; bclk = 0x53; }
	else if (srate < 45000000) { aclk = 0xb4; bclk = 0x51; }

	stv0299_writereg (fe, 0x13, aclk);
	stv0299_writereg (fe, 0x14, bclk);
	stv0299_writereg (fe, 0x1f, (ratio >> 16) & 0xff);
	stv0299_writereg (fe, 0x20, (ratio >>  8) & 0xff);
	stv0299_writereg (fe, 0x21, (ratio      ) & 0xf0);

        return 0;
}

static int alps_bsru6_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct budget_patch* budget = (struct budget_patch*) fe->dvb->priv;
	u8 data[4];
	u32 div;
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = data, .len = sizeof(data) };

	if ((params->frequency < 950000) || (params->frequency > 2150000)) return -EINVAL;

	div = (params->frequency + (125 - 1)) / 125; // round correctly
	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0x80 | ((div & 0x18000) >> 10) | 4;
	data[3] = 0xC4;

	if (params->frequency > 1530000) data[3] = 0xc0;

	if (i2c_transfer (&budget->i2c_adap, &msg, 1) != 1) return -EIO;
	return 0;
}

static struct stv0299_config alps_bsru6_config = {

	.demod_address = 0x68,
	.inittab = alps_bsru6_inittab,
	.mclk = 88000000UL,
	.invert = 1,
	.enhanced_tuning = 0,
	.skip_reinit = 0,
	.lock_output = STV0229_LOCKOUTPUT_1,
	.volt13_op0_op1 = STV0299_VOLT13_OP1,
	.min_delay_ms = 100,
	.set_symbol_rate = alps_bsru6_set_symbol_rate,
	.pll_set = alps_bsru6_pll_set,
};

static int grundig_29504_451_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct budget_patch* budget = (struct budget_patch*) fe->dvb->priv;
	u32 div;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = data, .len = sizeof(data) };

	div = params->frequency / 125;
	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0x8e;
	data[3] = 0x00;

	if (i2c_transfer (&budget->i2c_adap, &msg, 1) != 1) return -EIO;
	return 0;
}

struct tda8083_config grundig_29504_451_config = {
	.demod_address = 0x68,
	.pll_set = grundig_29504_451_pll_set,
};

static void frontend_init(struct budget_patch* budget)
{
	switch(budget->dev->pci->subsystem_device) {
	case 0x0000: // Hauppauge/TT WinTV DVB-S rev1.X

		// try the ALPS BSRV2 first of all
		budget->dvb_frontend = ves1x93_attach(&alps_bsrv2_config, &budget->i2c_adap);
		if (budget->dvb_frontend) {
			budget->dvb_frontend->ops->diseqc_send_master_cmd = budget_patch_diseqc_send_master_cmd;
			budget->dvb_frontend->ops->diseqc_send_burst = budget_patch_diseqc_send_burst;
			budget->dvb_frontend->ops->set_tone = budget_patch_set_tone;
			break;
		}

		// try the ALPS BSRU6 now
		budget->dvb_frontend = stv0299_attach(&alps_bsru6_config, &budget->i2c_adap);
		if (budget->dvb_frontend) {
			budget->dvb_frontend->ops->diseqc_send_master_cmd = budget_patch_diseqc_send_master_cmd;
			budget->dvb_frontend->ops->diseqc_send_burst = budget_patch_diseqc_send_burst;
			budget->dvb_frontend->ops->set_tone = budget_patch_set_tone;
			break;
		}

		// Try the grundig 29504-451
		budget->dvb_frontend = tda8083_attach(&grundig_29504_451_config, &budget->i2c_adap);
		if (budget->dvb_frontend) {
			budget->dvb_frontend->ops->diseqc_send_master_cmd = budget_patch_diseqc_send_master_cmd;
			budget->dvb_frontend->ops->diseqc_send_burst = budget_patch_diseqc_send_burst;
			budget->dvb_frontend->ops->set_tone = budget_patch_set_tone;
			break;
		}
		break;
	}

	if (budget->dvb_frontend == NULL) {
		printk("dvb-ttpci: A frontend driver was not found for device %04x/%04x subsystem %04x/%04x\n",
		       budget->dev->pci->vendor,
		       budget->dev->pci->device,
		       budget->dev->pci->subsystem_vendor,
		       budget->dev->pci->subsystem_device);
	} else {
		if (dvb_register_frontend(budget->dvb_adapter, budget->dvb_frontend)) {
			printk("budget-av: Frontend registration failed!\n");
			if (budget->dvb_frontend->ops->release)
				budget->dvb_frontend->ops->release(budget->dvb_frontend);
			budget->dvb_frontend = NULL;
		}
	}
}

static int budget_patch_attach (struct saa7146_dev* dev, struct saa7146_pci_extension_data *info)
{
        struct budget_patch *budget;
        int err;
	int count = 0;

        if (!(budget = kmalloc (sizeof(struct budget_patch), GFP_KERNEL)))
                return -ENOMEM;

        dprintk(2, "budget: %p\n", budget);

        if ((err = ttpci_budget_init (budget, dev, info, THIS_MODULE))) {
                kfree (budget);
                return err;
        }

/*
**      This code will setup the SAA7146_RPS1 to generate a square 
**      wave on GPIO3, changing when a field (TS_HEIGHT/2 "lines" of 
**      TS_WIDTH packets) has been acquired on SAA7146_D1B video port; 
**      then, this GPIO3 output which is connected to the D1B_VSYNC 
**      input, will trigger the acquisition of the alternate field 
**      and so on.
**      Currently, the TT_budget / WinTV_Nova cards have two ICs 
**      (74HCT4040, LVC74) for the generation of this VSYNC signal, 
**      which seems that can be done perfectly without this :-)).
*/                                                      

	// Setup RPS1 "program" (p35)

        // Wait reset Source Line Counter Threshold                     (p36)
        WRITE_RPS1(cpu_to_le32(CMD_PAUSE | RPS_INV | EVT_HS));
        // Wait Source Line Counter Threshold                           (p36)
        WRITE_RPS1(cpu_to_le32(CMD_PAUSE | EVT_HS));
        // Set GPIO3=1                                                  (p42)
        WRITE_RPS1(cpu_to_le32(CMD_WR_REG_MASK | (GPIO_CTRL>>2)));
        WRITE_RPS1(cpu_to_le32(GPIO3_MSK));
        WRITE_RPS1(cpu_to_le32(SAA7146_GPIO_OUTHI<<24));
        // Wait reset Source Line Counter Threshold                     (p36)
        WRITE_RPS1(cpu_to_le32(CMD_PAUSE | RPS_INV | EVT_HS));
        // Wait Source Line Counter Threshold
        WRITE_RPS1(cpu_to_le32(CMD_PAUSE | EVT_HS));
        // Set GPIO3=0                                                  (p42)
        WRITE_RPS1(cpu_to_le32(CMD_WR_REG_MASK | (GPIO_CTRL>>2)));
        WRITE_RPS1(cpu_to_le32(GPIO3_MSK));
        WRITE_RPS1(cpu_to_le32(SAA7146_GPIO_OUTLO<<24));
        // Jump to begin of RPS program                                 (p37)
        WRITE_RPS1(cpu_to_le32(CMD_JUMP));
        WRITE_RPS1(cpu_to_le32(dev->d_rps1.dma_handle));

        // Fix VSYNC level
        saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTLO);
        // Set RPS1 Address register to point to RPS code               (r108 p42)
        saa7146_write(dev, RPS_ADDR1, dev->d_rps1.dma_handle);
        // Set Source Line Counter Threshold, using BRS                 (rCC p43)
        saa7146_write(dev, RPS_THRESH1, ((TS_HEIGHT/2) | MASK_12));
        // Enable RPS1                                                  (rFC p33)
        saa7146_write(dev, MC1, (MASK_13 | MASK_29));

        dev->ext_priv = budget;

	budget->dvb_adapter->priv = budget;
	frontend_init(budget);

        return 0;
}


static int budget_patch_detach (struct saa7146_dev* dev)
{
        struct budget_patch *budget = (struct budget_patch*) dev->ext_priv;
        int err;

	if (budget->dvb_frontend) dvb_unregister_frontend(budget->dvb_frontend);

        err = ttpci_budget_deinit (budget);

        kfree (budget);

        return err;
}


static int __init budget_patch_init(void) 
{
	return saa7146_register_extension(&budget_extension);
}

static void __exit budget_patch_exit(void)
{
        saa7146_unregister_extension(&budget_extension); 
}


static struct saa7146_extension budget_extension = {
        .name           = "budget_patch dvb\0",
        .flags          = 0,
        
        .module         = THIS_MODULE,
        .pci_tbl        = pci_tbl,
        .attach         = budget_patch_attach,
        .detach         = budget_patch_detach,

        .irq_mask       = MASK_10,
        .irq_func       = ttpci_budget_irq10_handler,
};


module_init(budget_patch_init);
module_exit(budget_patch_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emard, Roberto Deza, Holger Waechtler, Michael Hunold, others");
MODULE_DESCRIPTION("Driver for full TS modified DVB-S SAA7146+AV7110 "
                   "based so-called Budget Patch cards");

