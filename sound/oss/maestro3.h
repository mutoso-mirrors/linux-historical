/*
 *      ESS Technology allegro audio driver.
 *
 *      Copyright (C) 1992-2000  Don Kim (don.kim@esstech.com)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *      Hacked for the maestro3 driver by zab
 */

// Allegro PCI configuration registers
#define PCI_LEGACY_AUDIO_CTRL   0x40
#define SOUND_BLASTER_ENABLE    0x00000001
#define FM_SYNTHESIS_ENABLE     0x00000002
#define GAME_PORT_ENABLE        0x00000004
#define MPU401_IO_ENABLE        0x00000008
#define MPU401_IRQ_ENABLE       0x00000010
#define ALIAS_10BIT_IO          0x00000020
#define SB_DMA_MASK             0x000000C0
#define SB_DMA_0                0x00000040
#define SB_DMA_1                0x00000040
#define SB_DMA_R                0x00000080
#define SB_DMA_3                0x000000C0
#define SB_IRQ_MASK             0x00000700
#define SB_IRQ_5                0x00000000
#define SB_IRQ_7                0x00000100
#define SB_IRQ_9                0x00000200
#define SB_IRQ_10               0x00000300
#define MIDI_IRQ_MASK           0x00003800
#define SERIAL_IRQ_ENABLE       0x00004000
#define DISABLE_LEGACY          0x00008000

#define PCI_ALLEGRO_CONFIG      0x50
#define SB_ADDR_240             0x00000004
#define MPU_ADDR_MASK           0x00000018
#define MPU_ADDR_330            0x00000000
#define MPU_ADDR_300            0x00000008
#define MPU_ADDR_320            0x00000010
#define MPU_ADDR_340            0x00000018
#define USE_PCI_TIMING          0x00000040
#define POSTED_WRITE_ENABLE     0x00000080
#define DMA_POLICY_MASK         0x00000700
#define DMA_DDMA                0x00000000
#define DMA_TDMA                0x00000100
#define DMA_PCPCI               0x00000200
#define DMA_WBDMA16             0x00000400
#define DMA_WBDMA4              0x00000500
#define DMA_WBDMA2              0x00000600
#define DMA_WBDMA1              0x00000700
#define DMA_SAFE_GUARD          0x00000800
#define HI_PERF_GP_ENABLE       0x00001000
#define PIC_SNOOP_MODE_0        0x00002000
#define PIC_SNOOP_MODE_1        0x00004000
#define SOUNDBLASTER_IRQ_MASK   0x00008000
#define RING_IN_ENABLE          0x00010000
#define SPDIF_TEST_MODE         0x00020000
#define CLK_MULT_MODE_SELECT_2  0x00040000
#define EEPROM_WRITE_ENABLE     0x00080000
#define CODEC_DIR_IN            0x00100000
#define HV_BUTTON_FROM_GD       0x00200000
#define REDUCED_DEBOUNCE        0x00400000
#define HV_CTRL_ENABLE          0x00800000
#define SPDIF_ENABLE            0x01000000
#define CLK_DIV_SELECT          0x06000000
#define CLK_DIV_BY_48           0x00000000
#define CLK_DIV_BY_49           0x02000000
#define CLK_DIV_BY_50           0x04000000
#define CLK_DIV_RESERVED        0x06000000
#define PM_CTRL_ENABLE          0x08000000
#define CLK_MULT_MODE_SELECT    0x30000000
#define CLK_MULT_MODE_SHIFT     28
#define CLK_MULT_MODE_0         0x00000000
#define CLK_MULT_MODE_1         0x10000000
#define CLK_MULT_MODE_2         0x20000000
#define CLK_MULT_MODE_3         0x30000000
#define INT_CLK_SELECT          0x40000000
#define INT_CLK_MULT_RESET      0x80000000

// M3
#define INT_CLK_SRC_NOT_PCI     0x00100000
#define INT_CLK_MULT_ENABLE     0x80000000

#define PCI_ACPI_CONTROL        0x54
#define PCI_ACPI_D0             0x00000000
#define PCI_ACPI_D1             0xB4F70000
#define PCI_ACPI_D2             0xB4F7B4F7

#define PCI_USER_CONFIG         0x58
#define EXT_PCI_MASTER_ENABLE   0x00000001
#define SPDIF_OUT_SELECT        0x00000002
#define TEST_PIN_DIR_CTRL       0x00000004
#define AC97_CODEC_TEST         0x00000020
#define TRI_STATE_BUFFER        0x00000080
#define IN_CLK_12MHZ_SELECT     0x00000100
#define MULTI_FUNC_DISABLE      0x00000200
#define EXT_MASTER_PAIR_SEL     0x00000400
#define PCI_MASTER_SUPPORT      0x00000800
#define STOP_CLOCK_ENABLE       0x00001000
#define EAPD_DRIVE_ENABLE       0x00002000
#define REQ_TRI_STATE_ENABLE    0x00004000
#define REQ_LOW_ENABLE          0x00008000
#define MIDI_1_ENABLE           0x00010000
#define MIDI_2_ENABLE           0x00020000
#define SB_AUDIO_SYNC           0x00040000
#define HV_CTRL_TEST            0x00100000
#define SOUNDBLASTER_TEST       0x00400000

#define PCI_USER_CONFIG_C       0x5C

#define PCI_DDMA_CTRL           0x60
#define DDMA_ENABLE             0x00000001


// Allegro registers
#define HOST_INT_CTRL           0x18
#define SB_INT_ENABLE           0x0001
#define MPU401_INT_ENABLE       0x0002
#define ASSP_INT_ENABLE         0x0010
#define RING_INT_ENABLE         0x0020
#define HV_INT_ENABLE           0x0040
#define CLKRUN_GEN_ENABLE       0x0100
#define HV_CTRL_TO_PME          0x0400
#define SOFTWARE_RESET_ENABLE   0x8000

/*
 * should be using the above defines, probably.
 */
#define REGB_ENABLE_RESET               0x01
#define REGB_STOP_CLOCK                 0x10

#define HOST_INT_STATUS         0x1A
#define SB_INT_PENDING          0x01
#define MPU401_INT_PENDING      0x02
#define ASSP_INT_PENDING        0x10
#define RING_INT_PENDING        0x20
#define HV_INT_PENDING          0x40

#define HARDWARE_VOL_CTRL       0x1B
#define SHADOW_MIX_REG_VOICE    0x1C
#define HW_VOL_COUNTER_VOICE    0x1D
#define SHADOW_MIX_REG_MASTER   0x1E
#define HW_VOL_COUNTER_MASTER   0x1F

#define CODEC_COMMAND           0x30
#define CODEC_READ_B            0x80

#define CODEC_STATUS            0x30
#define CODEC_BUSY_B            0x01

#define CODEC_DATA              0x32

#define RING_BUS_CTRL_A         0x36
#define RAC_PME_ENABLE          0x0100
#define RAC_SDFS_ENABLE         0x0200
#define LAC_PME_ENABLE          0x0400
#define LAC_SDFS_ENABLE         0x0800
#define SERIAL_AC_LINK_ENABLE   0x1000
#define IO_SRAM_ENABLE          0x2000
#define IIS_INPUT_ENABLE        0x8000

#define RING_BUS_CTRL_B         0x38
#define SECOND_CODEC_ID_MASK    0x0003
#define SPDIF_FUNC_ENABLE       0x0010
#define SECOND_AC_ENABLE        0x0020
#define SB_MODULE_INTF_ENABLE   0x0040
#define SSPE_ENABLE             0x0040
#define M3I_DOCK_ENABLE         0x0080

#define SDO_OUT_DEST_CTRL       0x3A
#define COMMAND_ADDR_OUT        0x0003
#define PCM_LR_OUT_LOCAL        0x0000
#define PCM_LR_OUT_REMOTE       0x0004
#define PCM_LR_OUT_MUTE         0x0008
#define PCM_LR_OUT_BOTH         0x000C
#define LINE1_DAC_OUT_LOCAL     0x0000
#define LINE1_DAC_OUT_REMOTE    0x0010
#define LINE1_DAC_OUT_MUTE      0x0020
#define LINE1_DAC_OUT_BOTH      0x0030
#define PCM_CLS_OUT_LOCAL       0x0000
#define PCM_CLS_OUT_REMOTE      0x0040
#define PCM_CLS_OUT_MUTE        0x0080
#define PCM_CLS_OUT_BOTH        0x00C0
#define PCM_RLF_OUT_LOCAL       0x0000
#define PCM_RLF_OUT_REMOTE      0x0100
#define PCM_RLF_OUT_MUTE        0x0200
#define PCM_RLF_OUT_BOTH        0x0300
#define LINE2_DAC_OUT_LOCAL     0x0000
#define LINE2_DAC_OUT_REMOTE    0x0400
#define LINE2_DAC_OUT_MUTE      0x0800
#define LINE2_DAC_OUT_BOTH      0x0C00
#define HANDSET_OUT_LOCAL       0x0000
#define HANDSET_OUT_REMOTE      0x1000
#define HANDSET_OUT_MUTE        0x2000
#define HANDSET_OUT_BOTH        0x3000
#define IO_CTRL_OUT_LOCAL       0x0000
#define IO_CTRL_OUT_REMOTE      0x4000
#define IO_CTRL_OUT_MUTE        0x8000
#define IO_CTRL_OUT_BOTH        0xC000

#define SDO_IN_DEST_CTRL        0x3C
#define STATUS_ADDR_IN          0x0003
#define PCM_LR_IN_LOCAL         0x0000
#define PCM_LR_IN_REMOTE        0x0004
#define PCM_LR_RESERVED         0x0008
#define PCM_LR_IN_BOTH          0x000C
#define LINE1_ADC_IN_LOCAL      0x0000
#define LINE1_ADC_IN_REMOTE     0x0010
#define LINE1_ADC_IN_MUTE       0x0020
#define MIC_ADC_IN_LOCAL        0x0000
#define MIC_ADC_IN_REMOTE       0x0040
#define MIC_ADC_IN_MUTE         0x0080
#define LINE2_DAC_IN_LOCAL      0x0000
#define LINE2_DAC_IN_REMOTE     0x0400
#define LINE2_DAC_IN_MUTE       0x0800
#define HANDSET_IN_LOCAL        0x0000
#define HANDSET_IN_REMOTE       0x1000
#define HANDSET_IN_MUTE         0x2000
#define IO_STATUS_IN_LOCAL      0x0000
#define IO_STATUS_IN_REMOTE     0x4000

#define SPDIF_IN_CTRL           0x3E
#define SPDIF_IN_ENABLE         0x0001

#define GPIO_DATA               0x60
#define GPIO_DATA_MASK          0x0FFF
#define GPIO_HV_STATUS          0x3000
#define GPIO_PME_STATUS         0x4000

#define GPIO_MASK               0x64
#define GPIO_DIRECTION          0x68
#define GPO_PRIMARY_AC97        0x0001
#define GPI_LINEOUT_SENSE       0x0004
#define GPO_SECONDARY_AC97      0x0008
#define GPI_VOL_DOWN            0x0010
#define GPI_VOL_UP              0x0020
#define GPI_IIS_CLK             0x0040
#define GPI_IIS_LRCLK           0x0080
#define GPI_IIS_DATA            0x0100
#define GPI_DOCKING_STATUS      0x0100
#define GPI_HEADPHONE_SENSE     0x0200
#define GPO_EXT_AMP_SHUTDOWN    0x1000

// M3
#define GPO_M3_EXT_AMP_SHUTDN   0x0002

#define ASSP_INDEX_PORT         0x80
#define ASSP_MEMORY_PORT        0x82
#define ASSP_DATA_PORT          0x84

#define MPU401_DATA_PORT        0x98
#define MPU401_STATUS_PORT      0x99

#define CLK_MULT_DATA_PORT      0x9C

#define ASSP_CONTROL_A          0xA2
#define ASSP_0_WS_ENABLE        0x01
#define ASSP_CTRL_A_RESERVED1   0x02
#define ASSP_CTRL_A_RESERVED2   0x04
#define ASSP_CLK_49MHZ_SELECT   0x08
#define FAST_PLU_ENABLE         0x10
#define ASSP_CTRL_A_RESERVED3   0x20
#define DSP_CLK_36MHZ_SELECT    0x40

#define ASSP_CONTROL_B          0xA4
#define RESET_ASSP              0x00
#define RUN_ASSP                0x01
#define ENABLE_ASSP_CLOCK       0x00
#define STOP_ASSP_CLOCK         0x10
#define RESET_TOGGLE            0x40

#define ASSP_CONTROL_C          0xA6
#define ASSP_HOST_INT_ENABLE    0x01
#define FM_ADDR_REMAP_DISABLE   0x02
#define HOST_WRITE_PORT_ENABLE  0x08

#define ASSP_HOST_INT_STATUS    0xAC
#define DSP2HOST_REQ_PIORECORD  0x01
#define DSP2HOST_REQ_I2SRATE    0x02
#define DSP2HOST_REQ_TIMER      0x04

// AC97 registers
// XXX fix this crap up
/*#define AC97_RESET              0x00*/

#define AC97_VOL_MUTE_B         0x8000
#define AC97_VOL_M              0x1F
#define AC97_LEFT_VOL_S         8

#define AC97_MASTER_VOL         0x02
#define AC97_LINE_LEVEL_VOL     0x04
#define AC97_MASTER_MONO_VOL    0x06
#define AC97_PC_BEEP_VOL        0x0A
#define AC97_PC_BEEP_VOL_M      0x0F
#define AC97_SROUND_MASTER_VOL  0x38
#define AC97_PC_BEEP_VOL_S      1

/*#define AC97_PHONE_VOL          0x0C
#define AC97_MIC_VOL            0x0E*/
#define AC97_MIC_20DB_ENABLE    0x40

/*#define AC97_LINEIN_VOL         0x10
#define AC97_CD_VOL             0x12
#define AC97_VIDEO_VOL          0x14
#define AC97_AUX_VOL            0x16*/
#define AC97_PCM_OUT_VOL        0x18
/*#define AC97_RECORD_SELECT      0x1A*/
#define AC97_RECORD_MIC         0x00
#define AC97_RECORD_CD          0x01
#define AC97_RECORD_VIDEO       0x02
#define AC97_RECORD_AUX         0x03
#define AC97_RECORD_MONO_MUX    0x02
#define AC97_RECORD_DIGITAL     0x03
#define AC97_RECORD_LINE        0x04
#define AC97_RECORD_STEREO      0x05
#define AC97_RECORD_MONO        0x06
#define AC97_RECORD_PHONE       0x07

/*#define AC97_RECORD_GAIN        0x1C*/
#define AC97_RECORD_VOL_M       0x0F

/*#define AC97_GENERAL_PURPOSE    0x20*/
#define AC97_POWER_DOWN_CTRL    0x26
#define AC97_ADC_READY          0x0001
#define AC97_DAC_READY          0x0002
#define AC97_ANALOG_READY       0x0004
#define AC97_VREF_ON            0x0008
#define AC97_PR0                0x0100
#define AC97_PR1                0x0200
#define AC97_PR2                0x0400
#define AC97_PR3                0x0800
#define AC97_PR4                0x1000

#define AC97_RESERVED1          0x28

#define AC97_VENDOR_TEST        0x5A

#define AC97_CLOCK_DELAY        0x5C
#define AC97_LINEOUT_MUX_SEL    0x0001
#define AC97_MONO_MUX_SEL       0x0002
#define AC97_CLOCK_DELAY_SEL    0x1F
#define AC97_DAC_CDS_SHIFT      6
#define AC97_ADC_CDS_SHIFT      11

#define AC97_MULTI_CHANNEL_SEL  0x74

/*#define AC97_VENDOR_ID1         0x7C
#define AC97_VENDOR_ID2         0x7E*/

/*
 * ASSP control regs
 */
#define DSP_PORT_TIMER_COUNT    0x06

#define DSP_PORT_MEMORY_INDEX   0x80

#define DSP_PORT_MEMORY_TYPE    0x82
#define MEMTYPE_INTERNAL_CODE   0x0002
#define MEMTYPE_INTERNAL_DATA   0x0003
#define MEMTYPE_MASK            0x0003

#define DSP_PORT_MEMORY_DATA    0x84

#define DSP_PORT_CONTROL_REG_A  0xA2
#define DSP_PORT_CONTROL_REG_B  0xA4
#define DSP_PORT_CONTROL_REG_C  0xA6

#define REV_A_CODE_MEMORY_BEGIN         0x0000
#define REV_A_CODE_MEMORY_END           0x0FFF
#define REV_A_CODE_MEMORY_UNIT_LENGTH   0x0040
#define REV_A_CODE_MEMORY_LENGTH        (REV_A_CODE_MEMORY_END - REV_A_CODE_MEMORY_BEGIN + 1)

#define REV_B_CODE_MEMORY_BEGIN         0x0000
#define REV_B_CODE_MEMORY_END           0x0BFF
#define REV_B_CODE_MEMORY_UNIT_LENGTH   0x0040
#define REV_B_CODE_MEMORY_LENGTH        (REV_B_CODE_MEMORY_END - REV_B_CODE_MEMORY_BEGIN + 1)

#define REV_A_DATA_MEMORY_BEGIN         0x1000
#define REV_A_DATA_MEMORY_END           0x2FFF
#define REV_A_DATA_MEMORY_UNIT_LENGTH   0x0080
#define REV_A_DATA_MEMORY_LENGTH        (REV_A_DATA_MEMORY_END - REV_A_DATA_MEMORY_BEGIN + 1)

#define REV_B_DATA_MEMORY_BEGIN         0x1000
#define REV_B_DATA_MEMORY_END           0x2BFF
#define REV_B_DATA_MEMORY_UNIT_LENGTH   0x0080
#define REV_B_DATA_MEMORY_LENGTH        (REV_B_DATA_MEMORY_END - REV_B_DATA_MEMORY_BEGIN + 1)


#define NUM_UNITS_KERNEL_CODE          16
#define NUM_UNITS_KERNEL_DATA           2

#define NUM_UNITS_KERNEL_CODE_WITH_HSP 16
#define NUM_UNITS_KERNEL_DATA_WITH_HSP  5

/*
 * Kernel data layout
 */

#define DP_SHIFT_COUNT                  7

#define KDATA_BASE_ADDR                 0x1000
#define KDATA_BASE_ADDR2                0x1080

#define KDATA_TASK0                     (KDATA_BASE_ADDR + 0x0000)
#define KDATA_TASK1                     (KDATA_BASE_ADDR + 0x0001)
#define KDATA_TASK2                     (KDATA_BASE_ADDR + 0x0002)
#define KDATA_TASK3                     (KDATA_BASE_ADDR + 0x0003)
#define KDATA_TASK4                     (KDATA_BASE_ADDR + 0x0004)
#define KDATA_TASK5                     (KDATA_BASE_ADDR + 0x0005)
#define KDATA_TASK6                     (KDATA_BASE_ADDR + 0x0006)
#define KDATA_TASK7                     (KDATA_BASE_ADDR + 0x0007)
#define KDATA_TASK_ENDMARK              (KDATA_BASE_ADDR + 0x0008)

#define KDATA_CURRENT_TASK              (KDATA_BASE_ADDR + 0x0009)
#define KDATA_TASK_SWITCH               (KDATA_BASE_ADDR + 0x000A)

#define KDATA_INSTANCE0_POS3D           (KDATA_BASE_ADDR + 0x000B)
#define KDATA_INSTANCE1_POS3D           (KDATA_BASE_ADDR + 0x000C)
#define KDATA_INSTANCE2_POS3D           (KDATA_BASE_ADDR + 0x000D)
#define KDATA_INSTANCE3_POS3D           (KDATA_BASE_ADDR + 0x000E)
#define KDATA_INSTANCE4_POS3D           (KDATA_BASE_ADDR + 0x000F)
#define KDATA_INSTANCE5_POS3D           (KDATA_BASE_ADDR + 0x0010)
#define KDATA_INSTANCE6_POS3D           (KDATA_BASE_ADDR + 0x0011)
#define KDATA_INSTANCE7_POS3D           (KDATA_BASE_ADDR + 0x0012)
#define KDATA_INSTANCE8_POS3D           (KDATA_BASE_ADDR + 0x0013)
#define KDATA_INSTANCE_POS3D_ENDMARK    (KDATA_BASE_ADDR + 0x0014)

#define KDATA_INSTANCE0_SPKVIRT         (KDATA_BASE_ADDR + 0x0015)
#define KDATA_INSTANCE_SPKVIRT_ENDMARK  (KDATA_BASE_ADDR + 0x0016)

#define KDATA_INSTANCE0_SPDIF           (KDATA_BASE_ADDR + 0x0017)
#define KDATA_INSTANCE_SPDIF_ENDMARK    (KDATA_BASE_ADDR + 0x0018)

#define KDATA_INSTANCE0_MODEM           (KDATA_BASE_ADDR + 0x0019)
#define KDATA_INSTANCE_MODEM_ENDMARK    (KDATA_BASE_ADDR + 0x001A)

#define KDATA_INSTANCE0_SRC             (KDATA_BASE_ADDR + 0x001B)
#define KDATA_INSTANCE1_SRC             (KDATA_BASE_ADDR + 0x001C)
#define KDATA_INSTANCE_SRC_ENDMARK      (KDATA_BASE_ADDR + 0x001D)

#define KDATA_INSTANCE0_MINISRC         (KDATA_BASE_ADDR + 0x001E)
#define KDATA_INSTANCE1_MINISRC         (KDATA_BASE_ADDR + 0x001F)
#define KDATA_INSTANCE2_MINISRC         (KDATA_BASE_ADDR + 0x0020)
#define KDATA_INSTANCE3_MINISRC         (KDATA_BASE_ADDR + 0x0021)
#define KDATA_INSTANCE_MINISRC_ENDMARK  (KDATA_BASE_ADDR + 0x0022)

#define KDATA_INSTANCE0_CPYTHRU         (KDATA_BASE_ADDR + 0x0023)
#define KDATA_INSTANCE1_CPYTHRU         (KDATA_BASE_ADDR + 0x0024)
#define KDATA_INSTANCE_CPYTHRU_ENDMARK  (KDATA_BASE_ADDR + 0x0025)

#define KDATA_CURRENT_DMA               (KDATA_BASE_ADDR + 0x0026)
#define KDATA_DMA_SWITCH                (KDATA_BASE_ADDR + 0x0027)
#define KDATA_DMA_ACTIVE                (KDATA_BASE_ADDR + 0x0028)

#define KDATA_DMA_XFER0                 (KDATA_BASE_ADDR + 0x0029)
#define KDATA_DMA_XFER1                 (KDATA_BASE_ADDR + 0x002A)
#define KDATA_DMA_XFER2                 (KDATA_BASE_ADDR + 0x002B)
#define KDATA_DMA_XFER3                 (KDATA_BASE_ADDR + 0x002C)
#define KDATA_DMA_XFER4                 (KDATA_BASE_ADDR + 0x002D)
#define KDATA_DMA_XFER5                 (KDATA_BASE_ADDR + 0x002E)
#define KDATA_DMA_XFER6                 (KDATA_BASE_ADDR + 0x002F)
#define KDATA_DMA_XFER7                 (KDATA_BASE_ADDR + 0x0030)
#define KDATA_DMA_XFER8                 (KDATA_BASE_ADDR + 0x0031)
#define KDATA_DMA_XFER_ENDMARK          (KDATA_BASE_ADDR + 0x0032)

#define KDATA_I2S_SAMPLE_COUNT          (KDATA_BASE_ADDR + 0x0033)
#define KDATA_I2S_INT_METER             (KDATA_BASE_ADDR + 0x0034)
#define KDATA_I2S_ACTIVE                (KDATA_BASE_ADDR + 0x0035)

#define KDATA_TIMER_COUNT_RELOAD        (KDATA_BASE_ADDR + 0x0036)
#define KDATA_TIMER_COUNT_CURRENT       (KDATA_BASE_ADDR + 0x0037)

#define KDATA_HALT_SYNCH_CLIENT         (KDATA_BASE_ADDR + 0x0038)
#define KDATA_HALT_SYNCH_DMA            (KDATA_BASE_ADDR + 0x0039)
#define KDATA_HALT_ACKNOWLEDGE          (KDATA_BASE_ADDR + 0x003A)

#define KDATA_ADC1_XFER0                (KDATA_BASE_ADDR + 0x003B)
#define KDATA_ADC1_XFER_ENDMARK         (KDATA_BASE_ADDR + 0x003C)
#define KDATA_ADC1_LEFT_VOLUME			(KDATA_BASE_ADDR + 0x003D)
#define KDATA_ADC1_RIGHT_VOLUME  		(KDATA_BASE_ADDR + 0x003E)
#define KDATA_ADC1_LEFT_SUR_VOL			(KDATA_BASE_ADDR + 0x003F)
#define KDATA_ADC1_RIGHT_SUR_VOL		(KDATA_BASE_ADDR + 0x0040)

#define KDATA_ADC2_XFER0                (KDATA_BASE_ADDR + 0x0041)
#define KDATA_ADC2_XFER_ENDMARK         (KDATA_BASE_ADDR + 0x0042)
#define KDATA_ADC2_LEFT_VOLUME			(KDATA_BASE_ADDR + 0x0043)
#define KDATA_ADC2_RIGHT_VOLUME			(KDATA_BASE_ADDR + 0x0044)
#define KDATA_ADC2_LEFT_SUR_VOL			(KDATA_BASE_ADDR + 0x0045)
#define KDATA_ADC2_RIGHT_SUR_VOL		(KDATA_BASE_ADDR + 0x0046)

#define KDATA_CD_XFER0					(KDATA_BASE_ADDR + 0x0047)					
#define KDATA_CD_XFER_ENDMARK			(KDATA_BASE_ADDR + 0x0048)
#define KDATA_CD_LEFT_VOLUME			(KDATA_BASE_ADDR + 0x0049)
#define KDATA_CD_RIGHT_VOLUME			(KDATA_BASE_ADDR + 0x004A)
#define KDATA_CD_LEFT_SUR_VOL			(KDATA_BASE_ADDR + 0x004B)
#define KDATA_CD_RIGHT_SUR_VOL			(KDATA_BASE_ADDR + 0x004C)

#define KDATA_MIC_XFER0					(KDATA_BASE_ADDR + 0x004D)
#define KDATA_MIC_XFER_ENDMARK			(KDATA_BASE_ADDR + 0x004E)
#define KDATA_MIC_VOLUME				(KDATA_BASE_ADDR + 0x004F)
#define KDATA_MIC_SUR_VOL				(KDATA_BASE_ADDR + 0x0050)

#define KDATA_I2S_XFER0                 (KDATA_BASE_ADDR + 0x0051)
#define KDATA_I2S_XFER_ENDMARK          (KDATA_BASE_ADDR + 0x0052)

#define KDATA_CHI_XFER0                 (KDATA_BASE_ADDR + 0x0053)
#define KDATA_CHI_XFER_ENDMARK          (KDATA_BASE_ADDR + 0x0054)

#define KDATA_SPDIF_XFER                (KDATA_BASE_ADDR + 0x0055)
#define KDATA_SPDIF_CURRENT_FRAME       (KDATA_BASE_ADDR + 0x0056)
#define KDATA_SPDIF_FRAME0              (KDATA_BASE_ADDR + 0x0057)
#define KDATA_SPDIF_FRAME1              (KDATA_BASE_ADDR + 0x0058)
#define KDATA_SPDIF_FRAME2              (KDATA_BASE_ADDR + 0x0059)

#define KDATA_SPDIF_REQUEST             (KDATA_BASE_ADDR + 0x005A)
#define KDATA_SPDIF_TEMP                (KDATA_BASE_ADDR + 0x005B)

#define KDATA_SPDIFIN_XFER0             (KDATA_BASE_ADDR + 0x005C)
#define KDATA_SPDIFIN_XFER_ENDMARK      (KDATA_BASE_ADDR + 0x005D)
#define KDATA_SPDIFIN_INT_METER         (KDATA_BASE_ADDR + 0x005E)

#define KDATA_DSP_RESET_COUNT           (KDATA_BASE_ADDR + 0x005F)
#define KDATA_DEBUG_OUTPUT              (KDATA_BASE_ADDR + 0x0060)

#define KDATA_KERNEL_ISR_LIST           (KDATA_BASE_ADDR + 0x0061)

#define KDATA_KERNEL_ISR_CBSR1          (KDATA_BASE_ADDR + 0x0062)
#define KDATA_KERNEL_ISR_CBER1          (KDATA_BASE_ADDR + 0x0063)
#define KDATA_KERNEL_ISR_CBCR           (KDATA_BASE_ADDR + 0x0064)
#define KDATA_KERNEL_ISR_AR0            (KDATA_BASE_ADDR + 0x0065)
#define KDATA_KERNEL_ISR_AR1            (KDATA_BASE_ADDR + 0x0066)
#define KDATA_KERNEL_ISR_AR2            (KDATA_BASE_ADDR + 0x0067)
#define KDATA_KERNEL_ISR_AR3            (KDATA_BASE_ADDR + 0x0068)
#define KDATA_KERNEL_ISR_AR4            (KDATA_BASE_ADDR + 0x0069)
#define KDATA_KERNEL_ISR_AR5            (KDATA_BASE_ADDR + 0x006A)
#define KDATA_KERNEL_ISR_BRCR           (KDATA_BASE_ADDR + 0x006B)
#define KDATA_KERNEL_ISR_PASR           (KDATA_BASE_ADDR + 0x006C)
#define KDATA_KERNEL_ISR_PAER           (KDATA_BASE_ADDR + 0x006D)

#define KDATA_CLIENT_SCRATCH0           (KDATA_BASE_ADDR + 0x006E)
#define KDATA_CLIENT_SCRATCH1           (KDATA_BASE_ADDR + 0x006F)
#define KDATA_KERNEL_SCRATCH            (KDATA_BASE_ADDR + 0x0070)
#define KDATA_KERNEL_ISR_SCRATCH        (KDATA_BASE_ADDR + 0x0071)

#define KDATA_OUEUE_LEFT                (KDATA_BASE_ADDR + 0x0072)
#define KDATA_QUEUE_RIGHT               (KDATA_BASE_ADDR + 0x0073)

#define KDATA_ADC1_REQUEST              (KDATA_BASE_ADDR + 0x0074)
#define KDATA_ADC2_REQUEST              (KDATA_BASE_ADDR + 0x0075)
#define KDATA_CD_REQUEST				(KDATA_BASE_ADDR + 0x0076)
#define KDATA_MIC_REQUEST				(KDATA_BASE_ADDR + 0x0077)

#define KDATA_ADC1_MIXER_REQUEST        (KDATA_BASE_ADDR + 0x0078)
#define KDATA_ADC2_MIXER_REQUEST        (KDATA_BASE_ADDR + 0x0079)
#define KDATA_CD_MIXER_REQUEST			(KDATA_BASE_ADDR + 0x007A)
#define KDATA_MIC_MIXER_REQUEST			(KDATA_BASE_ADDR + 0x007B)
#define KDATA_MIC_SYNC_COUNTER			(KDATA_BASE_ADDR + 0x007C)

/*
 * second 'segment' (?) reserved for mixer
 * buffers..
 */

#define KDATA_MIXER_WORD0               (KDATA_BASE_ADDR2 + 0x0000)
#define KDATA_MIXER_WORD1               (KDATA_BASE_ADDR2 + 0x0001)
#define KDATA_MIXER_WORD2               (KDATA_BASE_ADDR2 + 0x0002)
#define KDATA_MIXER_WORD3               (KDATA_BASE_ADDR2 + 0x0003)
#define KDATA_MIXER_WORD4               (KDATA_BASE_ADDR2 + 0x0004)
#define KDATA_MIXER_WORD5               (KDATA_BASE_ADDR2 + 0x0005)
#define KDATA_MIXER_WORD6               (KDATA_BASE_ADDR2 + 0x0006)
#define KDATA_MIXER_WORD7               (KDATA_BASE_ADDR2 + 0x0007)
#define KDATA_MIXER_WORD8               (KDATA_BASE_ADDR2 + 0x0008)
#define KDATA_MIXER_WORD9               (KDATA_BASE_ADDR2 + 0x0009)
#define KDATA_MIXER_WORDA               (KDATA_BASE_ADDR2 + 0x000A)
#define KDATA_MIXER_WORDB               (KDATA_BASE_ADDR2 + 0x000B)
#define KDATA_MIXER_WORDC               (KDATA_BASE_ADDR2 + 0x000C)
#define KDATA_MIXER_WORDD               (KDATA_BASE_ADDR2 + 0x000D)
#define KDATA_MIXER_WORDE               (KDATA_BASE_ADDR2 + 0x000E)
#define KDATA_MIXER_WORDF               (KDATA_BASE_ADDR2 + 0x000F)

#define KDATA_MIXER_XFER0               (KDATA_BASE_ADDR2 + 0x0010)
#define KDATA_MIXER_XFER1               (KDATA_BASE_ADDR2 + 0x0011)
#define KDATA_MIXER_XFER2               (KDATA_BASE_ADDR2 + 0x0012)
#define KDATA_MIXER_XFER3               (KDATA_BASE_ADDR2 + 0x0013)
#define KDATA_MIXER_XFER4               (KDATA_BASE_ADDR2 + 0x0014)
#define KDATA_MIXER_XFER5               (KDATA_BASE_ADDR2 + 0x0015)
#define KDATA_MIXER_XFER6               (KDATA_BASE_ADDR2 + 0x0016)
#define KDATA_MIXER_XFER7               (KDATA_BASE_ADDR2 + 0x0017)
#define KDATA_MIXER_XFER8               (KDATA_BASE_ADDR2 + 0x0018)
#define KDATA_MIXER_XFER9               (KDATA_BASE_ADDR2 + 0x0019)
#define KDATA_MIXER_XFER_ENDMARK        (KDATA_BASE_ADDR2 + 0x001A)

#define KDATA_MIXER_TASK_NUMBER         (KDATA_BASE_ADDR2 + 0x001B)
#define KDATA_CURRENT_MIXER             (KDATA_BASE_ADDR2 + 0x001C)
#define KDATA_MIXER_ACTIVE              (KDATA_BASE_ADDR2 + 0x001D)
#define KDATA_MIXER_BANK_STATUS         (KDATA_BASE_ADDR2 + 0x001E)
#define KDATA_DAC_LEFT_VOLUME	        (KDATA_BASE_ADDR2 + 0x001F)
#define KDATA_DAC_RIGHT_VOLUME          (KDATA_BASE_ADDR2 + 0x0020)

#define MAX_INSTANCE_MINISRC            (KDATA_INSTANCE_MINISRC_ENDMARK - KDATA_INSTANCE0_MINISRC)
#define MAX_VIRTUAL_DMA_CHANNELS        (KDATA_DMA_XFER_ENDMARK - KDATA_DMA_XFER0)
#define MAX_VIRTUAL_MIXER_CHANNELS      (KDATA_MIXER_XFER_ENDMARK - KDATA_MIXER_XFER0)
#define MAX_VIRTUAL_ADC1_CHANNELS       (KDATA_ADC1_XFER_ENDMARK - KDATA_ADC1_XFER0)

/*
 * client data area offsets
 */
#define CDATA_INSTANCE_READY            0x00

#define CDATA_HOST_SRC_ADDRL            0x01
#define CDATA_HOST_SRC_ADDRH            0x02
#define CDATA_HOST_SRC_END_PLUS_1L      0x03
#define CDATA_HOST_SRC_END_PLUS_1H      0x04
#define CDATA_HOST_SRC_CURRENTL         0x05
#define CDATA_HOST_SRC_CURRENTH         0x06

#define CDATA_IN_BUF_CONNECT            0x07
#define CDATA_OUT_BUF_CONNECT           0x08

#define CDATA_IN_BUF_BEGIN              0x09
#define CDATA_IN_BUF_END_PLUS_1         0x0A
#define CDATA_IN_BUF_HEAD               0x0B
#define CDATA_IN_BUF_TAIL               0x0C
#define CDATA_OUT_BUF_BEGIN             0x0D
#define CDATA_OUT_BUF_END_PLUS_1        0x0E
#define CDATA_OUT_BUF_HEAD              0x0F
#define CDATA_OUT_BUF_TAIL              0x10

#define CDATA_DMA_CONTROL               0x11
#define CDATA_RESERVED                  0x12

#define CDATA_FREQUENCY                 0x13
#define CDATA_LEFT_VOLUME               0x14
#define CDATA_RIGHT_VOLUME              0x15
#define CDATA_LEFT_SUR_VOL              0x16
#define CDATA_RIGHT_SUR_VOL             0x17

#define CDATA_HEADER_LEN                0x18

#define SRC3_DIRECTION_OFFSET           CDATA_HEADER_LEN
#define SRC3_MODE_OFFSET                (CDATA_HEADER_LEN + 1)
#define SRC3_WORD_LENGTH_OFFSET         (CDATA_HEADER_LEN + 2)
#define SRC3_PARAMETER_OFFSET           (CDATA_HEADER_LEN + 3)
#define SRC3_COEFF_ADDR_OFFSET          (CDATA_HEADER_LEN + 8)
#define SRC3_FILTAP_ADDR_OFFSET         (CDATA_HEADER_LEN + 10)
#define SRC3_TEMP_INBUF_ADDR_OFFSET     (CDATA_HEADER_LEN + 16)
#define SRC3_TEMP_OUTBUF_ADDR_OFFSET    (CDATA_HEADER_LEN + 17)

#define MINISRC_IN_BUFFER_SIZE   ( 0x50 * 2 )
#define MINISRC_OUT_BUFFER_SIZE  ( 0x50 * 2 * 2)
#define MINISRC_OUT_BUFFER_SIZE  ( 0x50 * 2 * 2)
#define MINISRC_TMP_BUFFER_SIZE  ( 112 + ( MINISRC_BIQUAD_STAGE * 3 + 4 ) * 2 * 2 )
#define MINISRC_BIQUAD_STAGE    2
#define MINISRC_COEF_LOC          0X175

#define DMACONTROL_BLOCK_MASK           0x000F
#define  DMAC_BLOCK0_SELECTOR           0x0000
#define  DMAC_BLOCK1_SELECTOR           0x0001
#define  DMAC_BLOCK2_SELECTOR           0x0002
#define  DMAC_BLOCK3_SELECTOR           0x0003
#define  DMAC_BLOCK4_SELECTOR           0x0004
#define  DMAC_BLOCK5_SELECTOR           0x0005
#define  DMAC_BLOCK6_SELECTOR           0x0006
#define  DMAC_BLOCK7_SELECTOR           0x0007
#define  DMAC_BLOCK8_SELECTOR           0x0008
#define  DMAC_BLOCK9_SELECTOR           0x0009
#define  DMAC_BLOCKA_SELECTOR           0x000A
#define  DMAC_BLOCKB_SELECTOR           0x000B
#define  DMAC_BLOCKC_SELECTOR           0x000C
#define  DMAC_BLOCKD_SELECTOR           0x000D
#define  DMAC_BLOCKE_SELECTOR           0x000E
#define  DMAC_BLOCKF_SELECTOR           0x000F
#define DMACONTROL_PAGE_MASK            0x00F0
#define  DMAC_PAGE0_SELECTOR            0x0030
#define  DMAC_PAGE1_SELECTOR            0x0020
#define  DMAC_PAGE2_SELECTOR            0x0010
#define  DMAC_PAGE3_SELECTOR            0x0000
#define DMACONTROL_AUTOREPEAT           0x1000
#define DMACONTROL_STOPPED              0x2000
#define DMACONTROL_DIRECTION            0x0100


/*
 * DSP Code images
 */

static u16 assp_kernel_image[] = {
    0x7980, 0x0030, 0x7980, 0x03B4, 0x7980, 0x03B4, 0x7980, 0x00FB, 0x7980, 0x00DD, 0x7980, 0x03B4, 
    0x7980, 0x0332, 0x7980, 0x0287, 0x7980, 0x03B4, 0x7980, 0x03B4, 0x7980, 0x03B4, 0x7980, 0x03B4, 
    0x7980, 0x031A, 0x7980, 0x03B4, 0x7980, 0x022F, 0x7980, 0x03B4, 0x7980, 0x03B4, 0x7980, 0x03B4, 
    0x7980, 0x03B4, 0x7980, 0x03B4, 0x7980, 0x0063, 0x7980, 0x006B, 0x7980, 0x03B4, 0x7980, 0x03B4, 
    0xBF80, 0x2C7C, 0x8806, 0x8804, 0xBE40, 0xBC20, 0xAE09, 0x1000, 0xAE0A, 0x0001, 0x6938, 0xEB08, 
    0x0053, 0x695A, 0xEB08, 0x00D6, 0x0009, 0x8B88, 0x6980, 0xE388, 0x0036, 0xBE30, 0xBC20, 0x6909, 
    0xB801, 0x9009, 0xBE41, 0xBE41, 0x6928, 0xEB88, 0x0078, 0xBE41, 0xBE40, 0x7980, 0x0038, 0xBE41, 
    0xBE41, 0x903A, 0x6938, 0xE308, 0x0056, 0x903A, 0xBE41, 0xBE40, 0xEF00, 0x903A, 0x6939, 0xE308, 
    0x005E, 0x903A, 0xEF00, 0x690B, 0x660C, 0xEF8C, 0x690A, 0x660C, 0x620B, 0x6609, 0xEF00, 0x6910, 
    0x660F, 0xEF04, 0xE388, 0x0075, 0x690E, 0x660F, 0x6210, 0x660D, 0xEF00, 0x690E, 0x660D, 0xEF00, 
    0xAE70, 0x0001, 0xBC20, 0xAE27, 0x0001, 0x6939, 0xEB08, 0x005D, 0x6926, 0xB801, 0x9026, 0x0026, 
    0x8B88, 0x6980, 0xE388, 0x00CB, 0x9028, 0x0D28, 0x4211, 0xE100, 0x007A, 0x4711, 0xE100, 0x00A0, 
    0x7A80, 0x0063, 0xB811, 0x660A, 0x6209, 0xE304, 0x007A, 0x0C0B, 0x4005, 0x100A, 0xBA01, 0x9012, 
    0x0C12, 0x4002, 0x7980, 0x00AF, 0x7A80, 0x006B, 0xBE02, 0x620E, 0x660D, 0xBA10, 0xE344, 0x007A, 
    0x0C10, 0x4005, 0x100E, 0xBA01, 0x9012, 0x0C12, 0x4002, 0x1003, 0xBA02, 0x9012, 0x0C12, 0x4000, 
    0x1003, 0xE388, 0x00BA, 0x1004, 0x7980, 0x00BC, 0x1004, 0xBA01, 0x9012, 0x0C12, 0x4001, 0x0C05, 
    0x4003, 0x0C06, 0x4004, 0x1011, 0xBFB0, 0x01FF, 0x9012, 0x0C12, 0x4006, 0xBC20, 0xEF00, 0xAE26, 
    0x1028, 0x6970, 0xBFD0, 0x0001, 0x9070, 0xE388, 0x007A, 0xAE28, 0x0000, 0xEF00, 0xAE70, 0x0300, 
    0x0C70, 0xB00C, 0xAE5A, 0x0000, 0xEF00, 0x7A80, 0x038A, 0x697F, 0xB801, 0x907F, 0x0056, 0x8B88, 
    0x0CA0, 0xB008, 0xAF71, 0xB000, 0x4E71, 0xE200, 0x00F3, 0xAE56, 0x1057, 0x0056, 0x0CA0, 0xB008, 
    0x8056, 0x7980, 0x03A1, 0x0810, 0xBFA0, 0x1059, 0xE304, 0x03A1, 0x8056, 0x7980, 0x03A1, 0x7A80, 
    0x038A, 0xBF01, 0xBE43, 0xBE59, 0x907C, 0x6937, 0xE388, 0x010D, 0xBA01, 0xE308, 0x010C, 0xAE71, 
    0x0004, 0x0C71, 0x5000, 0x6936, 0x9037, 0xBF0A, 0x109E, 0x8B8A, 0xAF80, 0x8014, 0x4C80, 0xBF0A, 
    0x0560, 0xF500, 0xBF0A, 0x0520, 0xB900, 0xBB17, 0x90A0, 0x6917, 0xE388, 0x0148, 0x0D17, 0xE100, 
    0x0127, 0xBF0C, 0x0578, 0xBF0D, 0x057C, 0x7980, 0x012B, 0xBF0C, 0x0538, 0xBF0D, 0x053C, 0x6900, 
    0xE308, 0x0135, 0x8B8C, 0xBE59, 0xBB07, 0x90A0, 0xBC20, 0x7980, 0x0157, 0x030C, 0x8B8B, 0xB903, 
    0x8809, 0xBEC6, 0x013E, 0x69AC, 0x90AB, 0x69AD, 0x90AB, 0x0813, 0x660A, 0xE344, 0x0144, 0x0309, 
    0x830C, 0xBC20, 0x7980, 0x0157, 0x6955, 0xE388, 0x0157, 0x7C38, 0xBF0B, 0x0578, 0xF500, 0xBF0B, 
    0x0538, 0xB907, 0x8809, 0xBEC6, 0x0156, 0x10AB, 0x90AA, 0x6974, 0xE388, 0x0163, 0xAE72, 0x0540, 
    0xF500, 0xAE72, 0x0500, 0xAE61, 0x103B, 0x7A80, 0x02F6, 0x6978, 0xE388, 0x0182, 0x8B8C, 0xBF0C, 
    0x0560, 0xE500, 0x7C40, 0x0814, 0xBA20, 0x8812, 0x733D, 0x7A80, 0x0380, 0x733E, 0x7A80, 0x0380, 
    0x8B8C, 0xBF0C, 0x056C, 0xE500, 0x7C40, 0x0814, 0xBA2C, 0x8812, 0x733F, 0x7A80, 0x0380, 0x7340, 
    0x7A80, 0x0380, 0x6975, 0xE388, 0x018E, 0xAE72, 0x0548, 0xF500, 0xAE72, 0x0508, 0xAE61, 0x1041, 
    0x7A80, 0x02F6, 0x6979, 0xE388, 0x01AD, 0x8B8C, 0xBF0C, 0x0560, 0xE500, 0x7C40, 0x0814, 0xBA18, 
    0x8812, 0x7343, 0x7A80, 0x0380, 0x7344, 0x7A80, 0x0380, 0x8B8C, 0xBF0C, 0x056C, 0xE500, 0x7C40, 
    0x0814, 0xBA24, 0x8812, 0x7345, 0x7A80, 0x0380, 0x7346, 0x7A80, 0x0380, 0x6976, 0xE388, 0x01B9, 
    0xAE72, 0x0558, 0xF500, 0xAE72, 0x0518, 0xAE61, 0x1047, 0x7A80, 0x02F6, 0x697A, 0xE388, 0x01D8, 
    0x8B8C, 0xBF0C, 0x0560, 0xE500, 0x7C40, 0x0814, 0xBA08, 0x8812, 0x7349, 0x7A80, 0x0380, 0x734A, 
    0x7A80, 0x0380, 0x8B8C, 0xBF0C, 0x056C, 0xE500, 0x7C40, 0x0814, 0xBA14, 0x8812, 0x734B, 0x7A80, 
    0x0380, 0x734C, 0x7A80, 0x0380, 0xBC21, 0xAE1C, 0x1090, 0x8B8A, 0xBF0A, 0x0560, 0xE500, 0x7C40, 
    0x0812, 0xB804, 0x8813, 0x8B8D, 0xBF0D, 0x056C, 0xE500, 0x7C40, 0x0815, 0xB804, 0x8811, 0x7A80, 
    0x034A, 0x8B8A, 0xBF0A, 0x0560, 0xE500, 0x7C40, 0x731F, 0xB903, 0x8809, 0xBEC6, 0x01F9, 0x548A, 
    0xBE03, 0x98A0, 0x7320, 0xB903, 0x8809, 0xBEC6, 0x0201, 0x548A, 0xBE03, 0x98A0, 0x1F20, 0x2F1F, 
    0x9826, 0xBC20, 0x6935, 0xE388, 0x03A1, 0x6933, 0xB801, 0x9033, 0xBFA0, 0x02EE, 0xE308, 0x03A1, 
    0x9033, 0xBF00, 0x6951, 0xE388, 0x021F, 0x7334, 0xBE80, 0x5760, 0xBE03, 0x9F7E, 0xBE59, 0x9034, 
    0x697E, 0x0D51, 0x9013, 0xBC20, 0x695C, 0xE388, 0x03A1, 0x735E, 0xBE80, 0x5760, 0xBE03, 0x9F7E, 
    0xBE59, 0x905E, 0x697E, 0x0D5C, 0x9013, 0x7980, 0x03A1, 0x7A80, 0x038A, 0xBF01, 0xBE43, 0x6977, 
    0xE388, 0x024E, 0xAE61, 0x104D, 0x0061, 0x8B88, 0x6980, 0xE388, 0x024E, 0x9071, 0x0D71, 0x000B, 
    0xAFA0, 0x8010, 0xAFA0, 0x8010, 0x0810, 0x660A, 0xE308, 0x0249, 0x0009, 0x0810, 0x660C, 0xE388, 
    0x024E, 0x800B, 0xBC20, 0x697B, 0xE388, 0x03A1, 0xBF0A, 0x109E, 0x8B8A, 0xAF80, 0x8014, 0x4C80, 
    0xE100, 0x0266, 0x697C, 0xBF90, 0x0560, 0x9072, 0x0372, 0x697C, 0xBF90, 0x0564, 0x9073, 0x0473, 
    0x7980, 0x0270, 0x697C, 0xBF90, 0x0520, 0x9072, 0x0372, 0x697C, 0xBF90, 0x0524, 0x9073, 0x0473, 
    0x697C, 0xB801, 0x907C, 0xBF0A, 0x10FD, 0x8B8A, 0xAF80, 0x8010, 0x734F, 0x548A, 0xBE03, 0x9880, 
    0xBC21, 0x7326, 0x548B, 0xBE03, 0x618B, 0x988C, 0xBE03, 0x6180, 0x9880, 0x7980, 0x03A1, 0x7A80, 
    0x038A, 0x0D28, 0x4711, 0xE100, 0x02BE, 0xAF12, 0x4006, 0x6912, 0xBFB0, 0x0C00, 0xE388, 0x02B6, 
    0xBFA0, 0x0800, 0xE388, 0x02B2, 0x6912, 0xBFB0, 0x0C00, 0xBFA0, 0x0400, 0xE388, 0x02A3, 0x6909, 
    0x900B, 0x7980, 0x02A5, 0xAF0B, 0x4005, 0x6901, 0x9005, 0x6902, 0x9006, 0x4311, 0xE100, 0x02ED, 
    0x6911, 0xBFC0, 0x2000, 0x9011, 0x7980, 0x02ED, 0x6909, 0x900B, 0x7980, 0x02B8, 0xAF0B, 0x4005, 
    0xAF05, 0x4003, 0xAF06, 0x4004, 0x7980, 0x02ED, 0xAF12, 0x4006, 0x6912, 0xBFB0, 0x0C00, 0xE388, 
    0x02E7, 0xBFA0, 0x0800, 0xE388, 0x02E3, 0x6912, 0xBFB0, 0x0C00, 0xBFA0, 0x0400, 0xE388, 0x02D4, 
    0x690D, 0x9010, 0x7980, 0x02D6, 0xAF10, 0x4005, 0x6901, 0x9005, 0x6902, 0x9006, 0x4311, 0xE100, 
    0x02ED, 0x6911, 0xBFC0, 0x2000, 0x9011, 0x7980, 0x02ED, 0x690D, 0x9010, 0x7980, 0x02E9, 0xAF10, 
    0x4005, 0xAF05, 0x4003, 0xAF06, 0x4004, 0xBC20, 0x6970, 0x9071, 0x7A80, 0x0078, 0x6971, 0x9070, 
    0x7980, 0x03A1, 0xBC20, 0x0361, 0x8B8B, 0x6980, 0xEF88, 0x0272, 0x0372, 0x7804, 0x9071, 0x0D71, 
    0x8B8A, 0x000B, 0xB903, 0x8809, 0xBEC6, 0x0309, 0x69A8, 0x90AB, 0x69A8, 0x90AA, 0x0810, 0x660A, 
    0xE344, 0x030F, 0x0009, 0x0810, 0x660C, 0xE388, 0x0314, 0x800B, 0xBC20, 0x6961, 0xB801, 0x9061, 
    0x7980, 0x02F7, 0x7A80, 0x038A, 0x5D35, 0x0001, 0x6934, 0xB801, 0x9034, 0xBF0A, 0x109E, 0x8B8A, 
    0xAF80, 0x8014, 0x4880, 0xAE72, 0x0550, 0xF500, 0xAE72, 0x0510, 0xAE61, 0x1051, 0x7A80, 0x02F6, 
    0x7980, 0x03A1, 0x7A80, 0x038A, 0x5D35, 0x0002, 0x695E, 0xB801, 0x905E, 0xBF0A, 0x109E, 0x8B8A, 
    0xAF80, 0x8014, 0x4780, 0xAE72, 0x0558, 0xF500, 0xAE72, 0x0518, 0xAE61, 0x105C, 0x7A80, 0x02F6, 
    0x7980, 0x03A1, 0x001C, 0x8B88, 0x6980, 0xEF88, 0x901D, 0x0D1D, 0x100F, 0x6610, 0xE38C, 0x0358, 
    0x690E, 0x6610, 0x620F, 0x660D, 0xBA0F, 0xE301, 0x037A, 0x0410, 0x8B8A, 0xB903, 0x8809, 0xBEC6, 
    0x036C, 0x6A8C, 0x61AA, 0x98AB, 0x6A8C, 0x61AB, 0x98AD, 0x6A8C, 0x61AD, 0x98A9, 0x6A8C, 0x61A9, 
    0x98AA, 0x7C04, 0x8B8B, 0x7C04, 0x8B8D, 0x7C04, 0x8B89, 0x7C04, 0x0814, 0x660E, 0xE308, 0x0379, 
    0x040D, 0x8410, 0xBC21, 0x691C, 0xB801, 0x901C, 0x7980, 0x034A, 0xB903, 0x8809, 0x8B8A, 0xBEC6, 
    0x0388, 0x54AC, 0xBE03, 0x618C, 0x98AA, 0xEF00, 0xBC20, 0xBE46, 0x0809, 0x906B, 0x080A, 0x906C, 
    0x080B, 0x906D, 0x081A, 0x9062, 0x081B, 0x9063, 0x081E, 0x9064, 0xBE59, 0x881E, 0x8065, 0x8166, 
    0x8267, 0x8368, 0x8469, 0x856A, 0xEF00, 0xBC20, 0x696B, 0x8809, 0x696C, 0x880A, 0x696D, 0x880B, 
    0x6962, 0x881A, 0x6963, 0x881B, 0x6964, 0x881E, 0x0065, 0x0166, 0x0267, 0x0368, 0x0469, 0x056A, 
    0xBE3A, 
};

/*
 * Mini sample rate converter code image
 * that is to be loaded at 0x400 on the DSP.
 */
static u16 assp_minisrc_image[] = {

    0xBF80, 0x101E, 0x906E, 0x006E, 0x8B88, 0x6980, 0xEF88, 0x906F, 0x0D6F, 0x6900, 0xEB08, 0x0412, 
    0xBC20, 0x696E, 0xB801, 0x906E, 0x7980, 0x0403, 0xB90E, 0x8807, 0xBE43, 0xBF01, 0xBE47, 0xBE41, 
    0x7A80, 0x002A, 0xBE40, 0x3029, 0xEFCC, 0xBE41, 0x7A80, 0x0028, 0xBE40, 0x3028, 0xEFCC, 0x6907, 
    0xE308, 0x042A, 0x6909, 0x902C, 0x7980, 0x042C, 0x690D, 0x902C, 0x1009, 0x881A, 0x100A, 0xBA01, 
    0x881B, 0x100D, 0x881C, 0x100E, 0xBA01, 0x881D, 0xBF80, 0x00ED, 0x881E, 0x050C, 0x0124, 0xB904, 
    0x9027, 0x6918, 0xE308, 0x04B3, 0x902D, 0x6913, 0xBFA0, 0x7598, 0xF704, 0xAE2D, 0x00FF, 0x8B8D, 
    0x6919, 0xE308, 0x0463, 0x691A, 0xE308, 0x0456, 0xB907, 0x8809, 0xBEC6, 0x0453, 0x10A9, 0x90AD, 
    0x7980, 0x047C, 0xB903, 0x8809, 0xBEC6, 0x0460, 0x1889, 0x6C22, 0x90AD, 0x10A9, 0x6E23, 0x6C22, 
    0x90AD, 0x7980, 0x047C, 0x101A, 0xE308, 0x046F, 0xB903, 0x8809, 0xBEC6, 0x046C, 0x10A9, 0x90A0, 
    0x90AD, 0x7980, 0x047C, 0xB901, 0x8809, 0xBEC6, 0x047B, 0x1889, 0x6C22, 0x90A0, 0x90AD, 0x10A9, 
    0x6E23, 0x6C22, 0x90A0, 0x90AD, 0x692D, 0xE308, 0x049C, 0x0124, 0xB703, 0xB902, 0x8818, 0x8B89, 
    0x022C, 0x108A, 0x7C04, 0x90A0, 0x692B, 0x881F, 0x7E80, 0x055B, 0x692A, 0x8809, 0x8B89, 0x99A0, 
    0x108A, 0x90A0, 0x692B, 0x881F, 0x7E80, 0x055B, 0x692A, 0x8809, 0x8B89, 0x99AF, 0x7B99, 0x0484, 
    0x0124, 0x060F, 0x101B, 0x2013, 0x901B, 0xBFA0, 0x7FFF, 0xE344, 0x04AC, 0x901B, 0x8B89, 0x7A80, 
    0x051A, 0x6927, 0xBA01, 0x9027, 0x7A80, 0x0523, 0x6927, 0xE308, 0x049E, 0x7980, 0x050F, 0x0624, 
    0x1026, 0x2013, 0x9026, 0xBFA0, 0x7FFF, 0xE304, 0x04C0, 0x8B8D, 0x7A80, 0x051A, 0x7980, 0x04B4, 
    0x9026, 0x1013, 0x3026, 0x901B, 0x8B8D, 0x7A80, 0x051A, 0x7A80, 0x0523, 0x1027, 0xBA01, 0x9027, 
    0xE308, 0x04B4, 0x0124, 0x060F, 0x8B89, 0x691A, 0xE308, 0x04EA, 0x6919, 0xE388, 0x04E0, 0xB903, 
    0x8809, 0xBEC6, 0x04DD, 0x1FA0, 0x2FAE, 0x98A9, 0x7980, 0x050F, 0xB901, 0x8818, 0xB907, 0x8809, 
    0xBEC6, 0x04E7, 0x10EE, 0x90A9, 0x7980, 0x050F, 0x6919, 0xE308, 0x04FE, 0xB903, 0x8809, 0xBE46, 
    0xBEC6, 0x04FA, 0x17A0, 0xBE1E, 0x1FAE, 0xBFBF, 0xFF00, 0xBE13, 0xBFDF, 0x8080, 0x99A9, 0xBE47, 
    0x7980, 0x050F, 0xB901, 0x8809, 0xBEC6, 0x050E, 0x16A0, 0x26A0, 0xBFB7, 0xFF00, 0xBE1E, 0x1EA0, 
    0x2EAE, 0xBFBF, 0xFF00, 0xBE13, 0xBFDF, 0x8080, 0x99A9, 0x850C, 0x860F, 0x6907, 0xE388, 0x0516, 
    0x0D07, 0x8510, 0xBE59, 0x881E, 0xBE4A, 0xEF00, 0x101E, 0x901C, 0x101F, 0x901D, 0x10A0, 0x901E, 
    0x10A0, 0x901F, 0xEF00, 0x101E, 0x301C, 0x9020, 0x731B, 0x5420, 0xBE03, 0x9825, 0x1025, 0x201C, 
    0x9025, 0x7325, 0x5414, 0xBE03, 0x8B8E, 0x9880, 0x692F, 0xE388, 0x0539, 0xBE59, 0xBB07, 0x6180, 
    0x9880, 0x8BA0, 0x101F, 0x301D, 0x9021, 0x731B, 0x5421, 0xBE03, 0x982E, 0x102E, 0x201D, 0x902E, 
    0x732E, 0x5415, 0xBE03, 0x9880, 0x692F, 0xE388, 0x054F, 0xBE59, 0xBB07, 0x6180, 0x9880, 0x8BA0, 
    0x6918, 0xEF08, 0x7325, 0x5416, 0xBE03, 0x98A0, 0x732E, 0x5417, 0xBE03, 0x98A0, 0xEF00, 0x8BA0, 
    0xBEC6, 0x056B, 0xBE59, 0xBB04, 0xAA90, 0xBE04, 0xBE1E, 0x99E0, 0x8BE0, 0x69A0, 0x90D0, 0x69A0, 
    0x90D0, 0x081F, 0xB805, 0x881F, 0x8B90, 0x69A0, 0x90D0, 0x69A0, 0x9090, 0x8BD0, 0x8BD8, 0xBE1F, 
    0xEF00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
};

