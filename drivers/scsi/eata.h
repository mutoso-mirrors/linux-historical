/*
 *        eata.h - used by the low-level driver for EATA/DMA SCSI host adapters.
 */

static int eata2x_detect(Scsi_Host_Template *);
static int eata2x_release(struct Scsi_Host *);
static int eata2x_queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
static int eata2x_eh_abort(Scsi_Cmnd *);
static int eata2x_eh_host_reset(Scsi_Cmnd *);
static int eata2x_bios_param(struct scsi_device *, struct block_device *,
                             sector_t, int *);
static int eata2x_slave_configure(Scsi_Device *);

#define EATA_VERSION "8.03.00"

#define EATA {                                                               \
                .name              = "EATA/DMA 2.0x rev. " EATA_VERSION " ",   \
                .detect                  = eata2x_detect,                      \
                .release                 = eata2x_release,                     \
                .queuecommand            = eata2x_queuecommand,                \
                .eh_abort_handler        = eata2x_eh_abort,                    \
                .eh_device_reset_handler = NULL,                               \
                .eh_bus_reset_handler    = NULL,                               \
                .eh_host_reset_handler   = eata2x_eh_host_reset,               \
                .bios_param              = eata2x_bios_param,                  \
		.slave_configure	 = eata2x_slave_configure,	     \
                .this_id                 = 7,                                  \
                .unchecked_isa_dma       = 1,                                  \
                .use_clustering          = ENABLE_CLUSTERING                   \
             }
