/* List here explicitly which ioctl's are known to have
 * compatible types passed or none at all... Please include
 * only stuff that is compatible on *all architectures*.
 */
#ifndef COMPATIBLE_IOCTL /* pointer to compatible structure or no argument */
#define COMPATIBLE_IOCTL(cmd)  HANDLE_IOCTL((cmd),(ioctl_trans_handler_t)sys_ioctl)
#endif

#ifndef ULONG_IOCTL /* argument is an unsigned long integer, not a pointer */
#define ULONG_IOCTL(cmd)  HANDLE_IOCTL((cmd),(ioctl_trans_handler_t)sys_ioctl)
#endif

/* Big T */
COMPATIBLE_IOCTL(TCGETA)
COMPATIBLE_IOCTL(TCSETA)
COMPATIBLE_IOCTL(TCSETAW)
COMPATIBLE_IOCTL(TCSETAF)
COMPATIBLE_IOCTL(TCSBRK)
COMPATIBLE_IOCTL(TCXONC)
COMPATIBLE_IOCTL(TCFLSH)
COMPATIBLE_IOCTL(TCGETS)
COMPATIBLE_IOCTL(TCSETS)
COMPATIBLE_IOCTL(TCSETSW)
COMPATIBLE_IOCTL(TCSETSF)
COMPATIBLE_IOCTL(TIOCLINUX)
/* Little t */
COMPATIBLE_IOCTL(TIOCGETD)
COMPATIBLE_IOCTL(TIOCSETD)
COMPATIBLE_IOCTL(TIOCEXCL)
COMPATIBLE_IOCTL(TIOCNXCL)
COMPATIBLE_IOCTL(TIOCCONS)
COMPATIBLE_IOCTL(TIOCGSOFTCAR)
COMPATIBLE_IOCTL(TIOCSSOFTCAR)
COMPATIBLE_IOCTL(TIOCSWINSZ)
COMPATIBLE_IOCTL(TIOCGWINSZ)
COMPATIBLE_IOCTL(TIOCMGET)
COMPATIBLE_IOCTL(TIOCMBIC)
COMPATIBLE_IOCTL(TIOCMBIS)
COMPATIBLE_IOCTL(TIOCMSET)
COMPATIBLE_IOCTL(TIOCPKT)
COMPATIBLE_IOCTL(TIOCNOTTY)
COMPATIBLE_IOCTL(TIOCSTI)
COMPATIBLE_IOCTL(TIOCOUTQ)
COMPATIBLE_IOCTL(TIOCSPGRP)
COMPATIBLE_IOCTL(TIOCGPGRP)
ULONG_IOCTL(TIOCSCTTY)
COMPATIBLE_IOCTL(TIOCGPTN)
COMPATIBLE_IOCTL(TIOCSPTLCK)
COMPATIBLE_IOCTL(TIOCSERGETLSR)
/* Big F */
COMPATIBLE_IOCTL(FBIOGET_VSCREENINFO)
COMPATIBLE_IOCTL(FBIOPUT_VSCREENINFO)
COMPATIBLE_IOCTL(FBIOPAN_DISPLAY)
COMPATIBLE_IOCTL(FBIOGET_CON2FBMAP)
COMPATIBLE_IOCTL(FBIOPUT_CON2FBMAP)
/* Little f */
COMPATIBLE_IOCTL(FIOCLEX)
COMPATIBLE_IOCTL(FIONCLEX)
COMPATIBLE_IOCTL(FIOASYNC)
COMPATIBLE_IOCTL(FIONBIO)
COMPATIBLE_IOCTL(FIONREAD)  /* This is also TIOCINQ */
/* 0x00 */
COMPATIBLE_IOCTL(FIBMAP)
COMPATIBLE_IOCTL(FIGETBSZ)
/* 0x03 -- HD/IDE ioctl's used by hdparm and friends.
 *         Some need translations, these do not.
 */
COMPATIBLE_IOCTL(HDIO_GET_IDENTITY)
COMPATIBLE_IOCTL(HDIO_SET_DMA)
COMPATIBLE_IOCTL(HDIO_SET_UNMASKINTR)
COMPATIBLE_IOCTL(HDIO_SET_NOWERR)
COMPATIBLE_IOCTL(HDIO_SET_32BIT)
COMPATIBLE_IOCTL(HDIO_SET_MULTCOUNT)
COMPATIBLE_IOCTL(HDIO_DRIVE_CMD)
COMPATIBLE_IOCTL(HDIO_SET_PIO_MODE)
COMPATIBLE_IOCTL(HDIO_SET_NICE)
/* 0x02 -- Floppy ioctls */
COMPATIBLE_IOCTL(FDMSGON)
COMPATIBLE_IOCTL(FDMSGOFF)
COMPATIBLE_IOCTL(FDSETEMSGTRESH)
COMPATIBLE_IOCTL(FDFLUSH)
COMPATIBLE_IOCTL(FDWERRORCLR)
COMPATIBLE_IOCTL(FDSETMAXERRS)
COMPATIBLE_IOCTL(FDGETMAXERRS)
COMPATIBLE_IOCTL(FDGETDRVTYP)
COMPATIBLE_IOCTL(FDEJECT)
COMPATIBLE_IOCTL(FDCLRPRM)
COMPATIBLE_IOCTL(FDFMTBEG)
COMPATIBLE_IOCTL(FDFMTEND)
COMPATIBLE_IOCTL(FDRESET)
COMPATIBLE_IOCTL(FDTWADDLE)
COMPATIBLE_IOCTL(FDFMTTRK)
COMPATIBLE_IOCTL(FDRAWCMD)
/* 0x12 */
COMPATIBLE_IOCTL(BLKROSET)
COMPATIBLE_IOCTL(BLKROGET)
COMPATIBLE_IOCTL(BLKRRPART)
COMPATIBLE_IOCTL(BLKFLSBUF)
COMPATIBLE_IOCTL(BLKSECTSET)
COMPATIBLE_IOCTL(BLKSSZGET)
ULONG_IOCTL(BLKRASET)
ULONG_IOCTL(BLKFRASET)
/* RAID */
COMPATIBLE_IOCTL(RAID_VERSION)
COMPATIBLE_IOCTL(GET_ARRAY_INFO)
COMPATIBLE_IOCTL(GET_DISK_INFO)
COMPATIBLE_IOCTL(PRINT_RAID_DEBUG)
COMPATIBLE_IOCTL(RAID_AUTORUN)
COMPATIBLE_IOCTL(CLEAR_ARRAY)
COMPATIBLE_IOCTL(ADD_NEW_DISK)
ULONG_IOCTL(HOT_REMOVE_DISK)
COMPATIBLE_IOCTL(SET_ARRAY_INFO)
COMPATIBLE_IOCTL(SET_DISK_INFO)
COMPATIBLE_IOCTL(WRITE_RAID_INFO)
COMPATIBLE_IOCTL(UNPROTECT_ARRAY)
COMPATIBLE_IOCTL(PROTECT_ARRAY)
ULONG_IOCTL(HOT_ADD_DISK)
ULONG_IOCTL(SET_DISK_FAULTY)
COMPATIBLE_IOCTL(RUN_ARRAY)
ULONG_IOCTL(START_ARRAY)
COMPATIBLE_IOCTL(STOP_ARRAY)
COMPATIBLE_IOCTL(STOP_ARRAY_RO)
COMPATIBLE_IOCTL(RESTART_ARRAY_RW)
/* DM */
COMPATIBLE_IOCTL(DM_VERSION)
COMPATIBLE_IOCTL(DM_LIST_DEVICES)
COMPATIBLE_IOCTL(DM_DEV_CREATE)
COMPATIBLE_IOCTL(DM_DEV_REMOVE)
COMPATIBLE_IOCTL(DM_DEV_RENAME)
COMPATIBLE_IOCTL(DM_DEV_SUSPEND)
COMPATIBLE_IOCTL(DM_DEV_STATUS)
COMPATIBLE_IOCTL(DM_DEV_WAIT)
COMPATIBLE_IOCTL(DM_TABLE_LOAD)
COMPATIBLE_IOCTL(DM_TABLE_CLEAR)
COMPATIBLE_IOCTL(DM_TABLE_DEPS)
COMPATIBLE_IOCTL(DM_TABLE_STATUS)
/* Big K */
COMPATIBLE_IOCTL(PIO_FONT)
COMPATIBLE_IOCTL(GIO_FONT)
ULONG_IOCTL(KDSIGACCEPT)
COMPATIBLE_IOCTL(KDGETKEYCODE)
COMPATIBLE_IOCTL(KDSETKEYCODE)
ULONG_IOCTL(KIOCSOUND)
ULONG_IOCTL(KDMKTONE)
COMPATIBLE_IOCTL(KDGKBTYPE)
ULONG_IOCTL(KDSETMODE)
COMPATIBLE_IOCTL(KDGETMODE)
ULONG_IOCTL(KDSKBMODE)
COMPATIBLE_IOCTL(KDGKBMODE)
ULONG_IOCTL(KDSKBMETA)
COMPATIBLE_IOCTL(KDGKBMETA)
COMPATIBLE_IOCTL(KDGKBENT)
COMPATIBLE_IOCTL(KDSKBENT)
COMPATIBLE_IOCTL(KDGKBSENT)
COMPATIBLE_IOCTL(KDSKBSENT)
COMPATIBLE_IOCTL(KDGKBDIACR)
COMPATIBLE_IOCTL(KDSKBDIACR)
COMPATIBLE_IOCTL(KDKBDREP)
COMPATIBLE_IOCTL(KDGKBLED)
ULONG_IOCTL(KDSKBLED)
COMPATIBLE_IOCTL(KDGETLED)
ULONG_IOCTL(KDSETLED)
COMPATIBLE_IOCTL(GIO_SCRNMAP)
COMPATIBLE_IOCTL(PIO_SCRNMAP)
COMPATIBLE_IOCTL(GIO_UNISCRNMAP)
COMPATIBLE_IOCTL(PIO_UNISCRNMAP)
COMPATIBLE_IOCTL(PIO_FONTRESET)
COMPATIBLE_IOCTL(PIO_UNIMAPCLR)
/* Big S */
COMPATIBLE_IOCTL(SCSI_IOCTL_GET_IDLUN)
COMPATIBLE_IOCTL(SCSI_IOCTL_DOORLOCK)
COMPATIBLE_IOCTL(SCSI_IOCTL_DOORUNLOCK)
COMPATIBLE_IOCTL(SCSI_IOCTL_TEST_UNIT_READY)
COMPATIBLE_IOCTL(SCSI_IOCTL_GET_BUS_NUMBER)
COMPATIBLE_IOCTL(SCSI_IOCTL_SEND_COMMAND)
COMPATIBLE_IOCTL(SCSI_IOCTL_PROBE_HOST)
COMPATIBLE_IOCTL(SCSI_IOCTL_GET_PCI)
/* Big T */
COMPATIBLE_IOCTL(TUNSETNOCSUM)
COMPATIBLE_IOCTL(TUNSETDEBUG)
COMPATIBLE_IOCTL(TUNSETPERSIST)
COMPATIBLE_IOCTL(TUNSETOWNER)
/* Big V */
COMPATIBLE_IOCTL(VT_SETMODE)
COMPATIBLE_IOCTL(VT_GETMODE)
COMPATIBLE_IOCTL(VT_GETSTATE)
COMPATIBLE_IOCTL(VT_OPENQRY)
ULONG_IOCTL(VT_ACTIVATE)
ULONG_IOCTL(VT_WAITACTIVE)
ULONG_IOCTL(VT_RELDISP)
ULONG_IOCTL(VT_DISALLOCATE)
COMPATIBLE_IOCTL(VT_RESIZE)
COMPATIBLE_IOCTL(VT_RESIZEX)
COMPATIBLE_IOCTL(VT_LOCKSWITCH)
COMPATIBLE_IOCTL(VT_UNLOCKSWITCH)
/* Little v */
/* Little v, the video4linux ioctls (conflict?) */
COMPATIBLE_IOCTL(VIDIOCGCAP)
COMPATIBLE_IOCTL(VIDIOCGCHAN)
COMPATIBLE_IOCTL(VIDIOCSCHAN)
COMPATIBLE_IOCTL(VIDIOCGPICT)
COMPATIBLE_IOCTL(VIDIOCSPICT)
COMPATIBLE_IOCTL(VIDIOCCAPTURE)
COMPATIBLE_IOCTL(VIDIOCKEY)
COMPATIBLE_IOCTL(VIDIOCGAUDIO)
COMPATIBLE_IOCTL(VIDIOCSAUDIO)
COMPATIBLE_IOCTL(VIDIOCSYNC)
COMPATIBLE_IOCTL(VIDIOCMCAPTURE)
COMPATIBLE_IOCTL(VIDIOCGMBUF)
COMPATIBLE_IOCTL(VIDIOCGUNIT)
COMPATIBLE_IOCTL(VIDIOCGCAPTURE)
COMPATIBLE_IOCTL(VIDIOCSCAPTURE)
/* BTTV specific... */
COMPATIBLE_IOCTL(_IOW('v',  BASE_VIDIOCPRIVATE+0, char [256]))
COMPATIBLE_IOCTL(_IOR('v',  BASE_VIDIOCPRIVATE+1, char [256]))
COMPATIBLE_IOCTL(_IOR('v' , BASE_VIDIOCPRIVATE+2, unsigned int))
COMPATIBLE_IOCTL(_IOW('v' , BASE_VIDIOCPRIVATE+3, char [16])) /* struct bttv_pll_info */
COMPATIBLE_IOCTL(_IOR('v' , BASE_VIDIOCPRIVATE+4, int))
COMPATIBLE_IOCTL(_IOR('v' , BASE_VIDIOCPRIVATE+5, int))
COMPATIBLE_IOCTL(_IOR('v' , BASE_VIDIOCPRIVATE+6, int))
COMPATIBLE_IOCTL(_IOR('v' , BASE_VIDIOCPRIVATE+7, int))
/* Little p (/dev/rtc, /dev/envctrl, etc.) */
COMPATIBLE_IOCTL(RTC_AIE_ON)
COMPATIBLE_IOCTL(RTC_AIE_OFF)
COMPATIBLE_IOCTL(RTC_UIE_ON)
COMPATIBLE_IOCTL(RTC_UIE_OFF)
COMPATIBLE_IOCTL(RTC_PIE_ON)
COMPATIBLE_IOCTL(RTC_PIE_OFF)
COMPATIBLE_IOCTL(RTC_WIE_ON)
COMPATIBLE_IOCTL(RTC_WIE_OFF)
COMPATIBLE_IOCTL(RTC_ALM_SET)
COMPATIBLE_IOCTL(RTC_ALM_READ)
COMPATIBLE_IOCTL(RTC_RD_TIME)
COMPATIBLE_IOCTL(RTC_SET_TIME)
COMPATIBLE_IOCTL(RTC_WKALM_SET)
COMPATIBLE_IOCTL(RTC_WKALM_RD)
/* Little m */
COMPATIBLE_IOCTL(MTIOCTOP)
/* Socket level stuff */
COMPATIBLE_IOCTL(FIOSETOWN)
COMPATIBLE_IOCTL(SIOCSPGRP)
COMPATIBLE_IOCTL(FIOGETOWN)
COMPATIBLE_IOCTL(SIOCGPGRP)
COMPATIBLE_IOCTL(SIOCATMARK)
COMPATIBLE_IOCTL(SIOCSIFLINK)
COMPATIBLE_IOCTL(SIOCSIFENCAP)
COMPATIBLE_IOCTL(SIOCGIFENCAP)
COMPATIBLE_IOCTL(SIOCSIFNAME)
/* FIXME: not compatible
COMPATIBLE_IOCTL(SIOCSIFBR)
COMPATIBLE_IOCTL(SIOCGIFBR)
*/
COMPATIBLE_IOCTL(SIOCSARP)
COMPATIBLE_IOCTL(SIOCGARP)
COMPATIBLE_IOCTL(SIOCDARP)
COMPATIBLE_IOCTL(SIOCSRARP)
COMPATIBLE_IOCTL(SIOCGRARP)
COMPATIBLE_IOCTL(SIOCDRARP)
COMPATIBLE_IOCTL(SIOCADDDLCI)
COMPATIBLE_IOCTL(SIOCDELDLCI)
COMPATIBLE_IOCTL(SIOCGMIIPHY)
COMPATIBLE_IOCTL(SIOCGMIIREG)
COMPATIBLE_IOCTL(SIOCSMIIREG)
COMPATIBLE_IOCTL(SIOCGIFVLAN)
COMPATIBLE_IOCTL(SIOCSIFVLAN)
/* SG stuff */
COMPATIBLE_IOCTL(SG_SET_TIMEOUT)
COMPATIBLE_IOCTL(SG_GET_TIMEOUT)
COMPATIBLE_IOCTL(SG_EMULATED_HOST)
ULONG_IOCTL(SG_SET_TRANSFORM)
COMPATIBLE_IOCTL(SG_GET_TRANSFORM)
COMPATIBLE_IOCTL(SG_SET_RESERVED_SIZE)
COMPATIBLE_IOCTL(SG_GET_RESERVED_SIZE)
COMPATIBLE_IOCTL(SG_GET_SCSI_ID)
COMPATIBLE_IOCTL(SG_SET_FORCE_LOW_DMA)
COMPATIBLE_IOCTL(SG_GET_LOW_DMA)
COMPATIBLE_IOCTL(SG_SET_FORCE_PACK_ID)
COMPATIBLE_IOCTL(SG_GET_PACK_ID)
COMPATIBLE_IOCTL(SG_GET_NUM_WAITING)
COMPATIBLE_IOCTL(SG_SET_DEBUG)
COMPATIBLE_IOCTL(SG_GET_SG_TABLESIZE)
COMPATIBLE_IOCTL(SG_GET_COMMAND_Q)
COMPATIBLE_IOCTL(SG_SET_COMMAND_Q)
COMPATIBLE_IOCTL(SG_GET_VERSION_NUM)
COMPATIBLE_IOCTL(SG_NEXT_CMD_LEN)
COMPATIBLE_IOCTL(SG_SCSI_RESET)
COMPATIBLE_IOCTL(SG_GET_REQUEST_TABLE)
COMPATIBLE_IOCTL(SG_SET_KEEP_ORPHAN)
COMPATIBLE_IOCTL(SG_GET_KEEP_ORPHAN)
/* PPP stuff */
COMPATIBLE_IOCTL(PPPIOCGFLAGS)
COMPATIBLE_IOCTL(PPPIOCSFLAGS)
COMPATIBLE_IOCTL(PPPIOCGASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCSASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCGUNIT)
COMPATIBLE_IOCTL(PPPIOCGRASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCSRASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCGMRU)
COMPATIBLE_IOCTL(PPPIOCSMRU)
COMPATIBLE_IOCTL(PPPIOCSMAXCID)
COMPATIBLE_IOCTL(PPPIOCGXASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCSXASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCXFERUNIT)
/* PPPIOCSCOMPRESS is translated */
COMPATIBLE_IOCTL(PPPIOCGNPMODE)
COMPATIBLE_IOCTL(PPPIOCSNPMODE)
COMPATIBLE_IOCTL(PPPIOCGDEBUG)
COMPATIBLE_IOCTL(PPPIOCSDEBUG)
/* PPPIOCSPASS is translated */
/* PPPIOCSACTIVE is translated */
/* PPPIOCGIDLE is translated */
COMPATIBLE_IOCTL(PPPIOCNEWUNIT)
COMPATIBLE_IOCTL(PPPIOCATTACH)
COMPATIBLE_IOCTL(PPPIOCDETACH)
COMPATIBLE_IOCTL(PPPIOCSMRRU)
COMPATIBLE_IOCTL(PPPIOCCONNECT)
COMPATIBLE_IOCTL(PPPIOCDISCONN)
COMPATIBLE_IOCTL(PPPIOCATTCHAN)
COMPATIBLE_IOCTL(PPPIOCGCHAN)
/* PPPOX */
COMPATIBLE_IOCTL(PPPOEIOCSFWD)
COMPATIBLE_IOCTL(PPPOEIOCDFWD)
/* LP */
COMPATIBLE_IOCTL(LPGETSTATUS)
/* CDROM stuff */
COMPATIBLE_IOCTL(CDROMPAUSE)
COMPATIBLE_IOCTL(CDROMRESUME)
COMPATIBLE_IOCTL(CDROMPLAYMSF)
COMPATIBLE_IOCTL(CDROMPLAYTRKIND)
COMPATIBLE_IOCTL(CDROMREADTOCHDR)
COMPATIBLE_IOCTL(CDROMREADTOCENTRY)
COMPATIBLE_IOCTL(CDROMSTOP)
COMPATIBLE_IOCTL(CDROMSTART)
COMPATIBLE_IOCTL(CDROMEJECT)
COMPATIBLE_IOCTL(CDROMVOLCTRL)
COMPATIBLE_IOCTL(CDROMSUBCHNL)
ULONG_IOCTL(CDROMEJECT_SW)
COMPATIBLE_IOCTL(CDROMMULTISESSION)
COMPATIBLE_IOCTL(CDROM_GET_MCN)
COMPATIBLE_IOCTL(CDROMRESET)
COMPATIBLE_IOCTL(CDROMVOLREAD)
COMPATIBLE_IOCTL(CDROMSEEK)
COMPATIBLE_IOCTL(CDROMPLAYBLK)
COMPATIBLE_IOCTL(CDROMCLOSETRAY)
ULONG_IOCTL(CDROM_SET_OPTIONS)
ULONG_IOCTL(CDROM_CLEAR_OPTIONS)
ULONG_IOCTL(CDROM_SELECT_SPEED)
ULONG_IOCTL(CDROM_SELECT_DISC)
ULONG_IOCTL(CDROM_MEDIA_CHANGED)
ULONG_IOCTL(CDROM_DRIVE_STATUS)
COMPATIBLE_IOCTL(CDROM_DISC_STATUS)
COMPATIBLE_IOCTL(CDROM_CHANGER_NSLOTS)
ULONG_IOCTL(CDROM_LOCKDOOR)
ULONG_IOCTL(CDROM_DEBUG)
COMPATIBLE_IOCTL(CDROM_GET_CAPABILITY)
/* Ignore cdrom.h about these next 5 ioctls, they absolutely do
 * not take a struct cdrom_read, instead they take a struct cdrom_msf
 * which is compatible.
 */
COMPATIBLE_IOCTL(CDROMREADMODE2)
COMPATIBLE_IOCTL(CDROMREADMODE1)
COMPATIBLE_IOCTL(CDROMREADRAW)
COMPATIBLE_IOCTL(CDROMREADCOOKED)
COMPATIBLE_IOCTL(CDROMREADALL)
/* DVD ioctls */
COMPATIBLE_IOCTL(DVD_READ_STRUCT)
COMPATIBLE_IOCTL(DVD_WRITE_STRUCT)
COMPATIBLE_IOCTL(DVD_AUTH)
/* Big L */
ULONG_IOCTL(LOOP_SET_FD)
COMPATIBLE_IOCTL(LOOP_CLR_FD)
COMPATIBLE_IOCTL(LOOP_GET_STATUS64)
COMPATIBLE_IOCTL(LOOP_SET_STATUS64)
/* Big A */
/* sparc only */
/* Big Q for sound/OSS */
COMPATIBLE_IOCTL(SNDCTL_SEQ_RESET)
COMPATIBLE_IOCTL(SNDCTL_SEQ_SYNC)
COMPATIBLE_IOCTL(SNDCTL_SYNTH_INFO)
COMPATIBLE_IOCTL(SNDCTL_SEQ_CTRLRATE)
COMPATIBLE_IOCTL(SNDCTL_SEQ_GETOUTCOUNT)
COMPATIBLE_IOCTL(SNDCTL_SEQ_GETINCOUNT)
COMPATIBLE_IOCTL(SNDCTL_SEQ_PERCMODE)
COMPATIBLE_IOCTL(SNDCTL_FM_LOAD_INSTR)
COMPATIBLE_IOCTL(SNDCTL_SEQ_TESTMIDI)
COMPATIBLE_IOCTL(SNDCTL_SEQ_RESETSAMPLES)
COMPATIBLE_IOCTL(SNDCTL_SEQ_NRSYNTHS)
COMPATIBLE_IOCTL(SNDCTL_SEQ_NRMIDIS)
COMPATIBLE_IOCTL(SNDCTL_MIDI_INFO)
COMPATIBLE_IOCTL(SNDCTL_SEQ_THRESHOLD)
COMPATIBLE_IOCTL(SNDCTL_SYNTH_MEMAVL)
COMPATIBLE_IOCTL(SNDCTL_FM_4OP_ENABLE)
COMPATIBLE_IOCTL(SNDCTL_SEQ_PANIC)
COMPATIBLE_IOCTL(SNDCTL_SEQ_OUTOFBAND)
COMPATIBLE_IOCTL(SNDCTL_SEQ_GETTIME)
COMPATIBLE_IOCTL(SNDCTL_SYNTH_ID)
COMPATIBLE_IOCTL(SNDCTL_SYNTH_CONTROL)
COMPATIBLE_IOCTL(SNDCTL_SYNTH_REMOVESAMPLE)
/* Big T for sound/OSS */
COMPATIBLE_IOCTL(SNDCTL_TMR_TIMEBASE)
COMPATIBLE_IOCTL(SNDCTL_TMR_START)
COMPATIBLE_IOCTL(SNDCTL_TMR_STOP)
COMPATIBLE_IOCTL(SNDCTL_TMR_CONTINUE)
COMPATIBLE_IOCTL(SNDCTL_TMR_TEMPO)
COMPATIBLE_IOCTL(SNDCTL_TMR_SOURCE)
COMPATIBLE_IOCTL(SNDCTL_TMR_METRONOME)
COMPATIBLE_IOCTL(SNDCTL_TMR_SELECT)
/* Little m for sound/OSS */
COMPATIBLE_IOCTL(SNDCTL_MIDI_PRETIME)
COMPATIBLE_IOCTL(SNDCTL_MIDI_MPUMODE)
COMPATIBLE_IOCTL(SNDCTL_MIDI_MPUCMD)
/* Big P for sound/OSS */
COMPATIBLE_IOCTL(SNDCTL_DSP_RESET)
COMPATIBLE_IOCTL(SNDCTL_DSP_SYNC)
COMPATIBLE_IOCTL(SNDCTL_DSP_SPEED)
COMPATIBLE_IOCTL(SNDCTL_DSP_STEREO)
COMPATIBLE_IOCTL(SNDCTL_DSP_GETBLKSIZE)
COMPATIBLE_IOCTL(SNDCTL_DSP_CHANNELS)
COMPATIBLE_IOCTL(SOUND_PCM_WRITE_FILTER)
COMPATIBLE_IOCTL(SNDCTL_DSP_POST)
COMPATIBLE_IOCTL(SNDCTL_DSP_SUBDIVIDE)
COMPATIBLE_IOCTL(SNDCTL_DSP_SETFRAGMENT)
COMPATIBLE_IOCTL(SNDCTL_DSP_GETFMTS)
COMPATIBLE_IOCTL(SNDCTL_DSP_SETFMT)
COMPATIBLE_IOCTL(SNDCTL_DSP_GETOSPACE)
COMPATIBLE_IOCTL(SNDCTL_DSP_GETISPACE)
COMPATIBLE_IOCTL(SNDCTL_DSP_NONBLOCK)
COMPATIBLE_IOCTL(SNDCTL_DSP_GETCAPS)
COMPATIBLE_IOCTL(SNDCTL_DSP_GETTRIGGER)
COMPATIBLE_IOCTL(SNDCTL_DSP_SETTRIGGER)
COMPATIBLE_IOCTL(SNDCTL_DSP_GETIPTR)
COMPATIBLE_IOCTL(SNDCTL_DSP_GETOPTR)
/* SNDCTL_DSP_MAPINBUF,  XXX needs translation */
/* SNDCTL_DSP_MAPOUTBUF,  XXX needs translation */
COMPATIBLE_IOCTL(SNDCTL_DSP_SETSYNCRO)
COMPATIBLE_IOCTL(SNDCTL_DSP_SETDUPLEX)
COMPATIBLE_IOCTL(SNDCTL_DSP_GETODELAY)
COMPATIBLE_IOCTL(SNDCTL_DSP_PROFILE)
COMPATIBLE_IOCTL(SOUND_PCM_READ_RATE)
COMPATIBLE_IOCTL(SOUND_PCM_READ_CHANNELS)
COMPATIBLE_IOCTL(SOUND_PCM_READ_BITS)
COMPATIBLE_IOCTL(SOUND_PCM_READ_FILTER)
/* Big C for sound/OSS */
COMPATIBLE_IOCTL(SNDCTL_COPR_RESET)
COMPATIBLE_IOCTL(SNDCTL_COPR_LOAD)
COMPATIBLE_IOCTL(SNDCTL_COPR_RDATA)
COMPATIBLE_IOCTL(SNDCTL_COPR_RCODE)
COMPATIBLE_IOCTL(SNDCTL_COPR_WDATA)
COMPATIBLE_IOCTL(SNDCTL_COPR_WCODE)
COMPATIBLE_IOCTL(SNDCTL_COPR_RUN)
COMPATIBLE_IOCTL(SNDCTL_COPR_HALT)
COMPATIBLE_IOCTL(SNDCTL_COPR_SENDMSG)
COMPATIBLE_IOCTL(SNDCTL_COPR_RCVMSG)
/* Big M for sound/OSS */
COMPATIBLE_IOCTL(SOUND_MIXER_READ_VOLUME)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_BASS)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_TREBLE)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_SYNTH)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_PCM)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_SPEAKER)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_LINE)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_MIC)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_CD)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_IMIX)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_ALTPCM)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_RECLEV)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_IGAIN)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_OGAIN)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_LINE1)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_LINE2)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_LINE3)
COMPATIBLE_IOCTL(MIXER_READ(SOUND_MIXER_DIGITAL1))
COMPATIBLE_IOCTL(MIXER_READ(SOUND_MIXER_DIGITAL2))
COMPATIBLE_IOCTL(MIXER_READ(SOUND_MIXER_DIGITAL3))
COMPATIBLE_IOCTL(MIXER_READ(SOUND_MIXER_PHONEIN))
COMPATIBLE_IOCTL(MIXER_READ(SOUND_MIXER_PHONEOUT))
COMPATIBLE_IOCTL(MIXER_READ(SOUND_MIXER_VIDEO))
COMPATIBLE_IOCTL(MIXER_READ(SOUND_MIXER_RADIO))
COMPATIBLE_IOCTL(MIXER_READ(SOUND_MIXER_MONITOR))
COMPATIBLE_IOCTL(SOUND_MIXER_READ_MUTE)
/* SOUND_MIXER_READ_ENHANCE,  same value as READ_MUTE */
/* SOUND_MIXER_READ_LOUD,  same value as READ_MUTE */
COMPATIBLE_IOCTL(SOUND_MIXER_READ_RECSRC)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_DEVMASK)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_RECMASK)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_STEREODEVS)
COMPATIBLE_IOCTL(SOUND_MIXER_READ_CAPS)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_VOLUME)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_BASS)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_TREBLE)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_SYNTH)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_PCM)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_SPEAKER)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_LINE)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_MIC)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_CD)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_IMIX)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_ALTPCM)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_RECLEV)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_IGAIN)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_OGAIN)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_LINE1)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_LINE2)
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_LINE3)
COMPATIBLE_IOCTL(MIXER_WRITE(SOUND_MIXER_DIGITAL1))
COMPATIBLE_IOCTL(MIXER_WRITE(SOUND_MIXER_DIGITAL2))
COMPATIBLE_IOCTL(MIXER_WRITE(SOUND_MIXER_DIGITAL3))
COMPATIBLE_IOCTL(MIXER_WRITE(SOUND_MIXER_PHONEIN))
COMPATIBLE_IOCTL(MIXER_WRITE(SOUND_MIXER_PHONEOUT))
COMPATIBLE_IOCTL(MIXER_WRITE(SOUND_MIXER_VIDEO))
COMPATIBLE_IOCTL(MIXER_WRITE(SOUND_MIXER_RADIO))
COMPATIBLE_IOCTL(MIXER_WRITE(SOUND_MIXER_MONITOR))
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_MUTE)
/* SOUND_MIXER_WRITE_ENHANCE,  same value as WRITE_MUTE */
/* SOUND_MIXER_WRITE_LOUD,  same value as WRITE_MUTE */
COMPATIBLE_IOCTL(SOUND_MIXER_WRITE_RECSRC)
COMPATIBLE_IOCTL(SOUND_MIXER_INFO)
COMPATIBLE_IOCTL(SOUND_OLD_MIXER_INFO)
COMPATIBLE_IOCTL(SOUND_MIXER_ACCESS)
COMPATIBLE_IOCTL(SOUND_MIXER_AGC)
COMPATIBLE_IOCTL(SOUND_MIXER_3DSE)
COMPATIBLE_IOCTL(SOUND_MIXER_PRIVATE1)
COMPATIBLE_IOCTL(SOUND_MIXER_PRIVATE2)
COMPATIBLE_IOCTL(SOUND_MIXER_PRIVATE3)
COMPATIBLE_IOCTL(SOUND_MIXER_PRIVATE4)
COMPATIBLE_IOCTL(SOUND_MIXER_PRIVATE5)
COMPATIBLE_IOCTL(SOUND_MIXER_GETLEVELS)
COMPATIBLE_IOCTL(SOUND_MIXER_SETLEVELS)
COMPATIBLE_IOCTL(OSS_GETVERSION)
/* AUTOFS */
ULONG_IOCTL(AUTOFS_IOC_READY)
ULONG_IOCTL(AUTOFS_IOC_FAIL)
COMPATIBLE_IOCTL(AUTOFS_IOC_CATATONIC)
COMPATIBLE_IOCTL(AUTOFS_IOC_PROTOVER)
COMPATIBLE_IOCTL(AUTOFS_IOC_EXPIRE)
COMPATIBLE_IOCTL(AUTOFS_IOC_EXPIRE_MULTI)
/* DEVFS */
COMPATIBLE_IOCTL(DEVFSDIOC_GET_PROTO_REV)
COMPATIBLE_IOCTL(DEVFSDIOC_SET_EVENT_MASK)
COMPATIBLE_IOCTL(DEVFSDIOC_RELEASE_EVENT_QUEUE)
COMPATIBLE_IOCTL(DEVFSDIOC_SET_DEBUG_MASK)
/* Raw devices */
COMPATIBLE_IOCTL(RAW_SETBIND)
COMPATIBLE_IOCTL(RAW_GETBIND)
/* SMB ioctls which do not need any translations */
COMPATIBLE_IOCTL(SMB_IOC_NEWCONN)
/* NCP ioctls which do not need any translations */
COMPATIBLE_IOCTL(NCP_IOC_CONN_LOGGED_IN)
COMPATIBLE_IOCTL(NCP_IOC_SIGN_INIT)
COMPATIBLE_IOCTL(NCP_IOC_SIGN_WANTED)
COMPATIBLE_IOCTL(NCP_IOC_SET_SIGN_WANTED)
COMPATIBLE_IOCTL(NCP_IOC_LOCKUNLOCK)
COMPATIBLE_IOCTL(NCP_IOC_GETROOT)
COMPATIBLE_IOCTL(NCP_IOC_SETROOT)
COMPATIBLE_IOCTL(NCP_IOC_GETCHARSETS)
COMPATIBLE_IOCTL(NCP_IOC_SETCHARSETS)
COMPATIBLE_IOCTL(NCP_IOC_GETDENTRYTTL)
COMPATIBLE_IOCTL(NCP_IOC_SETDENTRYTTL)
/* Little a */
COMPATIBLE_IOCTL(ATMSIGD_CTRL)
COMPATIBLE_IOCTL(ATMARPD_CTRL)
COMPATIBLE_IOCTL(ATMLEC_CTRL)
COMPATIBLE_IOCTL(ATMLEC_MCAST)
COMPATIBLE_IOCTL(ATMLEC_DATA)
COMPATIBLE_IOCTL(ATM_SETSC)
COMPATIBLE_IOCTL(SIOCSIFATMTCP)
COMPATIBLE_IOCTL(SIOCMKCLIP)
COMPATIBLE_IOCTL(ATMARP_MKIP)
COMPATIBLE_IOCTL(ATMARP_SETENTRY)
COMPATIBLE_IOCTL(ATMARP_ENCAP)
COMPATIBLE_IOCTL(ATMTCP_CREATE)
COMPATIBLE_IOCTL(ATMTCP_REMOVE)
COMPATIBLE_IOCTL(ATMMPC_CTRL)
COMPATIBLE_IOCTL(ATMMPC_DATA)
/* Big W */
/* WIOC_GETSUPPORT not yet implemented -E */
COMPATIBLE_IOCTL(WDIOC_GETSTATUS)
COMPATIBLE_IOCTL(WDIOC_GETBOOTSTATUS)
COMPATIBLE_IOCTL(WDIOC_GETTEMP)
COMPATIBLE_IOCTL(WDIOC_SETOPTIONS)
COMPATIBLE_IOCTL(WDIOC_KEEPALIVE)
/* Big R */
COMPATIBLE_IOCTL(RNDGETENTCNT)
COMPATIBLE_IOCTL(RNDADDTOENTCNT)
COMPATIBLE_IOCTL(RNDGETPOOL)
COMPATIBLE_IOCTL(RNDADDENTROPY)
COMPATIBLE_IOCTL(RNDZAPENTCNT)
COMPATIBLE_IOCTL(RNDCLEARPOOL)
/* Bluetooth ioctls */
COMPATIBLE_IOCTL(HCIDEVUP)
COMPATIBLE_IOCTL(HCIDEVDOWN)
COMPATIBLE_IOCTL(HCIDEVRESET)
COMPATIBLE_IOCTL(HCIDEVRESTAT)
COMPATIBLE_IOCTL(HCIGETDEVLIST)
COMPATIBLE_IOCTL(HCIGETDEVINFO)
COMPATIBLE_IOCTL(HCIGETCONNLIST)
COMPATIBLE_IOCTL(HCIGETCONNINFO)
COMPATIBLE_IOCTL(HCISETRAW)
COMPATIBLE_IOCTL(HCISETSCAN)
COMPATIBLE_IOCTL(HCISETAUTH)
COMPATIBLE_IOCTL(HCISETENCRYPT)
COMPATIBLE_IOCTL(HCISETPTYPE)
COMPATIBLE_IOCTL(HCISETLINKPOL)
COMPATIBLE_IOCTL(HCISETLINKMODE)
COMPATIBLE_IOCTL(HCISETACLMTU)
COMPATIBLE_IOCTL(HCISETSCOMTU)
COMPATIBLE_IOCTL(HCIINQUIRY)
COMPATIBLE_IOCTL(HCIUARTSETPROTO)
COMPATIBLE_IOCTL(HCIUARTGETPROTO)
COMPATIBLE_IOCTL(RFCOMMCREATEDEV)
COMPATIBLE_IOCTL(RFCOMMRELEASEDEV)
COMPATIBLE_IOCTL(RFCOMMGETDEVLIST)
COMPATIBLE_IOCTL(RFCOMMGETDEVINFO)
COMPATIBLE_IOCTL(RFCOMMSTEALDLC)
COMPATIBLE_IOCTL(BNEPCONNADD)
COMPATIBLE_IOCTL(BNEPCONNDEL)
COMPATIBLE_IOCTL(BNEPGETCONNLIST)
COMPATIBLE_IOCTL(BNEPGETCONNINFO)
/* Misc. */
COMPATIBLE_IOCTL(0x41545900)		/* ATYIO_CLKR */
COMPATIBLE_IOCTL(0x41545901)		/* ATYIO_CLKW */
COMPATIBLE_IOCTL(PCIIOC_CONTROLLER)
COMPATIBLE_IOCTL(PCIIOC_MMAP_IS_IO)
COMPATIBLE_IOCTL(PCIIOC_MMAP_IS_MEM)
COMPATIBLE_IOCTL(PCIIOC_WRITE_COMBINE)
/* USB */
COMPATIBLE_IOCTL(USBDEVFS_RESETEP)
COMPATIBLE_IOCTL(USBDEVFS_SETINTERFACE)
COMPATIBLE_IOCTL(USBDEVFS_SETCONFIGURATION)
COMPATIBLE_IOCTL(USBDEVFS_GETDRIVER)
COMPATIBLE_IOCTL(USBDEVFS_DISCARDURB)
COMPATIBLE_IOCTL(USBDEVFS_CLAIMINTERFACE)
COMPATIBLE_IOCTL(USBDEVFS_RELEASEINTERFACE)
COMPATIBLE_IOCTL(USBDEVFS_CONNECTINFO)
COMPATIBLE_IOCTL(USBDEVFS_HUB_PORTINFO)
COMPATIBLE_IOCTL(USBDEVFS_RESET)
COMPATIBLE_IOCTL(USBDEVFS_CLEAR_HALT)
/* MTD */
COMPATIBLE_IOCTL(MEMGETINFO)
COMPATIBLE_IOCTL(MEMERASE)
COMPATIBLE_IOCTL(MEMLOCK)
COMPATIBLE_IOCTL(MEMUNLOCK)
COMPATIBLE_IOCTL(MEMGETREGIONCOUNT)
COMPATIBLE_IOCTL(MEMGETREGIONINFO)
/* NBD */
ULONG_IOCTL(NBD_SET_SOCK)
ULONG_IOCTL(NBD_SET_BLKSIZE)
ULONG_IOCTL(NBD_SET_SIZE)
COMPATIBLE_IOCTL(NBD_DO_IT)
COMPATIBLE_IOCTL(NBD_CLEAR_SOCK)
COMPATIBLE_IOCTL(NBD_CLEAR_QUE)
COMPATIBLE_IOCTL(NBD_PRINT_DEBUG)
ULONG_IOCTL(NBD_SET_SIZE_BLOCKS)
COMPATIBLE_IOCTL(NBD_DISCONNECT)
/* i2c */
COMPATIBLE_IOCTL(I2C_SLAVE)
COMPATIBLE_IOCTL(I2C_SLAVE_FORCE)
COMPATIBLE_IOCTL(I2C_TENBIT)
COMPATIBLE_IOCTL(I2C_PEC)
COMPATIBLE_IOCTL(I2C_RETRIES)
COMPATIBLE_IOCTL(I2C_TIMEOUT)
/* wireless */
COMPATIBLE_IOCTL(SIOCSIWCOMMIT)
COMPATIBLE_IOCTL(SIOCGIWNAME)
COMPATIBLE_IOCTL(SIOCSIWNWID)
COMPATIBLE_IOCTL(SIOCGIWNWID)
COMPATIBLE_IOCTL(SIOCSIWFREQ)
COMPATIBLE_IOCTL(SIOCGIWFREQ)
COMPATIBLE_IOCTL(SIOCSIWMODE)
COMPATIBLE_IOCTL(SIOCGIWMODE)
COMPATIBLE_IOCTL(SIOCSIWSENS)
COMPATIBLE_IOCTL(SIOCGIWSENS)
COMPATIBLE_IOCTL(SIOCSIWRANGE)
COMPATIBLE_IOCTL(SIOCSIWPRIV)
COMPATIBLE_IOCTL(SIOCGIWPRIV)
COMPATIBLE_IOCTL(SIOCSIWSTATS)
COMPATIBLE_IOCTL(SIOCGIWSTATS)
COMPATIBLE_IOCTL(SIOCSIWAP)
COMPATIBLE_IOCTL(SIOCGIWAP)
COMPATIBLE_IOCTL(SIOCSIWSCAN)
COMPATIBLE_IOCTL(SIOCSIWRATE)
COMPATIBLE_IOCTL(SIOCGIWRATE)
COMPATIBLE_IOCTL(SIOCSIWRTS)
COMPATIBLE_IOCTL(SIOCGIWRTS)
COMPATIBLE_IOCTL(SIOCSIWFRAG)
COMPATIBLE_IOCTL(SIOCGIWFRAG)
COMPATIBLE_IOCTL(SIOCSIWTXPOW)
COMPATIBLE_IOCTL(SIOCGIWTXPOW)
COMPATIBLE_IOCTL(SIOCSIWRETRY)
COMPATIBLE_IOCTL(SIOCGIWRETRY)
COMPATIBLE_IOCTL(SIOCSIWPOWER)
COMPATIBLE_IOCTL(SIOCGIWPOWER)
