/* $XFree86$ */
/*
 * Mode initializing code (CRT2 section)
 * for SiS 300/305/540/630/730 and
 *     SiS 315/550/650/M650/651/661FX/M661xX/740/741/M741/330/660/M660/760/M760
 * (Universal module for Linux kernel framebuffer and XFree86 4.x)
 *
 * Copyright (C) 2001-2004 by Thomas Winischhofer, Vienna, Austria
 *
 * If distributed as part of the Linux kernel, the following license terms
 * apply:
 *
 * * This program is free software; you can redistribute it and/or modify
 * * it under the terms of the GNU General Public License as published by
 * * the Free Software Foundation; either version 2 of the named License,
 * * or any later version.
 * *
 * * This program is distributed in the hope that it will be useful,
 * * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * * GNU General Public License for more details.
 * *
 * * You should have received a copy of the GNU General Public License
 * * along with this program; if not, write to the Free Software
 * * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Otherwise, the following license terms apply:
 *
 * * Redistribution and use in source and binary forms, with or without
 * * modification, are permitted provided that the following conditions
 * * are met:
 * * 1) Redistributions of source code must retain the above copyright
 * *    notice, this list of conditions and the following disclaimer.
 * * 2) Redistributions in binary form must reproduce the above copyright
 * *    notice, this list of conditions and the following disclaimer in the
 * *    documentation and/or other materials provided with the distribution.
 * * 3) All advertising materials mentioning features or use of this software
 * *    must display the following acknowledgement: "This product includes
 * *    software developed by Thomas Winischhofer, Vienna, Austria."
 * * 4) The name of the author may not be used to endorse or promote products
 * *    derived from this software without specific prior written permission.
 * *
 * * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: 	Thomas Winischhofer <thomas@winischhofer.net>
 *
 * Formerly based on non-functional code-fragements for 300 series by SiS, Inc.
 * Used by permission.
 *
 * TW says: This code looks awful, I know. But please don't do anything about
 * this otherwise debugging will be hell.
 * The code is extremely fragile as regards the different chipsets, different
 * video bridges and combinations thereof. If anything is changed, extreme
 * care has to be taken that that change doesn't break it for other chipsets,
 * bridges or combinations thereof.
 * All comments in this file are by me, regardless if marked TW or not.
 *
 */

#if 1
#define SET_EMI		/* 302LV/ELV: Set EMI values */
#endif

#define COMPAL_HACK	/* Needed for Compal 1400x1050 (EMI) */
#define COMPAQ_HACK	/* Needed for Inventec/Compaq 1280x1024 (EMI) */
#define ASUS_HACK	/* Needed for Asus A2H 1024x768 (EMI) */

#include "init301.h"

#if 0
#define TWNEWPANEL
#endif

#ifdef SIS300
#include "oem300.h"
#endif

#ifdef SIS315H
#include "oem310.h"
#endif

#define SiS_I2CDELAY      1000
#define SiS_I2CDELAYSHORT  150

/*********************************************/
/*         HELPER: Lock/Unlock CRT2          */
/*********************************************/

void
SiS_UnLockCRT2(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  if(HwInfo->jChipType >= SIS_315H)
     SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2f,0x01);
  else
     SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x24,0x01);
}

void
SiS_LockCRT2(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  if(HwInfo->jChipType >= SIS_315H)
     SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2F,0xFE);
  else
     SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x24,0xFE);
}

/*********************************************/
/*            HELPER: Enable CRT2            */
/*********************************************/

void
SiS_EnableCRT2(SiS_Private *SiS_Pr)
{
  SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);
}

/*********************************************/
/*            HELPER: Write SR11             */
/*********************************************/

static void
SiS_SetRegSR11ANDOR(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo, USHORT DataAND, USHORT DataOR)
{
   if(HwInfo->jChipType >= SIS_661) DataAND &= 0x0f;
   SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x11,DataAND,DataOR);
}

/*********************************************/
/*    HELPER: Get Pointer to LCD structure   */
/*********************************************/

/* For 661 series only */
#ifdef SIS315H
#if 0   /* Need to wait until hardware using this really exists */
static UCHAR *
GetLCDPtr661(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo, int tabletype,
             USHORT ModeNo, USHORT ModeIdIndex, USHORT RRTI)
{
  UCHAR *ROMAddr =  HwInfo->pjVirtualRomBase;
  UCHAR *tableptr = NULL;
  UCHAR tablelengths[] = { 8, 7, 6, 6, 8, 6, 0, 0, 0 };
  USHORT modeflag, CRT2Index, tablelength, lcdid, myid, tableptri;

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     CRT2Index = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     CRT2Index = SiS_Pr->SiS_RefIndex[RRTI].Ext_CRT2CRTC;
     /* This is total bullshit: */
     if(SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO == SIS_RI_720x576) CRT2Index = 10;
  }

  if(tabletype <= 1) {
#if 0	/* Not yet implemented */
     if(ModeNo <= 0x13) {
        CRT2Index = SiS_Pr->SiS_SModeIDTable[ModeIdIndex]. + 5;
     } else {
        CRT2Index = SiS_Pr->SiS_RefIndex[RRTI]. + 5;
     }
     if(tabletype & 1) CRT2Index >>= 4;
#endif
  }

  CRT2Index &= 0x0f;

  tablelength = tablelengths[tabletype];
  if(SiS_Pr->SiS_LCDInfo & LCDPass11) {
     if((tabletype == 5) || (tabletype == 7)) tablelength = 8;
     if((tabletype == 3) || (tabletype == 8)) tablelength = 8;
  }

  if(!tablelength) return NULL;

  tableptri = ROMAddr[0x222] | (ROMAddr[0x223] << 8);
  tableptri += (tabletype << 1);
  if(!tableptri) return NULL;
  tableptr = &ROMAddr[tableptri];

  do {
     lcdid = tableptr[0];
     if(lcdid == 0xff) break;
     myid = SiS_Pr->SiS_LCDResInfo;
     if((lcdid & 0x80) && (lcdid != 0x80)) {
        lcdid &= 0x7f;
	myid = SiS_Pr->SiS_LCDTypeInfo;
     }
     if(SiS_Pr->SiS_LCDInfo & LCDPass11) myid &= ~0x1f;

     if(myid == lcdid) {
	lcdid = tableptr[1] | (tableptr[2] << 8);
	myid = SiS_Pr->SiS_LCDInfo661;
	if(modeflag & HalfDCLK) myid |= 0x200;
	if(ModeNo <= 0x13)      myid |= 0x400;
	lcdid &= myid;
	myid = tableptr[3] | (tableptr[4] << 8);
	if(lcdid == myid) break;
     }
     tableptr += 7;
  } while (1);

  if(lcdid == myid) {
     lcdid = tableptr[5] | (tableptr[6] << 8);
     lcdid += (tablelength * CRT2Index);
     return((UCHAR *)&ROMAddr[lcdid]);
  }

  return NULL;
}
#endif

static UCHAR *
GetLCDStructPtr661(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
   UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
   USHORT lcdres = SiS_Pr->SiS_LCDResInfo;
   USHORT lcdtype = SiS_Pr->SiS_LCDTypeInfo;
   USHORT romindex=0;
   UCHAR  *myptr = NULL;
   UCHAR  lcdid;

   if((ROMAddr) && SiS_Pr->SiS_UseROM) {
      romindex = ROMAddr[0x256] | (ROMAddr[0x257] << 8);
   }
   if(romindex) {
      myptr = &ROMAddr[romindex];
   } else {
      myptr = (UCHAR *)SiS_LCDStruct661;
   }

   while(myptr[0] != 0xff) {
      lcdid = myptr[0];
      if((lcdid & 0x80) && (lcdid != 0x80)) {
         lcdres = lcdtype;
	 lcdid &= 0x7f;
      }
      if(lcdid == lcdres) break;
      myptr += 26;
   }
   if(myptr[0] == 0xff) return NULL;

   return myptr;
}
#endif

/*********************************************/
/*           Adjust Rate for CRT2            */
/*********************************************/

static BOOLEAN
SiS_AdjustCRT2Rate(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
                   USHORT RefreshRateTableIndex, USHORT *i,
		   PSIS_HW_INFO HwInfo)
{
  USHORT tempax,tempbx,infoflag;

  tempbx = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex + (*i)].ModeID;

  tempax = 0;

  if(SiS_Pr->SiS_VBType & VB_SISVB) {

     if(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC) {

      	tempax |= SupportRAMDAC2;
	if(HwInfo->jChipType >= SIS_315H) {
	   tempax |= SupportRAMDAC2_135;
	   if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
	      tempax |= SupportRAMDAC2_162;
	      if(SiS_Pr->SiS_VBType & VB_SIS301C) {
		 tempax |= SupportRAMDAC2_202;
	      }
	   }
	}

     } else if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {

     	tempax |= SupportLCD;
	if(HwInfo->jChipType >= SIS_315H) {
	   if(SiS_Pr->SiS_VBType & VB_SIS301B302B) {
	      if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
	         if(tempbx == 0x2e) {  /* 640x480 */
		    tempax |= Support64048060Hz;
		 }
	      }
	   }
	}

     } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {

      	tempax |= SupportHiVision;

     } else if(SiS_Pr->SiS_VBInfo & (SetCRT2ToYPbPr525750|SetCRT2ToAVIDEO|SetCRT2ToSVIDEO|SetCRT2ToSCART)) {

        tempax |= SupportTV;
	if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
	   tempax |= SupportTV1024;
	}

     }

  } else {	/* for LVDS  */

     if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
     	if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
           tempax |= SupportCHTV;
      	}
     }

     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
     	tempax |= SupportLCD;
     }

  }

  /* Look backwards in table for matching CRT2 mode */
  for(; SiS_Pr->SiS_RefIndex[RefreshRateTableIndex+(*i)].ModeID == tempbx; (*i)--) {
     infoflag = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex + (*i)].Ext_InfoFlag;
     if(infoflag & tempax) return(1);
     if((*i) == 0) break;
  }

  /* Look through the whole mode-section of the table from the beginning
   * for a matching CRT2 mode if no mode was found yet.
   */
  for((*i) = 0; ; (*i)++) {
     if(SiS_Pr->SiS_RefIndex[RefreshRateTableIndex + (*i)].ModeID != tempbx) {
     	return(0);
     }
     infoflag = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex + (*i)].Ext_InfoFlag;
     if(infoflag & tempax) return(1);
  }
  return(1);
}

/*********************************************/
/*              Get rate pointer             */
/*********************************************/

USHORT
SiS_GetRatePtr(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
               PSIS_HW_INFO HwInfo)
{
  SHORT  LCDRefreshIndex[] = { 0x00, 0x00, 0x01, 0x01,
                               0x01, 0x01, 0x01, 0x01,
			       0x01, 0x01, 0x01, 0x01,
			       0x01, 0x01, 0x01, 0x01,
			       0x00, 0x00, 0x00, 0x00 };
  USHORT RefreshRateTableIndex,i,backup_i;
  USHORT modeflag,index,temp,backupindex;

  /* Do NOT check for UseCustomMode here, will skrew up FIFO */
  if(ModeNo == 0xfe) return 0;

  if(ModeNo <= 0x13)
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  else
     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;

  if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
     	if(modeflag & HalfDCLK) return(0);
     }
  }

  if(ModeNo < 0x14) return(0xFFFF);

  /* CR33 holds refresh rate index for CRT1 [3:0] and CRT2 [7:4]. */

  index = (SiS_GetReg(SiS_Pr->SiS_P3d4,0x33) >> SiS_Pr->SiS_SelectCRT2Rate) & 0x0F;
  backupindex = index;

  if(index > 0) index--;

  if(SiS_Pr->SiS_SetFlag & ProgrammingCRT2) {
     if(SiS_Pr->SiS_VBType & VB_SISVB) {
        if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
	   if(SiS_Pr->SiS_VBType & VB_NoLCD)		index = 0;
	   else if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) index = backupindex = 0;
	}
	if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
	   if(!(SiS_Pr->SiS_VBType & VB_NoLCD)) {
              temp = LCDRefreshIndex[SiS_Pr->SiS_LCDResInfo];
              if(index > temp) index = temp;
	   }
	}
     } else {
        if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) index = 0;
	if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
           if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) index = 0;
        }
     }
  }

  RefreshRateTableIndex = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].REFindex;
  ModeNo = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].ModeID;

  if(HwInfo->jChipType >= SIS_315H) {
     if(!(SiS_Pr->SiS_VBInfo & DriverMode)) {
        if( (SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_VESAID == 0x105) ||
            (SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_VESAID == 0x107) ) {
           if(backupindex <= 1) RefreshRateTableIndex++;
        }
     }
  }

  i = 0;
  do {
     if(SiS_Pr->SiS_RefIndex[RefreshRateTableIndex + i].ModeID != ModeNo) break;
     temp = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex + i].Ext_InfoFlag;
     temp &= ModeInfoFlag;
     if(temp < SiS_Pr->SiS_ModeType) break;
     i++;
     index--;
  } while(index != 0xFFFF);

  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC)) {
     if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
      	temp = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex + i - 1].Ext_InfoFlag;
      	if(temp & InterlaceMode) i++;
     }
  }

  i--;

  if((SiS_Pr->SiS_SetFlag & ProgrammingCRT2) && (!(SiS_Pr->SiS_VBInfo & DisableCRT2Display))) {
     backup_i = i;
     if(!(SiS_AdjustCRT2Rate(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex, &i, HwInfo))) {
	i = backup_i;
     }
  }

  return(RefreshRateTableIndex + i);
}

/*********************************************/
/*            STORE CRT2 INFO in CR34        */
/*********************************************/

static void
SiS_SaveCRT2Info(SiS_Private *SiS_Pr, USHORT ModeNo)
{
  USHORT temp1,temp2;

  /* Store CRT1 ModeNo in CR34 */
  SiS_SetReg(SiS_Pr->SiS_P3d4,0x34,ModeNo);
  temp1 = (SiS_Pr->SiS_VBInfo & SetInSlaveMode) >> 8;
  temp2 = ~(SetInSlaveMode >> 8);
  SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x31,temp2,temp1);
}

/*********************************************/
/*    HELPER: GET SOME DATA FROM BIOS ROM    */
/*********************************************/

static BOOLEAN
SiS_CR36BIOSWord23b(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  USHORT temp,temp1;
  UCHAR *ROMAddr;

  if((ROMAddr = (UCHAR *)HwInfo->pjVirtualRomBase) && SiS_Pr->SiS_UseROM) {
     if((ROMAddr[0x233] == 0x12) && (ROMAddr[0x234] == 0x34)) {
        temp = 1 << ((SiS_GetReg(SiS_Pr->SiS_P3d4,0x36) & 0xff) >> 4);
        temp1 = (ROMAddr[0x23c] << 8) | ROMAddr[0x23b];
        if(temp1 & temp) return(1);
     }
  }
  return(0);
}

static BOOLEAN
SiS_CR36BIOSWord23d(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  USHORT temp,temp1;
  UCHAR *ROMAddr;

  if((ROMAddr = (UCHAR *)HwInfo->pjVirtualRomBase) && SiS_Pr->SiS_UseROM) {
     if((ROMAddr[0x233] == 0x12) && (ROMAddr[0x234] == 0x34)) {
        temp = 1 << ((SiS_GetReg(SiS_Pr->SiS_P3d4,0x36) & 0xff) >> 4);
        temp1 = (ROMAddr[0x23e] << 8) | ROMAddr[0x23d];
        if(temp1 & temp) return(1);
     }
  }
  return(0);
}

/*********************************************/
/*          HELPER: DELAY FUNCTIONS          */
/*********************************************/

void
SiS_DDC2Delay(SiS_Private *SiS_Pr, USHORT delaytime)
{
  USHORT i, j;

  for(i=0; i<delaytime; i++) {
     j += SiS_GetReg(SiS_Pr->SiS_P3c4,0x05);
  }
}

static void
SiS_GenericDelay(SiS_Private *SiS_Pr, USHORT delay)
{
  USHORT temp,flag;

  flag = SiS_GetRegByte(0x61) & 0x10;

  while(delay) {
     temp = SiS_GetRegByte(0x61) & 0x10;
     if(temp == flag) continue;
     flag = temp;
     delay--;
  }
}

#ifdef SIS315H
static void
SiS_LongDelay(SiS_Private *SiS_Pr, USHORT delay)
{
  while(delay--) {
     SiS_GenericDelay(SiS_Pr,0x19df);
  }
}
#endif

static void
SiS_ShortDelay(SiS_Private *SiS_Pr, USHORT delay)
{
  while(delay--) {
     SiS_GenericDelay(SiS_Pr,0x42);
  }
}

static void
SiS_PanelDelay(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo, USHORT DelayTime)
{
  UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
  USHORT PanelID, DelayIndex, Delay=0;

  if(HwInfo->jChipType < SIS_315H) {

#ifdef SIS300

      PanelID = SiS_GetReg(SiS_Pr->SiS_P3d4,0x36);
      if(SiS_Pr->SiS_VBType & VB_SISVB) {
         if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x18) & 0x10)) PanelID = 0x12;
      }
      DelayIndex = PanelID >> 4;
      if((DelayTime >= 2) && ((PanelID & 0x0f) == 1))  {
         Delay = 3;
      } else {
         if(DelayTime >= 2) DelayTime -= 2;

         if(!(DelayTime & 0x01)) {
       	    Delay = SiS_Pr->SiS_PanelDelayTbl[DelayIndex].timer[0];
         } else {
       	    Delay = SiS_Pr->SiS_PanelDelayTbl[DelayIndex].timer[1];
         }
	 if((ROMAddr) && (SiS_Pr->SiS_UseROM)) {
            if(ROMAddr[0x220] & 0x40) {
               if(!(DelayTime & 0x01)) {
	          Delay = (USHORT)ROMAddr[0x225];
               } else {
	    	  Delay = (USHORT)ROMAddr[0x226];
               }
            }
         }
      }
      SiS_ShortDelay(SiS_Pr,Delay);

#endif  /* SIS300 */

   } else {

#ifdef SIS315H

      if(HwInfo->jChipType >= SIS_330) return;

      if((SiS_Pr->SiS_IF_DEF_LVDS == 1) ||
         (SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) ||
	 (SiS_Pr->SiS_CustomT == CUT_CLEVO1400)) {			/* 315 series, LVDS; Special */

         if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
            PanelID = SiS_GetReg(SiS_Pr->SiS_P3d4,0x36);
	    if(SiS_Pr->SiS_CustomT == CUT_CLEVO1400) {
	       if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x1b) & 0x10)) PanelID = 0x12;
	    }
	    if(SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) {
	       DelayIndex = PanelID & 0x0f;
	    } else {
	       DelayIndex = PanelID >> 4;
	    }
	    if((DelayTime >= 2) && ((PanelID & 0x0f) == 1))  {
               Delay = 3;
            } else {
               if(DelayTime >= 2) DelayTime -= 2;
               if(!(DelayTime & 0x01)) {
       		  Delay = SiS_Pr->SiS_PanelDelayTblLVDS[DelayIndex].timer[0];
               } else {
       		  Delay = SiS_Pr->SiS_PanelDelayTblLVDS[DelayIndex].timer[1];
               }
	       if((ROMAddr) && (SiS_Pr->SiS_UseROM)) {
                  if(ROMAddr[0x13c] & 0x40) {
                     if(!(DelayTime & 0x01)) {
	    	        Delay = (USHORT)ROMAddr[0x17e];
                     } else {
	    	        Delay = (USHORT)ROMAddr[0x17f];
                     }
                  }
               }
            }
	    SiS_ShortDelay(SiS_Pr,Delay);
	 }

      } else if(SiS_Pr->SiS_VBType & VB_SISVB) {			/* 315 series, all bridges */

	 DelayIndex = SiS_GetReg(SiS_Pr->SiS_P3d4,0x36) >> 4;
         if(!(DelayTime & 0x01)) {
       	    Delay = SiS_Pr->SiS_PanelDelayTbl[DelayIndex].timer[0];
         } else {
       	    Delay = SiS_Pr->SiS_PanelDelayTbl[DelayIndex].timer[1];
         }
	 Delay <<= 8;
	 SiS_DDC2Delay(SiS_Pr, Delay);

      }

#endif /* SIS315H */

   }
}

#ifdef SIS315H
static void
SiS_PanelDelayLoop(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
                      USHORT DelayTime, USHORT DelayLoop)
{
   int i;
   for(i=0; i<DelayLoop; i++) {
      SiS_PanelDelay(SiS_Pr, HwInfo, DelayTime);
   }
}
#endif

/*********************************************/
/*    HELPER: WAIT-FOR-RETRACE FUNCTIONS     */
/*********************************************/

void
SiS_WaitRetrace1(SiS_Private *SiS_Pr)
{
  USHORT watchdog;

  if(SiS_GetReg(SiS_Pr->SiS_P3c4,0x1f) & 0xc0) return;

  if(!(SiS_GetReg(SiS_Pr->SiS_P3d4,0x17) & 0x80)) return;

  watchdog = 65535;
  while((SiS_GetRegByte(SiS_Pr->SiS_P3da) & 0x08) && --watchdog);
  watchdog = 65535;
  while((!(SiS_GetRegByte(SiS_Pr->SiS_P3da) & 0x08)) && --watchdog);
}

static void
SiS_WaitRetrace2(SiS_Private *SiS_Pr, USHORT reg)
{
  USHORT watchdog;

  watchdog = 65535;
  while((SiS_GetReg(SiS_Pr->SiS_Part1Port,reg) & 0x02) && --watchdog);
  watchdog = 65535;
  while((!(SiS_GetReg(SiS_Pr->SiS_Part1Port,reg) & 0x02)) && --watchdog);
}

static void
SiS_WaitVBRetrace(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  if(HwInfo->jChipType < SIS_315H) {
#ifdef SIS300
     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
        if(!(SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00) & 0x20)) return;
     }
     if(!(SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00) & 0x80)) {
        SiS_WaitRetrace1(SiS_Pr);
     } else {
        SiS_WaitRetrace2(SiS_Pr, 0x25);
     }
#endif
  } else {
#ifdef SIS315H
     if(!(SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00) & 0x40)) {
        SiS_WaitRetrace1(SiS_Pr);
     } else {
        SiS_WaitRetrace2(SiS_Pr, 0x30);
     }
#endif
  }
}

static void
SiS_VBWait(SiS_Private *SiS_Pr)
{
  USHORT tempal,temp,i,j;

  temp = 0;
  for(i=0; i<3; i++) {
    for(j=0; j<100; j++) {
       tempal = SiS_GetRegByte(SiS_Pr->SiS_P3da);
       if(temp & 0x01) {
          if((tempal & 0x08))  continue;
          if(!(tempal & 0x08)) break;
       } else {
          if(!(tempal & 0x08)) continue;
          if((tempal & 0x08))  break;
       }
    }
    temp ^= 0x01;
  }
}

static void
SiS_VBLongWait(SiS_Private *SiS_Pr)
{
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
     SiS_VBWait(SiS_Pr);
  } else {
     SiS_WaitRetrace1(SiS_Pr);
  }
}

/*********************************************/
/*               HELPER: MISC                */
/*********************************************/

static BOOLEAN
SiS_Is301B(SiS_Private *SiS_Pr)
{
  USHORT flag;

  flag = SiS_GetReg(SiS_Pr->SiS_Part4Port,0x01);
  if(flag >= 0xb0) return TRUE;
  return FALSE;
}

static BOOLEAN
SiS_CRT2IsLCD(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  USHORT flag;

  if(HwInfo->jChipType == SIS_730) {
     flag = SiS_GetReg(SiS_Pr->SiS_P3c4,0x13);
     if(flag & 0x20) return TRUE;
  }
  flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
  if(flag & 0x20) return TRUE;
  return FALSE;
}

BOOLEAN
SiS_IsDualEdge(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
#ifdef SIS315H
  USHORT flag;

  if(HwInfo->jChipType >= SIS_315H) {
     if((HwInfo->jChipType != SIS_650) || (SiS_GetReg(SiS_Pr->SiS_P3d4,0x5f) & 0xf0)) {
        flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
        if(flag & EnableDualEdge) return TRUE;
     }
  }
#endif
  return FALSE;
}

BOOLEAN
SiS_IsVAMode(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
#ifdef SIS315H
  USHORT flag;

  if(HwInfo->jChipType >= SIS_315H) {
     flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
     if((flag & EnableDualEdge) && (flag & SetToLCDA)) return TRUE;
  }
#endif
  return FALSE;
}

static BOOLEAN
SiS_IsDualLink(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
#ifdef SIS315H
  if(HwInfo->jChipType >= SIS_315H) {
     if((SiS_CRT2IsLCD(SiS_Pr, HwInfo)) ||
        (SiS_IsVAMode(SiS_Pr, HwInfo))) {
        if(SiS_Pr->SiS_LCDInfo & LCDDualLink) return TRUE;
     }
  }
#endif
  return FALSE;
}

#ifdef SIS315H
static BOOLEAN
SiS_TVEnabled(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  if((SiS_GetReg(SiS_Pr->SiS_Part2Port,0x00) & 0x0f) != 0x0c) return TRUE;
  if(SiS_Pr->SiS_VBType & (VB_301C | VB_SIS301LV302LV)) {
     if(SiS_GetReg(SiS_Pr->SiS_Part2Port,0x4d) & 0x10) return TRUE;
  }
  return FALSE;
}
#endif

#ifdef SIS315H
static BOOLEAN
SiS_LCDAEnabled(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  if(SiS_GetReg(SiS_Pr->SiS_Part1Port,0x13) & 0x04) return TRUE;
  return FALSE;
}
#endif

#ifdef SIS315H
static BOOLEAN
SiS_WeHaveBacklightCtrl(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  USHORT flag;

  if((HwInfo->jChipType >= SIS_315H) && (HwInfo->jChipType < SIS_661)) {
     flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x79);
     if(flag & 0x10) return TRUE;
  }
  return FALSE;
}
#endif

#ifdef SIS315H
static BOOLEAN
SiS_IsNotM650orLater(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  USHORT flag;

  if(HwInfo->jChipType == SIS_650) {
     flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x5f);
     flag &= 0xF0;
     /* Check for revision != A0 only */
     if((flag == 0xe0) || (flag == 0xc0) ||
        (flag == 0xb0) || (flag == 0x90)) return FALSE;
  } else if(HwInfo->jChipType >= SIS_661) return FALSE;
  return TRUE;
}
#endif

#ifdef SIS315H
static BOOLEAN
SiS_IsYPbPr(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  USHORT flag;

  if(HwInfo->jChipType >= SIS_315H) {
     flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
     if(flag & EnableCHYPbPr) return TRUE;  /* = YPrPb = 0x08 */
  }
  return FALSE;
}
#endif

#ifdef SIS315H
static BOOLEAN
SiS_IsChScart(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  USHORT flag;

  if(HwInfo->jChipType >= SIS_315H) {
     flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
     if(flag & EnableCHScart) return TRUE;  /* = Scart = 0x04 */
  }
  return FALSE;
}
#endif

#ifdef SIS315H
static BOOLEAN
SiS_IsTVOrYPbPrOrScart(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  USHORT flag;

  if(HwInfo->jChipType >= SIS_315H) {
     flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
     if(flag & SetCRT2ToTV)        return TRUE;
     flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
     if(flag & EnableCHYPbPr)      return TRUE;  /* = YPrPb = 0x08 */
     if(flag & EnableCHScart)      return TRUE;  /* = Scart = 0x04 - TW */
  } else {
     flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
     if(flag & SetCRT2ToTV)        return TRUE;
  }
  return FALSE;
}
#endif

#ifdef SIS315H
static BOOLEAN
SiS_IsLCDOrLCDA(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  USHORT flag;

  if(HwInfo->jChipType >= SIS_315H) {
     flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
     if(flag & SetCRT2ToLCD) return TRUE;
     flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
     if(flag & SetToLCDA)    return TRUE;
  } else {
     flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
     if(flag & SetCRT2ToLCD) return TRUE;
  }
  return FALSE;
}
#endif

static BOOLEAN
SiS_BridgeIsOn(SiS_Private *SiS_Pr)
{
  USHORT flag;

  if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
     return FALSE;
  } else if(SiS_Pr->SiS_VBType & VB_SISVB) {
     flag = SiS_GetReg(SiS_Pr->SiS_Part4Port,0x00);
     if((flag == 1) || (flag == 2)) return FALSE;
  }
  return TRUE;
}

static BOOLEAN
SiS_BridgeIsEnabled(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  USHORT flag;

  if(!(SiS_BridgeIsOn(SiS_Pr))) {
     flag = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00);
     if(HwInfo->jChipType < SIS_315H) {
       flag &= 0xa0;
       if((flag == 0x80) || (flag == 0x20)) return FALSE;
     } else {
       flag &= 0x50;
       if((flag == 0x40) || (flag == 0x10)) return FALSE;
     }
  }
  return TRUE;
}

static BOOLEAN
SiS_BridgeInSlave(SiS_Private *SiS_Pr)
{
  USHORT flag1;

  flag1 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x31);
  if(flag1 & (SetInSlaveMode >> 8)) return TRUE;
  return FALSE;
}

/*********************************************/
/*       GET VIDEO BRIDGE CONFIG INFO        */
/*********************************************/

/* Setup general purpose IO for Chrontel communication */
void
SiS_SetChrontelGPIO(SiS_Private *SiS_Pr, USHORT myvbinfo)
{
   unsigned long  acpibase;
   unsigned short temp;

   if(!(SiS_Pr->SiS_ChSW)) return;

#ifndef LINUX_XF86
   SiS_SetRegLong(0xcf8,0x80000874);		   /* get ACPI base */
   acpibase = SiS_GetRegLong(0xcfc);
#else
   acpibase = pciReadLong(0x00000800, 0x74);
#endif
   acpibase &= 0xFFFF;
   temp = SiS_GetRegShort((USHORT)(acpibase + 0x3c));  /* ACPI register 0x3c: GP Event 1 I/O mode select */
   temp &= 0xFEFF;
   SiS_SetRegShort((USHORT)(acpibase + 0x3c), temp);
   temp = SiS_GetRegShort((USHORT)(acpibase + 0x3c));
   temp = SiS_GetRegShort((USHORT)(acpibase + 0x3a));  /* ACPI register 0x3a: GP Pin Level (low/high) */
   temp &= 0xFEFF;
   if(!(myvbinfo & SetCRT2ToTV)) temp |= 0x0100;
   SiS_SetRegShort((USHORT)(acpibase + 0x3a), temp);
   temp = SiS_GetRegShort((USHORT)(acpibase + 0x3a));
}

void
SiS_GetVBInfo(SiS_Private *SiS_Pr, USHORT ModeNo,
              USHORT ModeIdIndex,PSIS_HW_INFO HwInfo,
	      int checkcrt2mode)
{
  USHORT tempax,tempbx,temp;
  USHORT modeflag, resinfo=0;

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  } else {
     if(SiS_Pr->UseCustomMode) {
        modeflag = SiS_Pr->CModeFlag;
     } else {
   	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
     }
  }

  SiS_Pr->SiS_SetFlag = 0;

  SiS_Pr->SiS_ModeType = modeflag & ModeInfoFlag;

  tempbx = 0;
  if(SiS_BridgeIsOn(SiS_Pr) == 0) {
    	temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
#if 0
   	if(HwInfo->jChipType < SIS_661) {
	   /* NO - YPbPr not set yet ! */
	   if(SiS_Pr->SiS_YPbPr & <all ypbpr except 525i>) {
	      temp &= (SetCRT2ToHiVision | SwitchCRT2 | SetSimuScanMode); 	/* 0x83 */
	      temp |= SetCRT2ToHiVision;   					/* 0x80 */
   	   }
	   if(SiS_Pr->SiS_YPbPr & <ypbpr525i>) {
	      temp &= (SetCRT2ToHiVision | SwitchCRT2 | SetSimuScanMode); 	/* 0x83 */
	      temp |= SetCRT2ToSVIDEO;  					/* 0x08 */
	   }
	}
#endif
    	tempbx |= temp;
    	tempax = SiS_GetReg(SiS_Pr->SiS_P3d4,0x31) << 8;
        tempax &= (DriverMode | LoadDACFlag | SetNotSimuMode | SetPALTV);
    	tempbx |= tempax;

#ifdef SIS315H
	if(HwInfo->jChipType >= SIS_315H) {
    	   if(SiS_Pr->SiS_VBType & (VB_SIS301C|VB_SIS302B|VB_SIS301LV|VB_SIS302LV|VB_SIS302ELV)) {
	      if(ModeNo == 0x03) {
	         /* Mode 0x03 is never in driver mode */
		 SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x31,0xbf);
	      }
	      if(!(SiS_GetReg(SiS_Pr->SiS_P3d4,0x31) & (DriverMode >> 8))) {
	         /* Reset LCDA setting */
		 SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x38,0xfc);
	      }
	      if(IS_SIS650) {
	         if(SiS_Pr->SiS_UseLCDA) {
		    if(SiS_GetReg(SiS_Pr->SiS_P3d4,0x5f) & 0xF0) {
		       if((ModeNo <= 0x13) || (!(SiS_GetReg(SiS_Pr->SiS_P3d4,0x31) & (DriverMode >> 8)))) {
		          SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x38,(EnableDualEdge | SetToLCDA));
		       }
		    }
		 }
	      }
	      temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
       	      if((temp & (EnableDualEdge | SetToLCDA)) == (EnableDualEdge | SetToLCDA)) {
          	 tempbx |= SetCRT2ToLCDA;
	      }
	   }
	   if(HwInfo->jChipType >= SIS_661) {
	      tempbx &= ~(SetCRT2ToYPbPr525750 | SetCRT2ToHiVision);
	      temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
	      if(SiS_Pr->SiS_VBType & (VB_SIS301C|VB_SIS301LV|VB_SIS302LV|VB_SIS302ELV)) {
	         if(temp & 0x04) {
		    temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x35) & 0xe0;
		    if(temp == 0x60) tempbx |= SetCRT2ToHiVision;
		    else             tempbx |= SetCRT2ToYPbPr525750;
		 }
	      } else if(SiS_Pr->SiS_VBType & (VB_SIS301 | VB_SIS301B | VB_SIS302B)) {
	         if(temp & 0x04) {
		    temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x35) & 0xe0;
		    if(temp == 0x60) tempbx |= SetCRT2ToHiVision;
		 }
	      }
  	   }

	   if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
	      temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
	      if(temp & SetToLCDA) {
		 tempbx |= SetCRT2ToLCDA;
	      }
	      if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
	         if(temp & EnableCHYPbPr) {
		    tempbx |= SetCRT2ToCHYPbPr;
		 }
	      }
	   }
	}

#endif  /* SIS315H */

    	if(SiS_Pr->SiS_VBType & VB_SISVB) {
	   temp = SetCRT2ToSVIDEO   |
	          SetCRT2ToAVIDEO   |
	          SetCRT2ToSCART    |
	          SetCRT2ToLCDA     |
	          SetCRT2ToLCD      |
	          SetCRT2ToRAMDAC   |
                  SetCRT2ToHiVision |
		  SetCRT2ToYPbPr525750;
    	} else {
           if(HwInfo->jChipType >= SIS_315H) {
              if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
        	 temp = SetCRT2ToAVIDEO |
		        SetCRT2ToSVIDEO |
		        SetCRT2ToSCART  |
		        SetCRT2ToLCDA   |
		        SetCRT2ToLCD    |
		        SetCRT2ToCHYPbPr;
      	      } else {
        	 temp = SetCRT2ToLCDA   |
		        SetCRT2ToLCD;
	      }
	   } else {
      	      if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
          	 temp = SetCRT2ToTV | SetCRT2ToLCD;
              } else {
        	 temp = SetCRT2ToLCD;
	      }
	   }
    	}

    	if(!(tempbx & temp)) {
      	   tempax = DisableCRT2Display;
      	   tempbx = 0;
    	}

   	if(SiS_Pr->SiS_VBType & VB_SISVB) {
	   USHORT clearmask = ( DriverMode 	   |
				DisableCRT2Display |
				LoadDACFlag 	   |
				SetNotSimuMode 	   |
				SetInSlaveMode 	   |
				SetPALTV 	   |
				SwitchCRT2	   |
				SetSimuScanMode );
      	   if(tempbx & SetCRT2ToLCDA) {
              tempbx &= (clearmask | SetCRT2ToLCDA);
      	   }
	   if(tempbx & SetCRT2ToRAMDAC) {
              tempbx &= (clearmask | SetCRT2ToRAMDAC);
      	   }
	   if(tempbx & SetCRT2ToLCD) {
              tempbx &= (clearmask | SetCRT2ToLCD);
      	   }
	   if(tempbx & SetCRT2ToSCART) {
              tempbx &= (clearmask | SetCRT2ToSCART);
      	   }
	   if(tempbx & SetCRT2ToHiVision) {
              tempbx &= (clearmask | SetCRT2ToHiVision);
      	   }
	   if(tempbx & SetCRT2ToYPbPr525750) {
	      tempbx &= (clearmask | SetCRT2ToYPbPr525750);
	   }
   	} else {
	   if(HwInfo->jChipType >= SIS_315H) {
	      if(tempbx & SetCRT2ToLCDA) {
	         tempbx &= (0xFF00|SwitchCRT2|SetSimuScanMode);
	      }
	   }
      	   if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
              if(tempbx & SetCRT2ToTV) {
          	 tempbx &= (0xFF00|SetCRT2ToTV|SwitchCRT2|SetSimuScanMode);
	      }
      	   }
      	   if(tempbx & SetCRT2ToLCD) {
              tempbx &= (0xFF00|SetCRT2ToLCD|SwitchCRT2|SetSimuScanMode);
	   }
	   if(HwInfo->jChipType >= SIS_315H) {
	      if(tempbx & SetCRT2ToLCDA) {
	         tempbx |= SetCRT2ToLCD;
	      }
	   }
	}

    	if(tempax & DisableCRT2Display) {
      	   if(!(tempbx & (SwitchCRT2 | SetSimuScanMode))) {
              tempbx = SetSimuScanMode | DisableCRT2Display;
      	   }
    	}

    	if(!(tempbx & DriverMode)){
      	   tempbx |= SetSimuScanMode;
    	}

	/* LVDS/CHRONTEL (LCD/TV) and 301BDH (LCD) can only be slave in 8bpp modes */
	if(SiS_Pr->SiS_ModeType <= ModeVGA) {
	   if( (SiS_Pr->SiS_IF_DEF_LVDS == 1) ||
	       ((tempbx & SetCRT2ToLCD) && (SiS_Pr->SiS_VBType & VB_NoLCD)) ) {
	       modeflag &= (~CRT2Mode);
	   }
	}

    	if(!(tempbx & SetSimuScanMode)) {
      	   if(tempbx & SwitchCRT2) {
              if((!(modeflag & CRT2Mode)) && (checkcrt2mode)) {
		 if( (HwInfo->jChipType >= SIS_315H) &&
		     (SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) ) {
		    if(resinfo != SIS_RI_1600x1200) {
                       tempbx |= SetSimuScanMode;
		    }
		 } else {
            	    tempbx |= SetSimuScanMode;
	         }
              }
      	   } else {
              if(!(SiS_BridgeIsEnabled(SiS_Pr,HwInfo))) {
          	 if(!(tempbx & DriverMode)) {
            	    if(SiS_BridgeInSlave(SiS_Pr)) {
		       tempbx |= SetSimuScanMode;
            	    }
                 }
              }
      	   }
    	}

    	if(!(tempbx & DisableCRT2Display)) {
           if(tempbx & DriverMode) {
              if(tempbx & SetSimuScanMode) {
          	 if((!(modeflag & CRT2Mode)) && (checkcrt2mode)) {
	            if( (HwInfo->jChipType >= SIS_315H) &&
		        (SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) ) {
		       if(resinfo != SIS_RI_1600x1200) {
		          tempbx |= SetInSlaveMode;
		       }
		    } else {
            	       tempbx |= SetInSlaveMode;
                    }
	         }
              }
           } else {
              tempbx |= SetInSlaveMode;
      	   }
    	}

  }

  SiS_Pr->SiS_VBInfo = tempbx;

  if(HwInfo->jChipType == SIS_630) {
     SiS_SetChrontelGPIO(SiS_Pr, SiS_Pr->SiS_VBInfo);
  }

#ifdef TWDEBUG
#ifdef LINUX_KERNEL
  printk(KERN_DEBUG "sisfb: (VBInfo= 0x%04x, SetFlag=0x%04x)\n",
      SiS_Pr->SiS_VBInfo, SiS_Pr->SiS_SetFlag);
#endif
#ifdef LINUX_XF86
  xf86DrvMsgVerb(0, X_PROBED, 3, "(init301: VBInfo=0x%04x, SetFlag=0x%04x)\n",
      SiS_Pr->SiS_VBInfo, SiS_Pr->SiS_SetFlag);
#endif
#endif
}

/*********************************************/
/*           DETERMINE YPbPr MODE            */
/*********************************************/

void
SiS_SetYPbPr(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{

  UCHAR temp;

  /* Note: This variable is only used on 30xLV systems.
   * CR38 has a different meaning on LVDS/CH7019 systems.
   * On 661 and later, these bits moved to CR35.
   *
   * On 301, 301B, only HiVision 1080i is supported.
   * On 30xLV, 301C, only YPbPr 1080i is supported.
   */

  SiS_Pr->SiS_YPbPr = 0;
  if(HwInfo->jChipType >= SIS_661) return;

  if(SiS_Pr->SiS_VBType) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
	SiS_Pr->SiS_YPbPr = YPbPrHiVision;
     }
  }

  if(HwInfo->jChipType >= SIS_315H) {
     if(SiS_Pr->SiS_VBType & (VB_SIS301LV302LV | VB_SIS301C)) {
        temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
	if(temp & 0x08) {
	   switch((temp >> 4)) {
	   case 0x00: SiS_Pr->SiS_YPbPr = YPbPr525i;     break;
	   case 0x01: SiS_Pr->SiS_YPbPr = YPbPr525p;     break;
	   case 0x02: SiS_Pr->SiS_YPbPr = YPbPr750p;     break;
	   case 0x03: SiS_Pr->SiS_YPbPr = YPbPrHiVision; break;
	   }
	}
     }
  }

}

/*********************************************/
/*           DETERMINE TVMode flag           */
/*********************************************/

void
SiS_SetTVMode(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex, PSIS_HW_INFO HwInfo)
{
  UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
  USHORT temp, temp1, resinfo = 0, romindex = 0;
  UCHAR  OutputSelect = *SiS_Pr->pSiS_OutputSelect;

  SiS_Pr->SiS_TVMode = 0;

  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) return;
  if(SiS_Pr->UseCustomMode) return;

  if(ModeNo > 0x13) {
     resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  }

  if(HwInfo->jChipType < SIS_661) {

     if(SiS_Pr->SiS_VBInfo & SetPALTV) SiS_Pr->SiS_TVMode |= TVSetPAL;

     if(SiS_Pr->SiS_VBType & VB_SISVB) {
        temp = 0;
        if((HwInfo->jChipType == SIS_630) ||
           (HwInfo->jChipType == SIS_730)) {
           temp = 0x35;
	   romindex = 0xfe;
        } else if(HwInfo->jChipType >= SIS_315H) {
           temp = 0x38;
	   romindex = 0xf3;
	   if(HwInfo->jChipType >= SIS_330) romindex = 0x11b;
        }
        if(temp) {
           if(romindex && ROMAddr && SiS_Pr->SiS_UseROM) {
	      OutputSelect = ROMAddr[romindex];
	      if(!(OutputSelect & EnablePALMN)) {
                 SiS_SetRegAND(SiS_Pr->SiS_P3d4,temp,0x3F);
	      }
	   }
	   temp1 = SiS_GetReg(SiS_Pr->SiS_P3d4,temp);
	   if(SiS_Pr->SiS_TVMode & TVSetPAL) {
              if(temp1 & EnablePALM) {		/* 0x40 */
                 SiS_Pr->SiS_TVMode |= TVSetPALM;
	         SiS_Pr->SiS_TVMode &= ~TVSetPAL;
	      } else if(temp1 & EnablePALN) {	/* 0x80 */
	         SiS_Pr->SiS_TVMode |= TVSetPALN;
              }
	   } else {
              if(temp1 & EnableNTSCJ) {		/* 0x40 */
	         SiS_Pr->SiS_TVMode |= TVSetNTSCJ;
  	      }
	   }
        }
	/* Translate HiVision/YPbPr to our new flags */
	if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
	   if(SiS_Pr->SiS_YPbPr == YPbPr750p)          SiS_Pr->SiS_TVMode |= TVSetYPbPr750p;
	   else if(SiS_Pr->SiS_YPbPr == YPbPr525p)     SiS_Pr->SiS_TVMode |= TVSetYPbPr525p;
	   else	if(SiS_Pr->SiS_YPbPr == YPbPrHiVision) SiS_Pr->SiS_TVMode |= TVSetHiVision;
	   else					       SiS_Pr->SiS_TVMode |= TVSetYPbPr525i;
	   if(SiS_Pr->SiS_TVMode & (TVSetYPbPr750p | TVSetYPbPr525p | TVSetYPbPr525i)) {
	      SiS_Pr->SiS_VBInfo &= ~SetCRT2ToHiVision;
	      SiS_Pr->SiS_VBInfo |= SetCRT2ToYPbPr525750;
	   } else if(SiS_Pr->SiS_TVMode & TVSetHiVision) {
	      SiS_Pr->SiS_TVMode |= TVSetPAL;
	   }
	}
     } else if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
        if(SiS_Pr->SiS_CHOverScan) {
           if(SiS_Pr->SiS_IF_DEF_CH70xx == 1) {
              temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x35);
              if((temp & TVOverScan) || (SiS_Pr->SiS_CHOverScan == 1)) {
	         SiS_Pr->SiS_TVMode |= TVSetCHOverScan;
              }
           } else if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
      	      temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x79);
      	      if((temp & 0x80) || (SiS_Pr->SiS_CHOverScan == 1)) {
	         SiS_Pr->SiS_TVMode |= TVSetCHOverScan;
 	      }
	   }
           if(SiS_Pr->SiS_CHSOverScan) {
              SiS_Pr->SiS_TVMode |= TVSetCHOverScan;
           }
        }
        if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
	   temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
     	   if(SiS_Pr->SiS_TVMode & TVSetPAL) {
              if(temp & EnablePALM)      SiS_Pr->SiS_TVMode |= TVSetPALM;
	      else if(temp & EnablePALN) SiS_Pr->SiS_TVMode |= TVSetPALN;
           } else {
	      if(temp & EnableNTSCJ) {
	         SiS_Pr->SiS_TVMode |= TVSetNTSCJ;
  	      }
	   }
	}
     }

  } else {  /* 661 and later */

     temp1 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x35);
     if(temp1 & 0x01) {
        SiS_Pr->SiS_TVMode |= TVSetPAL;
	if(temp1 & 0x08) {
	   SiS_Pr->SiS_TVMode |= TVSetPALN;
	} else if(temp1 & 0x04) {
	   if(SiS_Pr->SiS_VBType & VB_SISVB) {
	      SiS_Pr->SiS_TVMode &= ~TVSetPAL;
	   }
	   SiS_Pr->SiS_TVMode |= TVSetPALM;
	}
     } else {
        if(temp1 & 0x02) {
	   SiS_Pr->SiS_TVMode |= TVSetNTSCJ;
	}
     }
     if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
        if(SiS_Pr->SiS_CHOverScan) {
           if((temp1 & 0x10) || (SiS_Pr->SiS_CHOverScan == 1)) {
	      SiS_Pr->SiS_TVMode |= TVSetCHOverScan;
	   }
	}
     }
     if(SiS_Pr->SiS_VBType & VB_SISVB) {
        if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {
	   temp1 &= 0xe0;
	   if(temp1 == 0x00)      SiS_Pr->SiS_TVMode |= TVSetYPbPr525i;
	   else if(temp1 == 0x20) SiS_Pr->SiS_TVMode |= TVSetYPbPr525p;
	   else if(temp1 == 0x40) SiS_Pr->SiS_TVMode |= TVSetYPbPr750p;
	} else if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
	   SiS_Pr->SiS_TVMode |= (TVSetHiVision | TVSetPAL);
	}
     }
  }

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToSCART) SiS_Pr->SiS_TVMode |= TVSetPAL;

  if(SiS_Pr->SiS_VBType & VB_SISVB) {

     if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
        SiS_Pr->SiS_TVMode |= TVSetPAL;
	SiS_Pr->SiS_TVMode &= ~(TVSetPALM | TVSetPALN | TVSetNTSCJ);
     } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {
        if(SiS_Pr->SiS_TVMode & (TVSetYPbPr525i | TVSetYPbPr525p | TVSetYPbPr750p)) {
	   SiS_Pr->SiS_TVMode &= ~(TVSetPAL | TVSetNTSCJ | TVSetPALM | TVSetPALN);
	}
     }

     if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
        if(!(SiS_Pr->SiS_VBInfo & SetNotSimuMode)) {
           SiS_Pr->SiS_TVMode |= TVSetTVSimuMode;
        }
     }

     if(!(SiS_Pr->SiS_TVMode & TVSetPAL)) {
        /* BIOS sets TVNTSC1024 without checking 525p here. Wrong? */
        if(!(SiS_Pr->SiS_TVMode & (TVSetHiVision | TVSetYPbPr525p | TVSetYPbPr750p))) {
           if(resinfo == SIS_RI_1024x768) {
              SiS_Pr->SiS_TVMode |= TVSetNTSC1024;
	   }
        }
     }

     SiS_Pr->SiS_TVMode |= TVRPLLDIV2XO;
     if((SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) &&
        (SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
	SiS_Pr->SiS_TVMode &= ~TVRPLLDIV2XO;
     } else if(SiS_Pr->SiS_TVMode & (TVSetYPbPr525p | TVSetYPbPr750p)) {
        SiS_Pr->SiS_TVMode &= ~TVRPLLDIV2XO;
     } else if(!(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)) {
        if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode) {
           SiS_Pr->SiS_TVMode &= ~TVRPLLDIV2XO;
        }
     }

  }

  SiS_Pr->SiS_VBInfo &= ~SetPALTV;

#ifdef TWDEBUG
  xf86DrvMsg(0, X_INFO, "(init301: TVMode %x, VBInfo %x)\n", SiS_Pr->SiS_TVMode, SiS_Pr->SiS_VBInfo);
#endif

}

/*********************************************/
/*               GET LCD INFO                */
/*********************************************/

void
SiS_GetLCDResInfo(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
		  PSIS_HW_INFO HwInfo)
{
#ifdef SIS300
  UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
#endif
#ifdef SIS315H
  UCHAR  *myptr = NULL;
#endif
  USHORT temp,modeflag,resinfo=0;
  const unsigned char SiS300SeriesLCDRes[] =
         { 0,  1,  2,  3,  7,  4,  5,  8,
	   0,  0, 10,  0,  0,  0,  0, 15 };

  SiS_Pr->SiS_LCDResInfo = 0;
  SiS_Pr->SiS_LCDTypeInfo = 0;
  SiS_Pr->SiS_LCDInfo = 0;

  if(SiS_Pr->UseCustomMode) {
     modeflag = SiS_Pr->CModeFlag;
  } else {
     if(ModeNo <= 0x13) {
    	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     } else {
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    	resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
     }
  }

  if(!(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA))) return;

  temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x36);

  if((HwInfo->jChipType < SIS_315H) || (HwInfo->jChipType >= SIS_661)) {
     SiS_Pr->SiS_LCDTypeInfo = temp >> 4;
  } else {
     SiS_Pr->SiS_LCDTypeInfo = (temp & 0x0F) - 1;
  }
  temp &= 0x0f;
  if(HwInfo->jChipType < SIS_315H) {
      /* Translate 300 series LCDRes to 315 series for unified usage */
      temp = SiS300SeriesLCDRes[temp];
  }
  SiS_Pr->SiS_LCDResInfo = temp;

  if(SiS_Pr->SiS_VBType & VB_SISVB) {
     if(SiS_Pr->SiS_LCDResInfo < SiS_Pr->SiS_PanelMin301)
	SiS_Pr->SiS_LCDResInfo = SiS_Pr->SiS_PanelMin301;
  } else {
     if(SiS_Pr->SiS_LCDResInfo < SiS_Pr->SiS_PanelMinLVDS)
	SiS_Pr->SiS_LCDResInfo = SiS_Pr->SiS_PanelMinLVDS;
  }

  if((!SiS_Pr->CP_HaveCustomData) || (SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_PanelCustom)) {
     if(SiS_Pr->SiS_LCDResInfo > SiS_Pr->SiS_PanelMax)
  	SiS_Pr->SiS_LCDResInfo = SiS_Pr->SiS_Panel1024x768;
  }

  if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
     if(SiS_Pr->SiS_CustomT == CUT_BARCO1366) {
        SiS_Pr->SiS_LCDResInfo = Panel_Barco1366;
     } else if(SiS_Pr->SiS_CustomT == CUT_PANEL848) {
        SiS_Pr->SiS_LCDResInfo = Panel_848x480;
     }
  }

  switch(SiS_Pr->SiS_LCDResInfo) {
     case Panel_800x600:   SiS_Pr->PanelXRes =  800; SiS_Pr->PanelYRes =  600; break;
     case Panel_1024x768:  SiS_Pr->PanelXRes = 1024; SiS_Pr->PanelYRes =  768; break;
     case Panel_1280x1024: SiS_Pr->PanelXRes = 1280; SiS_Pr->PanelYRes = 1024; break;
     case Panel_640x480_3:
     case Panel_640x480_2:
     case Panel_640x480:   SiS_Pr->PanelXRes =  640; SiS_Pr->PanelYRes =  480; break;
     case Panel_1024x600:  SiS_Pr->PanelXRes = 1024; SiS_Pr->PanelYRes =  600; break;
     case Panel_1152x864:  SiS_Pr->PanelXRes = 1152; SiS_Pr->PanelYRes =  864; break;
     case Panel_1280x960:  SiS_Pr->PanelXRes = 1280; SiS_Pr->PanelYRes =  960; break;
     case Panel_1152x768:  SiS_Pr->PanelXRes = 1152; SiS_Pr->PanelYRes =  768; break;
     case Panel_1400x1050: SiS_Pr->PanelXRes = 1400; SiS_Pr->PanelYRes = 1050; break;
     case Panel_1280x768:  SiS_Pr->PanelXRes = 1280; SiS_Pr->PanelYRes =  768; break;
     case Panel_1600x1200: SiS_Pr->PanelXRes = 1600; SiS_Pr->PanelYRes = 1200; break;
     case Panel_320x480:   SiS_Pr->PanelXRes =  320; SiS_Pr->PanelYRes =  480; break;
     case Panel_Custom:    SiS_Pr->PanelXRes = SiS_Pr->CP_MaxX;
    			   SiS_Pr->PanelYRes = SiS_Pr->CP_MaxY;
			   break;
     case Panel_Barco1366: SiS_Pr->PanelXRes = 1360; SiS_Pr->PanelYRes = 1024; break;
     case Panel_848x480:   SiS_Pr->PanelXRes =  848; SiS_Pr->PanelYRes =  480; break;
     default:		   SiS_Pr->PanelXRes = 1024; SiS_Pr->PanelYRes =  768; break;
  }

  temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x37);
  if(HwInfo->jChipType < SIS_661) {
     temp &= ~0xe;
  } else {
#ifdef SIS315H
     if(!(temp & 0x10)) {
        if(temp & 0x08) temp |= LCDPass11;
     }
     temp &= ~0xe;
     if(SiS_Pr->SiS_VBType & (VB_SIS302LV | VB_SIS302ELV)) {
        myptr = GetLCDStructPtr661(SiS_Pr, HwInfo);
        if(myptr) {
           if(myptr[2] & 0x01) temp |= LCDDualLink;
        }
     }
#endif
  }
  SiS_Pr->SiS_LCDInfo = temp;

  if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
     if(SiS_Pr->SiS_CustomT == CUT_PANEL848) {
        SiS_Pr->SiS_LCDInfo = 0x80 | 0x40 | 0x20;   /* neg h/v sync, RGB24 */
     }
  }

  if(!(SiS_Pr->UsePanelScaler))        SiS_Pr->SiS_LCDInfo &= ~DontExpandLCD;
  else if(SiS_Pr->UsePanelScaler == 1) SiS_Pr->SiS_LCDInfo |= DontExpandLCD;

  if(SiS_Pr->SiS_VBType & VB_SISVB) {
     if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_PanelCustom) {
	   /* For non-standard LCD resolution, we let the panel scale */
           SiS_Pr->SiS_LCDInfo |= DontExpandLCD;
        } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
	   if(ModeNo == 0x7c || ModeNo == 0x7d || ModeNo == 0x7e) {
	      /* We do not scale to 1280x960 (B/C bridges only) */
              SiS_Pr->SiS_LCDInfo |= DontExpandLCD;
	   }
	   if(((HwInfo->jChipType >= SIS_315H) && (ModeNo == 0x23 || ModeNo == 0x24 || ModeNo == 0x25)) ||
	      ((HwInfo->jChipType < SIS_315H)  && (ModeNo == 0x55 || ModeNo == 0x5a || ModeNo == 0x5b))) {
	      /* We do not scale to 1280x768 (B/C bridges only) */
              SiS_Pr->SiS_LCDInfo |= DontExpandLCD;
	   }
	   if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	      /* No non-scaling data available for LV bridges */
	      SiS_Pr->SiS_LCDInfo &= ~DontExpandLCD;
	   }
        } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768) {
           /* No idea about the timing and zoom factors */
           SiS_Pr->SiS_LCDInfo |= DontExpandLCD;
        } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
	   if(ModeNo == 0x3a || ModeNo == 0x4d || ModeNo == 0x65) {
	      /* We do not scale to 1280x1024 (all bridges) */
	      SiS_Pr->SiS_LCDInfo |= DontExpandLCD;
	   }
	} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) {
	   if(SiS_Pr->SiS_VBType & VB_SIS301B302B) {
	      /* No idea about the timing and zoom factors (C bridge only) */
	      SiS_Pr->SiS_LCDInfo |= DontExpandLCD;
	   }
	}
	if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
	   if(SiS_Pr->SiS_CustomT == CUT_CLEVO1024) {
              if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
	         SiS_Pr->SiS_LCDInfo &= ~DontExpandLCD;
	      }
	   }
	}
     }
  }

  if(HwInfo->jChipType >= SIS_315H) {
#ifdef SIS315H
     if(HwInfo->jChipType < SIS_661) {
        if(SiS_GetReg(SiS_Pr->SiS_P3d4,0x39) & 0x01) {
           SiS_Pr->SiS_LCDInfo &= (~DontExpandLCD);
	   SiS_Pr->SiS_LCDInfo |= LCDPass11;
	}
     }
#endif
  } else {
#ifdef SIS300
     if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
        if((ROMAddr) && SiS_Pr->SiS_UseROM) {
	   if((ROMAddr[0x233] == 0x12) && (ROMAddr[0x234] == 0x34)) {
              if(!(ROMAddr[0x235] & 0x02)) {
	         SiS_Pr->SiS_LCDInfo &= (~DontExpandLCD);
 	      }
	   }
        }
     } else if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
	if((SiS_Pr->SiS_SetFlag & SetDOSMode) && ((ModeNo == 0x03) || (ModeNo == 0x10))) {
           SiS_Pr->SiS_LCDInfo &= (~DontExpandLCD);
	}
     }
#endif
  }

  /* Trumpion: Assume non-expanding */
  if(SiS_Pr->SiS_IF_DEF_TRUMPION != 0) {
     SiS_Pr->SiS_LCDInfo &= (~DontExpandLCD);
  }

  if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
     SiS_Pr->SiS_LCDInfo &= (~LCDPass11);
  }

#ifdef SIS315H
  if((HwInfo->jChipType >= SIS_315H) && (HwInfo->jChipType < SIS_661)) {
     if(SiS_Pr->SiS_VBType & (VB_SIS302LV | VB_SIS302ELV)) {
	/* Enable 302LV/302ELV dual link mode.
	 * For 661, this is done above.
	 */
        if((SiS_Pr->SiS_CustomT == CUT_CLEVO1024) &&
	   (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)) {
	   /* (Sets this in SenseLCD; new paneltypes) */
	   SiS_Pr->SiS_LCDInfo |= LCDDualLink;
	}
        if((SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) ||
	   (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) ||
           (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200)) {
	   SiS_Pr->SiS_LCDInfo |= LCDDualLink;
	}
     }
  }
#endif

  if(!((HwInfo->jChipType < SIS_315H) && (SiS_Pr->SiS_SetFlag & SetDOSMode))) {

     if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600) {
	   if(ModeNo > 0x13) {
	      if(!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {
                 if((resinfo == SIS_RI_800x600) || (resinfo == SIS_RI_400x300)) {
                    SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;
		 }
              }
           }
        }
	if(ModeNo == 0x12) {
	   if(SiS_Pr->SiS_LCDInfo & LCDPass11) {
	      SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;
	   }
	}
     }

     if(modeflag & HalfDCLK) {
        if(SiS_Pr->SiS_IF_DEF_TRUMPION == 0) {
           if(!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {
	      if(!(((SiS_Pr->SiS_IF_DEF_LVDS == 1) || (HwInfo->jChipType < SIS_315H)) &&
	                                      (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480))) {
                 if(ModeNo > 0x13) {
                    if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
                       if(resinfo == SIS_RI_512x384) SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;
                    } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600) {
                       if(resinfo == SIS_RI_400x300) SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;
                    }
                 }
	      } else SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;
           } else SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;
        } else SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;
     }

  }

  if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
     if(SiS_Pr->SiS_VBInfo & SetNotSimuMode) {
     	SiS_Pr->SiS_SetFlag |= LCDVESATiming;
     }
  } else {
     SiS_Pr->SiS_SetFlag |= LCDVESATiming;
  }

  SiS_Pr->SiS_LCDInfo661 = 0;
  if(SiS_Pr->SiS_SetFlag & LCDVESATiming) SiS_Pr->SiS_LCDInfo661 |= 0x0001;
  if(SiS_Pr->SiS_SetFlag & EnableLVDSDDA) SiS_Pr->SiS_LCDInfo661 |= 0x0002;
  if(SiS_Pr->SiS_LCDInfo & LCDPass11)     SiS_Pr->SiS_LCDInfo661 |= 0x0008;
  if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) SiS_Pr->SiS_LCDInfo661 |= 0x0010;
  SiS_Pr->SiS_LCDInfo661 |= (SiS_Pr->SiS_LCDInfo & 0xe0);
  if(SiS_Pr->SiS_LCDInfo & LCDDualLink)   SiS_Pr->SiS_LCDInfo661 |= 0x0100;

#ifdef LINUX_KERNEL
#ifdef TWDEBUG
  printk(KERN_DEBUG "sisfb: (LCDInfo=0x%04x LCDResInfo=0x%02x LCDTypeInfo=0x%02x)\n",
	SiS_Pr->SiS_LCDInfo, SiS_Pr->SiS_LCDResInfo, SiS_Pr->SiS_LCDTypeInfo);
#endif
#endif
#ifdef LINUX_XF86
  xf86DrvMsgVerb(0, X_PROBED, 4,
  	"(init301: LCDInfo=0x%04x LCDResInfo=0x%02x LCDTypeInfo=0x%02x SetFlag=0x%04x)\n",
	SiS_Pr->SiS_LCDInfo, SiS_Pr->SiS_LCDResInfo, SiS_Pr->SiS_LCDTypeInfo, SiS_Pr->SiS_SetFlag);
#endif
}

/*********************************************/
/*                 GET VCLK                  */
/*********************************************/

USHORT
SiS_GetVCLK2Ptr(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
                USHORT RefreshRateTableIndex, PSIS_HW_INFO HwInfo)
{
  USHORT tempbx;
  const USHORT LCDXlat0VCLK[4]    = {VCLK40,       VCLK40,       VCLK40,       VCLK40};
  const USHORT LVDSXlat1VCLK[4]   = {VCLK40,       VCLK40,       VCLK40,       VCLK40};
  const USHORT LVDSXlat4VCLK[4]   = {VCLK28,       VCLK28,       VCLK28,       VCLK28};
#ifdef SIS300
  const USHORT LCDXlat1VCLK300[4] = {VCLK65_300,   VCLK65_300,   VCLK65_300,   VCLK65_300};
  const USHORT LCDXlat2VCLK300[4] = {VCLK108_2_300,VCLK108_2_300,VCLK108_2_300,VCLK108_2_300};
  const USHORT LVDSXlat2VCLK300[4]= {VCLK65_300,   VCLK65_300,   VCLK65_300,   VCLK65_300};
  const USHORT LVDSXlat3VCLK300[4]= {VCLK65_300,   VCLK65_300,   VCLK65_300,   VCLK65_300};
#endif
#ifdef SIS315H
  const USHORT LCDXlat1VCLK310[4] = {VCLK65_315,   VCLK65_315,   VCLK65_315,   VCLK65_315};
  const USHORT LCDXlat2VCLK310[4] = {VCLK108_2_315,VCLK108_2_315,VCLK108_2_315,VCLK108_2_315};
  const USHORT LVDSXlat2VCLK310[4]= {VCLK65_315,   VCLK65_315,   VCLK65_315,   VCLK65_315};
  const USHORT LVDSXlat3VCLK310[4]= {VCLK108_2_315,VCLK108_2_315,VCLK108_2_315,VCLK108_2_315};
#endif
  USHORT CRT2Index,VCLKIndex=0;
  USHORT modeflag,resinfo;
  const UCHAR  *CHTVVCLKPtr = NULL;
  const USHORT *LCDXlatVCLK1 = NULL;
  const USHORT *LCDXlatVCLK2 = NULL;
  const USHORT *LVDSXlatVCLK2 = NULL;
  const USHORT *LVDSXlatVCLK3 = NULL;

  if(HwInfo->jChipType >= SIS_315H) {
#ifdef SIS315H
     LCDXlatVCLK1 = LCDXlat1VCLK310;
     LCDXlatVCLK2 = LCDXlat2VCLK310;
     LVDSXlatVCLK2 = LVDSXlat2VCLK310;
     LVDSXlatVCLK3 = LVDSXlat3VCLK310;
#endif
  } else {
#ifdef SIS300
     LCDXlatVCLK1 = LCDXlat1VCLK300;
     LCDXlatVCLK2 = LCDXlat2VCLK300;
     LVDSXlatVCLK2 = LVDSXlat2VCLK300;
     LVDSXlatVCLK3 = LVDSXlat3VCLK300;
#endif
  }

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
     CRT2Index = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
     CRT2Index = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
  }

  if(SiS_Pr->SiS_VBType & VB_SISVB) {    /* 30x/B/LV */

     if(SiS_Pr->SiS_SetFlag & ProgrammingCRT2) {

        CRT2Index >>= 6;
        if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {      /*  LCD */

           if(HwInfo->jChipType < SIS_315H) {
	      if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600) {
	         VCLKIndex = LCDXlat0VCLK[CRT2Index];
	      } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
	    	 VCLKIndex = LCDXlatVCLK1[CRT2Index];
	      } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600) {
	    	 VCLKIndex = LCDXlatVCLK1[CRT2Index];
	      } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x768) {
	    	 VCLKIndex = LCDXlatVCLK1[CRT2Index];
	      } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768) {
	         VCLKIndex = VCLK81_300;	/* guessed */
	      } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x960) {
		 VCLKIndex = VCLK108_3_300;
		 if(resinfo == SIS_RI_1280x1024) VCLKIndex = VCLK100_300;
	      } else {
	    	 VCLKIndex = LCDXlatVCLK2[CRT2Index];
	      }
	   } else {
	      if( (SiS_Pr->SiS_VBType & VB_SIS301LV302LV) ||
	          (!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) ) {
      	         if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
		    VCLKIndex = VCLK108_2_315;
		 } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768) {
		    VCLKIndex = VCLK81_315;  	/* guessed */
		 } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
		    VCLKIndex = VCLK108_2_315;
		 } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) {
		    VCLKIndex = VCLK162_315;
		 } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x960) {
		    VCLKIndex = VCLK108_3_315;
		    if(resinfo == SIS_RI_1280x1024) VCLKIndex = VCLK100_315;
		 } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
		    VCLKIndex = LCDXlatVCLK1[CRT2Index];
	         } else {
		    VCLKIndex = LCDXlatVCLK2[CRT2Index];
      	         }
	      } else {
                 VCLKIndex = (UCHAR)SiS_GetRegByte((USHORT)(SiS_Pr->SiS_P3ca+0x02));  /*  Port 3cch */
         	 VCLKIndex = ((VCLKIndex >> 2) & 0x03);
        	 if(ModeNo > 0x13) {
          	    VCLKIndex = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
        	 }
		 if(ModeNo <= 0x13) {
		    if(HwInfo->jChipType <= SIS_315PRO) {
		       if(SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC == 1) VCLKIndex = 0x42;
	            } else {
		       if(SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC == 1) VCLKIndex = 0x00;
		    }
		 }
		 if(HwInfo->jChipType <= SIS_315PRO) {
		    if(VCLKIndex == 0) VCLKIndex = 0x41;
		    if(VCLKIndex == 1) VCLKIndex = 0x43;
		    if(VCLKIndex == 4) VCLKIndex = 0x44;
		 }
	      }
	   }

        } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {                 /*  TV */

	   if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
              if(SiS_Pr->SiS_TVMode & TVRPLLDIV2XO) VCLKIndex = HiTVVCLKDIV2;
     	      else                                  VCLKIndex = HiTVVCLK;
              if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode) {
            	 if(modeflag & Charx8Dot) 	    VCLKIndex = HiTVSimuVCLK;
            	 else 			  	    VCLKIndex = HiTVTextVCLK;
              }
           } else if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p) VCLKIndex = YPbPr750pVCLK - TVCLKBASE_315;
	   else if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p)   VCLKIndex = TVVCLKDIV2;
	   else if(SiS_Pr->SiS_TVMode & TVRPLLDIV2XO)     VCLKIndex = TVVCLKDIV2;
           else         		            	  VCLKIndex = TVVCLK;

	   if(HwInfo->jChipType < SIS_315H) {
              VCLKIndex += TVCLKBASE_300;
  	   } else {
	      VCLKIndex += TVCLKBASE_315;
	   }

        } else {         					/* VGA2 */

           VCLKIndex = (UCHAR)SiS_GetRegByte((USHORT)(SiS_Pr->SiS_P3ca+0x02));
           VCLKIndex = ((VCLKIndex >> 2) & 0x03);
           if(ModeNo > 0x13) {
              VCLKIndex = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
	      if(HwInfo->jChipType < SIS_315H) {
          	 VCLKIndex &= 0x3f;
		 if( (HwInfo->jChipType == SIS_630) &&
		     (HwInfo->jChipRevision >= 0x30)) {
		    if(VCLKIndex == 0x14) VCLKIndex = 0x34;
		 }
		 /* Better VGA2 clock for 1280x1024@75 */
		 if(VCLKIndex == 0x17) VCLKIndex = 0x45;
	      }
           }
        }

     } else {   /* If not programming CRT2 */

        VCLKIndex = (UCHAR)SiS_GetRegByte((USHORT)(SiS_Pr->SiS_P3ca+0x02));
        VCLKIndex = ((VCLKIndex >> 2) & 0x03);
        if(ModeNo > 0x13) {
           VCLKIndex = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
	   if(HwInfo->jChipType < SIS_315H) {
              VCLKIndex &= 0x3f;
	      if( (HwInfo->jChipType != SIS_630) &&
		  (HwInfo->jChipType != SIS_300) ) {
		 if(VCLKIndex == 0x1b) VCLKIndex = 0x35;
	      }
	   }
        }
     }

  } else {       /*   LVDS  */

     VCLKIndex = CRT2Index;

     if(SiS_Pr->SiS_SetFlag & ProgrammingCRT2) {  /* programming CRT2 */

        if( (SiS_Pr->SiS_IF_DEF_CH70xx != 0) && (SiS_Pr->SiS_VBInfo & SetCRT2ToTV) ) {

	   VCLKIndex &= 0x1f;
           tempbx = 0;
	   if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) tempbx += 1;
           if(SiS_Pr->SiS_TVMode & TVSetPAL) {
	      tempbx += 2;
	      if(SiS_Pr->SiS_ModeType > ModeVGA) {
		 if(SiS_Pr->SiS_CHSOverScan) tempbx = 8;
	      }
	      if(SiS_Pr->SiS_TVMode & TVSetPALM) {
		 tempbx = 4;
		 if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) tempbx += 1;
	      } else if(SiS_Pr->SiS_TVMode & TVSetPALN) {
		 tempbx = 6;
		 if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) tempbx += 1;
	      }
	   }
       	   switch(tempbx) {
             case  0: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKUNTSC;  break;
             case  1: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKONTSC;  break;
             case  2: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKUPAL;   break;
             case  3: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKOPAL;   break;
	     case  4: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKUPALM;  break;
             case  5: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKOPALM;  break;
             case  6: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKUPALN;  break;
             case  7: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKOPALN;  break;
	     case  8: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKSOPAL;  break;
	     default: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKOPAL;   break;
           }
           VCLKIndex = CHTVVCLKPtr[VCLKIndex];

        } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {

	   VCLKIndex >>= 6;
     	   if((SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600) ||
	      (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel320x480))
     	      VCLKIndex = LVDSXlat1VCLK[VCLKIndex];
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480   ||
	           SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_2 ||
		   SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_3)
	      VCLKIndex = LVDSXlat4VCLK[VCLKIndex];
     	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)
     	      VCLKIndex = LVDSXlatVCLK2[VCLKIndex];
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600)
              VCLKIndex = LVDSXlatVCLK2[VCLKIndex];
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x768)
              VCLKIndex = LVDSXlatVCLK2[VCLKIndex];
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768)
	      VCLKIndex = VCLK68_315;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200)
	      VCLKIndex = VCLK162_315;
     	   else
	      VCLKIndex = LVDSXlatVCLK3[VCLKIndex];

	   if(SiS_Pr->SiS_CustomT == CUT_BARCO1366) {
	      /* Special Timing: Barco iQ Pro R series */
	      VCLKIndex = 0x44;
	   }

	   if(SiS_Pr->SiS_CustomT == CUT_PANEL848) {
	      if(HwInfo->jChipType < SIS_315H) {
		 VCLKIndex = VCLK34_300;
	         /* if(resinfo == SIS_RI_1360x768) VCLKIndex = ?; */
	      } else {
		 VCLKIndex = VCLK34_315;
		 /* if(resinfo == SIS_RI_1360x768) VCLKIndex = ?; */
	      }
	   }

        } else {

	   VCLKIndex = (UCHAR)SiS_GetRegByte((USHORT)(SiS_Pr->SiS_P3ca+0x02));
           VCLKIndex = ((VCLKIndex >> 2) & 0x03);
           if(ModeNo > 0x13) {
              VCLKIndex = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
	      if(HwInfo->jChipType < SIS_315H) {
    	 	 VCLKIndex &= 0x3F;
              }
	      if( (HwInfo->jChipType == SIS_630) &&
                  (HwInfo->jChipRevision >= 0x30) ) {
		 if(VCLKIndex == 0x14) VCLKIndex = 0x2e;
	      }
	   }
        }

     } else {  /* if not programming CRT2 */

        VCLKIndex = (UCHAR)SiS_GetRegByte((USHORT)(SiS_Pr->SiS_P3ca+0x02));
        VCLKIndex = ((VCLKIndex >> 2) & 0x03);
        if(ModeNo > 0x13) {
           VCLKIndex = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
           if(HwInfo->jChipType < SIS_315H) {
	      VCLKIndex &= 0x3F;
	      if( (HwInfo->jChipType != SIS_630) &&
	          (HwInfo->jChipType != SIS_300) ) {
		 if(VCLKIndex == 0x1b) VCLKIndex = 0x35;
	      }
#if 0
	      if(HwInfo->jChipType == SIS_730) {
		 if(VCLKIndex == 0x0b) VCLKIndex = 0x40;   /* 1024x768-70 */
		 if(VCLKIndex == 0x0d) VCLKIndex = 0x41;   /* 1024x768-75 */
	      }
#endif
	   }
        }

     }

  }

#ifdef TWDEBUG
  xf86DrvMsg(0, X_INFO, "VCLKIndex %d (0x%x)\n", VCLKIndex, VCLKIndex);
#endif

  return(VCLKIndex);
}

/*********************************************/
/*        SET CRT2 MODE TYPE REGISTERS       */
/*********************************************/

static void
SiS_SetCRT2ModeRegs(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
                    PSIS_HW_INFO HwInfo)
{
  USHORT i,j,modeflag;
  USHORT tempcl,tempah=0;
#ifdef SIS300
  USHORT temp;
#endif
#ifdef SIS315H
  USHORT tempbl, tempah2, tempbl2;
#endif

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  } else {
     if(SiS_Pr->UseCustomMode) {
        modeflag = SiS_Pr->CModeFlag;
     } else {
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     }
  }

  /* BIOS does not do this (neither 301 nor LVDS) */
  /* (But it's harmless; see SetCRT2Offset) */
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x03,0x00);   /* fix write part1 index 0  BTDRAM bit Bug */

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {

     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x00,0xAF,0x40);
     SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2E,0xF7);

  } else {

     for(i=0,j=4; i<3; i++,j++) SiS_SetReg(SiS_Pr->SiS_Part1Port,j,0);

     tempcl = SiS_Pr->SiS_ModeType;

     if(HwInfo->jChipType < SIS_315H) {

#ifdef SIS300    /* ---- 300 series ---- */

        /* For 301BDH: (with LCD via LVDS) */
        if(SiS_Pr->SiS_VBType & VB_NoLCD) {
	   temp = SiS_GetReg(SiS_Pr->SiS_P3c4,0x32);
	   temp &= 0xef;
	   temp |= 0x02;
	   if((SiS_Pr->SiS_VBInfo & SetCRT2ToTV) || (SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC)) {
	      temp |= 0x10;
	      temp &= 0xfd;
	   }
	   SiS_SetReg(SiS_Pr->SiS_P3c4,0x32,temp);
        }

        if(ModeNo > 0x13) {
           tempcl -= ModeVGA;
           if((tempcl > 0) || (tempcl == 0)) {      /* tempcl is USHORT -> always true! */
              tempah = ((0x10 >> tempcl) | 0x80);
           }
        } else tempah = 0x80;

        if(SiS_Pr->SiS_VBInfo & SetInSlaveMode)  tempah ^= 0xA0;

#endif  /* SIS300 */

     } else {

#ifdef SIS315H    /* ------- 315/330 series ------ */

        if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
           if(SiS_Pr->SiS_VBInfo & DisableCRT2Display) {
	      SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2e,0x08);
           }
        }

        if(ModeNo > 0x13) {
           tempcl -= ModeVGA;
           if((tempcl > 0) || (tempcl == 0)) {  /* tempcl is USHORT -> always true! */
              tempah = (0x08 >> tempcl);
              if (tempah == 0) tempah = 1;
              tempah |= 0x40;
           }
        } else tempah = 0x40;

        if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) tempah ^= 0x50;

#endif  /* SIS315H */

     }

     if(SiS_Pr->SiS_VBInfo & DisableCRT2Display) tempah = 0;

     if(HwInfo->jChipType < SIS_315H) {
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x00,tempah);
     } else {
        if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
           SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x00,0xa0,tempah);
        } else if(SiS_Pr->SiS_VBType & VB_SISVB) {
           if(IS_SIS740) {
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x00,tempah);
	   } else {
              SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x00,0xa0,tempah);
	   }
        }
     }

     if(SiS_Pr->SiS_VBType & VB_SISVB) {

        tempah = 0x01;
        if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
      	   tempah |= 0x02;
        }
        if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC)) {
      	   tempah ^= 0x05;
      	   if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) {
              tempah ^= 0x01;
      	   }
        }

        if(SiS_Pr->SiS_VBInfo & DisableCRT2Display)  tempah = 0;

        if(HwInfo->jChipType < SIS_315H) {

      	   tempah = (tempah << 5) & 0xFF;
      	   SiS_SetReg(SiS_Pr->SiS_Part1Port,0x01,tempah);
      	   tempah = (tempah >> 5) & 0xFF;

        } else {

      	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2E,0xF8,tempah);

        }

        if((SiS_Pr->SiS_ModeType == ModeVGA) && (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode))) {
      	   tempah |= 0x10;
        }

        if((HwInfo->jChipType < SIS_315H) && (SiS_Pr->SiS_VBType & VB_SIS301)) {
	   if((SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) ||
	      (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x960)) {
	      tempah |= 0x80;
	   }
        } else {
	   tempah |= 0x80;
        }

        if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
	   if(!(SiS_Pr->SiS_TVMode & (TVSetYPbPr750p | TVSetYPbPr525p))) {
      	      if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
                 tempah |= 0x20;
	      }
      	   }
        }

        SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x0D,0x40,tempah);

        tempah = 0;

	if(SiS_IsDualLink(SiS_Pr, HwInfo)) tempah |= 0x40;

        if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
	   if(SiS_Pr->SiS_TVMode & TVRPLLDIV2XO) {
              tempah |= 0x40;
       	   }
        }

	if((SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) ||
	   (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x960)  ||
	   ((SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_PanelCustom) &&
	    (SiS_Pr->CP_MaxX >= 1280) && (SiS_Pr->CP_MaxY >= 960))) {
	   tempah |= 0x80;
        }

        SiS_SetReg(SiS_Pr->SiS_Part4Port,0x0C,tempah);

     } else {  /* LVDS */

        if(HwInfo->jChipType >= SIS_315H) {

	   /* LVDS can only be slave in 8bpp modes */
	   tempah = 0x80;
	   if((modeflag & CRT2Mode) && (SiS_Pr->SiS_ModeType > ModeVGA)) {
	      if(SiS_Pr->SiS_VBInfo & DriverMode) {
	         tempah |= 0x02;
	      }
	   }

	   if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
              tempah |= 0x02;
    	   }

	   if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
	      tempah ^= 0x01;
	   }

	   if(SiS_Pr->SiS_VBInfo & DisableCRT2Display) {
	      tempah = 1;
	   }

    	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2e,0xF0,tempah);

        } else {

	   tempah = 0;
	   if( (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) && (SiS_Pr->SiS_ModeType > ModeVGA) ) {
              tempah |= 0x02;
    	   }
	   tempah <<= 5;

	   if(SiS_Pr->SiS_VBInfo & DisableCRT2Display)  tempah = 0;

	   SiS_SetReg(SiS_Pr->SiS_Part1Port,0x01,tempah);

        }

     }

  }  /* LCDA */

  if(SiS_Pr->SiS_VBType & VB_SISVB) {

     if(HwInfo->jChipType >= SIS_315H) {

#ifdef SIS315H

        unsigned char bridgerev = SiS_GetReg(SiS_Pr->SiS_Part4Port,0x01);

	/* The following is nearly unpreditable and varies from machine
	 * to machine. Especially the 301DH seems to be a real trouble
	 * maker. Some BIOSes simply set the registers (like in the
	 * NoLCD-if-statements here), some set them according to the
	 * LCDA stuff. It is very likely that some machines are not
	 * treated correctly in the following, very case-orientated
	 * code. What do I do then...?
	 */

	/* 740 variants match for 30xB, 301B-DH, 30xLV */

        if(!(IS_SIS740)) {
           tempah = 0x04;						   /* For all bridges */
           tempbl = 0xfb;
           if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
              tempah = 0x00;
	      if(SiS_IsDualEdge(SiS_Pr, HwInfo)) {
	         tempbl = 0xff;
	      }
           }
           SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,tempbl,tempah);
	}

	/* The following two are responsible for eventually wrong colors
	 * in TV output. The DH (VB_NoLCD) conditions are unknown; the
	 * b0 was found in some 651 machine (Pim; P4_23=0xe5); the b1 version
	 * in a 650 box (Jake). What is the criteria?
	 */

	if((IS_SIS740) || (HwInfo->jChipType >= SIS_661)) {
	   tempah = 0x30;
	   tempbl = 0xc0;
	   if(SiS_Pr->SiS_VBInfo & DisableCRT2Display) {
	      tempah = 0x00;
	      tempbl = 0x00;
	   }
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2c,0xcf,tempah);
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x21,0x3f,tempbl);
	} else if(SiS_Pr->SiS_VBType & VB_SIS301) {
	   /* Fixes "TV-blue-bug" on 315+301 */
	   SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2c,0xcf);     /* For 301   */
	   SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x21,0x3f);
	} else if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	   SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2c,0x30);      /* For 30xLV */
	   SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x21,0xc0);
	} else if((SiS_Pr->SiS_VBType & VB_NoLCD) && (bridgerev == 0xb0)) {
	   SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2c,0x30);      /* For 30xB-DH rev b0 (or "DH on 651"?) */
	   SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x21,0xc0);
	} else {
	   tempah = 0x30; tempah2 = 0xc0;		       /* For 30xB (and 301BDH rev b1) */
	   tempbl = 0xcf; tempbl2 = 0x3f;
	   if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
	      tempah = tempah2 = 0x00;
	      if(SiS_IsDualEdge(SiS_Pr, HwInfo)) {
		 tempbl = tempbl2 = 0xff;
	      }
	   }
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2c,tempbl,tempah);
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x21,tempbl2,tempah2);
	}

	if(IS_SIS740) {
	   tempah = 0x80;
	   if(SiS_Pr->SiS_VBInfo & DisableCRT2Display) {
	      tempah = 0x00;
	   }
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x23,0x7f,tempah);
	} else {
	   tempah = 0x00;
           tempbl = 0x7f;
           if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
              tempbl = 0xff;
	      if(!(SiS_IsDualEdge(SiS_Pr, HwInfo))) {
	         tempah = 0x80;
	      }
           }
           SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x23,tempbl,tempah);
	}

	/* 661: Sets p4 27 and 34 here, done in SetGroup4 here */

#endif /* SIS315H */

     } else if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {

        SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x21,0x3f);

        if((SiS_Pr->SiS_VBInfo & DisableCRT2Display) ||
           (   (SiS_Pr->SiS_VBType & VB_NoLCD) &&
	       (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) ) ) {
	   SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x23,0x7F);
	} else {
	   SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x23,0x80);
	}

     }

  } else {  /* LVDS */

#ifdef SIS315H
     if(HwInfo->jChipType >= SIS_315H) {

        if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {

           tempah = 0x04;
	   tempbl = 0xfb;
           if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
              tempah = 0x00;
	      if(SiS_IsDualEdge(SiS_Pr, HwInfo)) {
	         tempbl = 0xff;
	      }
           }
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,tempbl,tempah);

	   if(SiS_Pr->SiS_VBInfo & DisableCRT2Display) {
	      SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x13,0xfb);
	   }

	   SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2c,0x30);

	} else if(HwInfo->jChipType == SIS_550) {

	   SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x13,0xfb);
	   SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2c,0x30);

	}

     }
#endif

  }

}

/*********************************************/
/*            GET RESOLUTION DATA            */
/*********************************************/

USHORT
SiS_GetResInfo(SiS_Private *SiS_Pr,USHORT ModeNo,USHORT ModeIdIndex)
{
  USHORT resindex;

  if(ModeNo <= 0x13)
     resindex = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  else
     resindex = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;

  return(resindex);
}

static void
SiS_GetCRT2ResInfo(SiS_Private *SiS_Pr,USHORT ModeNo,USHORT ModeIdIndex,
                   PSIS_HW_INFO HwInfo)
{
  USHORT xres,yres,modeflag=0,resindex;

  if(SiS_Pr->UseCustomMode) {
     SiS_Pr->SiS_VGAHDE = SiS_Pr->SiS_HDE = SiS_Pr->CHDisplay;
     SiS_Pr->SiS_VGAVDE = SiS_Pr->SiS_VDE = SiS_Pr->CVDisplay;
     return;
  }

  resindex = SiS_GetResInfo(SiS_Pr,ModeNo,ModeIdIndex);

  if(ModeNo <= 0x13) {
     xres = SiS_Pr->SiS_StResInfo[resindex].HTotal;
     yres = SiS_Pr->SiS_StResInfo[resindex].VTotal;
  } else {
     xres = SiS_Pr->SiS_ModeResInfo[resindex].HTotal;
     yres = SiS_Pr->SiS_ModeResInfo[resindex].VTotal;
     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
  }

  if((!SiS_Pr->SiS_IF_DEF_DSTN) && (!SiS_Pr->SiS_IF_DEF_FSTN)) {

     if((HwInfo->jChipType >= SIS_315H) && (SiS_Pr->SiS_IF_DEF_LVDS == 1)) {
        if((ModeNo != 0x03) && (SiS_Pr->SiS_SetFlag & SetDOSMode)) {
           if(yres == 350) yres = 400;
        }
        if(SiS_GetReg(SiS_Pr->SiS_P3d4,0x3a) & 0x01) {
 	   if(ModeNo == 0x12) yres = 400;
        }
     }

     if(ModeNo > 0x13) {
  	if(modeflag & HalfDCLK)       xres *= 2;
  	if(modeflag & DoubleScanMode) yres *= 2;
     }

  }

  if(SiS_Pr->SiS_VBType & VB_SISVB) {
        if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
           if(xres == 720) xres = 640;
	} else {
	   if(SiS_Pr->SiS_VBType & VB_NoLCD) {           /* 301BDH */
	        if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToHiVision)) {
                   if(xres == 720) xres = 640;
		}
		if(SiS_Pr->SiS_SetFlag & SetDOSMode) {
	           yres = 400;
	           if(HwInfo->jChipType >= SIS_315H) {
	              if(SiS_GetReg(SiS_Pr->SiS_P3c4,0x17) & 0x80) yres = 480;
	           } else {
	              if(SiS_GetReg(SiS_Pr->SiS_P3c4,0x13) & 0x80) yres = 480;
	           }
	        }
	   } else {
	      if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToHiVision)) {
	         if(xres == 720) xres = 640;
	      }
	      if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
		 if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) {
		    if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) {
        	       if(yres == 1024) yres = 1056;
      		    }
		 } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
		    if(!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {
		       /* BIOS bug - does this regardless of scaling */
      		       if(yres == 400) yres = 405;
		    }
      		    if(yres == 350) yres = 360;
      		    if(SiS_Pr->SiS_SetFlag & LCDVESATiming) {
        	       if(yres == 360) yres = 375;
      		    }
   	         } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
      		    if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) {
        	       if(!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {
          	          if(yres == 350) yres = 357;
          	          if(yres == 400) yres = 420;
            	          if(yres == 480) yres = 525;
        	       }
      		    }
    	         }
	      }
	   }
	}
  } else {
    	if(xres == 720) xres = 640;
	if(SiS_Pr->SiS_SetFlag & SetDOSMode) {
	   yres = 400;
	   if(HwInfo->jChipType >= SIS_315H) {
	      if(SiS_GetReg(SiS_Pr->SiS_P3c4,0x17) & 0x80) yres = 480;
	   } else {
	      if(SiS_GetReg(SiS_Pr->SiS_P3c4,0x13) & 0x80) yres = 480;
	   }
	   if(SiS_Pr->SiS_IF_DEF_DSTN || SiS_Pr->SiS_IF_DEF_FSTN) {
	      yres = 480;
	   }
	}
  }
  SiS_Pr->SiS_VGAHDE = SiS_Pr->SiS_HDE = xres;
  SiS_Pr->SiS_VGAVDE = SiS_Pr->SiS_VDE = yres;
}

/*********************************************/
/*           GET CRT2 TIMING DATA            */
/*********************************************/

static BOOLEAN
SiS_GetLVDSCRT1Ptr(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
		   USHORT RefreshRateTableIndex, USHORT *ResIndex,
		   USHORT *DisplayType)
 {
  USHORT tempbx,modeflag=0;
  USHORT Flag,CRT2CRTC;

  if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
     if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
        if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) return FALSE;
     }
  } else if(SiS_Pr->SiS_VBType & VB_SISVB) {
     if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) return FALSE;
  } else
     return FALSE;

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     CRT2CRTC = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     CRT2CRTC = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
  }

  Flag = 1;
  tempbx = 0;
  if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
     if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) {
        Flag = 0;
        tempbx = 18;
        if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) tempbx++;
        if(SiS_Pr->SiS_TVMode & TVSetPAL) {
      	   tempbx += 2;
	   if(SiS_Pr->SiS_ModeType > ModeVGA) {
	      if(SiS_Pr->SiS_CHSOverScan) tempbx = 99;
	   }
	   if(SiS_Pr->SiS_TVMode & TVSetPALM) {
	      tempbx = 18;  /* PALM uses NTSC data */
	      if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) tempbx++;
	   } else if(SiS_Pr->SiS_TVMode & TVSetPALN) {
	      tempbx = 20;  /* PALN uses PAL data  */
	      if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) tempbx++;
	   }
        }
     }
  }
  if(Flag) {
     tempbx = SiS_Pr->SiS_LCDResInfo;
     tempbx -= SiS_Pr->SiS_PanelMinLVDS;
     if(SiS_Pr->SiS_LCDResInfo <= SiS_Pr->SiS_Panel1280x1024) {
        if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx += 6;
        if(modeflag & HalfDCLK) tempbx += 3;
     } else {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
           tempbx = 14;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx += 2;
	   if(modeflag & HalfDCLK) tempbx++;
        } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600) {
           tempbx = 23;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx += 2;
	   if(modeflag & HalfDCLK) tempbx++;
        } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x768) {
           tempbx = 27;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx += 2;
	   if(modeflag & HalfDCLK) tempbx++;
        } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) {
           tempbx = 36;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx += 2;
	   if(modeflag & HalfDCLK) tempbx++;
        } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768) {
           tempbx = 40;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx += 2;
	   if(modeflag & HalfDCLK) tempbx++;
        } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_3) {
           tempbx = 54;
	   if(modeflag & HalfDCLK) tempbx++;
	} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_2) {
           tempbx = 52;
	   if(modeflag & HalfDCLK) tempbx++;
	} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480) {
           tempbx = 50;
	   if(modeflag & HalfDCLK) tempbx++;
        }

     }
     if(SiS_Pr->SiS_LCDInfo & LCDPass11) {
        tempbx = 12;
	if(modeflag & HalfDCLK) tempbx++;
     }
  }

#if 0
  if(SiS_Pr->SiS_IF_DEF_FSTN) {
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel320x480){
        tempbx = 22;
     }
  }
#endif

  *ResIndex = CRT2CRTC & 0x3F;
  *DisplayType = tempbx;
  return TRUE;
}

static void
SiS_GetCRT2Ptr(SiS_Private *SiS_Pr,USHORT ModeNo,USHORT ModeIdIndex,
	       USHORT RefreshRateTableIndex,USHORT *CRT2Index,USHORT *ResIndex,
	       PSIS_HW_INFO HwInfo)
{
  USHORT tempbx=0,tempal=0;
  USHORT Flag,resinfo=0;

  if(ModeNo <= 0x13) {
     tempal = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
     tempal = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
     resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  }

  if((SiS_Pr->SiS_VBType & VB_SISVB) && (SiS_Pr->SiS_IF_DEF_LVDS == 0)) {

     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {                            /* LCD */

	if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x960) {
	   tempbx = 15;
  	} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
	   tempbx = 20;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)         tempbx = 21;
	   else if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) tempbx = 22;
 	} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) {
	   tempbx = 23;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)         tempbx = 24;
	   else if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) tempbx = 25;
#if 0
	} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768) {
	   tempbx = 26;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)         tempbx = 27;
	   else if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) tempbx = 28;
#endif
 	} else if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
	   if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	      if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)       tempbx = 13;
	      else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) tempbx = 14;
	      else {
	         tempbx = 29;
		 if(ModeNo >= 0x13) {
	            /* see below */
	            if(resinfo == SIS_RI_1280x960) tempal = 10;
	         }
              }
	   } else {
	      tempbx = 29;
	      if(ModeNo >= 0x13) {
	         /* 1280x768 and 1280x960 have same CRT2CRTC,
	          * so we change it here if 1280x960 is chosen
	          */
	         if(resinfo == SIS_RI_1280x960) tempal = 10;
	      }
   	   }
	} else {
      	   tempbx = SiS_Pr->SiS_LCDResInfo - SiS_Pr->SiS_Panel1024x768;
      	   if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) {
              tempbx += 10;
       	   }
	}

#ifdef SIS315H
	if(SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) {
	   if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
	      tempbx = 50;
	      if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)         tempbx = 51;
	      else if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) tempbx = 52;
	   }
	}
#endif

     } else {						  	/* TV */

     	if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
           /* if(SiS_Pr->SiS_VGAVDE > 480) SiS_Pr->SiS_TVMode &= (~TVSetTVSimuMode); */
           tempbx = 2;
           if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
	      tempbx = 13;
              if(!(SiS_Pr->SiS_TVMode & TVSetTVSimuMode)) tempbx = 14;
           }
	} else if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {
	   if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p)      tempbx = 7;
	   else if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p) tempbx = 6;
	   else 					tempbx = 5;
	   if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode)     tempbx += 5;
       	} else {
           if(SiS_Pr->SiS_TVMode & TVSetPAL) 		tempbx = 3;
           else 					tempbx = 4;
           if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode) 	tempbx += 5;
       	}

     }

     tempal &= 0x3F;

     if(ModeNo > 0x13) {
        if(SiS_Pr->SiS_VBInfo & SetCRT2ToTVNoHiVision) {
      	   if(tempal == 6) tempal = 7;
           if((resinfo == SIS_RI_720x480) ||
	      (resinfo == SIS_RI_720x576) ||
	      (resinfo == SIS_RI_768x576)) {
	      tempal = 6;
	   }
	   if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {
              if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p) {
	         if(resinfo == SIS_RI_1024x768) {
	            tempal = 8;
	         }
	      }
	   }
	}
     }

     *CRT2Index = tempbx;
     *ResIndex = tempal;

  } else {   /* LVDS, 301B-DH (if running on LCD) */

     Flag = 1;
     tempbx = 0;
     if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
        if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) {
           Flag = 0;
           tempbx = 10;
	   if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) tempbx += 1;
           if(SiS_Pr->SiS_TVMode & TVSetPAL) {
	      tempbx += 2;
	      if(SiS_Pr->SiS_ModeType > ModeVGA) {
		 if(SiS_Pr->SiS_CHSOverScan) tempbx = 99;
	      }
	      if(SiS_Pr->SiS_TVMode & TVSetPALM) {
		 tempbx = 90;
		 if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) tempbx += 1;
	      } else if(SiS_Pr->SiS_TVMode & TVSetPALN) {
		 tempbx = 92;
		 if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) tempbx += 1;
	      }
           }
        }
     }

     if(Flag) {

	if(SiS_Pr->SiS_LCDResInfo <= SiS_Pr->SiS_Panel1280x1024) {
	   tempbx = SiS_Pr->SiS_LCDResInfo - SiS_Pr->SiS_PanelMinLVDS;
   	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)  tempbx += 3;

	   if(SiS_Pr->SiS_CustomT == CUT_BARCO1024) {
	      tempbx = 82;
	      if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)  tempbx++;
	   }
	} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768) {
	   tempbx = 18;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)  tempbx++;
	} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480) {
	   tempbx = 6;
	} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_2) {
	   tempbx = 30;
	} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_3) {
	   tempbx = 30;
	} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600) {
	   tempbx = 15;
  	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)  tempbx += 2;
	} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x768) {
	   tempbx = 16;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)  tempbx += 2;
	} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
	   tempbx = 8;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)  tempbx++;
	} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) {
	   tempbx = 21;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)  tempbx++;
	} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_PanelBarco1366) {
	   tempbx = 80;
   	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)  tempbx++;
	}

	if(SiS_Pr->SiS_LCDInfo & LCDPass11) {
	   tempbx = 7;
        }

	if(SiS_Pr->SiS_CustomT == CUT_PANEL848) {
	   tempbx = 84;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)  tempbx++;
	}

     }

     if(SiS_Pr->SiS_SetFlag & SetDOSMode) {
        if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) tempal = 7;
  	if(HwInfo->jChipType < SIS_315H) {
	   if(SiS_GetReg(SiS_Pr->SiS_P3c4,0x13) & 0x80) tempal++;
	}
     }

     *CRT2Index = tempbx;
     *ResIndex = tempal & 0x1F;
  }
}

#ifdef SIS315H
static void
SiS_GetCRT2PtrA(SiS_Private *SiS_Pr,USHORT ModeNo,USHORT ModeIdIndex,
		USHORT RefreshRateTableIndex,USHORT *CRT2Index,
		USHORT *ResIndex)
{
  USHORT tempbx,tempal;

  tempbx = SiS_Pr->SiS_LCDResInfo;

  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200)      tempbx = 4;
  else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) tempbx = 3;
  else tempbx -= SiS_Pr->SiS_Panel1024x768;

  if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)  tempbx += 5;

  if(ModeNo <= 0x13)
     tempal = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  else
     tempal = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;

  /* No customs required yet (Clevo, Compaq, etc) */

  *CRT2Index = tempbx;
  *ResIndex = tempal & 0x1F;
}
#endif

static void
SiS_GetRAMDAC2DATA(SiS_Private *SiS_Pr,USHORT ModeNo,USHORT ModeIdIndex,
                   USHORT RefreshRateTableIndex,PSIS_HW_INFO HwInfo)
{
  USHORT tempax=0,tempbx=0;
  USHORT temp1=0,modeflag=0,tempcx=0;
  USHORT index;

  SiS_Pr->SiS_RVBHCMAX  = 1;
  SiS_Pr->SiS_RVBHCFACT = 1;

  if(ModeNo <= 0x13) {

     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     index = SiS_GetModePtr(SiS_Pr,ModeNo,ModeIdIndex);

     tempax = SiS_Pr->SiS_StandTable[index].CRTC[0];
     tempbx = SiS_Pr->SiS_StandTable[index].CRTC[6];
     temp1 = SiS_Pr->SiS_StandTable[index].CRTC[7];

  } else {

     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     index = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT1CRTC;

     tempax = SiS_Pr->SiS_CRT1Table[index].CR[0];
     tempax |= (SiS_Pr->SiS_CRT1Table[index].CR[14] << 8);
     tempax &= 0x03FF;
     tempbx = SiS_Pr->SiS_CRT1Table[index].CR[6];
     tempcx = SiS_Pr->SiS_CRT1Table[index].CR[13] << 8;
     tempcx &= 0x0100;
     tempcx <<= 2;
     tempbx |= tempcx;
     temp1  = SiS_Pr->SiS_CRT1Table[index].CR[7];

  }

  if(temp1 & 0x01) tempbx |= 0x0100;
  if(temp1 & 0x20) tempbx |= 0x0200;

  tempax += 5;

  /* Charx8Dot is no more used (and assumed), so we set it */
  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
     modeflag |= Charx8Dot;
  }

  if(modeflag & Charx8Dot) tempax *= 8;
  else                     tempax *= 9;

  if(modeflag & HalfDCLK)  tempax <<= 1;

  tempbx++;

  SiS_Pr->SiS_VGAHT = SiS_Pr->SiS_HT = tempax;
  SiS_Pr->SiS_VGAVT = SiS_Pr->SiS_VT = tempbx;
}

static void
SiS_GetCRT2DataLVDS(SiS_Private *SiS_Pr,USHORT ModeNo,USHORT ModeIdIndex,
                    USHORT RefreshRateTableIndex,
		    PSIS_HW_INFO HwInfo)
{
   USHORT CRT2Index, ResIndex;
   const SiS_LVDSDataStruct *LVDSData = NULL;

   SiS_GetCRT2ResInfo(SiS_Pr, ModeNo, ModeIdIndex, HwInfo);

   if(SiS_Pr->SiS_VBType & VB_SISVB) {
      SiS_Pr->SiS_RVBHCMAX  = 1;
      SiS_Pr->SiS_RVBHCFACT = 1;
      SiS_Pr->SiS_NewFlickerMode = 0;
      SiS_Pr->SiS_RVBHRS = 50;
      SiS_Pr->SiS_RY1COE = 0;
      SiS_Pr->SiS_RY2COE = 0;
      SiS_Pr->SiS_RY3COE = 0;
      SiS_Pr->SiS_RY4COE = 0;
   }

   if((SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {

#ifdef SIS315H
      SiS_GetCRT2PtrA(SiS_Pr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                      &CRT2Index,&ResIndex);

      switch (CRT2Index) {
      	case  0:  LVDSData = SiS_Pr->SiS_LCDA1024x768Data_1;    break;
      	case  1:  LVDSData = SiS_Pr->SiS_LCDA1280x1024Data_1;   break;
	case  3:  LVDSData = SiS_Pr->SiS_LCDA1400x1050Data_1;   break;
	case  4:  LVDSData = SiS_Pr->SiS_LCDA1600x1200Data_1;   break;
      	case  5:  LVDSData = SiS_Pr->SiS_LCDA1024x768Data_2;    break;
      	case  6:  LVDSData = SiS_Pr->SiS_LCDA1280x1024Data_2;   break;
	case  8:  LVDSData = SiS_Pr->SiS_LCDA1400x1050Data_2;   break;
	case  9:  LVDSData = SiS_Pr->SiS_LCDA1600x1200Data_2;   break;
	default:  LVDSData = SiS_Pr->SiS_LCDA1024x768Data_1;    break;
      }
#endif

   } else {

      /* 301BDH needs LVDS Data */
      if((SiS_Pr->SiS_VBType & VB_NoLCD) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) {
	 SiS_Pr->SiS_IF_DEF_LVDS = 1;
      }

      SiS_GetCRT2Ptr(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex,
                     &CRT2Index, &ResIndex, HwInfo);

      /* 301BDH needs LVDS Data */
      if((SiS_Pr->SiS_VBType & VB_NoLCD) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) {
         SiS_Pr->SiS_IF_DEF_LVDS = 0;
      }

      switch (CRT2Index) {
      	case  0:  LVDSData = SiS_Pr->SiS_LVDS800x600Data_1;    break;
      	case  1:  LVDSData = SiS_Pr->SiS_LVDS1024x768Data_1;   break;
      	case  2:  LVDSData = SiS_Pr->SiS_LVDS1280x1024Data_1;  break;
      	case  3:  LVDSData = SiS_Pr->SiS_LVDS800x600Data_2;    break;
      	case  4:  LVDSData = SiS_Pr->SiS_LVDS1024x768Data_2;   break;
      	case  5:  LVDSData = SiS_Pr->SiS_LVDS1280x1024Data_2;  break;
	case  6:  LVDSData = SiS_Pr->SiS_LVDS640x480Data_1;    break;
        case  7:  LVDSData = SiS_Pr->SiS_LVDSXXXxXXXData_1;    break;
	case  8:  LVDSData = SiS_Pr->SiS_LVDS1400x1050Data_1;  break;
	case  9:  LVDSData = SiS_Pr->SiS_LVDS1400x1050Data_2;  break;
      	case 10:  LVDSData = SiS_Pr->SiS_CHTVUNTSCData;        break;
      	case 11:  LVDSData = SiS_Pr->SiS_CHTVONTSCData;        break;
      	case 12:  LVDSData = SiS_Pr->SiS_CHTVUPALData;         break;
      	case 13:  LVDSData = SiS_Pr->SiS_CHTVOPALData;         break;
      	case 14:  LVDSData = SiS_Pr->SiS_LVDS320x480Data_1;    break;
	case 15:  LVDSData = SiS_Pr->SiS_LVDS1024x600Data_1;   break;
	case 16:  LVDSData = SiS_Pr->SiS_LVDS1152x768Data_1;   break;
	case 17:  LVDSData = SiS_Pr->SiS_LVDS1024x600Data_2;   break;
	case 18:  LVDSData = SiS_Pr->SiS_LVDS1152x768Data_2;   break;
	case 19:  LVDSData = SiS_Pr->SiS_LVDS1280x768Data_1;   break;
	case 20:  LVDSData = SiS_Pr->SiS_LVDS1280x768Data_2;   break;
	case 21:  LVDSData = SiS_Pr->SiS_LVDS1600x1200Data_1;  break;
	case 22:  LVDSData = SiS_Pr->SiS_LVDS1600x1200Data_2;  break;
	case 30:  LVDSData = SiS_Pr->SiS_LVDS640x480Data_2;    break;
	case 80:  LVDSData = SiS_Pr->SiS_LVDSBARCO1366Data_1;  break;
	case 81:  LVDSData = SiS_Pr->SiS_LVDSBARCO1366Data_2;  break;
	case 82:  LVDSData = SiS_Pr->SiS_LVDSBARCO1024Data_1;  break;
	case 83:  LVDSData = SiS_Pr->SiS_LVDSBARCO1024Data_2;  break;
	case 84:  LVDSData = SiS_Pr->SiS_LVDS848x480Data_1;    break;
	case 85:  LVDSData = SiS_Pr->SiS_LVDS848x480Data_2;    break;
	case 90:  LVDSData = SiS_Pr->SiS_CHTVUPALMData;        break;
      	case 91:  LVDSData = SiS_Pr->SiS_CHTVOPALMData;        break;
      	case 92:  LVDSData = SiS_Pr->SiS_CHTVUPALNData;        break;
      	case 93:  LVDSData = SiS_Pr->SiS_CHTVOPALNData;        break;
	case 99:  LVDSData = SiS_Pr->SiS_CHTVSOPALData;	       break;  /* Super Overscan */
	default:  LVDSData = SiS_Pr->SiS_LVDS1024x768Data_1;   break;
      }
   }

   SiS_Pr->SiS_VGAHT = (LVDSData+ResIndex)->VGAHT;
   SiS_Pr->SiS_VGAVT = (LVDSData+ResIndex)->VGAVT;
   SiS_Pr->SiS_HT    = (LVDSData+ResIndex)->LCDHT;
   SiS_Pr->SiS_VT    = (LVDSData+ResIndex)->LCDVT;

   if(SiS_Pr->SiS_VBType & VB_SISVB) {

      if(!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {
         SiS_Pr->SiS_HDE = SiS_Pr->PanelXRes;
         SiS_Pr->SiS_VDE = SiS_Pr->PanelYRes;
      }

   } else {

      if(SiS_Pr->SiS_IF_DEF_TRUMPION == 0) {
         if((SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) && (!(SiS_Pr->SiS_LCDInfo & LCDPass11))) {
            if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {
               if((!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) || (SiS_Pr->SiS_SetFlag & SetDOSMode)) {
	          SiS_Pr->SiS_HDE = SiS_Pr->PanelXRes;
                  SiS_Pr->SiS_VDE = SiS_Pr->PanelYRes;

	 	  if(SiS_Pr->SiS_CustomT == CUT_BARCO1366) {
		     if(ResIndex < 0x08) {
		        SiS_Pr->SiS_HDE = 1280;
                        SiS_Pr->SiS_VDE = 1024;
		     }
		  }
               }
            }
         }
      }
   }
}

static void
SiS_GetCRT2Data301(SiS_Private *SiS_Pr,USHORT ModeNo,USHORT ModeIdIndex,
                   USHORT RefreshRateTableIndex,
		   PSIS_HW_INFO HwInfo)
{
  USHORT tempax,tempbx,modeflag;
  USHORT resinfo;
  USHORT CRT2Index,ResIndex;
  const SiS_LCDDataStruct *LCDPtr = NULL;
  const SiS_TVDataStruct  *TVPtr  = NULL;

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  } else {
     if(SiS_Pr->UseCustomMode) {
        modeflag = SiS_Pr->CModeFlag;
	resinfo = 0;
     } else {
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    	resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
     }
  }
  
  SiS_Pr->SiS_NewFlickerMode = 0;
  SiS_Pr->SiS_RVBHRS = 50;
  SiS_Pr->SiS_RY1COE = 0;
  SiS_Pr->SiS_RY2COE = 0;
  SiS_Pr->SiS_RY3COE = 0;
  SiS_Pr->SiS_RY4COE = 0;

  SiS_GetCRT2ResInfo(SiS_Pr,ModeNo,ModeIdIndex,HwInfo);

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC){

     if(SiS_Pr->UseCustomMode) {

        SiS_Pr->SiS_RVBHCMAX  = 1;
        SiS_Pr->SiS_RVBHCFACT = 1;
        SiS_Pr->SiS_VGAHT     = SiS_Pr->CHTotal;
        SiS_Pr->SiS_VGAVT     = SiS_Pr->CVTotal;
        SiS_Pr->SiS_HT        = SiS_Pr->CHTotal;
        SiS_Pr->SiS_VT        = SiS_Pr->CVTotal;
	SiS_Pr->SiS_HDE       = SiS_Pr->SiS_VGAHDE;
        SiS_Pr->SiS_VDE       = SiS_Pr->SiS_VGAVDE;

     } else {

        SiS_GetRAMDAC2DATA(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex, HwInfo);
     }

  } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {

     SiS_GetCRT2Ptr(SiS_Pr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                    &CRT2Index,&ResIndex,HwInfo);

     switch(CRT2Index) {
       case  2:  TVPtr = SiS_Pr->SiS_ExtHiTVData;   break;
       case  3:  TVPtr = SiS_Pr->SiS_ExtPALData;    break;
       case  4:  TVPtr = SiS_Pr->SiS_ExtNTSCData;   break;
       case  5:  TVPtr = SiS_Pr->SiS_Ext525iData;   break;
       case  6:  TVPtr = SiS_Pr->SiS_Ext525pData;   break;
       case  7:  TVPtr = SiS_Pr->SiS_Ext750pData;   break;
       case  8:  TVPtr = SiS_Pr->SiS_StPALData;     break;
       case  9:  TVPtr = SiS_Pr->SiS_StNTSCData;    break;
       case 10:  TVPtr = SiS_Pr->SiS_St525iData;    break;
       case 11:  TVPtr = SiS_Pr->SiS_St525pData;    break;
       case 12:  TVPtr = SiS_Pr->SiS_St750pData;    break;
       case 13:  TVPtr = SiS_Pr->SiS_St1HiTVData;   break;
       case 14:  TVPtr = SiS_Pr->SiS_St2HiTVData;   break;
       default:  TVPtr = SiS_Pr->SiS_StPALData;     break;
     }

     SiS_Pr->SiS_RVBHCMAX  = (TVPtr+ResIndex)->RVBHCMAX;
     SiS_Pr->SiS_RVBHCFACT = (TVPtr+ResIndex)->RVBHCFACT;
     SiS_Pr->SiS_VGAHT     = (TVPtr+ResIndex)->VGAHT;
     SiS_Pr->SiS_VGAVT     = (TVPtr+ResIndex)->VGAVT;
     SiS_Pr->SiS_HDE       = (TVPtr+ResIndex)->TVHDE;
     SiS_Pr->SiS_VDE       = (TVPtr+ResIndex)->TVVDE;
     SiS_Pr->SiS_RVBHRS    = (TVPtr+ResIndex)->RVBHRS;
     SiS_Pr->SiS_NewFlickerMode = (TVPtr+ResIndex)->FlickerMode;
     if(modeflag & HalfDCLK) {
        SiS_Pr->SiS_RVBHRS = (TVPtr+ResIndex)->HALFRVBHRS;
     }

     if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {

        if((resinfo == SIS_RI_1024x768)  ||
           (resinfo == SIS_RI_1280x1024) ||
           (resinfo == SIS_RI_1280x720)) {
	   SiS_Pr->SiS_NewFlickerMode = 0x40;
	}

        if(SiS_Pr->SiS_VGAVDE == 350) SiS_Pr->SiS_TVMode |= TVSetTVSimuMode;

        SiS_Pr->SiS_HT = ExtHiTVHT;
        SiS_Pr->SiS_VT = ExtHiTVVT;
        if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
           if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode) {
              SiS_Pr->SiS_HT = StHiTVHT;
              SiS_Pr->SiS_VT = StHiTVVT;
#if 0
              if(!(modeflag & Charx8Dot)) {
                 SiS_Pr->SiS_HT = StHiTextTVHT;
                 SiS_Pr->SiS_VT = StHiTextTVVT;
              }
#endif
           }
        }

     } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {

        if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p) {
           SiS_Pr->SiS_HT = 1650;
           SiS_Pr->SiS_VT = 750;
	} else if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p) {
	   SiS_Pr->SiS_HT = NTSCHT;
	   SiS_Pr->SiS_VT = NTSCVT;
        } else {
           SiS_Pr->SiS_HT = NTSCHT;
	   if(SiS_Pr->SiS_TVMode & TVSetNTSC1024) SiS_Pr->SiS_HT = NTSC2HT;
           SiS_Pr->SiS_VT = NTSCVT;
        }

     } else {

        SiS_Pr->SiS_RY1COE = (TVPtr+ResIndex)->RY1COE;
        SiS_Pr->SiS_RY2COE = (TVPtr+ResIndex)->RY2COE;
        SiS_Pr->SiS_RY3COE = (TVPtr+ResIndex)->RY3COE;
        SiS_Pr->SiS_RY4COE = (TVPtr+ResIndex)->RY4COE;

        if(modeflag & HalfDCLK) {
           SiS_Pr->SiS_RY1COE = 0x00;
           SiS_Pr->SiS_RY2COE = 0xf4;
           SiS_Pr->SiS_RY3COE = 0x10;
           SiS_Pr->SiS_RY4COE = 0x38;
        }

        if(!(SiS_Pr->SiS_TVMode & TVSetPAL)) {
           SiS_Pr->SiS_HT = NTSCHT;
	   if(SiS_Pr->SiS_TVMode & TVSetNTSC1024) SiS_Pr->SiS_HT = NTSC2HT;
           SiS_Pr->SiS_VT = NTSCVT;
        } else {
           SiS_Pr->SiS_HT = PALHT;
           SiS_Pr->SiS_VT = PALVT;
        }

     }

  } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {

     if(SiS_Pr->UseCustomMode) {

        SiS_Pr->SiS_RVBHCMAX  = 1;
        SiS_Pr->SiS_RVBHCFACT = 1;
        SiS_Pr->SiS_VGAHT     = SiS_Pr->CHTotal;
        SiS_Pr->SiS_VGAVT     = SiS_Pr->CVTotal;
        SiS_Pr->SiS_HT        = SiS_Pr->CHTotal;
        SiS_Pr->SiS_VT        = SiS_Pr->CVTotal;
	SiS_Pr->SiS_HDE       = SiS_Pr->SiS_VGAHDE;
        SiS_Pr->SiS_VDE       = SiS_Pr->SiS_VGAVDE;

     } else {

        SiS_GetCRT2Ptr(SiS_Pr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                      &CRT2Index,&ResIndex,HwInfo);

        switch(CRT2Index) {
         case  0: LCDPtr = SiS_Pr->SiS_ExtLCD1024x768Data;        break; /* VESA Timing */
         case  1: LCDPtr = SiS_Pr->SiS_ExtLCD1280x1024Data;       break; /* VESA Timing */
         case  5: LCDPtr = SiS_Pr->SiS_StLCD1024x768Data;         break; /* Obviously unused */
         case  6: LCDPtr = SiS_Pr->SiS_StLCD1280x1024Data;        break; /* Obviously unused */
         case 10: LCDPtr = SiS_Pr->SiS_St2LCD1024x768Data;        break; /* Non-VESA Timing */
         case 11: LCDPtr = SiS_Pr->SiS_St2LCD1280x1024Data;       break; /* Non-VESA Timing */
         case 13: LCDPtr = SiS_Pr->SiS_NoScaleData1024x768;       break; /* Non-expanding */
         case 14: LCDPtr = SiS_Pr->SiS_NoScaleData1280x1024;      break; /* Non-expanding */
         case 15: LCDPtr = SiS_Pr->SiS_LCD1280x960Data;           break; /* 1280x960 */
         case 20: LCDPtr = SiS_Pr->SiS_ExtLCD1400x1050Data;       break; /* VESA Timing */
         case 21: LCDPtr = SiS_Pr->SiS_NoScaleData1400x1050;      break; /* Non-expanding (let panel scale) */
         case 22: LCDPtr = SiS_Pr->SiS_StLCD1400x1050Data;        break; /* Non-VESA Timing (let panel scale) */
         case 23: LCDPtr = SiS_Pr->SiS_ExtLCD1600x1200Data;       break; /* VESA Timing */
         case 24: LCDPtr = SiS_Pr->SiS_NoScaleData1600x1200;      break; /* Non-expanding */
         case 25: LCDPtr = SiS_Pr->SiS_StLCD1600x1200Data;        break; /* Non-VESA Timing */
         case 26: LCDPtr = SiS_Pr->SiS_ExtLCD1280x768Data;        break; /* VESA Timing */
         case 27: LCDPtr = SiS_Pr->SiS_NoScaleData1280x768;       break; /* Non-expanding */
         case 28: LCDPtr = SiS_Pr->SiS_StLCD1280x768Data;         break; /* Non-VESA Timing */
         case 29: LCDPtr = SiS_Pr->SiS_NoScaleData;	          break; /* Generic no-scale data */
#ifdef SIS315H
	 case 50: LCDPtr = (SiS_LCDDataStruct *)SiS310_ExtCompaq1280x1024Data;	break;
	 case 51: LCDPtr = SiS_Pr->SiS_NoScaleData1280x1024;			break;
	 case 52: LCDPtr = SiS_Pr->SiS_St2LCD1280x1024Data;	  		break;
#endif
         default: LCDPtr = SiS_Pr->SiS_ExtLCD1024x768Data;	  break;
        }

        SiS_Pr->SiS_RVBHCMAX  = (LCDPtr+ResIndex)->RVBHCMAX;
        SiS_Pr->SiS_RVBHCFACT = (LCDPtr+ResIndex)->RVBHCFACT;
        SiS_Pr->SiS_VGAHT     = (LCDPtr+ResIndex)->VGAHT;
        SiS_Pr->SiS_VGAVT     = (LCDPtr+ResIndex)->VGAVT;
        SiS_Pr->SiS_HT        = (LCDPtr+ResIndex)->LCDHT;
        SiS_Pr->SiS_VT        = (LCDPtr+ResIndex)->LCDVT;

#ifdef TWDEBUG
        xf86DrvMsg(0, X_INFO,
    	    "GetCRT2Data: Index %d ResIndex %d\n", CRT2Index, ResIndex);
#endif

	if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
           tempax = 1024;
           if(SiS_Pr->SiS_SetFlag & LCDVESATiming) {
              if(HwInfo->jChipType < SIS_315H) {
                 if     (SiS_Pr->SiS_VGAVDE == 350) tempbx = 560;
                 else if(SiS_Pr->SiS_VGAVDE == 400) tempbx = 640;
                 else                               tempbx = 768;
              } else {
                 tempbx = 768;
              }
           } else {
              if     (SiS_Pr->SiS_VGAVDE == 357) tempbx = 527;
              else if(SiS_Pr->SiS_VGAVDE == 420) tempbx = 620;
              else if(SiS_Pr->SiS_VGAVDE == 525) tempbx = 775;
              else if(SiS_Pr->SiS_VGAVDE == 600) tempbx = 775;
              else if(SiS_Pr->SiS_VGAVDE == 350) tempbx = 560;
              else if(SiS_Pr->SiS_VGAVDE == 400) tempbx = 640;
              else                               tempbx = 768;
           }
	} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
           tempax = 1280;
           if     (SiS_Pr->SiS_VGAVDE == 360) tempbx = 768;
           else if(SiS_Pr->SiS_VGAVDE == 375) tempbx = 800;
           else if(SiS_Pr->SiS_VGAVDE == 405) tempbx = 864;
           else                               tempbx = 1024;
        } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x960) {
           tempax = 1280;
           if     (SiS_Pr->SiS_VGAVDE == 350)  tempbx = 700;
           else if(SiS_Pr->SiS_VGAVDE == 400)  tempbx = 800;
           else if(SiS_Pr->SiS_VGAVDE == 1024) tempbx = 960;
           else                                tempbx = 960;
	} else if((SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) &&
	          (HwInfo->jChipType >= SIS_661)) {
	   tempax = 1400;
	   tempbx = 1050;
	   if(SiS_Pr->SiS_VGAVDE == 1024) {
	      tempax = 1280;
	      tempbx = 1024;
	   }
        } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) {
           tempax = 1600;
	   tempbx = 1200;
	   if((HwInfo->jChipType < SIS_661) || (!(SiS_Pr->SiS_SetFlag & LCDVESATiming))) {
              if     (SiS_Pr->SiS_VGAVDE == 350)  tempbx = 875;
              else if(SiS_Pr->SiS_VGAVDE == 400)  tempbx = 1000;
           }
        } else {
	   tempax = SiS_Pr->PanelXRes;
           tempbx = SiS_Pr->PanelYRes;
	}
        if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
           tempax = SiS_Pr->SiS_VGAHDE;
           tempbx = SiS_Pr->SiS_VGAVDE;
        }
        SiS_Pr->SiS_HDE = tempax;
        SiS_Pr->SiS_VDE = tempbx;
     }
  }
}

static void
SiS_GetCRT2Data(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
                USHORT RefreshRateTableIndex, PSIS_HW_INFO HwInfo)
{

  if(SiS_Pr->SiS_VBType & VB_SISVB) {

     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {

        if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {

           SiS_GetCRT2DataLVDS(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex, HwInfo);

        } else {

	   if((SiS_Pr->SiS_VBType & VB_NoLCD) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) {

	      /* Need LVDS Data for LCD on 301B-DH */
	      SiS_GetCRT2DataLVDS(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex, HwInfo);

	   } else {

	      SiS_GetCRT2Data301(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex, HwInfo);

           }

        }

     } else {

     	SiS_GetCRT2Data301(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex, HwInfo);

     }

  } else {

     SiS_GetCRT2DataLVDS(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex, HwInfo);

  }
}

/*********************************************/
/*            GET LVDS DES DATA              */
/*********************************************/

static void
SiS_GetLVDSDesPtr(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
                  USHORT RefreshRateTableIndex, USHORT *PanelIndex,
		  USHORT *ResIndex, PSIS_HW_INFO HwInfo)
{
  USHORT tempbx,tempal,modeflag;

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     tempal = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     tempal = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
  }

  tempbx = 0;
  if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
        tempbx = 50;
        if((SiS_Pr->SiS_TVMode & TVSetPAL) && (!(SiS_Pr->SiS_TVMode & TVSetPALM))) tempbx += 2;
        if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) tempbx += 1;
        /* Nothing special needed for SOverscan    */
        /*     PALM uses NTSC data, PALN uses PAL data */
     }
  }

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
     tempbx = SiS_Pr->SiS_LCDTypeInfo;
     if(HwInfo->jChipType >= SIS_661) {
        /* As long as we don's use the BIOS tables, we
	 * need to convert the TypeInfo as for 315 series
	 */
        tempbx = SiS_Pr->SiS_LCDResInfo - 1;
     }
     if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx += 16;
     if(SiS_Pr->SiS_LCDInfo & LCDPass11) {
        tempbx = 32;
        if(modeflag & HalfDCLK) tempbx++;
     }
  }

  if(SiS_Pr->SiS_SetFlag & SetDOSMode) {
     if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480)  {
        tempal = 0x07;
        if(HwInfo->jChipType < SIS_315H) {
           if(SiS_GetReg(SiS_Pr->SiS_P3c4,0x13) & 0x80) tempal++;
        }
     }
  }

  *PanelIndex = tempbx;
  *ResIndex = tempal & 0x1F;
}

#ifdef SIS315H
static void
SiS_GetLVDSDesPtrA(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
                   USHORT RefreshRateTableIndex, USHORT *PanelIndex, USHORT *ResIndex,
		   PSIS_HW_INFO HwInfo)
{
  USHORT tempbx=0,tempal;

  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050)      tempbx = 2;
  else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) tempbx = 3;
  else tempbx = SiS_Pr->SiS_LCDResInfo - 2;

  if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)  tempbx += 4;

  if(SiS_Pr->SiS_CustomT == CUT_CLEVO1024) {
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
        if(SiS_IsDualLink(SiS_Pr, HwInfo)) {
	   tempbx = 80;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx++;
	}
     }
  }
  if((SiS_Pr->SiS_CustomT == CUT_UNIWILL1024) ||
     (SiS_Pr->SiS_CustomT == CUT_UNIWILL10242)) {
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
	tempbx = 82;
	if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx++;
     }
  }
  if(SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) {
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
	tempbx = 84;
	if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx++;
     }
  }
  if(SiS_Pr->SiS_CustomT == CUT_ASUSA2H_2) {
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
	tempbx = 86;
	if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx++;
     }
  }

  if(ModeNo <= 0x13)
     tempal = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  else
     tempal = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;

  *PanelIndex = tempbx;
  *ResIndex = tempal & 0x1F;
}
#endif

static void
SiS_GetLVDSDesData(SiS_Private *SiS_Pr, USHORT ModeNo,USHORT ModeIdIndex,
                   USHORT RefreshRateTableIndex, PSIS_HW_INFO HwInfo)
{
  USHORT modeflag;
  USHORT PanelIndex,ResIndex;
  const  SiS_LVDSDesStruct *PanelDesPtr = NULL;

  if((SiS_Pr->UseCustomMode) ||
     (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_PanelCustom) ||
     (SiS_Pr->SiS_CustomT == CUT_PANEL848)) {
     SiS_Pr->SiS_LCDHDES = 0;
     SiS_Pr->SiS_LCDVDES = 0;
     return;
  }

  if((SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {

#ifdef SIS315H
     SiS_GetLVDSDesPtrA(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex,
                        &PanelIndex, &ResIndex, HwInfo);

     switch (PanelIndex)
     {
     	case  0: PanelDesPtr = SiS_Pr->LVDS1024x768Des_1;   break;  /* --- expanding --- */
     	case  1: PanelDesPtr = SiS_Pr->LVDS1280x1024Des_1;  break;
	case  2: PanelDesPtr = SiS_Pr->LVDS1400x1050Des_1;  break;
	case  3: PanelDesPtr = SiS_Pr->LVDS1600x1200Des_1;  break;
     	case  4: PanelDesPtr = SiS_Pr->LVDS1024x768Des_2;   break;  /* --- non expanding --- */
     	case  5: PanelDesPtr = SiS_Pr->LVDS1280x1024Des_2;  break;
	case  6: PanelDesPtr = SiS_Pr->LVDS1400x1050Des_2;  break;
	case  7: PanelDesPtr = SiS_Pr->LVDS1600x1200Des_2;  break;
	case 80: PanelDesPtr = (SiS_LVDSDesStruct *)Clevo1024x768Des_1;   break;  /*  custom  */
	case 81: PanelDesPtr = (SiS_LVDSDesStruct *)Clevo1024x768Des_2;   break;
	case 82: PanelDesPtr = (SiS_LVDSDesStruct *)Uniwill1024x768Des_1; break;
	case 83: PanelDesPtr = (SiS_LVDSDesStruct *)Uniwill1024x768Des_2; break;
	case 84: PanelDesPtr = (SiS_LVDSDesStruct *)Compaq1280x1024Des_1; break;
	case 85: PanelDesPtr = (SiS_LVDSDesStruct *)Compaq1280x1024Des_2; break;
	case 86: PanelDesPtr = (SiS_LVDSDesStruct *)Asus1024x768Des_1;    break;  /*  custom  */
	case 87: PanelDesPtr = (SiS_LVDSDesStruct *)Asus1024x768Des_2;    break;
	default: PanelDesPtr = SiS_Pr->LVDS1024x768Des_1;   break;
     }
#endif

  } else {

     SiS_GetLVDSDesPtr(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex,
                       &PanelIndex, &ResIndex, HwInfo);

     switch (PanelIndex)
     {
     	case  0: PanelDesPtr = SiS_Pr->SiS_PanelType00_1;   break;   /* ---  */
     	case  1: PanelDesPtr = SiS_Pr->SiS_PanelType01_1;   break;
     	case  2: PanelDesPtr = SiS_Pr->SiS_PanelType02_1;   break;
     	case  3: PanelDesPtr = SiS_Pr->SiS_PanelType03_1;   break;
     	case  4: PanelDesPtr = SiS_Pr->SiS_PanelType04_1;   break;
     	case  5: PanelDesPtr = SiS_Pr->SiS_PanelType05_1;   break;
     	case  6: PanelDesPtr = SiS_Pr->SiS_PanelType06_1;   break;
     	case  7: PanelDesPtr = SiS_Pr->SiS_PanelType07_1;   break;
     	case  8: PanelDesPtr = SiS_Pr->SiS_PanelType08_1;   break;
     	case  9: PanelDesPtr = SiS_Pr->SiS_PanelType09_1;   break;
     	case 10: PanelDesPtr = SiS_Pr->SiS_PanelType0a_1;   break;
     	case 11: PanelDesPtr = SiS_Pr->SiS_PanelType0b_1;   break;
     	case 12: PanelDesPtr = SiS_Pr->SiS_PanelType0c_1;   break;
     	case 13: PanelDesPtr = SiS_Pr->SiS_PanelType0d_1;   break;
     	case 14: PanelDesPtr = SiS_Pr->SiS_PanelType0e_1;   break;
     	case 15: PanelDesPtr = SiS_Pr->SiS_PanelType0f_1;   break;
     	case 16: PanelDesPtr = SiS_Pr->SiS_PanelType00_2;   break;    /* --- */
     	case 17: PanelDesPtr = SiS_Pr->SiS_PanelType01_2;   break;
     	case 18: PanelDesPtr = SiS_Pr->SiS_PanelType02_2;   break;
     	case 19: PanelDesPtr = SiS_Pr->SiS_PanelType03_2;   break;
     	case 20: PanelDesPtr = SiS_Pr->SiS_PanelType04_2;   break;
     	case 21: PanelDesPtr = SiS_Pr->SiS_PanelType05_2;   break;
     	case 22: PanelDesPtr = SiS_Pr->SiS_PanelType06_2;   break;
     	case 23: PanelDesPtr = SiS_Pr->SiS_PanelType07_2;   break;
     	case 24: PanelDesPtr = SiS_Pr->SiS_PanelType08_2;   break;
     	case 25: PanelDesPtr = SiS_Pr->SiS_PanelType09_2;   break;
     	case 26: PanelDesPtr = SiS_Pr->SiS_PanelType0a_2;   break;
     	case 27: PanelDesPtr = SiS_Pr->SiS_PanelType0b_2;   break;
     	case 28: PanelDesPtr = SiS_Pr->SiS_PanelType0c_2;   break;
     	case 29: PanelDesPtr = SiS_Pr->SiS_PanelType0d_2;   break;
     	case 30: PanelDesPtr = SiS_Pr->SiS_PanelType0e_2;   break;
     	case 31: PanelDesPtr = SiS_Pr->SiS_PanelType0f_2;   break;
	case 32: PanelDesPtr = SiS_Pr->SiS_PanelTypeNS_1;   break;    /* pass 1:1 */
	case 33: PanelDesPtr = SiS_Pr->SiS_PanelTypeNS_2;   break;
     	case 50: PanelDesPtr = SiS_Pr->SiS_CHTVUNTSCDesData;   break; /* TV */
     	case 51: PanelDesPtr = SiS_Pr->SiS_CHTVONTSCDesData;   break;
     	case 52: PanelDesPtr = SiS_Pr->SiS_CHTVUPALDesData;    break;
     	case 53: PanelDesPtr = SiS_Pr->SiS_CHTVOPALDesData;    break;
	default:
		 if(HwInfo->jChipType < SIS_315H)
		    PanelDesPtr = SiS_Pr->SiS_PanelType0e_1;
		 else
		    PanelDesPtr = SiS_Pr->SiS_PanelType01_1;
		 break;
     }
  }
  SiS_Pr->SiS_LCDHDES = (PanelDesPtr+ResIndex)->LCDHDES;
  SiS_Pr->SiS_LCDVDES = (PanelDesPtr+ResIndex)->LCDVDES;

  if(SiS_Pr->SiS_LCDInfo & DontExpandLCD){
     if((SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
        if(ModeNo <= 0x13) {
           modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
	   if(!(modeflag & HalfDCLK)) {
	      SiS_Pr->SiS_LCDHDES = 632;
	   }
        }
     } else {
        if(!(SiS_Pr->SiS_SetFlag & SetDOSMode)) {
           if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1280x1024) {
              if(SiS_Pr->SiS_LCDResInfo >= SiS_Pr->SiS_Panel1024x768) {
                 if(ModeNo <= 0x13) {
	            modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
	            if(HwInfo->jChipType < SIS_315H) {
	               if(!(modeflag & HalfDCLK)) SiS_Pr->SiS_LCDHDES = 320;
	            } else {
	               if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)
	                  SiS_Pr->SiS_LCDHDES = 480;
                       if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050)
	                  SiS_Pr->SiS_LCDHDES = 804;
		       if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200)
	                  SiS_Pr->SiS_LCDHDES = 704;
                       if(!(modeflag & HalfDCLK)) {
                          SiS_Pr->SiS_LCDHDES = 320;
	                  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050)
	                     SiS_Pr->SiS_LCDHDES = 632;
		          else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200)
	                     SiS_Pr->SiS_LCDHDES = 542;
                       }
                    }
                 }
              }
           }
        }
     }
  }
}

/*********************************************/
/*          SET CRT2 AUTO-THRESHOLD          */
/*********************************************/

#ifdef SIS315H
static void
SiS_CRT2AutoThreshold(SiS_Private *SiS_Pr)
{
  SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x01,0x40);
}
#endif

/*********************************************/
/*           DISABLE VIDEO BRIDGE            */
/*********************************************/

/* NEVER use any variables (VBInfo), this will be called
 * from outside the context of modeswitch!
 * MUST call getVBType before calling this
 */
void
SiS_DisableBridge(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
#ifdef SIS315H
  USHORT tempah,pushax=0,modenum;
#endif
  USHORT temp=0;

  if(SiS_Pr->SiS_VBType & VB_SISVB) {

      if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {   /* ===== For 30xB/LV ===== */

        if(HwInfo->jChipType < SIS_315H) {

#ifdef SIS300	   /* 300 series */

           if(HwInfo->jChipType == SIS_300) {  /* For 300+301LV (A907) */

	      if(!(SiS_CR36BIOSWord23b(SiS_Pr,HwInfo))) {
	         if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	            SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x26,0xFE);
		    SiS_PanelDelay(SiS_Pr, HwInfo, 3);
		 }
	      }
	      SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1f,0x3f);
	      SiS_ShortDelay(SiS_Pr,1);
	      SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x00,0xDF);
	      SiS_DisplayOff(SiS_Pr);
	      SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);
	      SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	      if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	         if( (!(SiS_CRT2IsLCD(SiS_Pr, HwInfo))) ||
	             (!(SiS_CR36BIOSWord23d(SiS_Pr, HwInfo))) ) {
	            SiS_PanelDelay(SiS_Pr, HwInfo, 2);
                    SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x26,0xFD);
		 }
	      }

	   } else {

	      if(!(SiS_CR36BIOSWord23b(SiS_Pr,HwInfo))) {
	         SiS_SetRegSR11ANDOR(SiS_Pr,HwInfo,0xF7,0x08);
	         SiS_PanelDelay(SiS_Pr, HwInfo, 3);
	      }
	      if(SiS_Is301B(SiS_Pr)) {
	         SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1f,0x3f);
	         SiS_ShortDelay(SiS_Pr,1);
	      }
	      SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x00,0xDF);
	      SiS_DisplayOff(SiS_Pr);
	      SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);
	      SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	      SiS_UnLockCRT2(SiS_Pr,HwInfo);
	      SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x01,0x80);
	      SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x02,0x40);
	      if( (!(SiS_CRT2IsLCD(SiS_Pr, HwInfo))) ||
	          (!(SiS_CR36BIOSWord23d(SiS_Pr, HwInfo))) ) {
	         SiS_PanelDelay(SiS_Pr, HwInfo, 2);
                 SiS_SetRegSR11ANDOR(SiS_Pr,HwInfo,0xFB,0x04);
	      }
	   }

#endif  /* SIS300 */

        } else {

#ifdef SIS315H	   /* 315 series */

           if(IS_SIS550650740660) {		/* 550, 650, 740, 660 */

	      modenum = SiS_GetReg(SiS_Pr->SiS_P3d4,0x34) & 0x7f;

              if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {			/* LV */
#ifdef SET_EMI
	         if(SiS_Pr->SiS_VBType & (VB_SIS302LV | VB_SIS302ELV)) {
		    if(SiS_Pr->SiS_CustomT != CUT_CLEVO1400) {
	               SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x30,0x0c);
		    }
		 }
#endif
		 if( (modenum <= 0x13) ||
		     (!(SiS_IsDualEdge(SiS_Pr,HwInfo))) ||
		     (SiS_IsVAMode(SiS_Pr,HwInfo)) ) {
	     	    SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x26,0xFE);
		    if((SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) ||
		       (SiS_Pr->SiS_CustomT == CUT_CLEVO1400)) {
		       SiS_PanelDelay(SiS_Pr, HwInfo, 3);
		    }
	         }

		 if((SiS_Pr->SiS_CustomT != CUT_COMPAQ1280) &&
		    (SiS_Pr->SiS_CustomT != CUT_CLEVO1400)) {
		    SiS_DDC2Delay(SiS_Pr,0xff00);
		    SiS_DDC2Delay(SiS_Pr,0xe000);

	            SiS_SetRegByte(SiS_Pr->SiS_P3c6,0x00);

                    pushax = SiS_GetReg(SiS_Pr->SiS_P3c4,0x06);

		    if(IS_SIS740) {
		       SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x06,0xE3);
		    }

	            SiS_PanelDelay(SiS_Pr, HwInfo, 3);

		    if(!(SiS_IsNotM650orLater(SiS_Pr, HwInfo))) {
	               tempah = 0xef;
	               if(SiS_IsVAMode(SiS_Pr,HwInfo)) {
	                  tempah = 0xf7;
                       }
	               SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x4c,tempah);
	            }
		 }

              } else if(SiS_Pr->SiS_VBType & VB_NoLCD) {			/* B-DH */

	         if(!(SiS_IsNotM650orLater(SiS_Pr,HwInfo))) {
	            SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x4c,0xef);
	         }

	      }

	      if((SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) ||
	         (SiS_Pr->SiS_CustomT == CUT_CLEVO1400)) {
	         SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1F,0xef);
	      }

              if((SiS_Pr->SiS_VBType & VB_SIS301B302B) ||
	         (SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) ||
		 (SiS_Pr->SiS_CustomT == CUT_CLEVO1400)) {
	         tempah = 0x3f;
	         if(SiS_IsDualEdge(SiS_Pr,HwInfo)) {
	            tempah = 0x7f;
	            if(!(SiS_IsVAMode(SiS_Pr,HwInfo))) {
		       tempah = 0xbf;
                    }
	         }
	         SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1F,tempah);
	      }

              if((SiS_IsVAMode(SiS_Pr,HwInfo)) ||
	         ((SiS_Pr->SiS_VBType & VB_SIS301LV302LV) && (modenum <= 0x13))) {

	         if((SiS_Pr->SiS_VBType & VB_SIS301B302B) ||
		    (SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) ||
		    (SiS_Pr->SiS_CustomT == CUT_CLEVO1400)) {
		    SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x1E,0xDF);
		    SiS_DisplayOff(SiS_Pr);
		    SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);
		 } else {
	            SiS_DisplayOff(SiS_Pr);
	            SiS_PanelDelay(SiS_Pr, HwInfo, 2);
	            SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);
	            SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x1E,0xDF);
		    if((SiS_Pr->SiS_VBType & VB_SIS301LV302LV) && (modenum <= 0x13)) {
		       SiS_DisplayOff(SiS_Pr);
	               SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x80);
	               SiS_PanelDelay(SiS_Pr, HwInfo, 2);
	               SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);
	               temp = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00);
                       SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x10);
	               SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	               SiS_SetReg(SiS_Pr->SiS_Part1Port,0x00,temp);
		    }
		 }

	      } else {

	         if((SiS_Pr->SiS_VBType & VB_SIS301B302B) ||
		    (SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) ||
		    (SiS_Pr->SiS_CustomT == CUT_CLEVO1400)) {
		    if(!(SiS_IsDualEdge(SiS_Pr,HwInfo))) {
		       SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x00,0xdf);
		       SiS_DisplayOff(SiS_Pr);
		    }
		    SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x80);
		 } else {
                    SiS_DisplayOff(SiS_Pr);
	            SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x80);
	            SiS_PanelDelay(SiS_Pr, HwInfo, 2);
		 }

		 SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);
	         temp = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00);
                 SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x10);
	         SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	         SiS_SetReg(SiS_Pr->SiS_Part1Port,0x00,temp);

	      }

	      if((SiS_Pr->SiS_VBType & VB_SIS301LV302LV) &&
	         (SiS_Pr->SiS_CustomT != CUT_COMPAQ1280) &&
		 (SiS_Pr->SiS_CustomT != CUT_CLEVO1400)) {

		 SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1f,~0x10);

	         tempah = 0x3f;
	         if(SiS_IsDualEdge(SiS_Pr,HwInfo)) {
	            tempah = 0x7f;
	            if(!(SiS_IsVAMode(SiS_Pr,HwInfo))) {
		       tempah = 0xbf;
                    }
	         }
	         SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1F,tempah);

		 if(SiS_IsNotM650orLater(SiS_Pr,HwInfo)) {
	            SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2e,0x7f);
		 }

	         if(!(SiS_IsVAMode(SiS_Pr,HwInfo))) {
	            SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x00,0xdf);
	            if(!(SiS_CRT2IsLCD(SiS_Pr,HwInfo))) {
	               if(!(SiS_IsDualEdge(SiS_Pr,HwInfo))) {
		          SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x26,0xFD);
                       }
                    }
	         }

	         SiS_SetReg(SiS_Pr->SiS_P3c4,0x06,pushax);

		 if(SiS_Pr->SiS_VBType & (VB_SIS302LV | VB_SIS302ELV)) {
	            if( (SiS_IsVAMode(SiS_Pr, HwInfo)) ||
	                (SiS_CRT2IsLCD(SiS_Pr, HwInfo)) ) {
		       SiS_PanelDelayLoop(SiS_Pr, HwInfo, 3, 20);
		    }
	         }

  	      } else if(SiS_Pr->SiS_VBType & VB_NoLCD) {

	         /* NIL */

	      } else if((SiS_Pr->SiS_VBType & VB_SIS301B302B) ||
	                (SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) ||
			(SiS_Pr->SiS_CustomT == CUT_CLEVO1400)) {

		 if(!(SiS_IsNotM650orLater(SiS_Pr,HwInfo))) {
	            tempah = 0xef;
	            if(SiS_IsDualEdge(SiS_Pr,HwInfo)) {
		       if(modenum > 0x13) {
	                  tempah = 0xf7;
		       }
                    }
	            SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x4c,tempah);
		 }
		 if((SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) ||
		    (SiS_Pr->SiS_CustomT == CUT_CLEVO1400)) {
		    if((SiS_IsVAMode(SiS_Pr,HwInfo)) ||
		       (!(SiS_IsDualEdge(SiS_Pr,HwInfo)))) {
		       if((!(SiS_WeHaveBacklightCtrl(SiS_Pr, HwInfo))) ||
		          (!(SiS_CRT2IsLCD(SiS_Pr, HwInfo)))) {
			  SiS_PanelDelay(SiS_Pr, HwInfo, 2);
	     	          SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x26,0xFD);
			  SiS_PanelDelay(SiS_Pr, HwInfo, 4);
	               }
		    }
		 }

	      }

	  } else {			/* 315, 330 - all bridge types */

	     if(SiS_Is301B(SiS_Pr)) {
	        tempah = 0x3f;
	        if(SiS_IsDualEdge(SiS_Pr,HwInfo)) {
	           tempah = 0x7f;
	           if(!(SiS_IsVAMode(SiS_Pr,HwInfo))) {
		      tempah = 0xbf;
                   }
	        }
	        SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1F,tempah);
	        if(SiS_IsVAMode(SiS_Pr,HwInfo)) {
	           SiS_DisplayOff(SiS_Pr);
		   SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);
	        }
	     }
	     if( (!(SiS_Is301B(SiS_Pr))) ||
	         (!(SiS_IsVAMode(SiS_Pr,HwInfo))) ) {

 	 	if( (!(SiS_Is301B(SiS_Pr))) ||
		    (!(SiS_IsDualEdge(SiS_Pr,HwInfo))) ) {

	           SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x00,0xDF);
	           SiS_DisplayOff(SiS_Pr);

		}

                SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x80);

                SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);

	        temp = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00);
                SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x10);
	        SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x00,temp);

	     }

	  }    /* 315/330 */

#endif /* SIS315H */

	}

      } else {     /* ============ For 301 ================ */

        if(HwInfo->jChipType < SIS_315H) {
           if(!(SiS_CR36BIOSWord23b(SiS_Pr,HwInfo))) {
	      SiS_SetRegSR11ANDOR(SiS_Pr,HwInfo,0xF7,0x08);
	      SiS_PanelDelay(SiS_Pr, HwInfo, 3);
	   }
	}

        SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x00,0xDF);           /* disable VB */
        SiS_DisplayOff(SiS_Pr);

        if(HwInfo->jChipType >= SIS_315H) {
           SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x80);
	}

        SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);                /* disable lock mode */

	if(HwInfo->jChipType >= SIS_315H) {
            temp = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00);
            SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x10);
	    SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);
	    SiS_SetReg(SiS_Pr->SiS_Part1Port,0x00,temp);
	} else {
            SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);            /* disable CRT2 */
	    if( (!(SiS_CRT2IsLCD(SiS_Pr, HwInfo))) ||
	        (!(SiS_CR36BIOSWord23d(SiS_Pr,HwInfo))) ) {
		SiS_PanelDelay(SiS_Pr, HwInfo, 2);
		SiS_SetRegSR11ANDOR(SiS_Pr,HwInfo,0xFB,0x04);
	    }
	}

      }

  } else {     /* ============ For LVDS =============*/

    if(HwInfo->jChipType < SIS_315H) {

#ifdef SIS300	/* 300 series */

	if(SiS_Pr->SiS_IF_DEF_CH70xx == 1) {
	   SiS_SetCH700x(SiS_Pr,0x090E);
	}

	if(HwInfo->jChipType == SIS_730) {
	   if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x11) & 0x08)) {
	      SiS_WaitVBRetrace(SiS_Pr,HwInfo);
	   }
	   if(!(SiS_CR36BIOSWord23b(SiS_Pr,HwInfo))) {
	      SiS_SetRegSR11ANDOR(SiS_Pr,HwInfo,0xF7,0x08);
	      SiS_PanelDelay(SiS_Pr, HwInfo, 3);
	   }
	} else {
	   if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x11) & 0x08)) {
	      if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x13) & 0x40)) {
  	         if(!(SiS_CR36BIOSWord23b(SiS_Pr,HwInfo))) {
                    SiS_WaitVBRetrace(SiS_Pr,HwInfo);
		    if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x06) & 0x1c)) {
		       SiS_DisplayOff(SiS_Pr);
	            }
	            SiS_SetRegSR11ANDOR(SiS_Pr,HwInfo,0xF7,0x08);
	            SiS_PanelDelay(SiS_Pr, HwInfo, 3);
                 }
              }
	   }
	}

	SiS_DisplayOff(SiS_Pr);

	SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);

	SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	SiS_UnLockCRT2(SiS_Pr,HwInfo);
	SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x01,0x80);
	SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x02,0x40);

	if( (!(SiS_CRT2IsLCD(SiS_Pr, HwInfo))) ||
	    (!(SiS_CR36BIOSWord23d(SiS_Pr,HwInfo))) ) {
	   SiS_PanelDelay(SiS_Pr, HwInfo, 2);
	   SiS_SetRegSR11ANDOR(SiS_Pr,HwInfo,0xFB,0x04);
	}

#endif  /* SIS300 */

    } else {

#ifdef SIS315H	/* 315 series */

	if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {

	   if(HwInfo->jChipType == SIS_740) {
	      temp = SiS_GetCH701x(SiS_Pr,0x61);
	      if(temp < 1) {
	         SiS_SetCH701x(SiS_Pr,0xac76);
	         SiS_SetCH701x(SiS_Pr,0x0066);
	      }

	      if( (!(SiS_IsDualEdge(SiS_Pr,HwInfo))) ||
	          (SiS_IsTVOrYPbPrOrScart(SiS_Pr,HwInfo)) ) {
	  	 SiS_SetCH701x(SiS_Pr,0x3e49);
	      }
	   }

	   if( (!(SiS_IsDualEdge(SiS_Pr,HwInfo))) ||
	       (SiS_IsVAMode(SiS_Pr,HwInfo)) ) {
	      SiS_Chrontel701xBLOff(SiS_Pr);
	      SiS_Chrontel701xOff(SiS_Pr,HwInfo);
	   }

	   if(HwInfo->jChipType != SIS_740) {
	      if( (!(SiS_IsDualEdge(SiS_Pr,HwInfo))) ||
	          (SiS_IsTVOrYPbPrOrScart(SiS_Pr,HwInfo)) ) {
	   	 SiS_SetCH701x(SiS_Pr,0x0149);
  	      }
	   }

	}

	if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
	   SiS_SetRegSR11ANDOR(SiS_Pr,HwInfo,0xF7,0x08);
	   SiS_PanelDelay(SiS_Pr, HwInfo, 3);
	}

	if( (SiS_Pr->SiS_IF_DEF_CH70xx == 0)   ||
	    (!(SiS_IsDualEdge(SiS_Pr,HwInfo))) ||
	    (!(SiS_IsTVOrYPbPrOrScart(SiS_Pr,HwInfo))) ) {
	   SiS_DisplayOff(SiS_Pr);
	}

	if( (SiS_Pr->SiS_IF_DEF_CH70xx == 0)   ||
	    (!(SiS_IsDualEdge(SiS_Pr,HwInfo))) ||
	    (!(SiS_IsVAMode(SiS_Pr,HwInfo))) ) {
	   SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x80);
	}

	if(HwInfo->jChipType == SIS_740) {
	   SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2e,0x7f);
	}

	SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);

	if( (SiS_Pr->SiS_IF_DEF_CH70xx == 0)   ||
	    (!(SiS_IsDualEdge(SiS_Pr,HwInfo))) ||
	    (!(SiS_IsVAMode(SiS_Pr,HwInfo))) ) {
	   SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	}

	if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
	   if(SiS_CRT2IsLCD(SiS_Pr, HwInfo)) {
	      SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x1e,0xdf);
	      if(HwInfo->jChipType == SIS_550) {
	         SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x1e,0xbf);
	         SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x1e,0xef);
	      }
	   }
	} else {
	   if(HwInfo->jChipType == SIS_740) {
	      if(SiS_IsLCDOrLCDA(SiS_Pr,HwInfo)) {
	         SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x1e,0xdf);
	      }
	   } else if(SiS_IsVAMode(SiS_Pr,HwInfo)) {
	      SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x1e,0xdf);
	   }
	}

	if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
	   if(SiS_IsDualEdge(SiS_Pr,HwInfo)) {
	      /* SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x13,0xff); */
	   } else {
	      SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x13,0xfb);
	   }
	}

	SiS_UnLockCRT2(SiS_Pr,HwInfo);

	if(HwInfo->jChipType == SIS_550) {
	   SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x01,0x80); /* DirectDVD PAL?*/
	   SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x02,0x40); /* VB clock / 4 ? */
	} else if( (SiS_Pr->SiS_IF_DEF_CH70xx == 0)   ||
	           (!(SiS_IsDualEdge(SiS_Pr,HwInfo))) ||
		   (!(SiS_IsVAMode(SiS_Pr,HwInfo))) ) {
	   SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2e,0xf7);
	}

        if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
	   if(SiS_CRT2IsLCD(SiS_Pr, HwInfo)) {
	      if(!(SiS_WeHaveBacklightCtrl(SiS_Pr,HwInfo))) {
	 	 SiS_PanelDelay(SiS_Pr, HwInfo, 2);
		 SiS_SetRegSR11ANDOR(SiS_Pr,HwInfo,0xFB,0x04);
	      }
	   }
        }

#endif  /* SIS315H */

    }  /* 315 series */

  }  /* LVDS */

}

/*********************************************/
/*            ENABLE VIDEO BRIDGE            */
/*********************************************/

/* NEVER use any variables (VBInfo), this will be called
 * from outside the context of a mode switch!
 * MUST call getVBType before calling this
 */
void
SiS_EnableBridge(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  USHORT temp=0,tempah;
#ifdef SIS315H
  USHORT temp1,pushax=0;
  BOOLEAN delaylong = FALSE;
#endif


  if(SiS_Pr->SiS_VBType & VB_SISVB) {

    if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {   /* ====== For 301B et al  ====== */

      if(HwInfo->jChipType < SIS_315H) {

#ifdef SIS300     /* 300 series */

         if(HwInfo->jChipType == SIS_300) {

	    if(SiS_CRT2IsLCD(SiS_Pr, HwInfo)) {
	       if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	          SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x02);
	          if(!(SiS_CR36BIOSWord23d(SiS_Pr, HwInfo))) {
	             SiS_PanelDelay(SiS_Pr, HwInfo, 0);
	          }
	       }
	    }
	    temp = SiS_GetReg(SiS_Pr->SiS_P3c4,0x32) & 0xDF;             /* lock mode */
            if(SiS_BridgeInSlave(SiS_Pr)) {
               tempah = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
               if(!(tempah & SetCRT2ToRAMDAC))  temp |= 0x20;
            }
            SiS_SetReg(SiS_Pr->SiS_P3c4,0x32,temp);
	    SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);
	    SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x00,0x1F,0x20);        /* enable VB processor */
	    SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x1F,0xC0);
	    SiS_DisplayOn(SiS_Pr);
	    if(SiS_CRT2IsLCD(SiS_Pr, HwInfo)) {
	       if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	          if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x16) & 0x10)) {
		     if(!(SiS_CR36BIOSWord23b(SiS_Pr,HwInfo))) {
		        SiS_PanelDelay(SiS_Pr, HwInfo, 1);
                     }
		     SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x01);
	 	  }
	       }
	    }

	 } else {

	    if((SiS_Pr->SiS_VBType & VB_NoLCD) &&
	       (SiS_CRT2IsLCD(SiS_Pr, HwInfo))) {
	       /* This is only for LCD output on 301B-DH via LVDS */
	       SiS_SetRegSR11ANDOR(SiS_Pr,HwInfo,0xFB,0x00);
	       if(!(SiS_CR36BIOSWord23d(SiS_Pr,HwInfo))) {
	          SiS_PanelDelay(SiS_Pr, HwInfo, 0);
	       }
	       SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);   /* Enable CRT2 */
               SiS_DisplayOn(SiS_Pr);
	       SiS_UnLockCRT2(SiS_Pr,HwInfo);
	       SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x02,0xBF);
	       if(SiS_BridgeInSlave(SiS_Pr)) {
      		  SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x01,0x1F);
      	       } else {
      		  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x01,0x1F,0x40);
               }
	       if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x13) & 0x40)) {
	           if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x16) & 0x10)) {
		       if(!(SiS_CR36BIOSWord23b(SiS_Pr,HwInfo))) {
		           SiS_PanelDelay(SiS_Pr, HwInfo, 1);
                       }
		       SiS_WaitVBRetrace(SiS_Pr,HwInfo);
                       SiS_SetRegSR11ANDOR(SiS_Pr,HwInfo,0xF7,0x00);
                   }
	       }
            } else {
	       temp = SiS_GetReg(SiS_Pr->SiS_P3c4,0x32) & 0xDF;             /* lock mode */
               if(SiS_BridgeInSlave(SiS_Pr)) {
                  tempah = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
                  if(!(tempah & SetCRT2ToRAMDAC))  temp |= 0x20;
               }
               SiS_SetReg(SiS_Pr->SiS_P3c4,0x32,temp);
	       SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);
	       SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x00,0x1F,0x20);        /* enable VB processor */
	       SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x1F,0xC0);
	       SiS_DisplayOn(SiS_Pr);
	    }

         }
#endif /* SIS300 */

      } else {

#ifdef SIS315H    /* 315 series */

	 if(IS_SIS550650740660) {		/* 550, 650, 740, 660 */

	    UCHAR r30=0, r31=0, r32=0, r33=0, cr36=0;

	    if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {

	       if((SiS_Pr->SiS_CustomT != CUT_COMPAQ1280) &&
	          (SiS_Pr->SiS_CustomT != CUT_CLEVO1400)) {
	          SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1f,0xef);
#ifdef SET_EMI
		  if(SiS_Pr->SiS_VBType & (VB_SIS302LV | VB_SIS302ELV)) {
	             SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x30,0x0c);
		  }
#endif
	       }

               if(!(SiS_IsNotM650orLater(SiS_Pr,HwInfo))) {
	          tempah = 0x10;
		  if(SiS_LCDAEnabled(SiS_Pr, HwInfo)) {
		     if(SiS_TVEnabled(SiS_Pr, HwInfo)) tempah = 0x18;
		     else 			       tempah = 0x08;
		  }
		  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x4c,tempah);
	       }

	       if((SiS_Pr->SiS_CustomT != CUT_COMPAQ1280) &&
	          (SiS_Pr->SiS_CustomT != CUT_CLEVO1400)) {
	          SiS_SetRegByte(SiS_Pr->SiS_P3c6,0x00);
	          SiS_DisplayOff(SiS_Pr);
	          pushax = SiS_GetReg(SiS_Pr->SiS_P3c4,0x06);
	          if(IS_SIS740) {
	             SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x06,0xE3);
	          }
	       }

	       if( (SiS_IsVAMode(SiS_Pr,HwInfo)) ||
	           (SiS_CRT2IsLCD(SiS_Pr, HwInfo)) ) {
                  if(!(SiS_GetReg(SiS_Pr->SiS_Part4Port,0x26) & 0x02)) {
		     if((SiS_Pr->SiS_CustomT != CUT_COMPAQ1280) &&
		        (SiS_Pr->SiS_CustomT != CUT_CLEVO1400)) {
		        SiS_PanelDelayLoop(SiS_Pr, HwInfo, 3, 2);
			SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x02);
			if(SiS_Pr->SiS_VBType & (VB_SIS302LV | VB_SIS302ELV)) {
			   SiS_GenericDelay(SiS_Pr, 0x4500);
			}
	                SiS_PanelDelayLoop(SiS_Pr, HwInfo, 3, 2);
		     } else {
		        SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x02);
			SiS_PanelDelay(SiS_Pr, HwInfo, 0);
		     }
	          }
	       }

               if((SiS_Pr->SiS_CustomT != CUT_COMPAQ1280) &&
	          (SiS_Pr->SiS_CustomT != CUT_CLEVO1400)) {
	          if(!(SiS_GetReg(SiS_Pr->SiS_P3d4,0x31) & 0x40)) {
                     SiS_PanelDelayLoop(SiS_Pr, HwInfo, 3, 10);
		     delaylong = TRUE;
		  }
	       }

	    } else if(SiS_Pr->SiS_VBType & VB_NoLCD) {

	       if(!(SiS_IsNotM650orLater(SiS_Pr,HwInfo))) {
	          SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x4c,0x10);
	       }

  	    } else if(SiS_Pr->SiS_VBType & VB_SIS301B302B) {

	       if(!(SiS_IsNotM650orLater(SiS_Pr,HwInfo))) {
	          tempah = 0x10;
		  if(SiS_LCDAEnabled(SiS_Pr, HwInfo)) {
		     if(SiS_TVEnabled(SiS_Pr, HwInfo)) tempah = 0x18;
		     else 			       tempah = 0x08;
		  }
		  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x4c,tempah);
	       }

	    }

	    if(!(SiS_IsVAMode(SiS_Pr,HwInfo))) {
               temp = SiS_GetReg(SiS_Pr->SiS_P3c4,0x32) & 0xDF;
	       if(SiS_BridgeInSlave(SiS_Pr)) {
                  tempah = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
                  if(!(tempah & SetCRT2ToRAMDAC)) {
		     if(!(SiS_LCDAEnabled(SiS_Pr, HwInfo))) temp |= 0x20;
		  }
               }
               SiS_SetReg(SiS_Pr->SiS_P3c4,0x32,temp);

	       SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);                   /* enable CRT2 */

	       if((SiS_Pr->SiS_VBType & VB_SIS301B302B) ||
	          (SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) ||
		  (SiS_Pr->SiS_CustomT == CUT_CLEVO1400)) {
	          SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2e,0x7f);
		  temp = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x2e);
		  if(!(temp & 0x80)) {
		     SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2e,0x80);
		  }
	       } else {
	          SiS_PanelDelay(SiS_Pr, HwInfo, 2);
	       }
	    } else {
	       SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1e,0x20);
	    }

	    SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x00,0x1f,0x20);

	    if((SiS_Pr->SiS_VBType & VB_SIS301B302B) ||
	       (SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) ||
	       (SiS_Pr->SiS_CustomT == CUT_CLEVO1400)) {
	       temp = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x2e);
	       if(!(temp & 0x80)) {
		  SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2e,0x80);
	       }
	    }

	    tempah = 0xc0;
	    if(SiS_IsDualEdge(SiS_Pr, HwInfo)) {
	       tempah = 0x80;
	       if(!(SiS_IsVAMode(SiS_Pr, HwInfo))) {
	          tempah = 0x40;
               }
	    }
            SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x1F,tempah);

	    if((SiS_Pr->SiS_VBType & VB_SIS301B302B) ||
	       (((SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) ||
	         (SiS_Pr->SiS_CustomT == CUT_CLEVO1400))     &&
	        (!(SiS_WeHaveBacklightCtrl(SiS_Pr,HwInfo))))) {
               SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x00,0x7f);
	    }

	    if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {

	       if((SiS_Pr->SiS_CustomT != CUT_COMPAQ1280) &&
	          (SiS_Pr->SiS_CustomT != CUT_CLEVO1400)) {
	          SiS_PanelDelay(SiS_Pr, HwInfo, 2);
	       }
#ifdef COMPAQ_HACK
	       if(SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) {
	          SiS_PanelDelay(SiS_Pr, HwInfo, 2);
	       }
#endif

	       SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x1f,0x10);

	       if(SiS_Pr->SiS_CustomT != CUT_CLEVO1400) {
#ifdef SET_EMI
	          if(SiS_Pr->SiS_VBType & (VB_SIS302LV | VB_SIS302ELV)) {
	             SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x30,0x0c);
		  }
#endif
	          SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x27,0x0c);
#ifdef SET_EMI
	          if(SiS_Pr->SiS_VBType & (VB_SIS302LV | VB_SIS302ELV)) {

		     cr36 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x36);

		     /*                                              (P4_30|0x40)  */
		     /* Compal 1400x1050: 0x05, 0x60, 0x00                YES  (1.10.7w;  CR36=69)      */
		     /* Compal 1400x1050: 0x0d, 0x70, 0x40                YES  (1.10.7x;  CR36=69)      */
		     /* Acer   1280x1024: 0x12, 0xd0, 0x6b                NO   (1.10.9k;  CR36=73)      */
		     /* Compaq 1280x1024: 0x0d, 0x70, 0x6b                YES  (1.12.04b; CR36=03)      */
		     /* Clevo   1024x768: 0x05, 0x60, 0x33                NO   (1.10.8e;  CR36=12, DL!) */
		     /* Clevo   1024x768: 0x0d, 0x70, 0x40 (if type == 3) YES  (1.10.8y;  CR36=?2)      */
		     /* Clevo   1024x768: 0x05, 0x60, 0x33 (if type != 3) YES  (1.10.8y;  CR36=?2)      */
		     /* Asus    1024x768: ?                                ?   (1.10.8o;  CR36=?2)      */
		     /* Asus    1024x768: 0x08, 0x10, 0x3c (problematic)  YES  (1.10.8q;  CR36=22)      */

		     if(SiS_Pr->HaveEMI) {
		        r30 = SiS_Pr->EMI_30;
			r31 = SiS_Pr->EMI_31;
			r32 = SiS_Pr->EMI_32;
			r33 = SiS_Pr->EMI_33;
		     } else {
		        r30 = 0;
		     }

		     /* EMI_30 is read at driver start; however, the BIOS sets this
		      * (if it is used) only if the LCD is in use. In case we caught
		      * the machine while on TV output, this bit is not set and we
		      * don't know if it should be set - hence our detection is wrong.
		      * Work-around this here:
		      */

		     if((!SiS_Pr->HaveEMI) || (!SiS_Pr->HaveEMILCD)) {
		        if((cr36 & 0x0f) == 0x02) {			/* 1024x768 */
		           r30 |= 0x40;
			   if(SiS_Pr->SiS_CustomT == CUT_CLEVO1024) {
			      r30 &= ~0x40;
			   }
		        } else if((cr36 & 0x0f) == 0x03) {		/* 1280x1024 */
		           r30 |= 0x40;
			   if(SiS_Pr->SiS_CustomT != CUT_COMPAQ1280) {
			      r30 &= ~0x40;
			   }
		        } else if((cr36 & 0x0f) == 0x09) {		/* 1400x1050 */
		           r30 |= 0x40;
		        } else if((cr36 & 0x0f) == 0x0b) {		/* 1600x1200 - unknown */
		           r30 |= 0x40;
		        }
                     }

		     if(!SiS_Pr->HaveEMI) {
		        if((cr36 & 0x0f) == 0x02) {
			   if((cr36 & 0xf0) == 0x30) {
			      r31 = 0x0d; r32 = 0x70; r33 = 0x40;
			   } else {
			      r31 = 0x05; r32 = 0x60; r33 = 0x33;
			   }
		        } else if((cr36 & 0x0f) == 0x03) {
			   if(SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) {
			      r31 = 0x0d; r32 = 0x70; r33 = 0x6b;
			   } else {
			      r31 = 0x12; r32 = 0xd0; r33 = 0x6b;
			   }
			} else if((cr36 & 0x0f) == 0x09) {
			   if(SiS_Pr->SiS_CustomT == CUT_COMPAL1400_2) {
			      r31 = 0x0d; r32 = 0x70; r33 = 0x40;  /* BIOS values */
			   } else {
			      r31 = 0x05; r32 = 0x60; r33 = 0x00;
			   }
			} else {
			   r31 = 0x05; r32 = 0x60; r33 = 0x00;
			}
		     }

		     /* BIOS values don't work so well sometimes */
		     if(!SiS_Pr->OverruleEMI) {
#ifdef COMPAL_HACK
		        if(SiS_Pr->SiS_CustomT == CUT_COMPAL1400_2) {
		           if((cr36 & 0x0f) == 0x09) {
			      r30 = 0x60; r31 = 0x05; r32 = 0x60; r33 = 0x00;
			   }
 		        }
#endif
#ifdef COMPAQ_HACK
		        if(SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) {
		           if((cr36 & 0x0f) == 0x03) {
			      r30 = 0x20; r31 = 0x12; r32 = 0xd0; r33 = 0x6b;     /* rev 1 */
			   }
			}
#endif
#ifdef ASUS_HACK
		        if(SiS_Pr->SiS_CustomT == CUT_ASUSA2H_2) {
		           if((cr36 & 0x0f) == 0x02) {
			      /* r30 = 0x60; r31 = 0x05; r32 = 0x60; r33 = 0x33;  */   /* rev 2 */
			      /* r30 = 0x20; r31 = 0x05; r32 = 0x60; r33 = 0x33;  */   /* rev 3 */
			      /* r30 = 0x60; r31 = 0x0d; r32 = 0x70; r33 = 0x40;  */   /* rev 4 */
			      /* r30 = 0x20; r31 = 0x0d; r32 = 0x70; r33 = 0x40;  */   /* rev 5 */
			   }
			}
#endif
 		     }
		     if(!(SiS_Pr->OverruleEMI && (!r30) && (!r31) && (!r32) && (!r33))) {
		        SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x30,0x20);
		     }
		     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x31,r31);
		     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x32,r32);
		     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x33,r33);
		     if(!(SiS_Pr->OverruleEMI && (!r30) && (!r31) && (!r32) && (!r33))) {
		        SiS_SetReg(SiS_Pr->SiS_Part4Port,0x34,0x10);
		     } else {
		        SiS_SetReg(SiS_Pr->SiS_Part4Port,0x34,0x00);
		     }
		     if( (SiS_IsVAMode(SiS_Pr,HwInfo)) ||
	                 (SiS_CRT2IsLCD(SiS_Pr, HwInfo)) ) {
	                if(r30 & 0x40) {
		           SiS_PanelDelayLoop(SiS_Pr, HwInfo, 3, 5);
			   if(delaylong) {
			      SiS_PanelDelayLoop(SiS_Pr, HwInfo, 3, 5);
			      delaylong = FALSE;
			   }
			   SiS_WaitVBRetrace(SiS_Pr,HwInfo);
			   if(SiS_Pr->SiS_CustomT == CUT_ASUSA2H_2) {
			      SiS_GenericDelay(SiS_Pr, 0x500);
			   }
	                   SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x30,0x40);
	                }
		     }
		  }
#endif
	       }

	       if(SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) {

	          if( (SiS_IsVAMode(SiS_Pr,HwInfo)) ||
	              (SiS_CRT2IsLCD(SiS_Pr, HwInfo)) ) {
		     SiS_DisplayOn(SiS_Pr);
		     SiS_PanelDelay(SiS_Pr, HwInfo, 1);
		     SiS_WaitVBRetrace(SiS_Pr, HwInfo);
		     SiS_PanelDelay(SiS_Pr, HwInfo, 3);
		     if(!(SiS_WeHaveBacklightCtrl(SiS_Pr,HwInfo))) {
		        SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x01);
	  	     }
		  }

	       } else if(SiS_Pr->SiS_CustomT == CUT_CLEVO1400) {

	          if(!(SiS_WeHaveBacklightCtrl(SiS_Pr,HwInfo))) {
	             if( (SiS_IsVAMode(SiS_Pr, HwInfo)) ||
	                 (SiS_CRT2IsLCD(SiS_Pr, HwInfo)) ) {
		        SiS_DisplayOn(SiS_Pr);
		        SiS_PanelDelay(SiS_Pr, HwInfo, 1);
		        SiS_WaitVBRetrace(SiS_Pr,HwInfo);
		        SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x01);
		     }
		  }

	       } else {

	          SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2e,0x80);
	          if(!(SiS_WeHaveBacklightCtrl(SiS_Pr,HwInfo))) {
	             if( (SiS_IsVAMode(SiS_Pr,HwInfo)) ||
	                 ((SiS_CRT2IsLCD(SiS_Pr, HwInfo))) ) {
		        SiS_PanelDelayLoop(SiS_Pr, HwInfo, 3, 10);
		        if(delaylong) {
			   SiS_PanelDelayLoop(SiS_Pr, HwInfo, 3, 10);
		        }
                        SiS_WaitVBRetrace(SiS_Pr,HwInfo);
			if(SiS_Pr->SiS_VBType & (VB_SIS302LV | VB_SIS302ELV)) {
			   SiS_GenericDelay(SiS_Pr, 0x500);
			}
		        SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x01);
	             }
	          }

	          SiS_SetReg(SiS_Pr->SiS_P3c4,0x06,pushax);
	          SiS_DisplayOn(SiS_Pr);
	          SiS_SetRegByte(SiS_Pr->SiS_P3c6,0xff);

	          if(!(SiS_WeHaveBacklightCtrl(SiS_Pr,HwInfo))) {
	             SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x00,0x7f);
	          }

	       }

	    }

	 } else {			/* 315, 330 */

	    if(!(SiS_IsVAMode(SiS_Pr,HwInfo))) {
	       temp = SiS_GetReg(SiS_Pr->SiS_P3c4,0x32) & 0xDF;
	       if(SiS_BridgeInSlave(SiS_Pr)) {
                  tempah = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
                  if(!(tempah & SetCRT2ToRAMDAC))  temp |= 0x20;
               }
               SiS_SetReg(SiS_Pr->SiS_P3c4,0x32,temp);

	       SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);                   /* enable CRT2 */

	       temp = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x2E);
               if(!(temp & 0x80))
                  SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2E,0x80);
            }

	    SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x00,0x1f,0x20);

	    if(SiS_Is301B(SiS_Pr)) {

	       temp=SiS_GetReg(SiS_Pr->SiS_Part1Port,0x2E);
               if(!(temp & 0x80))
                  SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2E,0x80);

	       tempah = 0xc0;
	       if(SiS_IsDualEdge(SiS_Pr,HwInfo)) {
	          tempah = 0x80;
	          if(!(SiS_IsVAMode(SiS_Pr,HwInfo))) {
	             tempah = 0x40;
                  }
	       }
               SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x1F,tempah);

	       SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x00,0x7f);

	    } else {

	       SiS_VBLongWait(SiS_Pr);
               SiS_DisplayOn(SiS_Pr);
	       SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x00,0x7F);
               SiS_VBLongWait(SiS_Pr);

	    }

	 }   /* 315, 330 */

#endif /* SIS315H */

      }

    } else {	/* ============  For 301 ================ */

       if(HwInfo->jChipType < SIS_315H) {
          if(SiS_CRT2IsLCD(SiS_Pr, HwInfo)) {
             SiS_SetRegSR11ANDOR(SiS_Pr,HwInfo,0xFB,0x00);
	     SiS_PanelDelay(SiS_Pr, HwInfo, 0);
	  }
       }

       temp = SiS_GetReg(SiS_Pr->SiS_P3c4,0x32) & 0xDF;          /* lock mode */
       if(SiS_BridgeInSlave(SiS_Pr)) {
          tempah = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
          if(!(tempah & SetCRT2ToRAMDAC))  temp |= 0x20;
       }
       SiS_SetReg(SiS_Pr->SiS_P3c4,0x32,temp);

       SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);                  /* enable CRT2 */

       if(HwInfo->jChipType >= SIS_315H) {
          temp = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x2E);
          if(!(temp & 0x80)) {
             SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2E,0x80);         /* BVBDOENABLE=1 */
	  }
       }

       SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x00,0x1F,0x20);     /* enable VB processor */

       SiS_VBLongWait(SiS_Pr);
       SiS_DisplayOn(SiS_Pr);
       if(HwInfo->jChipType >= SIS_315H) {
          SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x00,0x7f);
       }
       SiS_VBLongWait(SiS_Pr);

       if(HwInfo->jChipType < SIS_315H) {
          if(SiS_CRT2IsLCD(SiS_Pr, HwInfo)) {
	     SiS_PanelDelay(SiS_Pr, HwInfo, 1);
             SiS_SetRegSR11ANDOR(SiS_Pr,HwInfo,0xF7,0x00);
	  }
       }

    }

  } else {   /* =================== For LVDS ================== */

    if(HwInfo->jChipType < SIS_315H) {

#ifdef SIS300    /* 300 series */

       if(SiS_CRT2IsLCD(SiS_Pr, HwInfo)) {
          if(HwInfo->jChipType == SIS_730) {
	     SiS_PanelDelay(SiS_Pr, HwInfo, 1);
	     SiS_PanelDelay(SiS_Pr, HwInfo, 1);
	     SiS_PanelDelay(SiS_Pr, HwInfo, 1);
	  }
          SiS_SetRegSR11ANDOR(SiS_Pr,HwInfo,0xFB,0x00);
	  if(!(SiS_CR36BIOSWord23d(SiS_Pr,HwInfo))) {
	     SiS_PanelDelay(SiS_Pr, HwInfo, 0);
	  }
       }

       SiS_EnableCRT2(SiS_Pr);
       SiS_DisplayOn(SiS_Pr);
       SiS_UnLockCRT2(SiS_Pr,HwInfo);
       SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x02,0xBF);
       if(SiS_BridgeInSlave(SiS_Pr)) {
      	  SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x01,0x1F);
       } else {
      	  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x01,0x1F,0x40);
       }

       if(SiS_Pr->SiS_IF_DEF_CH70xx == 1) {
          if(!(SiS_CRT2IsLCD(SiS_Pr, HwInfo))) {
	     SiS_WaitVBRetrace(SiS_Pr, HwInfo);
	     SiS_SetCH700x(SiS_Pr,0x0B0E);
          }
       }

       if(SiS_CRT2IsLCD(SiS_Pr, HwInfo)) {
          if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x13) & 0x40)) {
             if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x16) & 0x10)) {
	        if(!(SiS_CR36BIOSWord23b(SiS_Pr,HwInfo))) {
	 	   SiS_PanelDelay(SiS_Pr, HwInfo, 1);
        	   SiS_PanelDelay(SiS_Pr, HwInfo, 1);
	        }
	        SiS_WaitVBRetrace(SiS_Pr, HwInfo);
                SiS_SetRegSR11ANDOR(SiS_Pr,HwInfo,0xF7,0x00);
             }
	  }
       }

#endif  /* SIS300 */

    } else {

#ifdef SIS315H    /* 315 series */

       if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
	  if(SiS_CRT2IsLCD(SiS_Pr, HwInfo)) {
	     SiS_SetRegSR11ANDOR(SiS_Pr,HwInfo,0xFB,0x00);
	     SiS_PanelDelay(SiS_Pr, HwInfo, 0);
          }
       }

       SiS_EnableCRT2(SiS_Pr);
       SiS_UnLockCRT2(SiS_Pr,HwInfo);

       SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2e,0xf7);

       if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
          temp = SiS_GetCH701x(SiS_Pr,0x66);
	  temp &= 0x20;
	  SiS_Chrontel701xBLOff(SiS_Pr);
       }

       if(HwInfo->jChipType != SIS_550) {
          SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2e,0x7f);
       }

       if(HwInfo->jChipType == SIS_740) {
          if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
             if(SiS_IsLCDOrLCDA(SiS_Pr, HwInfo)) {
	   	SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1E,0x20);
	     }
	  }
       }

       temp1 = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x2E);
       if(!(temp1 & 0x80)) {
          SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2E,0x80);
       }

       if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
          if(temp) {
	     SiS_Chrontel701xBLOn(SiS_Pr, HwInfo);
	  }
       }

       if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
          if(SiS_CRT2IsLCD(SiS_Pr, HwInfo)) {
	     SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1E,0x20);
	     if(HwInfo->jChipType == SIS_550) {
		SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1E,0x40);
		SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1E,0x10);
	     }
	  }
       } else if(SiS_IsVAMode(SiS_Pr,HwInfo)) {
          if(HwInfo->jChipType != SIS_740) {
             SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1E,0x20);
	  }
       }

       if(!(SiS_WeHaveBacklightCtrl(SiS_Pr,HwInfo))) {
          SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x00,0x7f);
       }

       if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
       	  if(SiS_IsTVOrYPbPrOrScart(SiS_Pr,HwInfo)) {
             SiS_Chrontel701xOn(SiS_Pr,HwInfo);
          }
          if( (SiS_IsVAMode(SiS_Pr,HwInfo)) ||
	      (SiS_IsLCDOrLCDA(SiS_Pr,HwInfo)) ) {
             SiS_ChrontelDoSomething1(SiS_Pr,HwInfo);
          }
       }

       if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
       	  if(!(SiS_WeHaveBacklightCtrl(SiS_Pr,HwInfo))) {
 	     if( (SiS_IsVAMode(SiS_Pr,HwInfo)) ||
	         (SiS_IsLCDOrLCDA(SiS_Pr,HwInfo)) ) {
	     	SiS_Chrontel701xBLOn(SiS_Pr, HwInfo);
	     	SiS_ChrontelInitTVVSync(SiS_Pr,HwInfo);
             }
       	  }
       } else if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
       	  if(!(SiS_WeHaveBacklightCtrl(SiS_Pr,HwInfo))) {
	     if(SiS_CRT2IsLCD(SiS_Pr, HwInfo)) {
		SiS_PanelDelay(SiS_Pr, HwInfo, 1);
		SiS_SetRegSR11ANDOR(SiS_Pr,HwInfo,0xF7,0x00);
	     }
	  }
       }

#endif  /* SIS315H */

    } /* 310 series */

  }  /* LVDS */

}

/*********************************************/
/*         SET PART 1 REGISTER GROUP         */
/*********************************************/

/********** Set CRT2 OFFSET / PITCH **********/
static void
SiS_SetCRT2Offset(SiS_Private *SiS_Pr,USHORT ModeNo,
                  USHORT ModeIdIndex ,USHORT RefreshRateTableIndex,
	          PSIS_HW_INFO HwInfo)
{
  USHORT offset;
  UCHAR temp;

  if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) return;

  offset = SiS_GetOffset(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex,
                         HwInfo);

  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_2 ||
     SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_3) offset >>= 1;

  temp = (UCHAR)(offset & 0xFF);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x07,temp);
  temp = (UCHAR)(offset >> 8);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x09,temp);
  temp = (UCHAR)(((offset >> 3) & 0xFF) + 1);
  if(offset % 8) temp++;
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x03,temp);
}

/************* Set CRT2 Sync *************/
static void
SiS_SetCRT2Sync(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT RefreshRateTableIndex,
                PSIS_HW_INFO HwInfo)
{
  USHORT tempah=0,tempbl,infoflag;

  tempbl = 0xC0;

  if(SiS_Pr->UseCustomMode) {
     infoflag = SiS_Pr->CInfoFlag;
  } else {
     infoflag = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_InfoFlag;
  }

  if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {					/* LVDS */

     if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
        tempah = 0;
     } else if((SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) && (SiS_Pr->SiS_LCDInfo & LCDSync)) {
        tempah = SiS_Pr->SiS_LCDInfo;
     } else tempah = infoflag >> 8;

     tempah &= 0xC0;

     tempah |= 0x20;
     if(!(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit)) tempah |= 0x10;

     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
        if((SiS_Pr->SiS_CustomT == CUT_BARCO1366) ||
           (SiS_Pr->SiS_CustomT == CUT_BARCO1024)) {
	   tempah |= 0xc0;
        }
     }

     if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
        if(HwInfo->jChipType >= SIS_315H) {
           tempah >>= 3;
           SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,0xE7,tempah);
        } else {
           SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0x0F,0xe0);
        }
     } else {
        SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0x0F,tempah);
     }

  } else if(SiS_Pr->SiS_VBType & VB_SISVB) {

     if(HwInfo->jChipType < SIS_315H) {

#ifdef SIS300  /* ---- 300 series --- */

        if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {			/* 630 - 301B(-DH) */

	   tempah = infoflag >> 8;
           if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
	      if(SiS_Pr->SiS_LCDInfo & LCDSync) {
	         tempah = SiS_Pr->SiS_LCDInfo;
              }
           }
           tempah &= 0xC0;

           tempah |= 0x20;
           if(!(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit)) tempah |= 0x10;

 	   tempah &= 0x3f;
  	   tempah |= tempbl;
           SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0x0F,tempah);

        } else {							/* 630 - 301 */

           tempah = infoflag >> 8;
           tempah &= 0xC0;
           tempah |= 0x20;
	   if(!(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit)) tempah |= 0x10;
           SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0x0F,tempah);

        }

#endif /* SIS300 */

     } else {

#ifdef SIS315H  /* ------- 315 series ------ */

        if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {	  		/* 315 - 30xLV */

	   if(((SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) &&
	       (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024)) ||
	      ((SiS_Pr->SiS_CustomT == CUT_CLEVO1400)  &&
	       (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050))) {
	      tempah = infoflag >> 8;
	   } else {
              tempah = SiS_GetReg(SiS_Pr->SiS_P3d4,0x37);
	   }
	   tempah &= 0xC0;

           tempah |= 0x20;
           if(!(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit)) tempah |= 0x10;
           SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0x0F,tempah);

        } else {							/* 315 - 301, 301B */

           tempah = infoflag >> 8;
	   if(!SiS_Pr->UseCustomMode) {
	      if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
	         if(SiS_Pr->SiS_LCDInfo & LCDSync) {
	            tempah = SiS_Pr->SiS_LCDInfo;
	         }
	      }
	   }
	   tempah &= 0xC0;

           tempah |= 0x20;
           if(!(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit)) tempah |= 0x10;

	   if(SiS_Pr->SiS_VBType & VB_NoLCD) {			/* TEST, imitate BIOS bug */
	      if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
	         tempah |= 0xc0;
	      }
	   }
           SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0x0F,tempah);

        }
#endif  /* SIS315H */
      }
   }
}

/******** Set CRT2 FIFO on 300/630/730 *******/
#ifdef SIS300
static void
SiS_SetCRT2FIFO_300(SiS_Private *SiS_Pr,USHORT ModeNo,
                    PSIS_HW_INFO HwInfo)
{
  UCHAR  *ROMAddr  = HwInfo->pjVirtualRomBase;
  USHORT temp,index;
  USHORT modeidindex,refreshratetableindex;
  USHORT VCLK=0,MCLK,colorth=0,data2=0;
  USHORT tempal, tempah, tempbx, tempcl, tempax;
  USHORT CRT1ModeNo,CRT2ModeNo;
  USHORT SelectRate_backup;
  ULONG  data,eax;
  const UCHAR  LatencyFactor[] = {
  	97, 88, 86, 79, 77, 00,       /*; 64  bit    BQ=2   */
        00, 87, 85, 78, 76, 54,       /*; 64  bit    BQ=1   */
        97, 88, 86, 79, 77, 00,       /*; 128 bit    BQ=2   */
        00, 79, 77, 70, 68, 48,       /*; 128 bit    BQ=1   */
        80, 72, 69, 63, 61, 00,       /*; 64  bit    BQ=2   */
        00, 70, 68, 61, 59, 37,       /*; 64  bit    BQ=1   */
        86, 77, 75, 68, 66, 00,       /*; 128 bit    BQ=2   */
        00, 68, 66, 59, 57, 37        /*; 128 bit    BQ=1   */
  };
  const UCHAR  LatencyFactor730[] = {
         69, 63, 61,
	 86, 79, 77,
	103, 96, 94,
	120,113,111,
	137,130,128,    /* <-- last entry, data below */
	137,130,128,	/* to avoid using illegal values */
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
  };
  const UCHAR ThLowB[]   = {
  	81, 4, 72, 6, 88, 8,120,12,
        55, 4, 54, 6, 66, 8, 90,12,
        42, 4, 45, 6, 55, 8, 75,12
  };
  const UCHAR ThTiming[] = {
  	1, 2, 2, 3, 0, 1, 1, 2
  };

  SelectRate_backup = SiS_Pr->SiS_SelectCRT2Rate;

  if(!SiS_Pr->CRT1UsesCustomMode) {

     CRT1ModeNo = SiS_Pr->SiS_CRT1Mode;                                 	/* get CRT1 ModeNo */
     SiS_SearchModeID(SiS_Pr, &CRT1ModeNo, &modeidindex);
     SiS_Pr->SiS_SetFlag &= (~ProgrammingCRT2);
     SiS_Pr->SiS_SelectCRT2Rate = 0;
     refreshratetableindex = SiS_GetRatePtr(SiS_Pr, CRT1ModeNo, modeidindex, HwInfo);

     if(CRT1ModeNo >= 0x13) {
        index = SiS_Pr->SiS_RefIndex[refreshratetableindex].Ext_CRTVCLK;
        index &= 0x3F;
        VCLK = SiS_Pr->SiS_VCLKData[index].CLOCK;				/* Get VCLK */

	colorth = SiS_GetColorDepth(SiS_Pr,CRT1ModeNo,modeidindex); 	/* Get colordepth */
        colorth >>= 1;
        if(!colorth) colorth++;
     }

  } else {

     CRT1ModeNo = 0xfe;
     VCLK = SiS_Pr->CSRClock_CRT1;						/* Get VCLK */
     data2 = (SiS_Pr->CModeFlag_CRT1 & ModeInfoFlag) - 2;
     switch(data2) {								/* Get color depth */
        case 0 : colorth = 1; break;
        case 1 : colorth = 1; break;
        case 2 : colorth = 2; break;
        case 3 : colorth = 2; break;
        case 4 : colorth = 3; break;
        case 5 : colorth = 4; break;
        default: colorth = 2;
     }

  }

  if(CRT1ModeNo >= 0x13) {
    if(HwInfo->jChipType == SIS_300) {
       index = SiS_GetReg(SiS_Pr->SiS_P3c4,0x3A);
    } else {
       index = SiS_GetReg(SiS_Pr->SiS_P3c4,0x1A);
    }
    index &= 0x07;
    MCLK = SiS_Pr->SiS_MCLKData_0[index].CLOCK;				/* Get MCLK */

    data2 = (colorth * VCLK) / MCLK;

    temp = SiS_GetReg(SiS_Pr->SiS_P3c4,0x14);
    temp = ((temp & 0x00FF) >> 6) << 1;
    if(temp == 0) temp = 1;
    temp <<= 2;
    temp &= 0xff;

    data2 = temp - data2;

    if((28 * 16) % data2) {
      	data2 = (28 * 16) / data2;
      	data2++;
    } else {
      	data2 = (28 * 16) / data2;
    }

    if(HwInfo->jChipType == SIS_300) {

	tempah = SiS_GetReg(SiS_Pr->SiS_P3c4,0x18);
	tempah &= 0x62;
	tempah >>= 1;
	tempal = tempah;
	tempah >>= 3;
	tempal |= tempah;
	tempal &= 0x07;
	tempcl = ThTiming[tempal];
	tempbx = SiS_GetReg(SiS_Pr->SiS_P3c4,0x16);
	tempbx >>= 6;
	tempah = SiS_GetReg(SiS_Pr->SiS_P3c4,0x14);
	tempah >>= 4;
	tempah &= 0x0c;
	tempbx |= tempah;
	tempbx <<= 1;
	tempal = ThLowB[tempbx + 1];
	tempal *= tempcl;
	tempal += ThLowB[tempbx];
	data = tempal;

    } else if(HwInfo->jChipType == SIS_730) {

#ifndef LINUX_XF86
       SiS_SetRegLong(0xcf8,0x80000050);
       eax = SiS_GetRegLong(0xcfc);
#else
       eax = pciReadLong(0x00000000, 0x50);
#endif
       tempal = (USHORT)(eax >> 8);
       tempal &= 0x06;
       tempal <<= 5;

#ifndef LINUX_XF86
       SiS_SetRegLong(0xcf8,0x800000A0);
       eax = SiS_GetRegLong(0xcfc);
#else
       eax = pciReadLong(0x00000000, 0xA0);
#endif
       temp = (USHORT)(eax >> 28);
       temp &= 0x0F;
       tempal |= temp;

       tempbx = tempal;   /* BIOS BUG (2.04.5d, 2.04.6a use ah here, which is unset!) */
       tempbx = 0;        /* -- do it like the BIOS anyway... */
       tempax = tempbx;
       tempbx &= 0xc0;
       tempbx >>= 6;
       tempax &= 0x0f;
       tempax *= 3;
       tempbx += tempax;

       data = LatencyFactor730[tempbx];
       data += 15;
       temp = SiS_GetReg(SiS_Pr->SiS_P3c4,0x14);
       if(!(temp & 0x80)) data += 5;

    } else {

       index = 0;
       temp = SiS_GetReg(SiS_Pr->SiS_P3c4,0x14);
       if(temp & 0x0080) index += 12;

#ifndef LINUX_XF86
       SiS_SetRegLong(0xcf8,0x800000A0);
       eax = SiS_GetRegLong(0xcfc);
#else
       /* We use pci functions X offers. We use tag 0, because
        * we want to read/write to the host bridge (which is always
        * 00:00.0 on 630, 730 and 540), not the VGA device.
        */
       eax = pciReadLong(0x00000000, 0xA0);
#endif
       temp = (USHORT)(eax >> 24);
       if(!(temp&0x01)) index += 24;

#ifndef LINUX_XF86
       SiS_SetRegLong(0xcf8,0x80000050);
       eax = SiS_GetRegLong(0xcfc);
#else
       eax = pciReadLong(0x00000000, 0x50);
#endif
       temp=(USHORT)(eax >> 24);
       if(temp & 0x01) index += 6;

       temp = (temp & 0x0F) >> 1;
       index += temp;

       data = LatencyFactor[index];
       data += 15;
       temp = SiS_GetReg(SiS_Pr->SiS_P3c4,0x14);
       if(!(temp & 0x80)) data += 5;
    }

    data += data2;				/* CRT1 Request Period */

    SiS_Pr->SiS_SetFlag |= ProgrammingCRT2;
    SiS_Pr->SiS_SelectCRT2Rate = SelectRate_backup;

    if(!SiS_Pr->UseCustomMode) {

       CRT2ModeNo = ModeNo;
       SiS_SearchModeID(SiS_Pr, &CRT2ModeNo, &modeidindex);

       refreshratetableindex = SiS_GetRatePtr(SiS_Pr, CRT2ModeNo, modeidindex, HwInfo);

       index = SiS_GetVCLK2Ptr(SiS_Pr,CRT2ModeNo,modeidindex,
                               refreshratetableindex,HwInfo);
       VCLK = SiS_Pr->SiS_VCLKData[index].CLOCK;                         	/* Get VCLK  */

       if((SiS_Pr->SiS_CustomT == CUT_BARCO1366) || (SiS_Pr->SiS_CustomT == CUT_BARCO1024)) {
          if((ROMAddr) && SiS_Pr->SiS_UseROM) {
	     if(ROMAddr[0x220] & 0x01) {
                VCLK = ROMAddr[0x229] | (ROMAddr[0x22a] << 8);
	     }
          }
       }

    } else {

       CRT2ModeNo = 0xfe;
       VCLK = SiS_Pr->CSRClock;							/* Get VCLK */

    }

    colorth = SiS_GetColorDepth(SiS_Pr,CRT2ModeNo,modeidindex);   	/* Get colordepth */
    colorth >>= 1;
    if(!colorth) colorth++;

    data = data * VCLK * colorth;
    if(data % (MCLK << 4)) {
      	data = data / (MCLK << 4);
      	data++;
    } else {
      	data = data / (MCLK << 4);
    }

    if(data <= 6) data = 6;
    if(data > 0x14) data = 0x14;

    temp = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x01);
    if(HwInfo->jChipType == SIS_300) {
       if(data <= 0x0f) temp = (temp & (~0x1F)) | 0x13;
       else             temp = (temp & (~0x1F)) | 0x16;
       if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
       		temp = (temp & (~0x1F)) | 0x13;
       }
    } else {
       if( ( (HwInfo->jChipType == SIS_630) ||
             (HwInfo->jChipType == SIS_730) )  &&
           (HwInfo->jChipRevision >= 0x30) ) /* 630s or 730(s?) */
      {
	  temp = (temp & (~0x1F)) | 0x1b;
      } else {
	  temp = (temp & (~0x1F)) | 0x16;
      }
    }
    SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x01,0xe0,temp);

    if( (HwInfo->jChipType == SIS_630) &&
        (HwInfo->jChipRevision >= 0x30) ) /* 630s, NOT 730 */
    {
   	if(data > 0x13) data = 0x13;
    }
    SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x02,0xe0,data);

  } else {  /* If mode <= 0x13, we just restore everything */

    SiS_Pr->SiS_SetFlag |= ProgrammingCRT2;
    SiS_Pr->SiS_SelectCRT2Rate = SelectRate_backup;

  }
}
#endif

/**** Set CRT2 FIFO on 315/330 series ****/
#ifdef SIS315H
static void
SiS_SetCRT2FIFO_310(SiS_Private *SiS_Pr)
{
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x01,0x3B);
  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x02,~0x3F,0x04);
}
#endif

/*************** Set LCD-A ***************/
#ifdef SIS315H
static void
SiS_SetGroup1_LCDA(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
                   PSIS_HW_INFO HwInfo, USHORT RefreshRateTableIndex)
{
  USHORT modeflag,resinfo;
  USHORT push2,tempax,tempbx,tempcx,temp;
  ULONG tempeax=0,tempebx,tempecx,tempvcfact;

  /* This is not supported with LCDA */
  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_PanelCustom) return;
  if(SiS_Pr->UseCustomMode) return;

  if(IS_SIS330) {
     SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2D,0x10);			/* Xabre 1.01.03 */
  } else if(IS_SIS740) {
     if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {					/* 740/LVDS */
        SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,0xfb,0x04);      	/* 740/LVDS */
	SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2D,0x03);
     } else if(SiS_Pr->SiS_VBType & VB_SISVB) {
        SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2D,0x10);			/* 740/301LV, 301BDH */
     }
  } else {
     if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {					/* 650/LVDS */
        SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,0xfb,0x04);      	/* 650/LVDS */
	SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2D,0x00);			/* 650/LVDS 1.10.07 */
     } else if(SiS_Pr->SiS_VBType & VB_SISVB) {
        SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2D,0x0f);			/* 650/30xLv 1.10.6s */
     }
  }

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  } else {
     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  }

  tempax = SiS_Pr->SiS_LCDHDES;

  temp = (tempax & 0x0007);                        		/* BPLHDESKEW[2:0]   */
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1A,temp);                         /* Part1_1Ah  */
  temp = (tempax >> 3) & 0x00FF;                               	/* BPLHDESKEW[10:3]  */
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x16,temp);                         /* Part1_16h  */

  tempbx = SiS_Pr->SiS_HDE;
  if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
     tempbx = SiS_Pr->PanelXRes;
  }

  tempax += tempbx;	                                    	/* HDE + HSKEW = lcdhdee  */
  if(tempax >= SiS_Pr->SiS_HT) tempax -= SiS_Pr->SiS_HT;

  temp = tempax;
  if(SiS_Pr->SiS_VBType & VB_SISVB) {
     if(temp & 0x07) temp += 8;
  }
  temp >>= 3;                                        		/* BPLHDEE  */
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x17,temp);                        	/* Part1_17h  */

  tempcx = (SiS_Pr->SiS_HT - tempbx) >> 2;     	            	/* (HT-HDE) / 4  */

  /* 650/30xLV 1.10.6s, 740/LVDS */
  if( ((SiS_Pr->SiS_VBType & VB_SISVB) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) ||
      ((SiS_Pr->SiS_IF_DEF_LVDS == 1) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) ) {
     if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600)        tempcx = 0x28;
 	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)  tempcx = 0x18;
	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) tempcx = 0x30;
	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) tempcx = 0x40;
	else                                                          tempcx = 0x30;
     }
  }

  tempcx += tempax;  	                                  	/* lcdhrs  */
  if(tempcx >= SiS_Pr->SiS_HT) tempcx -= SiS_Pr->SiS_HT;

  temp = (tempcx >> 3) & 0x00FF;				/* BPLHRS */
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x14,temp);                 		/* Part1_14h  */

  temp += 10;
  if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
        if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {
	   temp += 6;
	   if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel800x600) {
	      temp++;
	      if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1024x768) {
	         temp += 7;
		 if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1600x1200) {
		    temp -= 10;
		 }
	      }
	   }
	}
     }
  }
  temp &= 0x1F;
  temp |= ((tempcx & 0x07) << 5);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x15,temp);                         /* Part1_15h  */

  if(SiS_Pr->SiS_IF_DEF_TRUMPION == 0) {
     tempax = SiS_Pr->PanelYRes;
  } else {
     tempax = SiS_Pr->SiS_VGAVDE;
  }

  tempbx = SiS_Pr->SiS_LCDVDES + tempax;
  if(tempbx >= SiS_Pr->SiS_VT) tempbx -= SiS_Pr->SiS_VT;
  push2 = tempbx;

  tempcx = (SiS_Pr->SiS_VGAVT - SiS_Pr->SiS_VGAVDE) >> 2;

  if( ((SiS_Pr->SiS_VBType & VB_SISVB) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) ||
      ((SiS_Pr->SiS_IF_DEF_LVDS == 1) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) ) {
     if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600)         tempcx = 1;
   	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)   tempcx = 3;
	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768)   tempcx = 3;
	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024)  tempcx = 1;
	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050)  tempcx = 1;
	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200)  tempcx = 1;
	else                                                           tempcx = 0x0057;
     }
  }

  tempbx += tempcx;
  if(SiS_Pr->SiS_VBType & VB_SISVB) {
     tempbx++;                                                	/* BPLVRS  */
  }
  if(tempbx >= SiS_Pr->SiS_VT) tempbx -= SiS_Pr->SiS_VT;
  temp = tempbx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,temp);                             /* Part1_18h  */

  tempcx >>= 3;
  if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
        if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {
	   if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600)         tempcx = 3;
   	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)   tempcx = 5;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768)   tempcx = 5;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024)  tempcx = 5;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050)  tempcx = 2;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200)  tempcx = 2;
	}
     }
  }
  tempcx += tempbx;
  tempcx++;                                                	/* BPLVRE  */
  temp = tempcx & 0x000F;
  if(SiS_Pr->SiS_VBType & VB_SISVB) {
     temp |= 0xC0;
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0xF0,temp); 		/* Part1_19h  */
  } else {
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0xF0,temp);
  }

  temp = ((tempbx >> 8) & 0x07) << 3;
  if(SiS_Pr->SiS_VGAVDE != SiS_Pr->SiS_VDE) temp |= 0x40;
  if(SiS_Pr->SiS_SetFlag & EnableLVDSDDA)   temp |= 0x40;
  if(SiS_Pr->SiS_VBType & VB_SISVB) {
     /* Don't check Part1Port,0x00 -> is not being set if LCDA! */
     /* We check SR06 instead here: */
     if(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit) {
        if(SiS_GetReg(SiS_Pr->SiS_P3c4,0x06) & 0x10) temp |= 0x80;
     }
  } else {
     if(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit) {
        if(SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00) & 0x01) temp |= 0x80;
     }
  }
  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x1A,0x07,temp);            /* Part1_1Ah */

  tempbx = push2;                                      		/* BPLVDEE */

  tempcx = SiS_Pr->SiS_LCDVDES;                        		/* NPLVDES */
  if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600) {
        if(resinfo == SIS_RI_800x600) tempcx++;
     }
  }
  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480) {
     tempbx = tempcx = SiS_Pr->SiS_VGAVDE;
     tempbx--;
  }

  temp = ((tempbx >> 8) & 0x07) << 3;
  temp = temp | ((tempcx >> 8) & 0x07);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1D,temp);                          /* Part1_1Dh */
  temp = tempbx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1C,temp);                          /* Part1_1Ch  */
  temp = tempcx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1B,temp);                          /* Part1_1Bh  */

  tempeax = SiS_Pr->SiS_VGAVDE << 18;
  tempebx = SiS_Pr->SiS_VDE;
  temp = (USHORT)(tempeax % tempebx);
  tempeax = tempeax / tempebx;
  if(temp) tempeax++;
  tempvcfact = tempeax;

  temp = (USHORT)(tempeax & 0x00FF);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x37,temp);

  temp = (USHORT)((tempeax & 0x00FF00) >> 8);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x36,temp);

  temp = (USHORT)((tempeax & 0x00030000) >> 16);
  if(SiS_Pr->SiS_VDE == SiS_Pr->SiS_VGAVDE) temp |= 0x04;
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x35,temp);

  if(SiS_Pr->SiS_VBType & (VB_SIS301C | VB_SIS302ELV)) {
     temp = (USHORT)(tempeax & 0x00FF);
     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x3c,temp);
     temp = (USHORT)((tempeax & 0x00FF00) >> 8);
     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x3b,temp);
     temp = (USHORT)(((tempeax & 0x00030000) >> 16) << 6);
     SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x3a,0x3f,temp);
     temp = 0;
     if(SiS_Pr->SiS_VDE != SiS_Pr->SiS_VGAVDE) temp |= 0x08;
     SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x30,0xf3,temp);
  }

  tempeax = SiS_Pr->SiS_VGAHDE << 16;
  tempebx = SiS_Pr->SiS_HDE;
  temp = tempeax % tempebx;
  tempeax /= tempebx;
  if(temp) tempeax++;
  if(tempebx == SiS_Pr->SiS_VGAHDE) tempeax = 0xFFFF;
  tempecx = tempeax;
  tempeax = ((SiS_Pr->SiS_VGAHDE << 16) / tempecx) - 1;
  tempecx = (tempecx << 16) | (tempeax & 0xFFFF);
  temp = (USHORT)(tempecx & 0x00FF);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1F,temp);                          /* Part1_1Fh  */

  tempeax = (SiS_Pr->SiS_VGAVDE << 18) / tempvcfact;
  tempbx = (USHORT)(tempeax & 0x0FFFF);

  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) tempbx--;

  if(SiS_Pr->SiS_SetFlag & EnableLVDSDDA)  tempbx = 1;

  temp = ((tempbx >> 8) & 0x07) << 3;
  temp = temp | ((tempecx >> 8) & 0x07);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x20,temp);                         /* Part1_20h */

  temp = tempbx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x21,temp);                         /* Part1_21h */

  tempecx >>= 16;   	                                  	/* BPLHCFACT  */
  if(modeflag & HalfDCLK) tempecx >>= 1;
  temp = (USHORT)((tempecx & 0x0000FF00) >> 8);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x22,temp);                         /* Part1_22h */

  temp = (USHORT)(tempecx & 0x000000FF);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x23,temp);

  if((SiS_Pr->SiS_IF_DEF_LVDS == 1) || (SiS_Pr->SiS_VBInfo & VB_SIS301LV302LV)) {
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1e,0x20);
  }
}
#endif  /* SIS 315 */

static USHORT
SiS_GetVGAHT2(SiS_Private *SiS_Pr)
{
  ULONG tempax,tempbx;

  tempbx = ((SiS_Pr->SiS_VGAVT - SiS_Pr->SiS_VGAVDE) * SiS_Pr->SiS_RVBHCMAX) & 0xFFFF;
  tempax = (SiS_Pr->SiS_VT - SiS_Pr->SiS_VDE) * SiS_Pr->SiS_RVBHCFACT;
  tempax = (tempax * SiS_Pr->SiS_HT) / tempbx;
  return((USHORT) tempax);
}

/******* Set Part 1 / SiS bridge *********/
static void
SiS_SetGroup1_301(SiS_Private *SiS_Pr, USHORT ModeNo,USHORT ModeIdIndex,
                  PSIS_HW_INFO HwInfo,USHORT RefreshRateTableIndex)
{
  USHORT  push1,push2;
  USHORT  tempax,tempbx,tempcx,temp;
  USHORT  resinfo,modeflag;
  unsigned char p1_7, p1_8;

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  } else {
     if(SiS_Pr->UseCustomMode) {
        modeflag = SiS_Pr->CModeFlag;
	resinfo = 0;
     } else {
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    	resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
     }
  }

  /* The following is only done if bridge is in slave mode: */

  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x03,0xff);                  /* set MAX HT */

  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)  modeflag |= Charx8Dot;

  if(modeflag & Charx8Dot) tempcx = 0x08;
  else                     tempcx = 0x09;

  tempax = SiS_Pr->SiS_VGAHDE;                                 	/* 0x04 Horizontal Display End */
  if(modeflag & HalfDCLK) tempax >>= 1;
  tempax = ((tempax / tempcx) - 1) & 0xff;
  tempbx = tempax;

  temp = tempax;
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x04,temp);

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
     if(!(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)) {
        temp += 2;
     }
  }
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
     if(resinfo == SIS_RI_800x600) temp -= 2;
  }
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x05,temp);                 /* 0x05 Horizontal Display Start */

  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x06,0x03);                 /* 0x06 Horizontal Blank end     */

  tempax = 0xFFFF;
  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) tempax = SiS_GetVGAHT2(SiS_Pr);
  if(tempax >= SiS_Pr->SiS_VGAHT) tempax = SiS_Pr->SiS_VGAHT;
  if(modeflag & HalfDCLK)         tempax >>= 1;
  tempax = (tempax / tempcx) - 5;
  tempcx = tempax;

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
     temp = tempcx - 1;
     if(!(modeflag & HalfDCLK)) {
        temp -= 6;
        if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode) {
           temp -= 2;
           if(ModeNo > 0x13) temp -= 10;
        }
     }
  } else {
     tempcx = (tempcx + tempbx) >> 1;
     temp = (tempcx & 0x00FF) + 2;
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
        temp--;
        if(!(modeflag & HalfDCLK)) {
           if((modeflag & Charx8Dot)) {
              temp += 4;
              if(SiS_Pr->SiS_VGAHDE >= 800) temp -= 6;
              if(HwInfo->jChipType >= SIS_315H) {
	         if(SiS_Pr->SiS_VGAHDE == 800) temp += 2;
              }
           }
        }
     } else {
        if(!(modeflag & HalfDCLK)) {
           temp -= 4;
           if((SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1280x960) &&
	      (SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1600x1200)) {
              if(SiS_Pr->SiS_VGAHDE >= 800) {
                 temp -= 7;
	         if(HwInfo->jChipType < SIS_315H) {
	            /* 650/301LV(x) does not do this, 630/301B, 300/301LV do */
                    if(SiS_Pr->SiS_ModeType == ModeEGA) {
                       if(SiS_Pr->SiS_VGAVDE == 1024) {
                          temp += 15;
                          if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1280x1024)
		  	     temp += 7;
                       }
                    }
	         }
		 if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1400x1050) {
                    if(SiS_Pr->SiS_VGAHDE >= 1280) {
                       if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) temp += 28;
		    }
                 }
              }
           }
        }
     }
  }

  p1_7 = temp;
  p1_8 = 0x00;

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
     if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode) {
        if(ModeNo <= 0x01) {
	   p1_7 = 0x2a;
	   if(!(SiS_Pr->SiS_TVMode & TVSetPAL)) p1_8 = 0x61;
	   else 	      			p1_8 = 0x41;
	} else if(SiS_Pr->SiS_ModeType == ModeText) {
	   if(!(SiS_Pr->SiS_TVMode & TVSetPAL)) p1_7 = 0x54;
	   else 	    			p1_7 = 0x55;
	   p1_8 = 0x00;
	} else if(ModeNo <= 0x13) {
	   if(modeflag & HalfDCLK) {
	      if(!(SiS_Pr->SiS_TVMode & TVSetPAL)) {
		 p1_7 = 0x30;
		 p1_8 = 0x03;
	      } else {
	 	 p1_7 = 0x2f;
		 p1_8 = 0x02;
	      }
	   } else {
	      p1_7 = 0x5b;
	      p1_8 = 0x03;
	   }
	} else if( ((HwInfo->jChipType >= SIS_315H) &&
	            ((ModeNo == 0x50) || (ModeNo == 0x56) || (ModeNo == 0x53))) ||
	           ((HwInfo->jChipType < SIS_315H) &&
		    (resinfo == SIS_RI_320x200 || resinfo == SIS_RI_320x240)) ) {
	   if(!(SiS_Pr->SiS_TVMode & TVSetPAL)) {
	      p1_7 = 0x30,
	      p1_8 = 0x03;
	   } else {
	      p1_7 = 0x2f;
	      p1_8 = 0x03;
	   }
        }
     }
  }

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {
     if(SiS_Pr->SiS_TVMode & (TVSetYPbPr525p|TVSetYPbPr750p)) {
        p1_7 = 0x63;
	if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p) p1_7 = 0x55;
     }
     if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode) {
        if(!(modeflag & HalfDCLK)) {
	   p1_7 = 0xb2;
	   if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p) {
	      p1_7 = 0xab;
	   }
	}
     }
  }

  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x07,p1_7);			/* 0x07 Horizontal Retrace Start */
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x08,p1_8);			/* 0x08 Horizontal Retrace End   */

  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x03);                	/* 0x18 SR08 (FIFO Threshold?)   */

  SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x19,0xF0);

  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x09,0xFF);                	/* 0x09 Set Max VT    */

  tempcx = 0x121;
  tempbx = SiS_Pr->SiS_VGAVDE;                               	/* 0x0E Vertical Display End */
  if     (tempbx == 357) tempbx = 350;
  else if(tempbx == 360) tempbx = 350;
  else if(tempbx == 375) tempbx = 350;
  else if(tempbx == 405) tempbx = 400;
  else if(tempbx == 420) tempbx = 400;
  else if(tempbx == 525) tempbx = 480;
  push2 = tempbx;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
      	if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) {
           if     (tempbx == 350) tempbx += 5;
           else if(tempbx == 480) tempbx += 5;
      	}
     }
  }
  tempbx -= 2;
  temp = tempbx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x10,temp);        		/* 0x10 vertical Blank Start */

  tempbx = push2;
  tempbx--;
  temp = tempbx & 0x00FF;
#if 0
  /* Missing code from 630/301B 2.04.5a and 650/302LV 1.10.6s (calles int 2f) */
  if(xxx()) {
      if(temp == 0xdf) temp = 0xda;
  }
#endif
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0E,temp);

  temp = 0;
  if(modeflag & DoubleScanMode) temp |= 0x80;
  if(HwInfo->jChipType >= SIS_661) {
     if(tempbx & 0x0200)        temp |= 0x20;
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x0B,0x5F,temp);
     if(tempbx & 0x0100)  tempcx |= 0x000a;
     if(tempbx & 0x0400)  tempcx |= 0x1200;
  } else {
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0B,temp);
     if(tempbx & 0x0100)  tempcx |= 0x0002;
     if(tempbx & 0x0400)  tempcx |= 0x0600;
  }

  if(tempbx & 0x0200)  tempcx |= 0x0040;

  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x11,0x00);                	/* 0x11 Vertical Blank End */

  tempax = (SiS_Pr->SiS_VGAVT - tempbx) >> 2;

  if((ModeNo > 0x13) || (HwInfo->jChipType < SIS_315H)) {
     if(resinfo != SIS_RI_1280x1024) {
	tempbx += (tempax << 1);
     }
  } else if(HwInfo->jChipType >= SIS_315H) {
     if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1400x1050) {
	tempbx += (tempax << 1);
     }
  }

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
     tempbx -= 10;
  } else {
     if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode) {
        if(SiS_Pr->SiS_TVMode & TVSetPAL) {
           tempbx += 40;
	   if(HwInfo->jChipType >= SIS_315H) {
	      if(SiS_Pr->SiS_VGAHDE == 800) tempbx += 10;
	   }
	}
     }
  }
  tempax >>= 2;
  tempax++;
  tempax += tempbx;
  push1 = tempax;
  if(SiS_Pr->SiS_TVMode & TVSetPAL) {
     if(tempbx <= 513)  {
     	if(tempax >= 513) tempbx = 513;
     }
  }
  temp = tempbx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0C,temp);			/* 0x0C Vertical Retrace Start */

  tempbx--;
  temp = tempbx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x10,temp);

  if(tempbx & 0x0100) tempcx |= 0x0008;

  if(tempbx & 0x0200) {
     SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x0B,0x20);
  }
  tempbx++;

  if(tempbx & 0x0100) tempcx |= 0x0004;
  if(tempbx & 0x0200) tempcx |= 0x0080;
  if(tempbx & 0x0400) {
     if(HwInfo->jChipType >= SIS_661)        tempcx |= 0x0800;
     else if(SiS_Pr->SiS_VBType & VB_SIS301) tempcx |= 0x0800;
     else                                    tempcx |= 0x0C00;
  }

  tempbx = push1;
  temp = tempbx & 0x000F;
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0D,temp);        		/* 0x0D vertical Retrace End */

  if(tempbx & 0x0010) tempcx |= 0x2000;

  temp = tempcx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0A,temp);              	/* 0x0A CR07 */

  temp = (tempcx & 0xFF00) >> 8;
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x17,temp);              	/* 0x17 SR0A */

  tempax = modeflag;
  temp = (tempax & 0xFF00) >> 8;
  temp = (temp >> 1) & 0x09;
  if(!(SiS_Pr->SiS_VBType & VB_SIS301)) temp |= 0x01;		/* Always 8 dotclock */
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x16,temp);              	/* 0x16 SR01 */

  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0F,0x00);              	/* 0x0F CR14 */

  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x12,0x00);              	/* 0x12 CR17 */

  temp = 0x00;
  if(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit) {
     if(SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00) & 0x01) {
	temp = 0x80;
     }
  }
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1A,temp);                	/* 0x1A SR0E */
}

/*********** Set Part 1 / LVDS ***********/
static void
SiS_SetGroup1_LVDS(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
 		   PSIS_HW_INFO HwInfo, USHORT RefreshRateTableIndex)
{
  USHORT modeflag, resinfo;
  USHORT push1, push2, tempax, tempbx, tempcx, temp;
#ifdef SIS315H
  USHORT pushcx;
#endif
  ULONG  tempeax=0, tempebx, tempecx, tempvcfact=0;

  /* This is not supported on LVDS */
  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_PanelCustom) return;
  if(SiS_Pr->UseCustomMode) return;

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  } else {
     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  }

  /* Set up Panel Link */

  /* 1. Horizontal setup */

  tempax = SiS_Pr->SiS_LCDHDES;

  if((!SiS_Pr->SiS_IF_DEF_FSTN) && (!SiS_Pr->SiS_IF_DEF_DSTN)) {
     if( (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480) &&
         (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) ) {
  	   tempax -= 8;
     }
  }

  tempcx = SiS_Pr->SiS_HT;    				  /* Horiz. Total */

  tempbx = SiS_Pr->SiS_HDE;                               /* Horiz. Display End */

  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_2 ||
     SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_3) {
     tempbx >>= 1;
  }

  if(!(SiS_Pr->SiS_LCDInfo & LCDPass11)) {
     if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
        if(SiS_Pr->SiS_IF_DEF_FSTN || SiS_Pr->SiS_IF_DEF_DSTN) {
	   tempbx = SiS_Pr->PanelXRes;
	} else if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {
	   tempbx = SiS_Pr->PanelXRes;
	   if(SiS_Pr->SiS_CustomT == CUT_BARCO1024) {
	      tempbx = 800;
	      if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel800x600) {
	         tempbx = 1024;
	      }
	   }
        }
     }
  }
  tempcx = (tempcx - tempbx) >> 2;		 /* HT-HDE / 4 */

  push1 = tempax;

  tempax += tempbx;

  if(tempax >= SiS_Pr->SiS_HT) tempax -= SiS_Pr->SiS_HT;

  push2 = tempax;

  if((!SiS_Pr->SiS_IF_DEF_FSTN) &&
     (!SiS_Pr->SiS_IF_DEF_DSTN) &&
     (SiS_Pr->SiS_CustomT != CUT_BARCO1366) &&
     (SiS_Pr->SiS_CustomT != CUT_BARCO1024) &&
     (SiS_Pr->SiS_CustomT != CUT_PANEL848)) {
     if(!(SiS_Pr->SiS_LCDInfo & LCDPass11)) {
        if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
           if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {
     	      if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600)        tempcx = 0x0028;
	      else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600)  tempcx = 0x0018;
     	      else if( (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) ||
	            (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x768) ) {
	  	   if(HwInfo->jChipType < SIS_315H) {
		      if(SiS_Pr->SiS_VBType & VB_SISVB) {
		         tempcx = 0x0017;  /* A901; sometimes 0x0018; */
		      } else {
		         tempcx = 0x0017;
#ifdef TWNEWPANEL
			 tempcx = 0x0018;
#endif
		      }
		   } else {
		      tempcx = 0x0018;
		   }
	      }
	      else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768)  tempcx = 0x0028;
	      else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) tempcx = 0x0030;
	      else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) tempcx = 0x0030;
	      else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) tempcx = 0x0040;
	   }
        }
     }
  }

  tempcx += tempax;                              /* lcdhrs  */
  if(tempcx >= SiS_Pr->SiS_HT) tempcx -= SiS_Pr->SiS_HT;

  tempax = tempcx >> 3;                          /* BPLHRS */
  temp = tempax & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x14,temp);		 /* Part1_14h; Panel Link Horizontal Retrace Start  */

  if(SiS_Pr->SiS_LCDInfo & LCDPass11) {
     temp = (tempax & 0x00FF) + 2;
  } else {
     temp = (tempax & 0x00FF) + 10;
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
        if((!SiS_Pr->SiS_IF_DEF_DSTN) &&
	   (!SiS_Pr->SiS_IF_DEF_FSTN) &&
	   (SiS_Pr->SiS_CustomT != CUT_BARCO1366) &&
	   (SiS_Pr->SiS_CustomT != CUT_BARCO1024) &&
	   (SiS_Pr->SiS_CustomT != CUT_PANEL848)) {
           if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {
	      temp += 6;
              if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel800x600) {
	         temp++;
	         if(HwInfo->jChipType >= SIS_315H) {
	            if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1024x768) {
	               temp += 7;
		       if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1600x1200) {
		          temp -= 0x14;
			  if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1280x768) {
			     temp -= 10;
			  }
		       }
	            }
	         }
	      }
           }
        }
     }
  }

  temp &= 0x1F;
  temp |= ((tempcx & 0x0007) << 5);
#if 0
  if(SiS_Pr->SiS_IF_DEF_FSTN) temp = 0x20;       /* WRONG? BIOS loads cl, not ah */
#endif
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x15,temp);    	 /* Part1_15h; Panel Link Horizontal Retrace End/Skew */

  tempbx = push2;
  tempcx = push1;                                /* lcdhdes  */

  temp = (tempcx & 0x0007);                      /* BPLHDESKEW  */
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1A,temp);   	 /* Part1_1Ah; Panel Link Vertical Retrace Start (2:0) */

  tempcx >>= 3;                                  /* BPLHDES */
  temp = (tempcx & 0x00FF);
#if 0 /* Not 550 FSTN */
  if(HwInfo->jChipType >= SIS_315H) {
     if(ModeNo == 0x5b) temp--; */
  }
#endif
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x16,temp);    	 /* Part1_16h; Panel Link Horizontal Display Enable Start  */

  if((HwInfo->jChipType < SIS_315H) ||
     (SiS_Pr->SiS_IF_DEF_FSTN) ||
     (SiS_Pr->SiS_IF_DEF_DSTN)) {
     if(tempbx & 0x07) tempbx += 8;
  }
  tempbx >>= 3;                                  /* BPLHDEE  */
  temp = tempbx & 0x00FF;
#if 0 /* Not 550 FSTN */
  if(HwInfo->jChipType >= SIS_315H) {
     if(ModeNo == 0x5b) temp--;
  }
#endif
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x17,temp);   	 /* Part1_17h; Panel Link Horizontal Display Enable End  */

  /* 2. Vertical setup */

  if(HwInfo->jChipType < SIS_315H) {
     tempcx = SiS_Pr->SiS_VGAVT;
     tempbx = SiS_Pr->SiS_VGAVDE;
     if((SiS_Pr->SiS_CustomT != CUT_BARCO1366) && (SiS_Pr->SiS_CustomT != CUT_BARCO1024)) {
        if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
           if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {
	      tempbx = SiS_Pr->PanelYRes;
           }
	}
     }
     tempcx -= tempbx;

  } else {

     tempcx = SiS_Pr->SiS_VGAVT - SiS_Pr->SiS_VGAVDE;           /* VGAVT-VGAVDE  */

  }

  tempbx = SiS_Pr->SiS_LCDVDES;	   		 	 	/* VGAVDES  */
  push1 = tempbx;

  tempax = SiS_Pr->SiS_VGAVDE;

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
     if((SiS_Pr->SiS_CustomT == CUT_BARCO1366) || (SiS_Pr->SiS_CustomT == CUT_BARCO1024)) {
        if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {
           tempax = 600;
	   if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel800x600) {
	      tempax = 768;
	   }
	}
     } else if( (SiS_Pr->SiS_IF_DEF_TRUMPION == 0)   &&
                (!(SiS_Pr->SiS_LCDInfo & LCDPass11)) &&
                ((SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) ||
	         (SiS_Pr->SiS_IF_DEF_FSTN) ||
	         (SiS_Pr->SiS_IF_DEF_DSTN)) ) {
	tempax = SiS_Pr->PanelYRes;
     }
  }

  tempbx += tempax;
  if(tempbx >= SiS_Pr->SiS_VT) tempbx -= SiS_Pr->SiS_VT;

  push2 = tempbx;

  tempcx >>= 1;

  if((SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) &&
     (SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) &&
     (SiS_Pr->SiS_CustomT != CUT_BARCO1366) &&
     (SiS_Pr->SiS_CustomT != CUT_BARCO1024) &&
     (SiS_Pr->SiS_CustomT != CUT_PANEL848)) {
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_2 ||
        SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_3) {
	tempcx = 0x0017;
     } else if(!(SiS_Pr->SiS_LCDInfo & LCDPass11)) {
        if(SiS_Pr->SiS_IF_DEF_FSTN || SiS_Pr->SiS_IF_DEF_DSTN) {
	   if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600)         tempcx = 0x0003;
  	   else if((SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) ||
	           (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768)) tempcx = 0x0003;
           else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024)  tempcx = 0x0001;
           else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050)  tempcx = 0x0001;
	   else 							  tempcx = 0x0057;
        } else  {
     	   if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600)         tempcx = 0x0001;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600)   tempcx = 0x0001;
     	   else if((SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) ||
	           (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x768)) {
		   if(HwInfo->jChipType < SIS_315H) {
		      if(SiS_Pr->SiS_VBType & VB_SISVB) {
		         tempcx = 0x0002;   /* A901; sometimes 0x0003; */
		      } else {
			 tempcx = 0x0002;
#ifdef TWNEWPANEL
			 tempcx = 0x0003;
#endif
		      }
		   } else tempcx = 0x0003;
           }
     	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768)  tempcx = 0x0003;
     	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) tempcx = 0x0001;
     	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) tempcx = 0x0001;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) tempcx = 0x0001;
     	   else 							 tempcx = 0x0057;
	}
     }
  }

  tempbx += tempcx;			 	/* BPLVRS  */

  if((HwInfo->jChipType < SIS_315H) ||
     (SiS_Pr->SiS_IF_DEF_FSTN) ||
     (SiS_Pr->SiS_IF_DEF_DSTN)) {
     tempbx++;
  }

  if(tempbx >= SiS_Pr->SiS_VT) tempbx -= SiS_Pr->SiS_VT;

  temp = tempbx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,temp);       	 /* Part1_18h; Panel Link Vertical Retrace Start  */

  tempcx >>= 3;

  if((!(SiS_Pr->SiS_LCDInfo & LCDPass11)) &&
     (SiS_Pr->SiS_CustomT != CUT_BARCO1366) &&
     (SiS_Pr->SiS_CustomT != CUT_BARCO1024) &&
     (SiS_Pr->SiS_CustomT != CUT_PANEL848)) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
        if( (HwInfo->jChipType < SIS_315H) &&
            (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480) )     tempcx = 0x0001;
	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_2)  tempcx = 0x0002;
	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_3)  tempcx = 0x0002;
        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600)    tempcx = 0x0003;
        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600)   tempcx = 0x0005;
        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x768)   tempcx = 0x0005;
	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768)   tempcx = 0x0011;
        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024)  tempcx = 0x0005;
        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050)  tempcx = 0x0002;
        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200)  tempcx = 0x0011;
        else if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480)  {
     		if(HwInfo->jChipType < SIS_315H) {
		   if(SiS_Pr->SiS_VBType & VB_SISVB) {
		      tempcx = 0x0004;   /* A901; Other BIOS sets 0x0005; */
		   } else {
		      tempcx = 0x0004;
#ifdef TWNEWPANEL
		      tempcx = 0x0005;
#endif
		   }
		} else {
		   tempcx = 0x0005;
		}
        }
     }
  }

  tempcx = tempcx + tempbx + 1;                  /* BPLVRE  */
  temp = tempcx & 0x000F;
  if(SiS_Pr->SiS_IF_DEF_FSTN ||
     SiS_Pr->SiS_IF_DEF_DSTN ||
     (SiS_Pr->SiS_CustomT == CUT_BARCO1366) ||
     (SiS_Pr->SiS_CustomT == CUT_BARCO1024) ||
     (SiS_Pr->SiS_CustomT == CUT_PANEL848)) {
     temp |= 0x30;
  }
  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0xf0,temp); /* Part1_19h; Panel Link Vertical Retrace End (3:0); Misc.  */

  temp = ((tempbx & 0x0700) >> 8) << 3;          /* BPLDESKEW =0 */
  if(SiS_Pr->SiS_IF_DEF_FSTN || SiS_Pr->SiS_IF_DEF_DSTN) {
     if(SiS_Pr->SiS_HDE != 640) {
        if(SiS_Pr->SiS_VGAVDE != SiS_Pr->SiS_VDE)   temp |= 0x40;
     }
  } else if(SiS_Pr->SiS_VGAVDE != SiS_Pr->SiS_VDE)  temp |= 0x40;
  if(SiS_Pr->SiS_SetFlag & EnableLVDSDDA)           temp |= 0x40;
  if(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit) {
     if(HwInfo->jChipType >= SIS_315H) {
        if(SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00) & 0x01) {
           temp |= 0x80;
        }
     } else {
	if( (HwInfo->jChipType == SIS_630) ||
	    (HwInfo->jChipType == SIS_730) ) {
	   if(HwInfo->jChipRevision >= 0x30) {
	      temp |= 0x80;
	   }
	}
     }
  }
  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x1A,0x87,temp);  /* Part1_1Ah; Panel Link Control Signal (7:3); Vertical Retrace Start (2:0) */

  if (HwInfo->jChipType < SIS_315H) {

#ifdef SIS300      /* 300 series */

        tempeax = SiS_Pr->SiS_VGAVDE << 6;
        temp = (USHORT)(tempeax % (ULONG)SiS_Pr->SiS_VDE);
        tempeax = tempeax / (ULONG)SiS_Pr->SiS_VDE;
        if(temp != 0) tempeax++;
        tempebx = tempeax;                         /* BPLVCFACT  */

  	if(SiS_Pr->SiS_SetFlag & EnableLVDSDDA) {
	   tempebx = 0x003F;
	}

  	temp = (USHORT)(tempebx & 0x00FF);
  	SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1E,temp);      /* Part1_1Eh; Panel Link Vertical Scaling Factor */

#endif /* SIS300 */

  } else {

#ifdef SIS315H  /* 315 series */

        if(HwInfo->jChipType == SIS_740) {
           SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1E,0x03);
        } else {
	   SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1E,0x23);
	}

	tempeax = SiS_Pr->SiS_VGAVDE << 18;
    	temp = (USHORT)(tempeax % (ULONG)SiS_Pr->SiS_VDE);
    	tempeax = tempeax / SiS_Pr->SiS_VDE;
    	if(temp != 0) tempeax++;
    	tempebx = tempeax;                         /* BPLVCFACT  */
        tempvcfact = tempeax;
    	temp = (USHORT)(tempebx & 0x00FF);
    	SiS_SetReg(SiS_Pr->SiS_Part1Port,0x37,temp);      /* Part1_37h; Panel Link Vertical Scaling Factor */
    	temp = (USHORT)((tempebx & 0x00FF00) >> 8);
    	SiS_SetReg(SiS_Pr->SiS_Part1Port,0x36,temp);      /* Part1_36h; Panel Link Vertical Scaling Factor */
    	temp = (USHORT)((tempebx & 0x00030000) >> 16);
	temp &= 0x03;
    	if(SiS_Pr->SiS_VDE == SiS_Pr->SiS_VGAVDE) temp |= 0x04;
    	SiS_SetReg(SiS_Pr->SiS_Part1Port,0x35,temp);      /* Part1_35h; Panel Link Vertical Scaling Factor */

#endif /* SIS315H */

  }

  tempbx = push2;                                  /* BPLVDEE  */
  tempcx = push1;

  push1 = temp;

  if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
   	if(!SiS_Pr->SiS_IF_DEF_FSTN && !SiS_Pr->SiS_IF_DEF_DSTN) {
		if(HwInfo->jChipType < SIS_315H) {
			if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600) {
      				if(resinfo == SIS_RI_1024x600) tempcx++;
				if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
					if(resinfo == SIS_RI_800x600) tempcx++;
		    		}
			} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600) {
      				if(resinfo == SIS_RI_800x600)  tempcx++;
				if(resinfo == SIS_RI_1024x768) tempcx++; /* Doesnt make sense anyway... */
			} else  if(resinfo == SIS_RI_1024x768) tempcx++;
		} else {
			if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600) {
      				if(resinfo == SIS_RI_800x600)  tempcx++;
			}
		}
	}
  }

  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480) {
     if((!SiS_Pr->SiS_IF_DEF_FSTN) && (!SiS_Pr->SiS_IF_DEF_DSTN)) {
        tempcx = SiS_Pr->SiS_VGAVDE;
        tempbx = SiS_Pr->SiS_VGAVDE - 1;
     }
  }

  temp = ((tempbx & 0x0700) >> 8) << 3;
  temp |= ((tempcx & 0x0700) >> 8);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1D,temp);     	/* Part1_1Dh; Vertical Display Overflow; Control Signal */

  temp = tempbx & 0x00FF;
  /* if(SiS_Pr->SiS_IF_DEF_FSTN) temp++;  */
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1C,temp);      	/* Part1_1Ch; Panel Link Vertical Display Enable End  */

  temp = tempcx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1B,temp);      	/* Part1_1Bh; Panel Link Vertical Display Enable Start  */

  /* 3. Additional horizontal setup (scaling, etc) */

  tempecx = SiS_Pr->SiS_VGAHDE;
  if(HwInfo->jChipType >= SIS_315H) {
     if((!SiS_Pr->SiS_IF_DEF_FSTN) && (!SiS_Pr->SiS_IF_DEF_DSTN)) {
        if(modeflag & HalfDCLK) tempecx >>= 1;
     }
  }
  tempebx = SiS_Pr->SiS_HDE;
  if(tempecx == tempebx) tempeax = 0xFFFF;
  else {
     tempeax = tempecx;
     tempeax <<= 16;
     temp = (USHORT)(tempeax % tempebx);
     tempeax = tempeax / tempebx;
     if(HwInfo->jChipType >= SIS_315H) {
        if(temp) tempeax++;
     }
  }
  tempecx = tempeax;

  if(HwInfo->jChipType >= SIS_315H) {
     tempeax = SiS_Pr->SiS_VGAHDE;
     if((!SiS_Pr->SiS_IF_DEF_FSTN) && (!SiS_Pr->SiS_IF_DEF_DSTN)) {
        if(modeflag & HalfDCLK) tempeax >>= 1;
     }
     tempeax <<= 16;
     tempeax = (tempeax / tempecx) - 1;
  } else {
     tempeax = ((SiS_Pr->SiS_VGAHT << 16) / tempecx) - 1;
  }
  tempecx <<= 16;
  tempecx |= (tempeax & 0xFFFF);
  temp = (USHORT)(tempecx & 0x00FF);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1F,temp);  	 /* Part1_1Fh; Panel Link DDA Operational Number in each horiz. line */

  tempbx = SiS_Pr->SiS_VDE;
  if(HwInfo->jChipType >= SIS_315H) {
     tempeax = (SiS_Pr->SiS_VGAVDE << 18) / tempvcfact;
     tempbx = (USHORT)(tempeax & 0x0FFFF);
  } else {
     tempeax = SiS_Pr->SiS_VGAVDE << 6;
     tempbx = push1 & 0x3f;
     if(tempbx == 0) tempbx = 64;
     tempeax /= tempbx;
     tempbx = (USHORT)(tempeax & 0x0FFFF);
  }
  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) tempbx--;
  if(SiS_Pr->SiS_SetFlag & EnableLVDSDDA) {
     if((!SiS_Pr->SiS_IF_DEF_FSTN) && (!SiS_Pr->SiS_IF_DEF_DSTN)) tempbx = 1;
     else if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480)  tempbx = 1;
  }

  temp = ((tempbx & 0xFF00) >> 8) << 3;
  temp |= (USHORT)((tempecx & 0x0700) >> 8);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x20,temp);  	/* Part1_20h; Overflow register */

  temp = tempbx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x21,temp);  	/* Part1_21h; Panel Link Vertical Accumulator Register */

  tempecx >>= 16;                               /* BPLHCFACT  */
  if((HwInfo->jChipType < SIS_315H) || (SiS_Pr->SiS_IF_DEF_FSTN) || (SiS_Pr->SiS_IF_DEF_DSTN)) {
     if(modeflag & HalfDCLK) tempecx >>= 1;
  }
  temp = (USHORT)((tempecx & 0xFF00) >> 8);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x22,temp);     	/* Part1_22h; Panel Link Horizontal Scaling Factor High */

  temp = (USHORT)(tempecx & 0x00FF);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x23,temp);         /* Part1_22h; Panel Link Horizontal Scaling Factor Low */

  /* 630/301B and 630/LVDS do something for 640x480 panels here */

#ifdef SIS315H
  if(SiS_Pr->SiS_IF_DEF_FSTN || SiS_Pr->SiS_IF_DEF_DSTN) {
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x25,0x00);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x26,0x00);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x27,0x00);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x28,0x87);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x29,0x5A);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2A,0x4B);
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x44,~0x007,0x03);
     tempax = SiS_Pr->SiS_HDE;                       		/* Blps = lcdhdee(lcdhdes+HDE) + 64 */
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_2 ||
        SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_3) tempax >>= 1;
     tempax += 64;
     temp = tempax & 0x00FF;
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x38,temp);
     temp = ((tempax & 0xFF00) >> 8) << 3;
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x35,~0x078,temp);
     tempax += 32;		                     		/* Blpe=lBlps+32 */
     temp = tempax & 0x00FF;
     if(SiS_Pr->SiS_IF_DEF_FSTN) temp = 0;
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x39,temp);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3A,0x00);        	/* Bflml=0 */
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x3C,~0x007,0x00);

     tempax = SiS_Pr->SiS_VDE;
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_2 ||
        SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_3) tempax >>= 1;
     tempax >>= 1;
     temp = tempax & 0x00FF;
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3B,temp);
     temp = ((tempax & 0xFF00) >> 8) << 3;
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x3C,~0x038,temp);

     tempeax = SiS_Pr->SiS_HDE;
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_2 ||
        SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_3) tempeax >>= 1;
     tempeax <<= 2;                       			/* BDxFIFOSTOP = (HDE*4)/128 */
     tempebx = 128;
     temp = (USHORT)(tempeax % tempebx);
     tempeax = tempeax / tempebx;
     if(temp) tempeax++;
     temp = (USHORT)(tempeax & 0x003F);
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x45,~0x0FF,temp);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3F,0x00);         	/* BDxWadrst0 */
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3E,0x00);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3D,0x10);
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x3C,~0x040,0x00);

     tempax = SiS_Pr->SiS_HDE;
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_2 ||
        SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_3) tempax >>= 1;
     tempax >>= 4;                        			/* BDxWadroff = HDE*4/8/8 */
     pushcx = tempax;
     temp = tempax & 0x00FF;
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x43,temp);
     temp = ((tempax & 0xFF00) >> 8) << 3;
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x44,~0x0F8,temp);

     tempax = SiS_Pr->SiS_VDE;                             	/* BDxWadrst1 = BDxWadrst0 + BDxWadroff * VDE */
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_2 ||
        SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480_3) tempax >>= 1;
     tempeax = (tempax * pushcx);
     tempebx = 0x00100000 + tempeax;
     temp = (USHORT)tempebx & 0x000000FF;
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x42,temp);
     temp = (USHORT)((tempebx & 0x0000FF00) >> 8);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x41,temp);
     temp = (USHORT)((tempebx & 0x00FF0000) >> 16);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x40,temp);
     temp = (USHORT)(((tempebx & 0x01000000) >> 24) << 7);
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x3C,~0x080,temp);

     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2F,0x03);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x03,0x50);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x04,0x00);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2F,0x01);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x19,0x38);

     if(SiS_Pr->SiS_IF_DEF_FSTN) {
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2b,0x02);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2c,0x00);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2d,0x00);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x35,0x0c);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x36,0x00);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x37,0x00);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x38,0x80);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x39,0xA0);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3a,0x00);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3b,0xf0);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3c,0x00);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3d,0x10);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3e,0x00);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3f,0x00);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x40,0x10);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x41,0x25);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x42,0x80);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x43,0x14);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x44,0x03);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x45,0x0a);
     }
  }
#endif  /* SIS315H */

}

/************** Set Part 1 ***************/
static void
SiS_SetGroup1(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
              PSIS_HW_INFO HwInfo, USHORT RefreshRateTableIndex)
{
  UCHAR  *ROMAddr  = HwInfo->pjVirtualRomBase;
  USHORT  temp=0, tempax=0, tempbx=0, tempcx=0;
  USHORT  pushbx=0, CRT1Index=0;
#ifdef SIS315H
  USHORT  tempbl=0;
#endif
  USHORT  modeflag, resinfo=0;

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  } else {
     if(SiS_Pr->UseCustomMode) {
	modeflag = SiS_Pr->CModeFlag;
     } else {
    	CRT1Index = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT1CRTC;
    	resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     }
  }

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {

#ifdef SIS315H
     SiS_SetCRT2Sync(SiS_Pr, ModeNo, RefreshRateTableIndex, HwInfo);
     SiS_SetGroup1_LCDA(SiS_Pr, ModeNo, ModeIdIndex, HwInfo, RefreshRateTableIndex);
#endif

  } else {

     if( (HwInfo->jChipType >= SIS_315H) &&
         (SiS_Pr->SiS_IF_DEF_LVDS == 1) &&
	 (SiS_Pr->SiS_VBInfo & SetInSlaveMode) ) {

        SiS_SetCRT2Sync(SiS_Pr, ModeNo, RefreshRateTableIndex, HwInfo);

     } else {

        SiS_SetCRT2Offset(SiS_Pr, ModeNo, ModeIdIndex,
      		          RefreshRateTableIndex, HwInfo);

        if (HwInfo->jChipType < SIS_315H ) {
#ifdef SIS300
    	      SiS_SetCRT2FIFO_300(SiS_Pr, ModeNo, HwInfo);
#endif
        } else {
#ifdef SIS315H
              SiS_SetCRT2FIFO_310(SiS_Pr);
#endif
	}

        SiS_SetCRT2Sync(SiS_Pr, ModeNo, RefreshRateTableIndex, HwInfo);

	/* 1. Horizontal setup */

        if(HwInfo->jChipType < SIS_315H ) {

#ifdef SIS300   /* ------------- 300 series --------------*/

    		temp = (SiS_Pr->SiS_VGAHT - 1) & 0x0FF;   			/* BTVGA2HT 0x08,0x09 */
    		SiS_SetReg(SiS_Pr->SiS_Part1Port,0x08,temp);                   /* CRT2 Horizontal Total */

    		temp = (((SiS_Pr->SiS_VGAHT - 1) & 0xFF00) >> 8) << 4;
    		SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x09,0x0f,temp);          /* CRT2 Horizontal Total Overflow [7:4] */

    		temp = (SiS_Pr->SiS_VGAHDE + 12) & 0x0FF;                       /* BTVGA2HDEE 0x0A,0x0C */
    		SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0A,temp);                   /* CRT2 Horizontal Display Enable End */

    		pushbx = SiS_Pr->SiS_VGAHDE + 12;                               /* bx  BTVGA@HRS 0x0B,0x0C */
    		tempcx = (SiS_Pr->SiS_VGAHT - SiS_Pr->SiS_VGAHDE) >> 2;
    		tempbx = pushbx + tempcx;
    		tempcx <<= 1;
    		tempcx += tempbx;

    		if(SiS_Pr->SiS_VBType & VB_SISVB) {

		   if(SiS_Pr->UseCustomMode) {
		      tempbx = SiS_Pr->CHSyncStart + 12;
		      tempcx = SiS_Pr->CHSyncEnd + 12;
		   }

      		   if(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC) {
		      unsigned char cr4, cr14, cr5, cr15;
		      if(SiS_Pr->UseCustomMode) {
		         cr4  = SiS_Pr->CCRT1CRTC[4];
			 cr14 = SiS_Pr->CCRT1CRTC[14];
			 cr5  = SiS_Pr->CCRT1CRTC[5];
			 cr15 = SiS_Pr->CCRT1CRTC[15];
		      } else {
		         cr4  = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[4];
			 cr14 = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[14];
			 cr5  = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[5];
			 cr15 = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[15];
		      }
        	      tempbx = ((cr4 | ((cr14 & 0xC0) << 2)) - 1) << 3;
        	      tempcx = (((cr5 & 0x1F) | ((cr15 & 0x04) << (6-2))) - 1) << 3;
      		   }

    		   if((SiS_Pr->SiS_VBInfo & SetCRT2ToTV) && (resinfo == SIS_RI_1024x768)){
        	      if(!(SiS_Pr->SiS_TVMode & TVSetPAL)){
      			 tempbx = 1040;
      			 tempcx = 1042;
      		      }
    		   }
	        }

    		temp = tempbx & 0x00FF;
    		SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0B,temp);                   /* CRT2 Horizontal Retrace Start */
#endif /* SIS300 */

 	} else {

#ifdef SIS315H  /* ------------------- 315/330 series --------------- */

	        tempcx = SiS_Pr->SiS_VGAHT;				       /* BTVGA2HT 0x08,0x09 */
		if(modeflag & HalfDCLK) {
		    if(SiS_Pr->SiS_VBType & VB_SISVB) {
		       tempcx >>= 1;
		    } else {
		       tempax = SiS_Pr->SiS_VGAHDE >> 1;
		       tempcx = SiS_Pr->SiS_HT - SiS_Pr->SiS_HDE + tempax;
		       if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
			  tempcx = SiS_Pr->SiS_HT - tempax;
		       }
		    }
		}
		tempcx--;

		temp = tempcx & 0xff;
		SiS_SetReg(SiS_Pr->SiS_Part1Port,0x08,temp);                  /* CRT2 Horizontal Total */

		temp = ((tempcx & 0xff00) >> 8) << 4;
		SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x09,0x0F,temp);         /* CRT2 Horizontal Total Overflow [7:4] */

		tempcx = SiS_Pr->SiS_VGAHT;				       /* BTVGA2HDEE 0x0A,0x0C */
		tempbx = SiS_Pr->SiS_VGAHDE;
		tempcx -= tempbx;
		tempcx >>= 2;
		if(modeflag & HalfDCLK) {
		   tempbx >>= 1;
		   tempcx >>= 1;
		}
		tempbx += 16;

		temp = tempbx & 0xff;
		SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0A,temp);                  /* CRT2 Horizontal Display Enable End */

		pushbx = tempbx;
		tempcx >>= 1;
		tempbx += tempcx;
		tempcx += tempbx;

		if(SiS_Pr->SiS_VBType & VB_SISVB) {

		   if(HwInfo->jChipType >= SIS_661) {
		      if((SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) ||
		         (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024)) {
			 if(resinfo == SIS_RI_1280x1024) {
		            tempcx = 0x30;
			 } else if(resinfo == SIS_RI_1600x1200) {
			    tempcx = 0xff;
			 }
		      }
		   }

		   if(SiS_Pr->UseCustomMode) {
		      tempbx = SiS_Pr->CHSyncStart + 16;
		      tempcx = SiS_Pr->CHSyncEnd + 16;
		      tempax = SiS_Pr->SiS_VGAHT;
		      if(modeflag & HalfDCLK) tempax >>= 1;
		      tempax--;
		      if(tempcx > tempax)  tempcx = tempax;
		   }

             	   if(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC) {
		      unsigned char cr4, cr14, cr5, cr15;
		      if(SiS_Pr->UseCustomMode) {
		         cr4  = SiS_Pr->CCRT1CRTC[4];
			 cr14 = SiS_Pr->CCRT1CRTC[14];
			 cr5  = SiS_Pr->CCRT1CRTC[5];
			 cr15 = SiS_Pr->CCRT1CRTC[15];
		      } else {
		         cr4  = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[4];
			 cr14 = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[14];
			 cr5  = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[5];
			 cr15 = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[15];
		      }
                      tempbx = ((cr4 | ((cr14 & 0xC0) << 2)) - 3) << 3; 		/* (VGAHRS-3)*8 */
                      tempcx = (((cr5 & 0x1f) | ((cr15 & 0x04) << (5-2))) - 3) << 3; 	/* (VGAHRE-3)*8 */
		      tempcx &= 0x00FF;
		      tempcx |= (tempbx & 0xFF00);
                      tempbx += 16;
                      tempcx += 16;
		      tempax = SiS_Pr->SiS_VGAHT;
		      if(modeflag & HalfDCLK) tempax >>= 1;
		      tempax--;
		      if(tempcx > tempax)  tempcx = tempax;
             	   }

		   if(SiS_Pr->SiS_TVMode & TVSetNTSC1024) {
      		      tempbx = 1040;
      		      tempcx = 1042;
      	     	   }

                }

		temp = tempbx & 0xff;
	 	SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0B,temp);                 /* CRT2 Horizontal Retrace Start */
#endif  /* SIS315H */

     	}  /* 315/330 series */

  	/* The following is done for all bridge/chip types/series */

  	tempax = tempbx & 0xFF00;
  	tempbx = pushbx;
  	tempbx = (tempbx & 0x00FF) | ((tempbx & 0xFF00) << 4);
  	tempax |= (tempbx & 0xFF00);
  	temp = (tempax & 0xFF00) >> 8;
  	SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0C,temp);                        /* Overflow */

  	temp = tempcx & 0x00FF;
  	SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0D,temp);                        /* CRT2 Horizontal Retrace End */

  	/* 2. Vertical setup */

  	tempcx = SiS_Pr->SiS_VGAVT - 1;
  	temp = tempcx & 0x00FF;

	if(HwInfo->jChipType < SIS_661) {
           if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
	      if(HwInfo->jChipType < SIS_315H) {
	         if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
	            if(SiS_Pr->SiS_VBInfo & (SetCRT2ToSVIDEO | SetCRT2ToAVIDEO)) {
	               temp--;
	            }
                 }
	      } else {
 	         temp--;
              }
           } else if(HwInfo->jChipType >= SIS_315H) {
	      temp--;
	   }
	}
  	SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0E,temp);                        /* CRT2 Vertical Total */

  	tempbx = SiS_Pr->SiS_VGAVDE - 1;
  	temp = tempbx & 0x00FF;
  	SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0F,temp);                        /* CRT2 Vertical Display Enable End */

  	temp = ((tempbx & 0xFF00) << 3) >> 8;
  	temp |= ((tempcx & 0xFF00) >> 8);
  	SiS_SetReg(SiS_Pr->SiS_Part1Port,0x12,temp);                        /* Overflow (and HWCursor Test Mode) */

	if((HwInfo->jChipType >= SIS_315H) && (HwInfo->jChipType < SIS_661)) {
           tempbx++;
   	   tempax = tempbx;
	   tempcx++;
	   tempcx -= tempax;
	   tempcx >>= 2;
	   tempbx += tempcx;
	   if(tempcx < 4) tempcx = 4;
	   tempcx >>= 2;
	   tempcx += tempbx;
	   tempcx++;
	} else {
  	   tempbx = (SiS_Pr->SiS_VGAVT + SiS_Pr->SiS_VGAVDE) >> 1;                 /*  BTVGA2VRS     0x10,0x11   */
  	   tempcx = ((SiS_Pr->SiS_VGAVT - SiS_Pr->SiS_VGAVDE) >> 4) + tempbx + 1;  /*  BTVGA2VRE     0x11        */
	}

  	if(SiS_Pr->SiS_VBType & VB_SISVB) {

	   if(SiS_Pr->UseCustomMode) {
	      tempbx = SiS_Pr->CVSyncStart;
	      tempcx = (tempcx & 0xFF00) | (SiS_Pr->CVSyncEnd & 0x00FF);
	   }

    	   if(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC) {
	      unsigned char cr8, cr7, cr13, cr9;
	      if(SiS_Pr->UseCustomMode) {
	         cr8  = SiS_Pr->CCRT1CRTC[8];
		 cr7  = SiS_Pr->CCRT1CRTC[7];
		 cr13 = SiS_Pr->CCRT1CRTC[13];
		 cr9  = SiS_Pr->CCRT1CRTC[9];
	      } else {
	         cr8  = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[8];
		 cr7  = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[7];
		 cr13 = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[13];
		 cr9  = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[9];
	      }
      	      tempbx = cr8;
      	      if(cr7 & 0x04)  tempbx |= 0x0100;
      	      if(cr7 & 0x80)  tempbx |= 0x0200;
      	      if(cr13 & 0x08) tempbx |= 0x0400;
      	      tempcx = (tempcx & 0xFF00) | (cr9 & 0x00FF);
    	   }
  	}
  	temp = tempbx & 0x00FF;
  	SiS_SetReg(SiS_Pr->SiS_Part1Port,0x10,temp);           /* CRT2 Vertical Retrace Start */

  	temp = ((tempbx & 0xFF00) >> 8) << 4;
  	temp |= (tempcx & 0x000F);
  	SiS_SetReg(SiS_Pr->SiS_Part1Port,0x11,temp);           /* CRT2 Vert. Retrace End; Overflow; "Enable CRTC Check" */

  	/* 3. Panel compensation delay */

  	if(HwInfo->jChipType < SIS_315H) {

#ifdef SIS300  /* ---------- 300 series -------------- */

	   if(SiS_Pr->SiS_VBType & VB_SISVB) {
	        temp = 0x20;

		if(HwInfo->jChipType == SIS_300) {
		   temp = 0x10;
		   if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)  temp = 0x2c;
		   if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) temp = 0x20;
		}
		if(SiS_Pr->SiS_VBType & VB_SIS301) {
		   if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) temp = 0x20;
		}
		if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x960)     temp = 0x24;
		if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_PanelCustom)       temp = 0x2c;
		if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) 		temp = 0x08;
		if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
      		   if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) 	temp = 0x2c;
      		   else 					temp = 0x20;
    	        }
		if((ROMAddr) && (SiS_Pr->SiS_UseROM)) {
		    if(ROMAddr[0x220] & 0x80) {
		        if(SiS_Pr->SiS_VBInfo & SetCRT2ToTVNoYPbPrHiVision)
				temp = ROMAddr[0x221];
			else if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision)
				temp = ROMAddr[0x222];
		        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024)
				temp = ROMAddr[0x223];
			else
				temp = ROMAddr[0x224];
			temp &= 0x3c;
		    }
		}
		if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
		   if(SiS_Pr->PDC) {
			temp = SiS_Pr->PDC & 0x3c;
		   }
		}
	   } else {
	        temp = 0x20;
		if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) {
		   if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480) temp = 0x04;
		}
		if((ROMAddr) && SiS_Pr->SiS_UseROM) {
		    if(ROMAddr[0x220] & 0x80) {
		        temp = ROMAddr[0x220] & 0x3c;
		    }
		}
		if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
		   if(SiS_Pr->PDC) {
		      temp = SiS_Pr->PDC & 0x3c;
		   }
		}
	   }

    	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,~0x03C,temp);         /* Panel Link Delay Compensation; (Software Command Reset; Power Saving) */

#endif  /* SIS300 */

  	} else {

#ifdef SIS315H   /* --------------- 315/330 series ---------------*/

   	   if(HwInfo->jChipType < SIS_661) {

	      if(SiS_Pr->SiS_VBType & VB_SISVB) {

                 temp = 0x10;
                 if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)  temp = 0x2c;
    	         if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) temp = 0x20;
    	         if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x960)  temp = 0x24;
		 if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_PanelCustom)    temp = 0x2c;
		 if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
		    temp = 0x08;
		    if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
		       if(SiS_Pr->SiS_VBInfo & SetInSlaveMode)  temp = 0x2c;
      		       else 					temp = 0x20;
		    }
		 }
		 if((SiS_Pr->SiS_VBType & VB_SIS301B302B) && (!(SiS_Pr->SiS_VBType & VB_NoLCD))) {
		    tempbl = 0x00;
		    if((ROMAddr) && (SiS_Pr->SiS_UseROM)) {
		       if(HwInfo->jChipType < SIS_330) {
		          if(ROMAddr[0x13c] & 0x80) tempbl = 0xf0;
		       } else {
		          if(ROMAddr[0x1bc] & 0x80) tempbl = 0xf0;
		       }
		    }
		 } else {  /* LV (550/301LV checks ROM byte, other LV BIOSes do not) */
		    tempbl = 0xF0;
		 }
		 if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD|SetCRT2ToLCDA)) {
		    if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
		       if(SiS_Pr->PDC) {
		          temp = SiS_Pr->PDC;
		          tempbl = 0;
		       }
		    }
		 }

	      } else {  /* LVDS */

	         if(HwInfo->jChipType == SIS_740) {
		    temp = 0x03;
	         } else {
		    temp = 0x00;
		 }
	 	 if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) temp = 0x0a;
		 tempbl = 0xF0;
		 if(HwInfo->jChipType == SIS_650) {
		    if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
		       if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) tempbl = 0x0F;
		    }
		 }

		 if(SiS_Pr->SiS_IF_DEF_DSTN || SiS_Pr->SiS_IF_DEF_FSTN) {
		    temp = 0x08;
		    tempbl = 0;
		    if((ROMAddr) && (SiS_Pr->SiS_UseROM)) {
		       if(ROMAddr[0x13c] & 0x80) tempbl = 0xf0;
		    }
		 }
	      }

	      SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2D,tempbl,temp);	    /* Panel Link Delay Compensation */

	   } /* < 661 */

    	   tempax = 0;
    	   if (modeflag & DoubleScanMode) tempax |= 0x80;
    	   if (modeflag & HalfDCLK)       tempax |= 0x40;
    	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2C,0x3f,tempax);

#endif  /* SIS315H */

  	}

     }  /* Slavemode */

     if(SiS_Pr->SiS_VBType & VB_SISVB) {

        /* For 301BDH with LCD, we set up the Panel Link */
        if( (SiS_Pr->SiS_VBType & VB_NoLCD) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) ) {

	    SiS_SetGroup1_LVDS(SiS_Pr, ModeNo, ModeIdIndex,
	                       HwInfo, RefreshRateTableIndex);

        } else if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {

    	    SiS_SetGroup1_301(SiS_Pr, ModeNo, ModeIdIndex,
	                      HwInfo, RefreshRateTableIndex);
        }

     } else {

        if(HwInfo->jChipType < SIS_315H) {

	   SiS_SetGroup1_LVDS(SiS_Pr, ModeNo, ModeIdIndex,
	                        HwInfo, RefreshRateTableIndex);
	} else {

	   if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
              if((!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) || (SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
    	          SiS_SetGroup1_LVDS(SiS_Pr, ModeNo,ModeIdIndex,
	                              HwInfo,RefreshRateTableIndex);
              }
	   } else {
	      SiS_SetGroup1_LVDS(SiS_Pr, ModeNo,ModeIdIndex,
	                         HwInfo,RefreshRateTableIndex);
	   }

	}

     }
  } /* LCDA */
}

/*********************************************/
/*         SET PART 2 REGISTER GROUP         */
/*********************************************/

#ifdef SIS315H
static UCHAR *
SiS_GetGroup2CLVXPtr(SiS_Private *SiS_Pr, int tabletype, PSIS_HW_INFO HwInfo)
{
   UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
   const UCHAR  *tableptr = NULL;
   USHORT a, b, p = 0;

   a = SiS_Pr->SiS_VGAHDE;
   b = SiS_Pr->SiS_HDE;
   if(tabletype) {
      a = SiS_Pr->SiS_VGAVDE;
      b = SiS_Pr->SiS_VDE;
   }

   if((HwInfo->jChipType >= SIS_661) && (ROMAddr = (UCHAR *)HwInfo->pjVirtualRomBase) && SiS_Pr->SiS_UseROM) {

      if(a < b) {
         p = ROMAddr[0x278] | (ROMAddr[0x279] << 8);
      } else if(a == b) {
         p = ROMAddr[0x27a] | (ROMAddr[0x27b] << 8);
      } else {
         if(SiS_Pr->SiS_TVMode & TVSetPAL) {
	    p = ROMAddr[0x27e] | (ROMAddr[0x27f] << 8);
	 } else {
	    p = ROMAddr[0x27c] | (ROMAddr[0x27d] << 8);
	 }
	 if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {
	    if(SiS_Pr->SiS_TVMode & TVSetYPbPr525i) 	 p = ROMAddr[0x280] | (ROMAddr[0x281] << 8);
	    else if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p) p = ROMAddr[0x282] | (ROMAddr[0x283] << 8);
	    else 				 	 p = ROMAddr[0x284] | (ROMAddr[0x285] << 8);
	 } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
	    p = ROMAddr[0x286] | (ROMAddr[0x287] << 8);
	 }
	 do {
	    if((ROMAddr[p] | ROMAddr[p+1] << 8) == a) break;
	    p += 0x42;
	 } while((ROMAddr[p] | ROMAddr[p+1] << 8) != 0xffff);
	 if((ROMAddr[p] | ROMAddr[p+1] << 8) == 0xffff) p -= 0x42;
      }
      p += 2;
      return(&ROMAddr[p]);

   } else {

      if(a < b) {
         tableptr = SiS_Part2CLVX_1;
      } else if(a == b) {
         tableptr = SiS_Part2CLVX_2;
      } else {
         if(SiS_Pr->SiS_TVMode & TVSetPAL) {
	    tableptr = SiS_Part2CLVX_4;
	 } else {
	    tableptr = SiS_Part2CLVX_3;
	 }
	 if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {
	    if(SiS_Pr->SiS_TVMode & TVSetYPbPr525i) 	 tableptr = SiS_Part2CLVX_3;
	    else if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p) tableptr = SiS_Part2CLVX_3;
	    else 				         tableptr = SiS_Part2CLVX_5;

	 } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
	    tableptr = SiS_Part2CLVX_6;
	 }
	 do {
	    if((tableptr[p] | tableptr[p+1] << 8) == a) break;
	    p += 0x42;
	 } while((tableptr[p] | tableptr[p+1] << 8) != 0xffff);
	 if((tableptr[p] | tableptr[p+1] << 8) == 0xffff) p -= 0x42;
      }
      p += 2;
      return((UCHAR *)&tableptr[p]);
   }
}


static void
SiS_SetGroup2_C_ELV(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
	      	    USHORT RefreshRateTableIndex, PSIS_HW_INFO HwInfo)
{
   UCHAR *tableptr;
   int i, j;
   UCHAR temp;

   if(!(SiS_Pr->SiS_VBType & (VB_SIS301C | VB_SIS302ELV))) return;

   tableptr = SiS_GetGroup2CLVXPtr(SiS_Pr, 0, HwInfo);
   for(i = 0x80, j = 0; i <= 0xbf; i++, j++) {
      SiS_SetReg(SiS_Pr->SiS_Part2Port, i, tableptr[j]);
   }
   if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
      tableptr = SiS_GetGroup2CLVXPtr(SiS_Pr, 1, HwInfo);
      for(i = 0xc0, j = 0; i <= 0xff; i++, j++) {
         SiS_SetReg(SiS_Pr->SiS_Part2Port, i, tableptr[j]);
      }
   }
   temp = 0x10;
   if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) temp |= 0x04;
   SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x4e,0xeb,temp);
}

static void
SiS_GetCRT2Part2Ptr(SiS_Private *SiS_Pr,USHORT ModeNo,USHORT ModeIdIndex,
		    USHORT RefreshRateTableIndex,USHORT *CRT2Index,
		    USHORT *ResIndex,PSIS_HW_INFO HwInfo)
{
  USHORT tempbx,tempal;

  if(ModeNo <= 0x13)
      	tempal = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  else
      	tempal = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;

  tempbx = SiS_Pr->SiS_LCDResInfo;

  if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)      tempbx += 16;
  else if(SiS_Pr->SiS_SetFlag & LCDVESATiming) tempbx += 32;

  if(SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) {
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
        tempbx = 100;
        if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)      tempbx = 101;
  	else if(SiS_Pr->SiS_SetFlag & LCDVESATiming) tempbx = 102;
     }
  } else if(SiS_Pr->SiS_CustomT == CUT_CLEVO1024) {
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
        if(SiS_IsDualLink(SiS_Pr, HwInfo)) {
           tempbx = 103;
           if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)      tempbx = 104;
  	   else if(SiS_Pr->SiS_SetFlag & LCDVESATiming) tempbx = 105;
	}
     }
  } else if(SiS_Pr->SiS_CustomT == CUT_ASUSA2H_2) {
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
        if(SiS_Pr->SiS_SetFlag & LCDVESATiming) tempbx = 106;
     }
  }

  *CRT2Index = tempbx;
  *ResIndex = tempal & 0x3F;
}
#endif

#ifdef SIS300
/* For ECS A907. Highly preliminary. */
static void
SiS_Set300Part2Regs(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
    		    USHORT ModeIdIndex, USHORT RefreshRateTableIndex,
		    USHORT ModeNo)
{
  USHORT crt2crtc, resindex;
  int    i,j;
  const  SiS_Part2PortTblStruct *CRT2Part2Ptr = NULL;

  if(HwInfo->jChipType != SIS_300) return;
  if(!(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)) return;
  if(SiS_Pr->UseCustomMode) return;

  if(ModeNo <= 0x13) {
     crt2crtc = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
     crt2crtc = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
  }

  resindex = crt2crtc & 0x3F;
  if(SiS_Pr->SiS_SetFlag & LCDVESATiming) CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1024x768_1;
  else                                    CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1024x768_2;

  /* The BIOS code (1.16.51,56) is obviously a fragment! */
  if(ModeNo > 0x13) {
     CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1024x768_1;
     resindex = 4;
  }

  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x01,0x80,(CRT2Part2Ptr+resindex)->CR[0]);
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x02,0x80,(CRT2Part2Ptr+resindex)->CR[1]);
  for(i = 2, j = 0x04; j <= 0x06; i++, j++ ) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
  }
  for(j = 0x1c; j <= 0x1d; i++, j++ ) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
  }
  for(j = 0x1f; j <= 0x21; i++, j++ ) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
  }
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x23,(CRT2Part2Ptr+resindex)->CR[10]);
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x25,0x0f,(CRT2Part2Ptr+resindex)->CR[11]);
}
#endif

static void
SiS_SetTVSpecial(SiS_Private *SiS_Pr, USHORT ModeNo)
{
  if(!(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)) return;
  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTVNoHiVision)) return;
  if(SiS_Pr->SiS_TVMode & (TVSetYPbPr525p | TVSetYPbPr750p)) return;

  if(!(SiS_Pr->SiS_TVMode & TVSetPAL)) {
     if(SiS_Pr->SiS_TVMode & TVSetNTSC1024) {
        const UCHAR specialtv[] = {
		0xa7,0x07,0xf2,0x6e,0x17,0x8b,0x73,0x53,
		0x13,0x40,0x34,0xf4,0x63,0xbb,0xcc,0x7a,
		0x58,0xe4,0x73,0xda,0x13
	};
	int i, j;
	for(i = 0x1c, j = 0; i <= 0x30; i++, j++) {
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,i,specialtv[j]);
	}
	SiS_SetReg(SiS_Pr->SiS_Part2Port,0x43,0x72);
	if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750)) {
	   if(SiS_Pr->SiS_TVMode & TVSetPALM) {
	      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,0x14);
	   } else {
	      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,0x15);
	   }
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,0x02,0x1b);
	}
     }
  } else {
     SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,0x21);
     SiS_SetReg(SiS_Pr->SiS_Part2Port,0x02,0x5a);
  }
}

static void
SiS_SetGroup2(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,USHORT RefreshRateTableIndex,
	      PSIS_HW_INFO HwInfo)
{
  USHORT      i, j, tempax, tempbx, tempcx, temp;
  USHORT      push1, push2, modeflag, crt2crtc;
  ULONG       longtemp, tempeax;
  const       UCHAR *PhasePoint;
  const       UCHAR *TimingPoint;
#ifdef SIS315H
  USHORT      resindex, CRT2Index;
  const       SiS_Part2PortTblStruct *CRT2Part2Ptr = NULL;
#endif
#ifdef SIS300
  const UCHAR atable[] = {
       0xc3,0x9e,0xc3,0x9e,0x02,0x02,0x02,
       0xab,0x87,0xab,0x9e,0xe7,0x02,0x02
  };
#endif

#ifdef SIS315H   
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) return;
#endif

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     crt2crtc = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
     if(SiS_Pr->UseCustomMode) {
        modeflag = SiS_Pr->CModeFlag;
	crt2crtc = 0;
     } else {
        modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    	crt2crtc = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
     }
  }

  temp = 0;
  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToAVIDEO)) temp |= 0x08;
  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToSVIDEO)) temp |= 0x04;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToSCART)     temp |= 0x02;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision)  temp |= 0x01;

  if(!(SiS_Pr->SiS_TVMode & TVSetPAL)) 	      temp |= 0x10;

  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x00,temp);

  PhasePoint  = SiS_Pr->SiS_PALPhase;
  TimingPoint = SiS_Pr->SiS_PALTiming;

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {

     TimingPoint = SiS_Pr->SiS_HiTVExtTiming;
     if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
        TimingPoint = SiS_Pr->SiS_HiTVSt2Timing;
        if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode) {
	   TimingPoint = SiS_Pr->SiS_HiTVSt1Timing;
#if 0
           if(!(modeflag & Charx8Dot))  TimingPoint = SiS_Pr->SiS_HiTVTextTiming;
#endif
        }
     }

  } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {

     if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p)      TimingPoint = &SiS_YPbPrTable[2][0];
     else if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p) TimingPoint = &SiS_YPbPrTable[1][0];
     else					  TimingPoint = &SiS_YPbPrTable[0][0];

     PhasePoint = SiS_Pr->SiS_NTSCPhase;

  } else if(SiS_Pr->SiS_TVMode & TVSetPAL) {

     if( (SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) &&
         ( (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) ||
	   (SiS_Pr->SiS_TVMode & TVSetTVSimuMode) ) ) {
        PhasePoint = SiS_Pr->SiS_PALPhase2;
     }

  } else {

     TimingPoint = SiS_Pr->SiS_NTSCTiming;
     PhasePoint  = SiS_Pr->SiS_NTSCPhase;
     if(SiS_Pr->SiS_TVMode & TVSetNTSCJ) {
	PhasePoint = SiS_Pr->SiS_PALPhase;
     }

     if( (SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) &&
	 ( (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) ||
	   (SiS_Pr->SiS_TVMode & TVSetTVSimuMode) ) ) {
        PhasePoint = SiS_Pr->SiS_NTSCPhase2;
	if(SiS_Pr->SiS_TVMode & TVSetNTSCJ) {
	   PhasePoint = SiS_Pr->SiS_PALPhase2;
	}
     }

  }

  if(SiS_Pr->SiS_TVMode & TVSetPALM) {
     PhasePoint = SiS_Pr->SiS_PALMPhase;
     if( (SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) &&
	 ( (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) ||
	   (SiS_Pr->SiS_TVMode & TVSetTVSimuMode) ) ) {
        PhasePoint = SiS_Pr->SiS_PALMPhase2;
     }
  }

  if(SiS_Pr->SiS_TVMode & TVSetPALN) {
     PhasePoint = SiS_Pr->SiS_PALNPhase;
     if( (SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) &&
	 ( (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) ||
	   (SiS_Pr->SiS_TVMode & TVSetTVSimuMode) ) ) {
	PhasePoint = SiS_Pr->SiS_PALNPhase2;
     }
  }

  if(SiS_Pr->SiS_TVMode & TVSetNTSC1024) {
     PhasePoint = SiS_Pr->SiS_SpecialPhase;
     if(SiS_Pr->SiS_TVMode & TVSetPALM) {
        PhasePoint = SiS_Pr->SiS_SpecialPhaseM;
     } else if(SiS_Pr->SiS_TVMode & TVSetNTSCJ) {
        PhasePoint = SiS_Pr->SiS_SpecialPhaseJ;
     }
  }

  for(i=0x31, j=0; i<=0x34; i++, j++) {
     SiS_SetReg(SiS_Pr->SiS_Part2Port,i,PhasePoint[j]);
  }

  for(i=0x01, j=0; i<=0x2D; i++, j++) {
     SiS_SetReg(SiS_Pr->SiS_Part2Port,i,TimingPoint[j]);
  }
  for(i=0x39; i<=0x45; i++, j++) {
     SiS_SetReg(SiS_Pr->SiS_Part2Port,i,TimingPoint[j]);
  }

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
     if(SiS_Pr->SiS_ModeType != ModeText) {
        SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x3A,0x1F);
     }
  }

  SiS_SetRegOR(SiS_Pr->SiS_Part2Port,0x0A,SiS_Pr->SiS_NewFlickerMode);

  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x35,SiS_Pr->SiS_RY1COE);
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x36,SiS_Pr->SiS_RY2COE);
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x37,SiS_Pr->SiS_RY3COE);
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x38,SiS_Pr->SiS_RY4COE);

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) 	tempax = 950;
  else if(SiS_Pr->SiS_TVMode & TVSetPAL)      	tempax = 520;
  else 			            		tempax = 440;

  if( ( (!(SiS_Pr->SiS_VBInfo & SetCRT2ToTVNoHiVision)) && (SiS_Pr->SiS_VDE <= tempax) ) ||
      ( (SiS_Pr->SiS_VBInfo & SetCRT2ToTVNoHiVision) &&
        ((SiS_Pr->SiS_VGAHDE == 1024) || (SiS_Pr->SiS_VDE <= tempax)) ) ) {

     tempax -= SiS_Pr->SiS_VDE;
     tempax >>= 2;
     tempax &= 0x00ff;

     temp = tempax + (USHORT)TimingPoint[0];
     SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,temp);

     temp = tempax + (USHORT)TimingPoint[1];
     SiS_SetReg(SiS_Pr->SiS_Part2Port,0x02,temp);

     if((SiS_Pr->SiS_VBInfo & SetCRT2ToTVNoHiVision) && (SiS_Pr->SiS_VGAHDE >= 1024)) {
        if(SiS_Pr->SiS_TVMode & TVSetPAL) {
           SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,0x19);
           SiS_SetReg(SiS_Pr->SiS_Part2Port,0x02,0x52);
        } else {
           SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,0x17);
           SiS_SetReg(SiS_Pr->SiS_Part2Port,0x02,0x1d);
        }
     }

  }

  tempcx = SiS_Pr->SiS_HT;
  if(SiS_IsDualLink(SiS_Pr, HwInfo)) tempcx >>= 1;
  tempcx--;
  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) tempcx--;
  temp = tempcx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x1B,temp);
  temp = (tempcx & 0xFF00) >> 8;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x1D,0xF0,temp);

  tempcx++;
  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) tempcx++;
  tempcx >>= 1;

  push1 = tempcx;

  tempcx += 7;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) tempcx -= 4;
  temp = (tempcx & 0x00FF) << 4;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x22,0x0F,temp);

  tempbx = TimingPoint[j] | ((TimingPoint[j+1]) << 8);
  tempbx += tempcx;

  temp = tempbx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x24,temp);
  temp = ((tempbx & 0xFF00) >> 8) << 4;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x25,0x0F,temp);

  tempbx += 8;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
     tempbx -= 4;
     tempcx = tempbx;
  }
  temp = (tempbx & 0x00FF) << 4;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x29,0x0F,temp);

  j += 2;
  tempcx += ((TimingPoint[j] | ((TimingPoint[j+1]) << 8)));
  temp = tempcx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x27,temp);
  temp = ((tempcx & 0xFF00) >> 8) << 4;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x28,0x0F,temp);

  tempcx += 8;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) tempcx -= 4;
  temp = (tempcx & 0x00FF) << 4;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x2A,0x0F,temp);

  tempcx = push1;

  j += 2;
  tempcx -= (TimingPoint[j] | ((TimingPoint[j+1]) << 8));
  temp = (tempcx & 0x00FF) << 4;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x2D,0x0F,temp);

  tempcx -= 11;
  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) {
     tempcx = SiS_GetVGAHT2(SiS_Pr) - 1;
  }
  temp = tempcx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x2E,temp);

  tempbx = SiS_Pr->SiS_VDE;
  if(SiS_Pr->SiS_VGAVDE == 360) tempbx = 746;
  if(SiS_Pr->SiS_VGAVDE == 375) tempbx = 746;
  if(SiS_Pr->SiS_VGAVDE == 405) tempbx = 853;
  if(HwInfo->jChipType < SIS_315H) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) tempbx >>= 1;
  } else {
     if( (SiS_Pr->SiS_VBInfo & SetCRT2ToTV) &&
         (!(SiS_Pr->SiS_TVMode & (TVSetYPbPr525p|TVSetYPbPr750p))) ) {
	tempbx >>= 1;
	if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode) {
	   if(ModeNo <= 0x13) {
	      if(crt2crtc == 1) tempbx++;
	   }
	} else if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
	   if(crt2crtc == 4) {
              if(SiS_Pr->SiS_ModeType <= 3) tempbx++;
	   }
	}
     }
  }
  tempbx -= 2;
  temp = tempbx & 0x00FF;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
     if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
        if((ModeNo == 0x2f) || (ModeNo == 0x5d) || (ModeNo == 0x5e)) temp++;
     }
  }

  if(HwInfo->jChipType < SIS_661) {
     /* From 1.10.7w - doesn't make sense */
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
        if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
           if(!(SiS_Pr->SiS_TVMode & TVSetPAL)) {
	      if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) {   /* SetFlag?? */
	         if(ModeNo == 0x03) temp++;
	      }
	   }
        }
     }
  }
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x2F,temp);

  temp = (tempcx >> 8) & 0x0F;
  temp |= (((tempbx >> 8) << 6) & 0xC0);
  if(!(SiS_Pr->SiS_VBInfo & (SetCRT2ToHiVision | SetCRT2ToYPbPr525750 | SetCRT2ToSCART))) {
     temp |= 0x10;
     if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToSVIDEO)) temp |= 0x20;
  }
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x30,temp);

  if((HwInfo->jChipType > SIS_315H) && (HwInfo->jChipType < SIS_661)) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
        if(SiS_Pr->SiS_VBType & (VB_SIS301C | VB_SIS302LV | VB_SIS302ELV)) {
           if( (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) ||
               (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) ) {
              SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x10,0x60);
	   }
        }
     }
  }

  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
     tempbx = SiS_Pr->SiS_VDE;
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
        if(!(SiS_Pr->SiS_TVMode & (TVSetYPbPr525p | TVSetYPbPr750p))) {
           tempbx >>= 1;
	}
     }
     tempbx -= 3;
     if(HwInfo->jChipType >= SIS_661) {
        if(SiS_Pr->SiS_VBType & (VB_SIS301C | VB_SIS302LV | VB_SIS302ELV)) {  /* Why not 301B/LV? */
           temp = 0;
	   if(tempcx & 0x0400) temp |= 0x20;
	   if(tempbx & 0x0400) temp |= 0x40;
	   SiS_SetReg(SiS_Pr->SiS_Part4Port,0x10,temp);
	}
     }
     tempbx &= 0x03ff;
     temp = ((tempbx & 0xFF00) >> 8) << 5;
     temp |= 0x18;
     SiS_SetReg(SiS_Pr->SiS_Part2Port,0x46,temp);
     temp = tempbx & 0x00FF;
     SiS_SetReg(SiS_Pr->SiS_Part2Port,0x47,temp);

  }

  tempbx = 0;
  if(!(modeflag & HalfDCLK)) {
     if(SiS_Pr->SiS_VGAHDE >= SiS_Pr->SiS_HDE) {
        tempax = 0;
        tempbx |= 0x2000;
     }
  }

  tempcx = 0x0101;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
     if(SiS_Pr->SiS_VGAHDE >= 1024) {
        if((!(modeflag & HalfDCLK)) || (HwInfo->jChipType < SIS_315H)) {
           tempcx = 0x1920;
           if(SiS_Pr->SiS_VGAHDE >= 1280) {
              tempcx = 0x1420;
              tempbx &= ~0x2000;
           }
        }
     }
  }

  if(!(tempbx & 0x2000)) {
     if(modeflag & HalfDCLK) {
        tempcx = (tempcx & 0xFF00) | ((tempcx << 1) & 0x00FF);
     }
     longtemp = (SiS_Pr->SiS_VGAHDE * ((tempcx & 0xFF00) >> 8)) / (tempcx & 0x00FF);
     longtemp <<= 13;
     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
     	longtemp <<= 3;
     }
     tempeax = longtemp / SiS_Pr->SiS_HDE;
     if(longtemp % SiS_Pr->SiS_HDE) tempeax++;
     tempax = (USHORT)tempeax;
     tempbx |= (tempax & 0x1F00);
     tempcx = (tempax & 0xFF00) >> (8 + 5);
  }

  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x44,tempax);
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x45,0xC0,(tempbx >> 8));

  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {

     temp = tempcx & 0x0007;
     if(tempbx & 0x2000) temp = 0;
     if((HwInfo->jChipType < SIS_661) || (!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD))) {
        temp |= 0x18;
     }
     SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x46,0xE0,temp);

     if(SiS_Pr->SiS_TVMode & TVSetPAL) {
        tempbx = 0x0382;
        tempcx = 0x007e;
     } else {
        tempbx = 0x0369;
        tempcx = 0x0061;
     }
     temp = (tempbx & 0x00FF) ;
     SiS_SetReg(SiS_Pr->SiS_Part2Port,0x4B,temp);
     temp = (tempcx & 0x00FF) ;
     SiS_SetReg(SiS_Pr->SiS_Part2Port,0x4C,temp);
     temp = (tempcx & 0x0300) >> (8 - 2);
     temp |= ((tempbx >> 8) & 0x03);
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {
        temp |= 0x10;
	if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p)      temp |= 0x20;
	else if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p) temp |= 0x40;
     }
     SiS_SetReg(SiS_Pr->SiS_Part2Port,0x4D,temp);

     temp = SiS_GetReg(SiS_Pr->SiS_Part2Port,0x43);
     SiS_SetReg(SiS_Pr->SiS_Part2Port,0x43,(USHORT)(temp - 3));

     SiS_SetTVSpecial(SiS_Pr, ModeNo);

     if(SiS_Pr->SiS_VBType & VB_SIS301C) {
        temp = 0;
        if(SiS_Pr->SiS_TVMode & TVSetPALM) temp = 8;
        SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x4e,0xf7,temp);
     }

  }

  if(SiS_Pr->SiS_TVMode & TVSetPALM) {
     if(!(SiS_Pr->SiS_TVMode & TVSetNTSC1024)) {
        temp = SiS_GetReg(SiS_Pr->SiS_Part2Port,0x01);
        SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,temp - 1);
     }
     SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x00,0xEF);
  }

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
     if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,0x0B,0x00);
     }
  }

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) return;

  /* From here: Part2 LCD setup */

  tempbx = SiS_Pr->SiS_HDE;
  if(SiS_IsDualLink(SiS_Pr, HwInfo)) tempbx >>= 1;
  tempbx--;			         	/* RHACTE = HDE - 1 */
  temp = tempbx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x2C,temp);
  temp = (tempbx & 0xFF00) >> 4;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x2B,0x0F,temp);

  temp = 0x01;
  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
     if(SiS_Pr->SiS_ModeType == ModeEGA) {
        if(SiS_Pr->SiS_VGAHDE >= 1024) {
           temp = 0x02;
	   if(HwInfo->jChipType >= SIS_315H) {
              if(SiS_Pr->SiS_SetFlag & LCDVESATiming) {
                 temp = 0x01;
	      }
	   }
        }
     }
  }
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x0B,temp);

  tempbx = SiS_Pr->SiS_VDE - 1;
  temp = tempbx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x03,temp);
  temp = ((tempbx & 0xFF00) >> 8) & 0x07;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x0C,0xF8,temp);

  tempcx = SiS_Pr->SiS_VT - 1;
  temp = tempcx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x19,temp);

  temp = ((tempcx & 0xFF00) >> 8) << 5;

  /* Enable dithering; only do this for 32bpp mode */
  if(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit) {
     if(SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00) & 0x01) {
        temp |= 0x10;
     }
  }

  /* Must do special for Compaq1280; Acer1280 OK, Clevo1400 OK, COMPAL1400 OK */
  /* Compaq1280 panel loses sync if using CR37 sync info. */
  if((SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) {
     if((HwInfo->jChipType >= SIS_315H) && (SiS_Pr->SiS_VBType & VB_SIS301LV302LV)) {
	if((SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) &&
	   (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024)) {
	   if(SiS_Pr->SiS_LCDInfo & LCDSync) {
	      temp |= ((SiS_Pr->SiS_LCDInfo & 0xc0) >> 6);
	   }
	} else if((SiS_Pr->SiS_CustomT == CUT_CLEVO1400) &&
	          (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050)) {
	   temp |= 0x03;
	} else {
           temp |= (SiS_GetReg(SiS_Pr->SiS_P3d4,0x37) >> 6);
	   temp |= 0x08;
	   if(!(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit)) temp |= 0x04;
	}
     } else {
        if(SiS_Pr->SiS_LCDInfo & LCDSync) {
	   temp |= ((SiS_Pr->SiS_LCDInfo & 0xc0) >> 6);
	}
     }
  }

  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x1A,temp);

  SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x09,0xF0);
  SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x0A,0xF0);

  SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x17,0xFB);
  SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x18,0xDF);

#if 0  /* Use the 315/330 series code for now */
  if((HwInfo->jChipType >= SIS_661)          &&
     (SiS_Pr->SiS_VBType & VB_SIS301LV302LV) &&
     (ROMAddr && SiS_Pr->SiS_UseROM)) {

      /* This is done for the LVDS bridges only, since
       * the TMDS panels already work correctly with
       * the old code. Besides, we only do that if
       * we can get the data from the ROM, I am tired
       * of carrying a lot of tables around.
       */

#ifdef SIS315H 							/* ------------ 661/741/760 series --------- */
      UCHAR *myptr = NULL, myptr1 = NULL;

      myptr = (UCHAR *)GetLCDPtr661(SiS_Pr, HwInfo, 6, ModeNo, ModeIdIndex, RefreshRateTableIndex);
      myptr1 = (UCHAR *)GetLCDStructPtr661(SiS_Pr, HwInfo);

      tempbx = (myptr[3] | (myptr[4] << 8)) & 0x0fff;
      tempcx = SiS_Pr->PanelYRes;
      if(SiS_Pr->SiS_LCDInfo & LCDPass11) {
         tempcx = SiS_Pr->SiS_VDE;
      }

      tempcx += tempbx;
      if(tempcx >= SiS_Pr->SiS_VT) tempcx -= SiS_Pr->SiS_VT;

      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x05,tempbx);
      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x06,tempcx);

      tempcx &= 0x07ff;
      tempbx &= 0x07ff;
      temp = (tempcx >> 8) << 3;
      temp |= (tempbx >> 8);
      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x02,temp);

      tempbx = (myptr[4] | (myptr[5] << 8)) >> 4;
      tempcx = myptr1[6];
      if(SiS_Pr->SiS_LCDInfo & LCDPass11) tempcx = myptr[7];

      tempcx += tempbx;
      if(tempcx >= SiS_Pr->SiS_VT) tempcx -= SiS_Pr->SiS_VT;

      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x04,tempbx);
      temp = tempcx & 0x000f;
      temp |= ((tempbx & 0x0f00) >> 4);
      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,temp);

      tempax = SiS_Pr->SiS_HT;
      tempbx = (myptr[0] | (myptr[1] << 8)) & 0x0fff;
      tempcx = SiS_Pr->PanelXRes;
      if(SiS_Pr->SiS_LCDInfo & LCDPass11) tempcx = SiS_Pr->SiS_HDE;

      if(SiS_IsDualLink(SiS_Pr, HwInfo)) {
         tempax >>= 1;
	 tempbx >>= 1;
	 tempcx >>= 1;
      }
      if(SiS_Pr->SiS_VBType & VB_SIS302LV)                 tempbx++;
      if(SiS_Pr->SiS_VBType & (VB_SIS301C | VB_SIS302ELV)) tempbx++;

      tempcx += tempbx;
      if(tempcx >= tempax) tempcx -= tempax;

      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x1f,tempbx);
      temp = ((tempbx & 0xff00) >> 8) << 4;
      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x20,temp);
      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x23,tempcx);
      temp = tempcx >> 8;
      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x25,temp);

      tempax = SiS_Pr->SiS_HT;
      tempbx = (myptr[1] | (myptr[2] << 8)) >> 4;
      tempcx = myptr1[5];
      if(SiS_Pr->SiS_LCDInfo & LCDPass11) {
         tempcx = myptr[6];
      }
      if(SiS_IsDualLink(SiS_Pr, HwInfo)) {
         tempax >>= 1;
	 tempbx >>= 1;
	 tempcx >>= 1;
      }
      if(SiS_Pr->SiS_VBType & VB_SIS302LV) tempbx++;

      tempcx += tempbx;
      if(tempcx >= tempax) tempcx -= tempax;

      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x1c,tempbx);
      temp = (tempbx & 0x0f00) >> 4;
      SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x1d,0x0f,temp);
      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x21,tempcx);

      if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) {
         if(SiS_Pr->SiS_VGAVDE == 525) {
	    temp = 0xc3;
	    if(SiS_Pr->SiS_ModeType <= ModeVGA) {
	       temp++;
	       if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) temp += 2;
	    }
	    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x2f,temp);
	    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x30,0xb3);
	 } else if(SiS_Pr->SiS_VGAVDE == 420) {
	    temp = 0x4d;
	    if(SiS_Pr->SiS_ModeType <= ModeVGA) {
	       temp++;
	       if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) temp++;
	    }
	    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x2f,temp);
	 }
      }

#endif

  } else
#endif
         if((HwInfo->jChipType >= SIS_315H)                    &&
            (SiS_Pr->SiS_VBType & VB_SIS301LV302LV)            &&
            ((SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)  ||
             (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) ||
             (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) ||
             (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200)) ) {

#ifdef SIS315H 							/* ------------- 315/330 series ------------ */

      SiS_GetCRT2Part2Ptr(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex,
                          &CRT2Index, &resindex, HwInfo);

      switch(CRT2Index) {
        case Panel_1024x768      : CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1024x768_1;  break;  /* "Normal" */
        case Panel_1280x1024     : CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1280x1024_1; break;
	case Panel_1400x1050     : CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1400x1050_1; break;
	case Panel_1600x1200     : CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1600x1200_1; break;
        case Panel_1024x768  + 16: CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1024x768_2;  break;  /* Non-Expanding */
        case Panel_1280x1024 + 16: CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1280x1024_2; break;
	case Panel_1400x1050 + 16: CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1400x1050_2; break;
	case Panel_1600x1200 + 16: CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1600x1200_2; break;
        case Panel_1024x768  + 32: CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1024x768_3;  break;  /* VESA Timing */
        case Panel_1280x1024 + 32: CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1280x1024_3; break;
	case Panel_1400x1050 + 32: CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1400x1050_3; break;
	case Panel_1600x1200 + 32: CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1600x1200_3; break;
	case 100:		   CRT2Part2Ptr = (SiS_Part2PortTblStruct *)SiS310_CRT2Part2_Compaq1280x1024_1; break;  /* Custom */
	case 101:		   CRT2Part2Ptr = (SiS_Part2PortTblStruct *)SiS310_CRT2Part2_Compaq1280x1024_2; break;
	case 102:		   CRT2Part2Ptr = (SiS_Part2PortTblStruct *)SiS310_CRT2Part2_Compaq1280x1024_3; break;
	case 103:		   CRT2Part2Ptr = (SiS_Part2PortTblStruct *)SiS310_CRT2Part2_Clevo1024x768_1; break;    /* Custom */
	case 104:		   CRT2Part2Ptr = (SiS_Part2PortTblStruct *)SiS310_CRT2Part2_Clevo1024x768_2; break;
	case 105:		   CRT2Part2Ptr = (SiS_Part2PortTblStruct *)SiS310_CRT2Part2_Clevo1024x768_3; break;
	case 106:		   CRT2Part2Ptr = (SiS_Part2PortTblStruct *)SiS310_CRT2Part2_Asus1024x768_3; break;
	default:                   CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1024x768_3;  break;
      }

      SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x01,0x80,(CRT2Part2Ptr+resindex)->CR[0]);
      SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x02,0x80,(CRT2Part2Ptr+resindex)->CR[1]);
      for(i = 2, j = 0x04; j <= 0x06; i++, j++ ) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
      }
      for(j = 0x1c; j <= 0x1d; i++, j++ ) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
      }
      for(j = 0x1f; j <= 0x21; i++, j++ ) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
      }
      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x23,(CRT2Part2Ptr+resindex)->CR[10]);
      SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x25,0x0f,(CRT2Part2Ptr+resindex)->CR[11]);

      if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) {
        if(SiS_Pr->SiS_VGAVDE == 525) {
	  temp = 0xc3;
	  if(SiS_Pr->SiS_ModeType <= ModeVGA) {
	     temp++;
	     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) temp += 2;
	  }
	  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x2f,temp);
	  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x30,0xb3);
	} else if(SiS_Pr->SiS_VGAVDE == 420) {
	  temp = 0x4d;
	  if(SiS_Pr->SiS_ModeType <= ModeVGA) {
	     temp++;
	     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) temp++;
	  }
	  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x2f,temp);
	}
     }

     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
	   /* See Sync above, 0x1a */
	   temp = 1;
	   if(ModeNo <= 0x13) temp = 3;
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,0x0b,temp);
	}
     }
#endif

  } else {   /* ------ 300 series and other bridges, other LCD resolutions ------ */

      /* Using this on the 301B with an auto-expanding 1024 panel (CR37=1) makes
       * the panel scale at modes < 1024 (no black bars); if the panel is non-expanding,
       * the bridge scales all modes to 1024.
       * !!! Malfunction at 640x480 and 640x400 when panel is auto-expanding - black screen !!!
       */

    tempcx = SiS_Pr->SiS_VT;
    tempbx = SiS_Pr->PanelYRes;

    if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
       tempbx = SiS_Pr->SiS_VDE - 1;
       tempcx--;
    }

    tempax = 1;
    if(!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {
       if(tempbx != SiS_Pr->SiS_VDE) {
          tempax = tempbx;
          if(tempax < SiS_Pr->SiS_VDE) {
             tempax = 0;
             tempcx = 0;
          } else {
             tempax -= SiS_Pr->SiS_VDE;
          }
          tempax >>= 1;
       }
       tempcx -= tempax; /* lcdvdes */
       tempbx -= tempax; /* lcdvdee */
    }

    /* Non-expanding: lcdvdees = tempcx = VT-1; lcdvdee = tempbx = VDE-1 */

#ifdef TWDEBUG
    xf86DrvMsg(0, X_INFO, "lcdvdes 0x%x lcdvdee 0x%x\n", tempcx, tempbx);
#endif

    temp = tempcx & 0x00FF;   				/* RVEQ1EQ=lcdvdes */
    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x05,temp);
    temp = tempbx & 0x00FF;   				/* RVEQ2EQ=lcdvdee */
    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x06,temp);

    temp = ((tempbx & 0xFF00) >> 8) << 3;
    temp |= ((tempcx & 0xFF00) >> 8);
    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x02,temp);

    tempbx = SiS_Pr->SiS_VT;    /* push2; */
    tempax = SiS_Pr->SiS_VDE;   /* push1; */
    tempcx = (tempbx - tempax) >> 4;
    tempbx += tempax;
    tempbx >>= 1;
    if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx -= 10;

    /* non-expanding: lcdvrs = tempbx = ((VT + VDE) / 2) - 10 */

    if(SiS_Pr->UseCustomMode) {
       tempbx = SiS_Pr->CVSyncStart;
    }

#ifdef TWDEBUG
    xf86DrvMsg(0, X_INFO, "lcdvrs 0x%x\n", tempbx);
#endif

    temp = tempbx & 0x00FF;   				/* RTVACTEE = lcdvrs */
    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x04,temp);

    temp = ((tempbx & 0xFF00) >> 8) << 4;
    tempbx += (tempcx + 1);
    temp |= (tempbx & 0x000F);

    if(SiS_Pr->UseCustomMode) {
       temp &= 0xf0;
       temp |= (SiS_Pr->CVSyncEnd & 0x0f);
    }

#ifdef TWDEBUG
    xf86DrvMsg(0, X_INFO, "lcdvre[3:0] 0x%x\n", (temp & 0x0f));
#endif

    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,temp);

    /* Code from 630/301B (I+II) BIOS */

#ifdef SIS300
    if(!SiS_Pr->UseCustomMode) {
       if( ( ( (HwInfo->jChipType == SIS_630) ||
               (HwInfo->jChipType == SIS_730) ) &&
             (HwInfo->jChipRevision > 2) )  &&
           (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) &&
           (!(SiS_Pr->SiS_SetFlag & LCDVESATiming))  &&
           (!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) ) {
          if(ModeNo == 0x13) {
             SiS_SetReg(SiS_Pr->SiS_Part2Port,0x04,0xB9);
             SiS_SetReg(SiS_Pr->SiS_Part2Port,0x05,0xCC);
             SiS_SetReg(SiS_Pr->SiS_Part2Port,0x06,0xA6);
          } else {
             if((crt2crtc & 0x3F) == 4) {
                SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,0x2B);
                SiS_SetReg(SiS_Pr->SiS_Part2Port,0x02,0x13);
                SiS_SetReg(SiS_Pr->SiS_Part2Port,0x04,0xE5);
                SiS_SetReg(SiS_Pr->SiS_Part2Port,0x05,0x08);
                SiS_SetReg(SiS_Pr->SiS_Part2Port,0x06,0xE2);
             }
          }
       }

       if(HwInfo->jChipType < SIS_315H) {
          if(SiS_Pr->SiS_LCDTypeInfo == 0x0c) {
             crt2crtc &= 0x1f;
             tempcx = 0;
             if(!(SiS_Pr->SiS_VBInfo & SetNotSimuMode)) {
                if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
                   tempcx += 7;
                }
             }
             tempcx += crt2crtc;
             if(crt2crtc >= 4) {
                SiS_SetReg(SiS_Pr->SiS_Part2Port,0x06,0xff);
             }

             if(!(SiS_Pr->SiS_VBInfo & SetNotSimuMode)) {
                if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
                   if(crt2crtc == 4) {
                      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,0x28);
                   }
                }
             }
             SiS_SetReg(SiS_Pr->SiS_Part2Port,0x02,0x18);
             SiS_SetReg(SiS_Pr->SiS_Part2Port,0x04,atable[tempcx]);
          }
       }
    }
#endif

    tempcx = (SiS_Pr->SiS_HT - SiS_Pr->SiS_HDE) >> 2;     /* (HT - HDE) >> 2 */
    tempbx = SiS_Pr->SiS_HDE + 7;            		  /* lcdhdee         */
    if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
       tempbx += 2;
    }
    push1 = tempbx;

#ifdef TWDEBUG
    xf86DrvMsg(0, X_INFO, "lcdhdee 0x%x\n", tempbx);
#endif

    temp = tempbx & 0x00FF;    			          /* RHEQPLE = lcdhdee */
    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x23,temp);
    temp = (tempbx & 0xFF00) >> 8;
    SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x25,0xF0,temp);

    temp = 7;
    if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
       temp += 2;
    }
    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x1F,temp);  	  /* RHBLKE = lcdhdes[7:0] */
    SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x20,0x0F);	  /* lcdhdes [11:8] */

    tempbx += tempcx;
    push2 = tempbx;

    if(SiS_Pr->UseCustomMode) {
       tempbx = SiS_Pr->CHSyncStart + 7;
       if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
          tempbx += 2;
       }
    }

#ifdef TWDEBUG
    xf86DrvMsg(0, X_INFO, "lcdhrs 0x%x\n", tempbx);
#endif

    temp = tempbx & 0x00FF;            		          /* RHBURSTS = lcdhrs */
    if(!SiS_Pr->UseCustomMode) {
       if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
          if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
             if(SiS_Pr->SiS_HDE == 1280) temp = 0x47;
          }
       }
    }
    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x1C,temp);
    temp = (tempbx & 0x0F00) >> 4;
    SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x1D,0x0F,temp);

    tempbx = push2;
    tempcx <<= 1;
    tempbx += tempcx;

    if(SiS_Pr->UseCustomMode) {
       tempbx = SiS_Pr->CHSyncEnd + 7;
       if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
          tempbx += 2;
       }
    }

#ifdef TWDEBUG
    xf86DrvMsg(0, X_INFO, "lcdhre 0x%x\n", tempbx);
#endif

    temp = tempbx & 0x00FF;            		          /* RHSYEXP2S = lcdhre */
    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x21,temp);

    if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) {
       if(SiS_Pr->SiS_VGAVDE == 525) {
          if(SiS_Pr->SiS_ModeType <= ModeVGA)
    	     temp=0xC6;
          else
       	     temp=0xC3;
          SiS_SetReg(SiS_Pr->SiS_Part2Port,0x2f,temp);
          SiS_SetReg(SiS_Pr->SiS_Part2Port,0x30,0xB3);
       } else if(SiS_Pr->SiS_VGAVDE == 420) {
          if(SiS_Pr->SiS_ModeType <= ModeVGA)
	     temp=0x4F;
          else
       	     temp=0x4D;
          SiS_SetReg(SiS_Pr->SiS_Part2Port,0x2f,temp);
       }
    }

#ifdef SIS300
    SiS_Set300Part2Regs(SiS_Pr, HwInfo, ModeIdIndex,
                        RefreshRateTableIndex, ModeNo);
#endif

  } /* HwInfo */
}

/*********************************************/
/*         SET PART 3 REGISTER GROUP         */
/*********************************************/

static void
SiS_SetGroup3(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
              PSIS_HW_INFO HwInfo)
{
  USHORT modeflag, i;
  const UCHAR  *tempdi;

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) return;

  if(ModeNo<=0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  } else {
     if(SiS_Pr->UseCustomMode) {
        modeflag = SiS_Pr->CModeFlag;
     } else {
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     }
  }

#ifndef SIS_CP
  SiS_SetReg(SiS_Pr->SiS_Part3Port,0x00,0x00);
#endif

#ifdef SIS_CP
  SIS_CP_INIT301_CP
#endif

  if(SiS_Pr->SiS_TVMode & TVSetPAL) {
     SiS_SetReg(SiS_Pr->SiS_Part3Port,0x13,0xFA);
     SiS_SetReg(SiS_Pr->SiS_Part3Port,0x14,0xC8);
  } else {
     SiS_SetReg(SiS_Pr->SiS_Part3Port,0x13,0xF5);
     SiS_SetReg(SiS_Pr->SiS_Part3Port,0x14,0xB7);
  }

  if(SiS_Pr->SiS_TVMode & TVSetPALM) {
     SiS_SetReg(SiS_Pr->SiS_Part3Port,0x13,0xFA);
     SiS_SetReg(SiS_Pr->SiS_Part3Port,0x14,0xC8);
     SiS_SetReg(SiS_Pr->SiS_Part3Port,0x3D,0xA8);
  }

  tempdi = NULL;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
     tempdi = SiS_Pr->SiS_HiTVGroup3Data;
     if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode) {
        tempdi = SiS_Pr->SiS_HiTVGroup3Simu;
#if 0
        if(!(modeflag & Charx8Dot)) {
           tempdi = SiS_Pr->SiS_HiTVGroup3Text;
        }
#endif
     }
  } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {
     if(!(SiS_Pr->SiS_TVMode & TVSetYPbPr525i)) {
        tempdi = SiS_HiTVGroup3_1;
        if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p) tempdi = SiS_HiTVGroup3_2;
     }
  }
  if(tempdi) {
     for(i=0; i<=0x3E; i++){
        SiS_SetReg(SiS_Pr->SiS_Part3Port,i,tempdi[i]);
     }
     if(SiS_Pr->SiS_VBType & (VB_SIS301C | VB_SIS302ELV)) {
	if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p) {
	   SiS_SetReg(SiS_Pr->SiS_Part3Port,0x28,0x3f);
	}
     }
  }

  if(!(SiS_Pr->SiS_VBInfo & (SetCRT2ToHiVision | SetCRT2ToYPbPr525750))) {
#ifdef SIS_CP
     SIS_CP_INIT301_CP2
#endif
  }
}

/*********************************************/
/*         SET PART 4 REGISTER GROUP         */
/*********************************************/

#ifdef SIS315H
static void
SiS_SetGroup4_C_ELV(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
   USHORT temp, temp1;

   if(!(SiS_Pr->SiS_VBType & (VB_SIS301C | VB_SIS302ELV))) return;

   SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x3a,0x08);
   temp = SiS_GetReg(SiS_Pr->SiS_Part4Port,0x3a);
   if(!(temp & 0x01)) {
      SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x3a,0xdf);
      SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x25,0xfc);
      if(HwInfo->jChipType < SIS_661) {
         SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x25,0xf8);
      }
      SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x0f,0xfb);
      temp = 0;
      if(!(SiS_Pr->SiS_TVMode & TVSetYPbPr750p)) {
         temp |= 0x0002;
         if(!(SiS_Pr->SiS_TVMode & TVSetYPbPr525p)) {
	    temp ^= 0x0402;
	    if(!(SiS_Pr->SiS_TVMode & TVSetHiVision)) {
	       temp ^= 0x0002;
	    }
	 }
      }
      if(HwInfo->jChipType >= SIS_661) {
         temp1 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x39);
         if(temp1 & 0x01) temp |= 0x10;
         if(temp1 & 0x02) temp |= 0x01;
	 SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x26,0xec,(temp & 0xff));
      } else {
         temp1 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x3b) & 0x03;
	 if(temp1 == 0x01) temp |= 0x01;
	 if(temp1 == 0x03) temp |= 0x04;  /* ? why not 0x10? */
	 SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x26,0xea,(temp & 0xff));
      }
      SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x3a,0xfb,(temp >> 8));
   }
}
#endif

static void
SiS_SetCRT2VCLK(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
                 USHORT RefreshRateTableIndex, PSIS_HW_INFO HwInfo)
{
  USHORT vclkindex;
  USHORT temp, reg1, reg2;

  if(SiS_Pr->UseCustomMode) {
     reg1 = SiS_Pr->CSR2B;
     reg2 = SiS_Pr->CSR2C;
  } else {
     vclkindex = SiS_GetVCLK2Ptr(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex,
                                 HwInfo);
     reg1 = SiS_Pr->SiS_VBVCLKData[vclkindex].Part4_A;
     reg2 = SiS_Pr->SiS_VBVCLKData[vclkindex].Part4_B;
  }

  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
     if(SiS_Pr->SiS_TVMode & TVSetNTSC1024) {
        SiS_SetReg(SiS_Pr->SiS_Part4Port,0x0a,0x57);
 	SiS_SetReg(SiS_Pr->SiS_Part4Port,0x0b,0x46);
	SiS_SetReg(SiS_Pr->SiS_Part4Port,0x1f,0xf6);
     } else {
        SiS_SetReg(SiS_Pr->SiS_Part4Port,0x0a,reg1);
        SiS_SetReg(SiS_Pr->SiS_Part4Port,0x0b,reg2);
     }
  } else {
     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x0a,0x01);
     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x0b,reg2);
     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x0a,reg1);
  }
  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x12,0x00);
  temp = 0x08;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC) temp |= 0x20;
  SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x12,temp);
}

/* Set 301 VGA2 registers */
static void
SiS_SetGroup4(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
  	      USHORT RefreshRateTableIndex, PSIS_HW_INFO HwInfo)
{
  USHORT tempax,tempcx,tempbx,modeflag,temp,temp2,resinfo;
  ULONG tempebx,tempeax,templong;

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  } else {
     if(SiS_Pr->UseCustomMode) {
        modeflag = SiS_Pr->CModeFlag;
	resinfo = 0;
     } else {
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
     }
  }

  if(HwInfo->jChipType >= SIS_315H) {
     if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
        if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
           SiS_SetReg(SiS_Pr->SiS_Part4Port,0x24,0x0e);
        }
     }
  }

  if(SiS_Pr->SiS_VBType & (VB_SIS301C | VB_SIS302LV)) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
        SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x10,0x9f);
     }
  }

  if(HwInfo->jChipType >= SIS_315H) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
        if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	   if(SiS_IsDualLink(SiS_Pr, HwInfo)) {
	      SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x27,0x2c);
	   }
#ifdef SET_EMI
	   if(SiS_Pr->SiS_VBType & (VB_SIS302LV | VB_SIS302ELV)) {
	      SiS_SetReg(SiS_Pr->SiS_Part4Port,0x2a,0x00);
	      SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x30,0x0c);
	      SiS_SetReg(SiS_Pr->SiS_Part4Port,0x34,0x10);
	   }
#endif
	}
   	return;
     }
  }

  temp = SiS_Pr->SiS_RVBHCFACT;
  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x13,temp);

  tempbx = SiS_Pr->SiS_RVBHCMAX;
  temp = tempbx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x14,temp);

  temp2 = (((tempbx & 0xFF00) >> 8) << 7) & 0x00ff;

  tempcx = SiS_Pr->SiS_VGAHT - 1;
  temp = tempcx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x16,temp);

  temp = (((tempcx & 0xFF00) >> 8) << 3) & 0x00ff;
  temp2 |= temp;

  tempcx = SiS_Pr->SiS_VGAVT - 1;
  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV))  tempcx -= 5;

  temp = tempcx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x17,temp);

  temp = temp2 | ((tempcx & 0xFF00) >> 8);
  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x15,temp);

  tempbx = SiS_Pr->SiS_VGAHDE;
  if(modeflag & HalfDCLK)  tempbx >>= 1;
  if(HwInfo->jChipType >= SIS_661) {
     if(SiS_IsDualLink(SiS_Pr, HwInfo)) tempbx >>= 1;
  }

  temp = 0xA0;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
     temp = 0;
     if(tempbx > 800) {
        temp = 0xA0;
        if(tempbx != 1024) {
           temp = 0xC0;
           if(tempbx != 1280) temp = 0;
	}
     }
  } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
     if(tempbx <= 800) {
        temp = 0x80;
     }
  } else {
     temp = 0x80;
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
        temp = 0;
        if(tempbx > 800) temp = 0x60;
     }
  }

  if(SiS_Pr->SiS_TVMode & (TVSetYPbPr525p | TVSetYPbPr750p)) {
     temp = 0;
     if(SiS_Pr->SiS_VGAHDE == 1024) temp = 0x20;
  }

  if(HwInfo->jChipType < SIS_661) {
     if(SiS_IsDualLink(SiS_Pr, HwInfo)) temp = 0;
  }

  if(SiS_Pr->SiS_VBType & VB_SIS301) {
     if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1280x1024)
        temp |= 0x0A;
  }

  SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x0E,0x10,temp);

  tempebx = SiS_Pr->SiS_VDE;

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
     if(!(temp & 0xE0)) tempebx >>=1;
  }

  tempcx = SiS_Pr->SiS_RVBHRS;
  temp = tempcx & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x18,temp);

  tempeax = SiS_Pr->SiS_VGAVDE;
  tempcx |= 0x4000;
  if(tempeax <= tempebx) {
     tempcx ^= 0x4000;
  } else {
     tempeax -= tempebx;
  }

  templong = (tempeax * 256 * 1024) % tempebx;
  tempeax = (tempeax * 256 * 1024) / tempebx;
  tempebx = tempeax;
  if(templong != 0) tempebx++;

  temp = (USHORT)(tempebx & 0x000000FF);
  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x1B,temp);
  temp = (USHORT)((tempebx & 0x0000FF00) >> 8);
  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x1A,temp);

  tempbx = (USHORT)(tempebx >> 16);
  temp = tempbx & 0x00FF;
  temp <<= 4;
  temp |= ((tempcx & 0xFF00) >> 8);
  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x19,temp);

  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {

     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x1C,0x28);

     tempbx = 0;
     tempax = SiS_Pr->SiS_VGAHDE;
     if(modeflag & HalfDCLK) 		tempax >>= 1;
     if(SiS_IsDualLink(SiS_Pr, HwInfo)) tempax >>= 1;
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
	if(tempax > 800) tempax -= 800;
     } else {
        if(tempax > 800) {
	   tempbx = 8;
           if(tempax == 1024)
	      tempax *= 25;
           else
	      tempax *= 20;

	   temp = tempax % 32;
	   tempax /= 32;
	   tempax--;
	   if (temp!=0) tempax++;
        }
     }
     tempax--;
     temp = ((tempax & 0xFF00) >> 8) & 0x03;
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {			/* From 1.10.7w */
	if(ModeNo > 0x13) {					/* From 1.10.7w */
	   if(resinfo == SIS_RI_1024x768) tempax = 0x1f;	/* From 1.10.7w */
	}							/* From 1.10.7w */
     }								/* From 1.10.7w */
     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x1D,tempax & 0x00FF);
     temp <<= 4;
     temp |= tempbx;
     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x1E,temp);

     if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	if(IS_SIS550650740660) {
	   temp = 0x0026;  /* 1.10.7w; 1.10.8r; needs corresponding code in Dis/EnableBridge! */
	} else {
	   temp = 0x0036;
	}
     } else {
	temp = 0x0036;
     }
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
        if(!(SiS_Pr->SiS_TVMode & (TVSetNTSC1024 | TVSetHiVision | TVSetYPbPr750p | TVSetYPbPr525p))) {
	   temp |= 0x01;
	   if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
	      if(!(SiS_Pr->SiS_TVMode & TVSetTVSimuMode)) {
  	         temp &= 0xFE;
	      }
	   }
	}
     }
     SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x1F,0xC0,temp);

     tempbx = SiS_Pr->SiS_HT;
     if(SiS_IsDualLink(SiS_Pr, HwInfo)) tempbx >>= 1;
     tempbx >>= 1;
     tempbx -= 2;
     temp = ((tempbx & 0x0700) >> 8) << 3;
     SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x21,0xC0,temp);
     temp = tempbx & 0x00FF;
     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x22,temp);

     if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
           SiS_SetReg(SiS_Pr->SiS_Part4Port,0x24,0x0e);
	   /* LCD-too-dark-error-source, see FinalizeLCD() */
	}
	if(HwInfo->jChipType >= SIS_315H) {
	   if(SiS_IsDualLink(SiS_Pr, HwInfo)) {
	      SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x27,0x2c);
	   }
	}
#ifdef SET_EMI
	if(SiS_Pr->SiS_VBType & (VB_SIS302LV | VB_SIS302ELV)) {
	   SiS_SetReg(SiS_Pr->SiS_Part4Port,0x2a,0x00);
	   SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x30,0x0c);
	   SiS_SetReg(SiS_Pr->SiS_Part4Port,0x34,0x10);
	}
#endif
     }

  }  /* 301B */

  SiS_SetCRT2VCLK(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex, HwInfo);
}

/*********************************************/
/*         SET PART 5 REGISTER GROUP         */
/*********************************************/

/* Set 301 Palette address port registers */
static void
SiS_SetGroup5(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
              PSIS_HW_INFO HwInfo)
{

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)  return;

  if(SiS_Pr->SiS_ModeType == ModeVGA) {
     if(!(SiS_Pr->SiS_VBInfo & (SetInSlaveMode | LoadDACFlag))) {
        SiS_EnableCRT2(SiS_Pr);
        SiS_LoadDAC(SiS_Pr, HwInfo, ModeNo, ModeIdIndex);
     }
  }
}

/*********************************************/
/*     MODIFY CRT1 GROUP FOR SLAVE MODE      */
/*********************************************/

static void
SiS_ModCRT1CRTC(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
                USHORT RefreshRateTableIndex, PSIS_HW_INFO HwInfo)
{
  USHORT tempah,i,modeflag,j;
  USHORT ResIndex,DisplayType;
  const SiS_LVDSCRT1DataStruct *LVDSCRT1Ptr=NULL;

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  } else {
     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
  }

  if((SiS_Pr->SiS_CustomT == CUT_BARCO1366) ||
     (SiS_Pr->SiS_CustomT == CUT_BARCO1024) ||
     (SiS_Pr->SiS_CustomT == CUT_PANEL848))
     return;

  if(!(SiS_GetLVDSCRT1Ptr(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex,
                          &ResIndex, &DisplayType))) return;

  if(HwInfo->jChipType < SIS_315H) {
     if(SiS_Pr->SiS_SetFlag & SetDOSMode) return;
  }

  switch(DisplayType) {
    case 0 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1800x600_1;           break;
    case 1 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x768_1;          break;
    case 2 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11280x1024_1;         break;
    case 3 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1800x600_1_H;         break;
    case 4 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x768_1_H;        break;
    case 5 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11280x1024_1_H;       break;
    case 6 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1800x600_2;           break;
    case 7 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x768_2;          break;
    case 8 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11280x1024_2;         break;
    case 9 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1800x600_2_H;         break;
    case 10: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x768_2_H;        break;
    case 11: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11280x1024_2_H;       break;
    case 12: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1XXXxXXX_1;           break;
    case 13: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1XXXxXXX_1_H;         break;
    case 14: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11400x1050_1;         break;
    case 15: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11400x1050_1_H;       break;
    case 16: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11400x1050_2;         break;
    case 17: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11400x1050_2_H;       break;
    case 18: LVDSCRT1Ptr = SiS_Pr->SiS_CHTVCRT1UNTSC;               break;
    case 19: LVDSCRT1Ptr = SiS_Pr->SiS_CHTVCRT1ONTSC;               break;
    case 20: LVDSCRT1Ptr = SiS_Pr->SiS_CHTVCRT1UPAL;                break;
    case 21: LVDSCRT1Ptr = SiS_Pr->SiS_CHTVCRT1OPAL;                break;
    case 22: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1320x480_1;           break; /* FSTN */
    case 23: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x600_1;          break;
    case 24: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x600_1_H;        break;
    case 25: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x600_2;          break;
    case 26: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x600_2_H;        break;
    case 27: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11152x768_1;          break;
    case 28: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11152x768_1_H;        break;
    case 29: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11152x768_2;          break;
    case 30: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11152x768_2_H;        break;
    case 36: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11600x1200_1;         break;
    case 37: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11600x1200_1_H;       break;
    case 38: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11600x1200_2;         break;
    case 39: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11600x1200_2_H;       break;
    case 40: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11280x768_1;          break;
    case 41: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11280x768_1_H;        break;
    case 42: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11280x768_2;          break;
    case 43: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11280x768_2_H;        break;
    case 50: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1640x480_1;           break;
    case 51: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1640x480_1_H;         break;
    case 52: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1640x480_2;           break;
    case 53: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1640x480_2_H;         break;
    case 54: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1640x480_3;           break;
    case 55: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1640x480_3_H;         break;
    case 99: LVDSCRT1Ptr = SiS_Pr->SiS_CHTVCRT1SOPAL;               break;
    default: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x768_1;          break;
  }

  SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x11,0x7f);                        /*unlock cr0-7  */

  tempah = (LVDSCRT1Ptr + ResIndex)->CR[0];
  SiS_SetReg(SiS_Pr->SiS_P3d4,0x00,tempah);

  for(i=0x02,j=1;i<=0x05;i++,j++){
    tempah = (LVDSCRT1Ptr + ResIndex)->CR[j];
    SiS_SetReg(SiS_Pr->SiS_P3d4,i,tempah);
  }
  for(i=0x06,j=5;i<=0x07;i++,j++){
    tempah = (LVDSCRT1Ptr + ResIndex)->CR[j];
    SiS_SetReg(SiS_Pr->SiS_P3d4,i,tempah);
  }
  for(i=0x10,j=7;i<=0x11;i++,j++){
    tempah = (LVDSCRT1Ptr + ResIndex)->CR[j];
    SiS_SetReg(SiS_Pr->SiS_P3d4,i,tempah);
  }
  for(i=0x15,j=9;i<=0x16;i++,j++){
    tempah = (LVDSCRT1Ptr + ResIndex)->CR[j];
    SiS_SetReg(SiS_Pr->SiS_P3d4,i,tempah);
  }
  for(i=0x0A,j=11;i<=0x0C;i++,j++){
    tempah = (LVDSCRT1Ptr + ResIndex)->CR[j];
    SiS_SetReg(SiS_Pr->SiS_P3c4,i,tempah);
  }

  tempah = (LVDSCRT1Ptr + ResIndex)->CR[14];
  tempah &= 0xE0;
  SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x0E,0x1f,tempah);

  tempah = (LVDSCRT1Ptr + ResIndex)->CR[14];
  tempah &= 0x01;
  tempah <<= 5;
  if(modeflag & DoubleScanMode)  tempah |= 0x080;
  SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x09,~0x020,tempah);

  /* 650/LVDS BIOS - doesn't make sense */
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
     if(modeflag & HalfDCLK)
        SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x11,0x7f);
  }
}

/*********************************************/
/*              SET CRT2 ECLK                */
/*********************************************/

static void
SiS_SetCRT2ECLK(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
           USHORT RefreshRateTableIndex, PSIS_HW_INFO HwInfo)
{
  UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
  USHORT clkbase, vclkindex=0;
  UCHAR  sr2b, sr2c;

  if((SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480) || (SiS_Pr->SiS_IF_DEF_TRUMPION == 1)) {
	SiS_Pr->SiS_SetFlag &= (~ProgrammingCRT2);
        if((SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK & 0x3f) == 2) {
	   RefreshRateTableIndex--;
	}
	vclkindex = SiS_GetVCLK2Ptr(SiS_Pr, ModeNo, ModeIdIndex,
                                    RefreshRateTableIndex, HwInfo);
	SiS_Pr->SiS_SetFlag |= ProgrammingCRT2;
  } else {
        vclkindex = SiS_GetVCLK2Ptr(SiS_Pr, ModeNo, ModeIdIndex,
                                    RefreshRateTableIndex, HwInfo);
  }

  sr2b = SiS_Pr->SiS_VCLKData[vclkindex].SR2B;
  sr2c = SiS_Pr->SiS_VCLKData[vclkindex].SR2C;

  if((SiS_Pr->SiS_CustomT == CUT_BARCO1366) || (SiS_Pr->SiS_CustomT == CUT_BARCO1024)) {
     if((ROMAddr) && SiS_Pr->SiS_UseROM) {
	if(ROMAddr[0x220] & 0x01) {
           sr2b = ROMAddr[0x227];
	   sr2c = ROMAddr[0x228];
	}
     }
  }

  clkbase = 0x02B;
  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
     if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
    	clkbase += 3;
     }
  }

  SiS_SetReg(SiS_Pr->SiS_P3c4,0x05,0x86);
  SiS_SetReg(SiS_Pr->SiS_P3c4,0x31,0x20);
  SiS_SetReg(SiS_Pr->SiS_P3c4,clkbase,sr2b);
  SiS_SetReg(SiS_Pr->SiS_P3c4,clkbase+1,sr2c);
  SiS_SetReg(SiS_Pr->SiS_P3c4,0x31,0x10);
  SiS_SetReg(SiS_Pr->SiS_P3c4,clkbase,sr2b);
  SiS_SetReg(SiS_Pr->SiS_P3c4,clkbase+1,sr2c);
  SiS_SetReg(SiS_Pr->SiS_P3c4,0x31,0x00);
  SiS_SetReg(SiS_Pr->SiS_P3c4,clkbase,sr2b);
  SiS_SetReg(SiS_Pr->SiS_P3c4,clkbase+1,sr2c);
}

/*********************************************/
/*           SET UP CHRONTEL CHIPS           */
/*********************************************/

static void
SiS_SetCHTVReg(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
               USHORT RefreshRateTableIndex)
{
  USHORT temp, tempbx, tempcl;
  USHORT TVType, resindex;
  const SiS_CHTVRegDataStruct *CHTVRegData = NULL;

  if(ModeNo <= 0x13)
    	tempcl = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  else
    	tempcl = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;

  TVType = 0;
  if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) TVType += 1;
  if(SiS_Pr->SiS_TVMode & TVSetPAL) {
  	TVType += 2;
	if(SiS_Pr->SiS_ModeType > ModeVGA) {
	   if(SiS_Pr->SiS_CHSOverScan) TVType = 8;
	}
	if(SiS_Pr->SiS_TVMode & TVSetPALM) {
		TVType = 4;
		if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) TVType += 1;
	} else if(SiS_Pr->SiS_TVMode & TVSetPALN) {
		TVType = 6;
		if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) TVType += 1;
	}
  }
  switch(TVType) {
    	case  0: CHTVRegData = SiS_Pr->SiS_CHTVReg_UNTSC; break;
    	case  1: CHTVRegData = SiS_Pr->SiS_CHTVReg_ONTSC; break;
    	case  2: CHTVRegData = SiS_Pr->SiS_CHTVReg_UPAL;  break;
    	case  3: CHTVRegData = SiS_Pr->SiS_CHTVReg_OPAL;  break;
	case  4: CHTVRegData = SiS_Pr->SiS_CHTVReg_UPALM; break;
    	case  5: CHTVRegData = SiS_Pr->SiS_CHTVReg_OPALM; break;
    	case  6: CHTVRegData = SiS_Pr->SiS_CHTVReg_UPALN; break;
    	case  7: CHTVRegData = SiS_Pr->SiS_CHTVReg_OPALN; break;
	case  8: CHTVRegData = SiS_Pr->SiS_CHTVReg_SOPAL; break;
	default: CHTVRegData = SiS_Pr->SiS_CHTVReg_OPAL;  break;
  }
  resindex = tempcl & 0x3F;

  if(SiS_Pr->SiS_IF_DEF_CH70xx == 1) {

#ifdef SIS300

     /* Chrontel 7005 - I assume that it does not come with a 315 series chip */

     /* We don't support modes >800x600 */
     if (resindex > 5) return;

     if(SiS_Pr->SiS_TVMode & TVSetPAL) {
    	SiS_SetCH700x(SiS_Pr,0x4304);   /* 0x40=76uA (PAL); 0x03=15bit non-multi RGB*/
    	SiS_SetCH700x(SiS_Pr,0x6909);	/* Black level for PAL (105)*/
     } else {
    	SiS_SetCH700x(SiS_Pr,0x0304);   /* upper nibble=71uA (NTSC), 0x03=15bit non-multi RGB*/
    	SiS_SetCH700x(SiS_Pr,0x7109);	/* Black level for NTSC (113)*/
     }

     temp = CHTVRegData[resindex].Reg[0];
     tempbx=((temp&0x00FF)<<8)|0x00;	/* Mode register */
     SiS_SetCH700x(SiS_Pr,tempbx);
     temp = CHTVRegData[resindex].Reg[1];
     tempbx=((temp&0x00FF)<<8)|0x07;	/* Start active video register */
     SiS_SetCH700x(SiS_Pr,tempbx);
     temp = CHTVRegData[resindex].Reg[2];
     tempbx=((temp&0x00FF)<<8)|0x08;	/* Position overflow register */
     SiS_SetCH700x(SiS_Pr,tempbx);
     temp = CHTVRegData[resindex].Reg[3];
     tempbx=((temp&0x00FF)<<8)|0x0A;	/* Horiz Position register */
     SiS_SetCH700x(SiS_Pr,tempbx);
     temp = CHTVRegData[resindex].Reg[4];
     tempbx=((temp&0x00FF)<<8)|0x0B;	/* Vertical Position register */
     SiS_SetCH700x(SiS_Pr,tempbx);

     /* Set minimum flicker filter for Luma channel (SR1-0=00),
                minimum text enhancement (S3-2=10),
   	        maximum flicker filter for Chroma channel (S5-4=10)
	        =00101000=0x28 (When reading, S1-0->S3-2, and S3-2->S1-0!)
      */
     SiS_SetCH700x(SiS_Pr,0x2801);

     /* Set video bandwidth
            High bandwith Luma composite video filter(S0=1)
            low bandwith Luma S-video filter (S2-1=00)
	    disable peak filter in S-video channel (S3=0)
	    high bandwidth Chroma Filter (S5-4=11)
	    =00110001=0x31
     */
     SiS_SetCH700x(SiS_Pr,0xb103);       /* old: 3103 */

     /* Register 0x3D does not exist in non-macrovision register map
            (Maybe this is a macrovision register?)
      */
#ifndef SIS_CP
     SiS_SetCH70xx(SiS_Pr,0x003D);
#endif

     /* Register 0x10 only contains 1 writable bit (S0) for sensing,
            all other bits a read-only. Macrovision?
      */
     SiS_SetCH70xxANDOR(SiS_Pr,0x0010,0x1F);

     /* Register 0x11 only contains 3 writable bits (S0-S2) for
            contrast enhancement (set to 010 -> gain 1 Yout = 17/16*(Yin-30) )
      */
     SiS_SetCH70xxANDOR(SiS_Pr,0x0211,0xF8);

     /* Clear DSEN
      */
     SiS_SetCH70xxANDOR(SiS_Pr,0x001C,0xEF);

     if(!(SiS_Pr->SiS_TVMode & TVSetPAL)) {		/* ---- NTSC ---- */
       if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) {
         if(resindex == 0x04) {   			/* 640x480 overscan: Mode 16 */
      	   SiS_SetCH70xxANDOR(SiS_Pr,0x0020,0xEF);   	/* loop filter off */
           SiS_SetCH70xxANDOR(SiS_Pr,0x0121,0xFE);      /* ACIV on, no need to set FSCI */
         } else if(resindex == 0x05) {    		/* 800x600 overscan: Mode 23 */
           SiS_SetCH70xxANDOR(SiS_Pr,0x0118,0xF0);	/* 0x18-0x1f: FSCI 469,762,048 */
           SiS_SetCH70xxANDOR(SiS_Pr,0x0C19,0xF0);
           SiS_SetCH70xxANDOR(SiS_Pr,0x001A,0xF0);
           SiS_SetCH70xxANDOR(SiS_Pr,0x001B,0xF0);
           SiS_SetCH70xxANDOR(SiS_Pr,0x001C,0xF0);
           SiS_SetCH70xxANDOR(SiS_Pr,0x001D,0xF0);
           SiS_SetCH70xxANDOR(SiS_Pr,0x001E,0xF0);
           SiS_SetCH70xxANDOR(SiS_Pr,0x001F,0xF0);
           SiS_SetCH70xxANDOR(SiS_Pr,0x0120,0xEF);       /* Loop filter on for mode 23 */
           SiS_SetCH70xxANDOR(SiS_Pr,0x0021,0xFE);       /* ACIV off, need to set FSCI */
         }
       } else {
         if(resindex == 0x04) {     			 /* ----- 640x480 underscan; Mode 17 */
           SiS_SetCH70xxANDOR(SiS_Pr,0x0020,0xEF); 	 /* loop filter off */
           SiS_SetCH70xxANDOR(SiS_Pr,0x0121,0xFE);
         } else if(resindex == 0x05) {   		 /* ----- 800x600 underscan: Mode 24 */
#if 0
           SiS_SetCH70xxANDOR(SiS_Pr,0x0118,0xF0);       /* (FSCI was 0x1f1c71c7 - this is for mode 22) */
           SiS_SetCH70xxANDOR(SiS_Pr,0x0919,0xF0);	 /* FSCI for mode 24 is 428,554,851 */
           SiS_SetCH70xxANDOR(SiS_Pr,0x081A,0xF0);       /* 198b3a63 */
           SiS_SetCH70xxANDOR(SiS_Pr,0x0b1B,0xF0);
           SiS_SetCH70xxANDOR(SiS_Pr,0x041C,0xF0);
           SiS_SetCH70xxANDOR(SiS_Pr,0x011D,0xF0);
           SiS_SetCH70xxANDOR(SiS_Pr,0x061E,0xF0);
           SiS_SetCH70xxANDOR(SiS_Pr,0x051F,0xF0);
           SiS_SetCH70xxANDOR(SiS_Pr,0x0020,0xEF);       /* loop filter off for mode 24 */
           SiS_SetCH70xxANDOR(SiS_Pr,0x0021,0xFE);	 /* ACIV off, need to set FSCI */
#endif     /* All alternatives wrong (datasheet wrong?), don't use FSCI */
	   SiS_SetCH70xxANDOR(SiS_Pr,0x0020,0xEF); 	 /* loop filter off */
           SiS_SetCH70xxANDOR(SiS_Pr,0x0121,0xFE);
         }
       }
     } else {						/* ---- PAL ---- */
           /* We don't play around with FSCI in PAL mode */
         if(resindex == 0x04) {
           SiS_SetCH70xxANDOR(SiS_Pr,0x0020,0xEF); 	/* loop filter off */
           SiS_SetCH70xxANDOR(SiS_Pr,0x0121,0xFE);      /* ACIV on */
         } else {
           SiS_SetCH70xxANDOR(SiS_Pr,0x0020,0xEF); 	/* loop filter off */
           SiS_SetCH70xxANDOR(SiS_Pr,0x0121,0xFE);      /* ACIV on */
         }
     }
     
#endif  /* 300 */

  } else {

     /* Chrontel 7019 - assumed that it does not come with a 300 series chip */

#ifdef SIS315H

     /* We don't support modes >1024x768 */
     if (resindex > 6) return;

     temp = CHTVRegData[resindex].Reg[0];
     if(SiS_Pr->SiS_TVMode & TVSetNTSCJ) {
        temp |= 0x10;
     }
     tempbx=((temp & 0x00FF) << 8) | 0x00;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[1];
     tempbx=((temp & 0x00FF) << 8) | 0x01;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[2];
     tempbx=((temp & 0x00FF) << 8) | 0x02;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[3];
     tempbx=((temp & 0x00FF) << 8) | 0x04;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[4];
     tempbx=((temp & 0x00FF) << 8) | 0x03;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[5];
     tempbx=((temp & 0x00FF) << 8) | 0x05;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[6];
     tempbx=((temp & 0x00FF) << 8) | 0x06;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[7];
     if(SiS_Pr->SiS_TVMode & TVSetNTSCJ) {
	temp = 0x66;
     }
     tempbx=((temp & 0x00FF) << 8) | 0x07;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[8];
     tempbx=((temp & 0x00FF) << 8) | 0x08;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[9];
     tempbx=((temp & 0x00FF) << 8) | 0x15;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[10];
     tempbx=((temp & 0x00FF) << 8) | 0x1f;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[11];
     tempbx=((temp & 0x00FF) << 8) | 0x0c;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[12];
     tempbx=((temp & 0x00FF) << 8) | 0x0d;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[13];
     tempbx=((temp & 0x00FF) << 8) | 0x0e;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[14];
     tempbx=((temp & 0x00FF) << 8) | 0x0f;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[15];
     tempbx=((temp & 0x00FF) << 8) | 0x10;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = SiS_GetCH701x(SiS_Pr,0x21) & ~0x02;
     /* D1 should be set for PAL, PAL-N and NTSC-J,
        but I won't do that for PAL unless somebody
	tells me to do so. Since the BIOS uses
	non-default CIV values and blacklevels,
	this might be compensated anyway.
      */
     if(SiS_Pr->SiS_TVMode & (TVSetPALN | TVSetNTSCJ)) temp |= 0x02;
     SiS_SetCH701x(SiS_Pr,((temp << 8) | 0x21));

#endif	/* 315 */

  }

#ifdef SIS_CP
  SIS_CP_INIT301_CP3
#endif

}

void
SiS_Chrontel701xBLOn(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  USHORT temp;

  /* Enable Chrontel 7019 LCD panel backlight */
  if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
     if(HwInfo->jChipType == SIS_740) {
        SiS_SetCH701x(SiS_Pr,0x6566);
     } else {
        temp = SiS_GetCH701x(SiS_Pr,0x66);
        temp |= 0x20;
	SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x66);
     }
  }
}

void
SiS_Chrontel701xBLOff(SiS_Private *SiS_Pr)
{
  USHORT temp;

  /* Disable Chrontel 7019 LCD panel backlight */
  if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
     temp = SiS_GetCH701x(SiS_Pr,0x66);
     temp &= 0xDF;
     SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x66);
  }
}

#ifdef SIS315H  /* ----------- 315 series only ---------- */

static void
SiS_ChrontelPowerSequencing(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  UCHAR regtable[]      = { 0x67, 0x68, 0x69, 0x6a, 0x6b };
  UCHAR table1024_740[] = { 0x01, 0x02, 0x01, 0x01, 0x01 };
  UCHAR table1400_740[] = { 0x01, 0x6e, 0x01, 0x01, 0x01 };
  UCHAR asus1024_740[]  = { 0x19, 0x6e, 0x01, 0x19, 0x09 };
  UCHAR asus1400_740[]  = { 0x19, 0x6e, 0x01, 0x19, 0x09 };
  UCHAR table1024_650[] = { 0x01, 0x02, 0x01, 0x01, 0x02 };
  UCHAR table1400_650[] = { 0x01, 0x02, 0x01, 0x01, 0x02 };
  UCHAR *tableptr = NULL;
  int i;

  /* Set up Power up/down timing */

  if(HwInfo->jChipType == SIS_740) {
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
        if(SiS_Pr->SiS_CustomT == CUT_ASUSL3000D) tableptr = asus1024_740;
        else    			          tableptr = table1024_740;
     } else if((SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) ||
               (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) ||
	       (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200)) {
	if(SiS_Pr->SiS_CustomT == CUT_ASUSL3000D) tableptr = asus1400_740;
        else					  tableptr = table1400_740;
     } else return;
  } else {
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
        tableptr = table1024_650;
     } else if((SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) ||
               (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) ||
	       (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200)) {
        tableptr = table1400_650;
     } else return;
  }

  for(i=0; i<5; i++) {
     SiS_SetCH701x(SiS_Pr,(tableptr[i] << 8) | regtable[i]);
  }
}

static void
SiS_SetCH701xForLCD(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  UCHAR regtable[]      = { 0x1c, 0x5f, 0x64, 0x6f, 0x70, 0x71,
                            0x72, 0x73, 0x74, 0x76, 0x78, 0x7d, 0x66 };
  UCHAR table1024_740[] = { 0x60, 0x02, 0x00, 0x07, 0x40, 0xed,
                            0xa3, 0xc8, 0xc7, 0xac, 0xe0, 0x02, 0x44 };
  UCHAR table1280_740[] = { 0x60, 0x03, 0x11, 0x00, 0x40, 0xe3,
   			    0xad, 0xdb, 0xf6, 0xac, 0xe0, 0x02, 0x44 };
  UCHAR table1400_740[] = { 0x60, 0x03, 0x11, 0x00, 0x40, 0xe3,
                            0xad, 0xdb, 0xf6, 0xac, 0xe0, 0x02, 0x44 };
  UCHAR table1600_740[] = { 0x60, 0x04, 0x11, 0x00, 0x40, 0xe3,
  			    0xad, 0xde, 0xf6, 0xac, 0x60, 0x1a, 0x44 };
  UCHAR table1024_650[] = { 0x60, 0x02, 0x00, 0x07, 0x40, 0xed,
                            0xa3, 0xc8, 0xc7, 0xac, 0x60, 0x02 };
  UCHAR table1280_650[] = { 0x60, 0x03, 0x11, 0x00, 0x40, 0xe3,
   		   	    0xad, 0xdb, 0xf6, 0xac, 0xe0, 0x02 };
  UCHAR table1400_650[] = { 0x60, 0x03, 0x11, 0x00, 0x40, 0xef,
                            0xad, 0xdb, 0xf6, 0xac, 0x60, 0x02 };
  UCHAR table1600_650[] = { 0x60, 0x04, 0x11, 0x00, 0x40, 0xe3,
  			    0xad, 0xde, 0xf6, 0xac, 0x60, 0x1a };
  UCHAR *tableptr = NULL;
  USHORT tempbh;
  int i;

  if(HwInfo->jChipType == SIS_740) {
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
        tableptr = table1024_740;
     } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
        tableptr = table1280_740;
     } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
        tableptr = table1400_740;
     } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) {
        tableptr = table1600_740;
     } else return;
  } else {
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
        tableptr = table1024_650;
     } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
        tableptr = table1280_650;
     } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
        tableptr = table1400_650;
     } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) {
        tableptr = table1600_650;
     } else return;
  }

  tempbh = SiS_GetCH701x(SiS_Pr,0x74);
  if((tempbh == 0xf6) || (tempbh == 0xc7)) {
     tempbh = SiS_GetCH701x(SiS_Pr,0x73);
     if(tempbh == 0xc8) {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) return;
     } else if(tempbh == 0xdb) {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) return;
	if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) return;
     } else if(tempbh == 0xde) {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) return;
     }
  }

  if(HwInfo->jChipType == SIS_740) {
     tempbh = 0x0d;
  } else {
     tempbh = 0x0c;
  }
  for(i = 0; i < tempbh; i++) {
     SiS_SetCH701x(SiS_Pr,(tableptr[i] << 8) | regtable[i]);
  }
  SiS_ChrontelPowerSequencing(SiS_Pr,HwInfo);
  tempbh = SiS_GetCH701x(SiS_Pr,0x1e);
  tempbh |= 0xc0;
  SiS_SetCH701x(SiS_Pr,(tempbh << 8) | 0x1e);

  if(HwInfo->jChipType == SIS_740) {
     tempbh = SiS_GetCH701x(SiS_Pr,0x1c);
     tempbh &= 0xfb;
     SiS_SetCH701x(SiS_Pr,(tempbh << 8) | 0x1c);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2d,0x03);
     tempbh = SiS_GetCH701x(SiS_Pr,0x64);
     tempbh |= 0x40;
     SiS_SetCH701x(SiS_Pr,(tempbh << 8) | 0x64);
     tempbh = SiS_GetCH701x(SiS_Pr,0x03);
     tempbh &= 0x3f;
     SiS_SetCH701x(SiS_Pr,(tempbh << 8) | 0x03);
  }
}

static void
SiS_ChrontelResetVSync(SiS_Private *SiS_Pr)
{
  unsigned char temp, temp1;

  temp1 = SiS_GetCH701x(SiS_Pr,0x49);
  SiS_SetCH701x(SiS_Pr,0x3e49);
  temp = SiS_GetCH701x(SiS_Pr,0x47);
  temp &= 0x7f;	/* Use external VSYNC */
  SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x47);
  SiS_LongDelay(SiS_Pr,3);
  temp = SiS_GetCH701x(SiS_Pr,0x47);
  temp |= 0x80;	/* Use internal VSYNC */
  SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x47);
  SiS_SetCH701x(SiS_Pr,(temp1 << 8) | 0x49);
}

void
SiS_Chrontel701xOn(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  USHORT temp;

  if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
     if(HwInfo->jChipType == SIS_740) {
        temp = SiS_GetCH701x(SiS_Pr,0x1c);
        temp |= 0x04;	/* Invert XCLK phase */
        SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x1c);
     }
     if(SiS_IsYPbPr(SiS_Pr, HwInfo)) {
        temp = SiS_GetCH701x(SiS_Pr,0x01);
	temp &= 0x3f;
	temp |= 0x80;	/* Enable YPrPb (HDTV) */
	SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x01);
     }
     if(SiS_IsChScart(SiS_Pr, HwInfo)) {
        temp = SiS_GetCH701x(SiS_Pr,0x01);
	temp &= 0x3f;
	temp |= 0xc0;	/* Enable SCART + CVBS */
	SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x01);
     }
     if(HwInfo->jChipType == SIS_740) {
        SiS_ChrontelResetVSync(SiS_Pr);
        SiS_SetCH701x(SiS_Pr,0x2049);   /* Enable TV path */
     } else {
        SiS_SetCH701x(SiS_Pr,0x2049);   /* Enable TV path */
        temp = SiS_GetCH701x(SiS_Pr,0x49);
        if(SiS_IsYPbPr(SiS_Pr,HwInfo)) {
           temp = SiS_GetCH701x(SiS_Pr,0x73);
	   temp |= 0x60;
	   SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x73);
        }
        temp = SiS_GetCH701x(SiS_Pr,0x47);
        temp &= 0x7f;
        SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x47);
        SiS_LongDelay(SiS_Pr,2);
        temp = SiS_GetCH701x(SiS_Pr,0x47);
        temp |= 0x80;
        SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x47);
     }
  }
}

void
SiS_Chrontel701xOff(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  USHORT temp;

  /* Complete power down of LVDS */
  if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
     if(HwInfo->jChipType == SIS_740) {
        SiS_LongDelay(SiS_Pr,1);
	SiS_GenericDelay(SiS_Pr,0x16ff);
	SiS_SetCH701x(SiS_Pr,0xac76);
	SiS_SetCH701x(SiS_Pr,0x0066);
     } else {
        SiS_LongDelay(SiS_Pr,2);
	temp = SiS_GetCH701x(SiS_Pr,0x76);
	temp &= 0xfc;
	SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x76);
	SiS_SetCH701x(SiS_Pr,0x0066);
     }
  }
}

static void
SiS_ChrontelResetDB(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
     USHORT temp;

     if(HwInfo->jChipType == SIS_740) {

        temp = SiS_GetCH701x(SiS_Pr,0x4a);  /* Version ID */
        temp &= 0x01;
        if(!temp) {

           if(SiS_WeHaveBacklightCtrl(SiS_Pr,HwInfo)) {
	      temp = SiS_GetCH701x(SiS_Pr,0x49);
	      SiS_SetCH701x(SiS_Pr,0x3e49);
	   }
	   /* Reset Chrontel 7019 datapath */
           SiS_SetCH701x(SiS_Pr,0x1048);
           SiS_LongDelay(SiS_Pr,1);
           SiS_SetCH701x(SiS_Pr,0x1848);

	   if(SiS_WeHaveBacklightCtrl(SiS_Pr, HwInfo)) {
	      SiS_ChrontelResetVSync(SiS_Pr);
	      SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x49);
	   }

        } else {

	   /* Clear/set/clear GPIO */
           temp = SiS_GetCH701x(SiS_Pr,0x5c);
	   temp &= 0xef;
	   SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x5c);
	   temp = SiS_GetCH701x(SiS_Pr,0x5c);
	   temp |= 0x10;
	   SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x5c);
	   temp = SiS_GetCH701x(SiS_Pr,0x5c);
	   temp &= 0xef;
	   SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x5c);
	   temp = SiS_GetCH701x(SiS_Pr,0x61);
	   if(!temp) {
	      SiS_SetCH701xForLCD(SiS_Pr, HwInfo);
	   }
        }

     } else { /* 650 */
        /* Reset Chrontel 7019 datapath */
        SiS_SetCH701x(SiS_Pr,0x1048);
        SiS_LongDelay(SiS_Pr,1);
        SiS_SetCH701x(SiS_Pr,0x1848);
     }
}

void
SiS_ChrontelInitTVVSync(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
     USHORT temp;

     if(HwInfo->jChipType == SIS_740) {

        if(SiS_WeHaveBacklightCtrl(SiS_Pr,HwInfo)) {
           SiS_ChrontelResetVSync(SiS_Pr);
        }

     } else {

        SiS_SetCH701x(SiS_Pr,0xaf76);  /* Power up LVDS block */
        temp = SiS_GetCH701x(SiS_Pr,0x49);
        temp &= 1;
        if(temp != 1) {  /* TV block powered? (0 = yes, 1 = no) */
	   temp = SiS_GetCH701x(SiS_Pr,0x47);
	   temp &= 0x70;
	   SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x47);  /* enable VSYNC */
	   SiS_LongDelay(SiS_Pr,3);
	   temp = SiS_GetCH701x(SiS_Pr,0x47);
	   temp |= 0x80;
	   SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x47);  /* disable VSYNC */
        }

     }
}

static void
SiS_ChrontelDoSomething3(SiS_Private *SiS_Pr, USHORT ModeNo, PSIS_HW_INFO HwInfo)
{
     USHORT temp,temp1;

     if(HwInfo->jChipType == SIS_740) {

        temp = SiS_GetCH701x(SiS_Pr,0x61);
        if(temp < 1) {
           temp++;
	   SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x61);
        }
        SiS_SetCH701x(SiS_Pr,0x4566);  /* Panel power on */
        SiS_SetCH701x(SiS_Pr,0xaf76);  /* All power on */
        SiS_LongDelay(SiS_Pr,1);
        SiS_GenericDelay(SiS_Pr,0x16ff);

     } else {  /* 650 */

        temp1 = 0;
        temp = SiS_GetCH701x(SiS_Pr,0x61);
        if(temp < 2) {
           temp++;
	   SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x61);
	   temp1 = 1;
        }
        SiS_SetCH701x(SiS_Pr,0xac76);
        temp = SiS_GetCH701x(SiS_Pr,0x66);
        temp |= 0x5f;
        SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x66);
        if(ModeNo > 0x13) {
           if(SiS_WeHaveBacklightCtrl(SiS_Pr, HwInfo)) {
	      SiS_GenericDelay(SiS_Pr,0x3ff);
	   } else {
	      SiS_GenericDelay(SiS_Pr,0x2ff);
	   }
        } else {
           if(!temp1)
	      SiS_GenericDelay(SiS_Pr,0x2ff);
        }
        temp = SiS_GetCH701x(SiS_Pr,0x76);
        temp |= 0x03;
        SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x76);
        temp = SiS_GetCH701x(SiS_Pr,0x66);
        temp &= 0x7f;
        SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x66);
        SiS_LongDelay(SiS_Pr,1);

     }
}

static void
SiS_ChrontelDoSomething2(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
     USHORT temp,tempcl,tempch;

     SiS_LongDelay(SiS_Pr, 1);
     tempcl = 3;
     tempch = 0;

     do {
       temp = SiS_GetCH701x(SiS_Pr,0x66);
       temp &= 0x04;  /* PLL stable? -> bail out */
       if(temp == 0x04) break;

       if(HwInfo->jChipType == SIS_740) {
          /* Power down LVDS output, PLL normal operation */
          SiS_SetCH701x(SiS_Pr,0xac76);
       }

       SiS_SetCH701xForLCD(SiS_Pr,HwInfo);

       if(tempcl == 0) {
           if(tempch == 3) break;
	   SiS_ChrontelResetDB(SiS_Pr,HwInfo);
	   tempcl = 3;
	   tempch++;
       }
       tempcl--;
       temp = SiS_GetCH701x(SiS_Pr,0x76);
       temp &= 0xfb;  /* Reset PLL */
       SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x76);
       SiS_LongDelay(SiS_Pr,2);
       temp = SiS_GetCH701x(SiS_Pr,0x76);
       temp |= 0x04;  /* PLL normal operation */
       SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x76);
       if(HwInfo->jChipType == SIS_740) {
          SiS_SetCH701x(SiS_Pr,0xe078);	/* PLL loop filter */
       } else {
          SiS_SetCH701x(SiS_Pr,0x6078);
       }
       SiS_LongDelay(SiS_Pr,2);
    } while(0);

    SiS_SetCH701x(SiS_Pr,0x0077);  /* MV? */
}

void
SiS_ChrontelDoSomething1(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
     USHORT temp;

     temp = SiS_GetCH701x(SiS_Pr,0x03);
     temp |= 0x80;	/* Set datapath 1 to TV   */
     temp &= 0xbf;	/* Set datapath 2 to LVDS */
     SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x03);

     if(HwInfo->jChipType == SIS_740) {

        temp = SiS_GetCH701x(SiS_Pr,0x1c);
        temp &= 0xfb;	/* Normal XCLK phase */
        SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x1c);

        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2d,0x03);

        temp = SiS_GetCH701x(SiS_Pr,0x64);
        temp |= 0x40;	/* ? Bit not defined */
        SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x64);

        temp = SiS_GetCH701x(SiS_Pr,0x03);
        temp &= 0x3f;	/* D1 input to both LVDS and TV */
        SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x03);

	if(SiS_Pr->SiS_CustomT == CUT_ASUSL3000D) {
	   SiS_SetCH701x(SiS_Pr,0x4063); /* LVDS off */
	   SiS_LongDelay(SiS_Pr, 1);
	   SiS_SetCH701x(SiS_Pr,0x0063); /* LVDS on */
	   SiS_ChrontelResetDB(SiS_Pr, HwInfo);
	   SiS_ChrontelDoSomething2(SiS_Pr, HwInfo);
	   SiS_ChrontelDoSomething3(SiS_Pr, 0, HwInfo);
	} else {
           temp = SiS_GetCH701x(SiS_Pr,0x66);
           if(temp != 0x45) {
              SiS_ChrontelResetDB(SiS_Pr, HwInfo);
              SiS_ChrontelDoSomething2(SiS_Pr, HwInfo);
              SiS_ChrontelDoSomething3(SiS_Pr, 0, HwInfo);
           }
	}

     } else { /* 650 */

        SiS_ChrontelResetDB(SiS_Pr,HwInfo);
        SiS_ChrontelDoSomething2(SiS_Pr,HwInfo);
        temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x34);
        SiS_ChrontelDoSomething3(SiS_Pr,temp,HwInfo);
        SiS_SetCH701x(SiS_Pr,0xaf76);  /* All power on, LVDS normal operation */

     }

}
#endif  /* 315 series  */

/*********************************************/
/*      MAIN: SET CRT2 REGISTER GROUP        */
/*********************************************/

BOOLEAN
SiS_SetCRT2Group(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo, USHORT ModeNo)
{
#ifdef SIS300
   UCHAR  *ROMAddr  = HwInfo->pjVirtualRomBase;
#endif
   USHORT ModeIdIndex, RefreshRateTableIndex;
#if 0
   USHORT temp;
#endif

   SiS_Pr->SiS_SetFlag |= ProgrammingCRT2;

   if(!SiS_Pr->UseCustomMode) {
      SiS_SearchModeID(SiS_Pr, &ModeNo, &ModeIdIndex);
   } else {
      ModeIdIndex = 0;
   }

   /* Used for shifting CR33 */
   SiS_Pr->SiS_SelectCRT2Rate = 4;

   SiS_UnLockCRT2(SiS_Pr, HwInfo);

   RefreshRateTableIndex = SiS_GetRatePtr(SiS_Pr, ModeNo, ModeIdIndex, HwInfo);

   SiS_SaveCRT2Info(SiS_Pr,ModeNo);

   if(SiS_Pr->SiS_SetFlag & LowModeTests) {
      SiS_DisableBridge(SiS_Pr,HwInfo);
      if((SiS_Pr->SiS_IF_DEF_LVDS == 1) && (HwInfo->jChipType == SIS_730)) {
         SiS_SetReg(SiS_Pr->SiS_Part1Port,0x00,0x80);
      }
      SiS_SetCRT2ModeRegs(SiS_Pr, ModeNo, ModeIdIndex, HwInfo);
   }

   if(SiS_Pr->SiS_VBInfo & DisableCRT2Display) {
      SiS_LockCRT2(SiS_Pr, HwInfo);
      SiS_DisplayOn(SiS_Pr);
      return TRUE;
   }

   SiS_GetCRT2Data(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex, HwInfo);

   /* Set up Panel Link for LVDS, 301BDH and 30xLV(for LCDA) */
   if( (SiS_Pr->SiS_IF_DEF_LVDS == 1) ||
       ((SiS_Pr->SiS_VBType & VB_NoLCD) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) ||
       ((HwInfo->jChipType >= SIS_315H) && (SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)) ) {
      SiS_GetLVDSDesData(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex, HwInfo);
   } else {
      SiS_Pr->SiS_LCDHDES = SiS_Pr->SiS_LCDVDES = 0;
   }

#ifdef LINUX_XF86
#ifdef TWDEBUG
  xf86DrvMsg(0, X_INFO, "(init301: LCDHDES 0x%03x LCDVDES 0x%03x)\n", SiS_Pr->SiS_LCDHDES, SiS_Pr->SiS_LCDVDES);
  xf86DrvMsg(0, X_INFO, "(init301: HDE     0x%03x VDE     0x%03x)\n", SiS_Pr->SiS_HDE, SiS_Pr->SiS_VDE);
  xf86DrvMsg(0, X_INFO, "(init301: VGAHDE  0x%03x VGAVDE  0x%03x)\n", SiS_Pr->SiS_VGAHDE, SiS_Pr->SiS_VGAVDE);
  xf86DrvMsg(0, X_INFO, "(init301: HT      0x%03x VT      0x%03x)\n", SiS_Pr->SiS_HT, SiS_Pr->SiS_VT);
  xf86DrvMsg(0, X_INFO, "(init301: VGAHT   0x%03x VGAVT   0x%03x)\n", SiS_Pr->SiS_VGAHT, SiS_Pr->SiS_VGAVT);
#endif
#endif

   if(SiS_Pr->SiS_SetFlag & LowModeTests) {
      SiS_SetGroup1(SiS_Pr, ModeNo, ModeIdIndex, HwInfo, RefreshRateTableIndex);
   }

   if(SiS_Pr->SiS_VBType & VB_SISVB) {

        if(SiS_Pr->SiS_SetFlag & LowModeTests) {

	   SiS_SetGroup2(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex, HwInfo);
#ifdef SIS315H
	   SiS_SetGroup2_C_ELV(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex, HwInfo);
#endif
      	   SiS_SetGroup3(SiS_Pr, ModeNo, ModeIdIndex, HwInfo);
      	   SiS_SetGroup4(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex, HwInfo);
#ifdef SIS315H
	   SiS_SetGroup4_C_ELV(SiS_Pr, HwInfo);
#endif
      	   SiS_SetGroup5(SiS_Pr, ModeNo, ModeIdIndex, HwInfo);

	   /* For 301BDH (Panel link initialization): */
	   if((SiS_Pr->SiS_VBType & VB_NoLCD) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) {
	      if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {
		 if(!((SiS_Pr->SiS_SetFlag & SetDOSMode) && ((ModeNo == 0x03) || (ModeNo == 0x10)))) {
		    if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
		       SiS_ModCRT1CRTC(SiS_Pr,ModeNo,ModeIdIndex,
		                       RefreshRateTableIndex,HwInfo);
		    }
                 }
	      }
	      SiS_SetCRT2ECLK(SiS_Pr,ModeNo,ModeIdIndex,
		              RefreshRateTableIndex,HwInfo);
	   }
        }

   } else {

        if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {
	   if(SiS_Pr->SiS_IF_DEF_TRUMPION == 0) {
    	      SiS_ModCRT1CRTC(SiS_Pr,ModeNo,ModeIdIndex,
	                      RefreshRateTableIndex,HwInfo);
	   }
	}

        SiS_SetCRT2ECLK(SiS_Pr,ModeNo,ModeIdIndex,
	                RefreshRateTableIndex,HwInfo);

	if(SiS_Pr->SiS_SetFlag & LowModeTests) {
     	   if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
	      if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
	         if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
#ifdef SIS315H
		    SiS_SetCH701xForLCD(SiS_Pr,HwInfo);
#endif
		 }
	      }
	      if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
       		 SiS_SetCHTVReg(SiS_Pr,ModeNo,ModeIdIndex,
		               RefreshRateTableIndex);
	      }
     	   }
	}

   }

#ifdef SIS300
   if(HwInfo->jChipType < SIS_315H) {
      if(SiS_Pr->SiS_SetFlag & LowModeTests) {
	 if(SiS_Pr->SiS_UseOEM) {
	    if((SiS_Pr->SiS_UseROM) && ROMAddr && (SiS_Pr->SiS_UseOEM == -1)) {
	       if((ROMAddr[0x233] == 0x12) && (ROMAddr[0x234] == 0x34)) {
	          SiS_OEM300Setting(SiS_Pr,HwInfo,ModeNo,ModeIdIndex,
	       			    RefreshRateTableIndex);
	       }
	    } else {
       	       SiS_OEM300Setting(SiS_Pr,HwInfo,ModeNo,ModeIdIndex,
	       			 RefreshRateTableIndex);
	    }
	 }
	 if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
            if((SiS_Pr->SiS_CustomT == CUT_BARCO1366) ||
	       (SiS_Pr->SiS_CustomT == CUT_BARCO1024)) {
	       SetOEMLCDData2(SiS_Pr, HwInfo, ModeNo, ModeIdIndex,RefreshRateTableIndex);
	    }
            if(HwInfo->jChipType == SIS_730) {
               SiS_DisplayOn(SiS_Pr);
	    }
         }
      }
      if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
          if(HwInfo->jChipType != SIS_730) {
             SiS_DisplayOn(SiS_Pr);
	  }
      }
   }
#endif

#ifdef SIS315H
   if(HwInfo->jChipType >= SIS_315H) {
      if(SiS_Pr->SiS_SetFlag & LowModeTests) {
	 if(HwInfo->jChipType < SIS_661) {
	    SiS_FinalizeLCD(SiS_Pr, ModeNo, ModeIdIndex, HwInfo);
            if(SiS_Pr->SiS_UseOEM) {
               SiS_OEM310Setting(SiS_Pr, HwInfo, ModeNo, ModeIdIndex);
            }
	 } else {
	    SiS_OEM661Setting(SiS_Pr, HwInfo, ModeNo, ModeIdIndex, RefreshRateTableIndex);
	 }
         SiS_CRT2AutoThreshold(SiS_Pr);
      }
   }
#endif

   if(SiS_Pr->SiS_SetFlag & LowModeTests) {
      SiS_EnableBridge(SiS_Pr, HwInfo);
   }

   SiS_DisplayOn(SiS_Pr);

   if(SiS_Pr->SiS_IF_DEF_CH70xx == 1) {
      if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
	 /* Disable LCD panel when using TV */
	 SiS_SetRegSR11ANDOR(SiS_Pr,HwInfo,0xFF,0x0C);
      } else {
	 /* Disable TV when using LCD */
	 SiS_SetCH70xxANDOR(SiS_Pr,0x010E,0xF8);
      }
   }

   if(SiS_Pr->SiS_SetFlag & LowModeTests) {
      SiS_LockCRT2(SiS_Pr,HwInfo);
   }

   return TRUE;
}


/*********************************************/
/*     ENABLE/DISABLE LCD BACKLIGHT (SIS)    */
/*********************************************/

void
SiS_SiS30xBLOn(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  /* Switch on LCD backlight on SiS30xLV */
  SiS_DDC2Delay(SiS_Pr,0xff00);
  if(!(SiS_GetReg(SiS_Pr->SiS_Part4Port,0x26) & 0x02)) {
     SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x02);
     SiS_WaitVBRetrace(SiS_Pr,HwInfo);
  }
  if(!(SiS_GetReg(SiS_Pr->SiS_Part4Port,0x26) & 0x01)) {
     SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x01);
  }
}

void
SiS_SiS30xBLOff(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  /* Switch off LCD backlight on SiS30xLV */
  SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x26,0xFE);
  SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x26,0xFD);
  SiS_DDC2Delay(SiS_Pr,0xe000);
}

/*********************************************/
/*          DDC RELATED FUNCTIONS            */
/*********************************************/

static void
SiS_SetupDDCN(SiS_Private *SiS_Pr)
{
  SiS_Pr->SiS_DDC_NData = ~SiS_Pr->SiS_DDC_Data;
  SiS_Pr->SiS_DDC_NClk  = ~SiS_Pr->SiS_DDC_Clk;
  if((SiS_Pr->SiS_DDC_Index == 0x11) && (SiS_Pr->SiS_SensibleSR11)) {
     SiS_Pr->SiS_DDC_NData &= 0x0f;
     SiS_Pr->SiS_DDC_NClk  &= 0x0f;
  }
}

/* The Chrontel 700x is connected to the 630/730 via
 * the 630/730's DDC/I2C port.
 *
 * On 630(S)T chipset, the index changed from 0x11 to
 * 0x0a, possibly for working around the DDC problems
 */

static BOOLEAN
SiS_SetChReg(SiS_Private *SiS_Pr, USHORT tempbx, USHORT myor)
{
  USHORT tempah,temp,i;

  for(i=0; i<20; i++) {				/* Do 20 attempts to write */
     if(i) {
        SiS_SetStop(SiS_Pr);
	SiS_DDC2Delay(SiS_Pr,SiS_I2CDELAYSHORT);
     }
     if(SiS_SetStart(SiS_Pr)) continue;		/* Set start condition */
     tempah = SiS_Pr->SiS_DDC_DeviceAddr;
     temp = SiS_WriteDDC2Data(SiS_Pr,tempah);	/* Write DAB (S0=0=write) */
     if(temp) continue;				/*    (ERROR: no ack) */
     tempah = tempbx & 0x00FF;			/* Write RAB */
     tempah |= myor;                            /* (700x: set bit 7, see datasheet) */
     temp = SiS_WriteDDC2Data(SiS_Pr,tempah);
     if(temp) continue;				/*    (ERROR: no ack) */
     tempah = (tempbx & 0xFF00) >> 8;
     temp = SiS_WriteDDC2Data(SiS_Pr,tempah);	/* Write data */
     if(temp) continue;				/*    (ERROR: no ack) */
     if(SiS_SetStop(SiS_Pr)) continue;		/* Set stop condition */
     SiS_Pr->SiS_ChrontelInit = 1;
     return TRUE;
  }
  return FALSE;
}

/* Write to Chrontel 700x */
/* Parameter is [Data (S15-S8) | Register no (S7-S0)] */
void
SiS_SetCH700x(SiS_Private *SiS_Pr, USHORT tempbx)
{
  SiS_Pr->SiS_DDC_DeviceAddr = 0xEA;  		/* DAB (Device Address Byte) */

  if(!(SiS_Pr->SiS_ChrontelInit)) {
     SiS_Pr->SiS_DDC_Index = 0x11;		/* Bit 0 = SC;  Bit 1 = SD */
     SiS_Pr->SiS_DDC_Data  = 0x02;              /* Bitmask in IndexReg for Data */
     SiS_Pr->SiS_DDC_Clk   = 0x01;              /* Bitmask in IndexReg for Clk */
     SiS_SetupDDCN(SiS_Pr);
  }

  if( (!(SiS_SetChReg(SiS_Pr, tempbx, 0x80))) &&
      (!(SiS_Pr->SiS_ChrontelInit)) ) {
     SiS_Pr->SiS_DDC_Index = 0x0a;		/* Bit 7 = SC;  Bit 6 = SD */
     SiS_Pr->SiS_DDC_Data  = 0x80;              /* Bitmask in IndexReg for Data */
     SiS_Pr->SiS_DDC_Clk   = 0x40;              /* Bitmask in IndexReg for Clk */
     SiS_SetupDDCN(SiS_Pr);

     SiS_SetChReg(SiS_Pr, tempbx, 0x80);
  }
}

/* Write to Chrontel 701x */
/* Parameter is [Data (S15-S8) | Register no (S7-S0)] */
void
SiS_SetCH701x(SiS_Private *SiS_Pr, USHORT tempbx)
{
  SiS_Pr->SiS_DDC_Index = 0x11;			/* Bit 0 = SC;  Bit 1 = SD */
  SiS_Pr->SiS_DDC_Data  = 0x08;                 /* Bitmask in IndexReg for Data */
  SiS_Pr->SiS_DDC_Clk   = 0x04;                 /* Bitmask in IndexReg for Clk */
  SiS_SetupDDCN(SiS_Pr);
  SiS_Pr->SiS_DDC_DeviceAddr = 0xEA;  		/* DAB (Device Address Byte) */

  SiS_SetChReg(SiS_Pr, tempbx, 0);
}

void
SiS_SetCH70xx(SiS_Private *SiS_Pr, USHORT tempbx)
{
   if(SiS_Pr->SiS_IF_DEF_CH70xx == 1)
      SiS_SetCH700x(SiS_Pr,tempbx);
   else
      SiS_SetCH701x(SiS_Pr,tempbx);
}

static USHORT
SiS_GetChReg(SiS_Private *SiS_Pr, USHORT myor)
{
  USHORT tempah,temp,i;

  for(i=0; i<20; i++) {				/* Do 20 attempts to read */
     if(i) {
        SiS_SetStop(SiS_Pr);
	SiS_DDC2Delay(SiS_Pr,SiS_I2CDELAYSHORT);
     }
     if(SiS_SetStart(SiS_Pr)) continue;		/* Set start condition */
     tempah = SiS_Pr->SiS_DDC_DeviceAddr;
     temp = SiS_WriteDDC2Data(SiS_Pr,tempah);	/* Write DAB (S0=0=write) */
     if(temp) continue;				/*        (ERROR: no ack) */
     tempah = SiS_Pr->SiS_DDC_ReadAddr | myor;	/* Write RAB (700x: | 0x80) */
     temp = SiS_WriteDDC2Data(SiS_Pr,tempah);
     if(temp) continue;				/*        (ERROR: no ack) */
     if (SiS_SetStart(SiS_Pr)) continue;	/* Re-start */
     tempah = SiS_Pr->SiS_DDC_DeviceAddr | 0x01;/* DAB | 0x01 = Read */
     temp = SiS_WriteDDC2Data(SiS_Pr,tempah);	/* DAB (S0=1=read) */
     if(temp) continue;				/*        (ERROR: no ack) */
     tempah = SiS_ReadDDC2Data(SiS_Pr,tempah);	/* Read byte */
     if(SiS_SetStop(SiS_Pr)) continue;		/* Stop condition */
     SiS_Pr->SiS_ChrontelInit = 1;
     return(tempah);
  }
  return 0xFFFF;
}

/* Read from Chrontel 700x */
/* Parameter is [Register no (S7-S0)] */
USHORT
SiS_GetCH700x(SiS_Private *SiS_Pr, USHORT tempbx)
{
  USHORT result;

  SiS_Pr->SiS_DDC_DeviceAddr = 0xEA;		/* DAB */

  if(!(SiS_Pr->SiS_ChrontelInit)) {
     SiS_Pr->SiS_DDC_Index = 0x11;		/* Bit 0 = SC;  Bit 1 = SD */
     SiS_Pr->SiS_DDC_Data  = 0x02;              /* Bitmask in IndexReg for Data */
     SiS_Pr->SiS_DDC_Clk   = 0x01;              /* Bitmask in IndexReg for Clk */
     SiS_SetupDDCN(SiS_Pr);
  }

  SiS_Pr->SiS_DDC_ReadAddr = tempbx;

  if( ((result = SiS_GetChReg(SiS_Pr,0x80)) == 0xFFFF) &&
      (!SiS_Pr->SiS_ChrontelInit) ) {

     SiS_Pr->SiS_DDC_Index = 0x0a;
     SiS_Pr->SiS_DDC_Data  = 0x80;
     SiS_Pr->SiS_DDC_Clk   = 0x40;
     SiS_SetupDDCN(SiS_Pr);

     result = SiS_GetChReg(SiS_Pr,0x80);
  }
  return(result);
}

/* Read from Chrontel 701x */
/* Parameter is [Register no (S7-S0)] */
USHORT
SiS_GetCH701x(SiS_Private *SiS_Pr, USHORT tempbx)
{
  SiS_Pr->SiS_DDC_Index = 0x11;			/* Bit 0 = SC;  Bit 1 = SD */
  SiS_Pr->SiS_DDC_Data  = 0x08;                 /* Bitmask in IndexReg for Data */
  SiS_Pr->SiS_DDC_Clk   = 0x04;                 /* Bitmask in IndexReg for Clk */
  SiS_SetupDDCN(SiS_Pr);
  SiS_Pr->SiS_DDC_DeviceAddr = 0xEA;		/* DAB */

  SiS_Pr->SiS_DDC_ReadAddr = tempbx;

  return(SiS_GetChReg(SiS_Pr,0));
}

/* Read from Chrontel 70xx */
/* Parameter is [Register no (S7-S0)] */
USHORT
SiS_GetCH70xx(SiS_Private *SiS_Pr, USHORT tempbx)
{
   if(SiS_Pr->SiS_IF_DEF_CH70xx == 1)
      return(SiS_GetCH700x(SiS_Pr, tempbx));
   else
      return(SiS_GetCH701x(SiS_Pr, tempbx));
}

/* Our own DDC functions */
USHORT
SiS_InitDDCRegs(SiS_Private *SiS_Pr, unsigned long VBFlags, int VGAEngine,
                USHORT adaptnum, USHORT DDCdatatype, BOOLEAN checkcr32)
{
     unsigned char ddcdtype[] = { 0xa0, 0xa0, 0xa0, 0xa2, 0xa6 };
     unsigned char flag, cr32;
     USHORT        temp = 0, myadaptnum = adaptnum;

     if(adaptnum != 0) {
        if(!(VBFlags & (VB_301|VB_301B|VB_301C|VB_302B))) return 0xFFFF;
	if((VBFlags & VB_30xBDH) && (adaptnum == 1)) return 0xFFFF;
     }	
     
     /* adapternum for SiS bridges: 0 = CRT1, 1 = LCD, 2 = VGA2 */
     
     SiS_Pr->SiS_ChrontelInit = 0;   /* force re-detection! */

     SiS_Pr->SiS_DDC_SecAddr = 0;
     SiS_Pr->SiS_DDC_DeviceAddr = ddcdtype[DDCdatatype];
     SiS_Pr->SiS_DDC_Port = SiS_Pr->SiS_P3c4;
     SiS_Pr->SiS_DDC_Index = 0x11;
     flag = 0xff;

     cr32 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x32);

#if 0
     if(VBFlags & VB_SISBRIDGE) {
	if(myadaptnum == 0) {
	   if(!(cr32 & 0x20)) {
	      myadaptnum = 2;
	      if(!(cr32 & 0x10)) {
	         myadaptnum = 1;
		 if(!(cr32 & 0x08)) {
		    myadaptnum = 0;
		 }
	      }
	   }
        }
     }
#endif

     if(VGAEngine == SIS_300_VGA) {		/* 300 series */
	
        if(myadaptnum != 0) {
	   flag = 0;
	   if(VBFlags & VB_SISBRIDGE) {
	      SiS_Pr->SiS_DDC_Port = SiS_Pr->SiS_Part4Port;
              SiS_Pr->SiS_DDC_Index = 0x0f;
	   }
        }

	if(!(VBFlags & VB_301)) {
	   if((cr32 & 0x80) && (checkcr32)) {
              if(myadaptnum >= 1) {
	         if(!(cr32 & 0x08)) {
	             myadaptnum = 1;
		     if(!(cr32 & 0x10)) return 0xFFFF;
                 }
	      }
	   }
	}

	temp = 4 - (myadaptnum * 2);
	if(flag) temp = 0;

     } else {						/* 315/330 series */

     	/* here we simplify: 0 = CRT1, 1 = CRT2 (VGA, LCD) */

	if(VBFlags & VB_SISBRIDGE) {
	   if(myadaptnum == 2) {
	      myadaptnum = 1;
           }
	}

        if(myadaptnum == 1) {
     	   flag = 0;
	   if(VBFlags & VB_SISBRIDGE) {
	      SiS_Pr->SiS_DDC_Port = SiS_Pr->SiS_Part4Port;
              SiS_Pr->SiS_DDC_Index = 0x0f;
	   }
        }

        if((cr32 & 0x80) && (checkcr32)) {
           if(myadaptnum >= 1) {
	      if(!(cr32 & 0x08)) {
	         myadaptnum = 1;
		 if(!(cr32 & 0x10)) return 0xFFFF;
	      }
	   }
        }

        temp = myadaptnum;
        if(myadaptnum == 1) {
           temp = 0;
	   if(VBFlags & VB_LVDS) flag = 0xff;
        }

	if(flag) temp = 0;
    }
    
    SiS_Pr->SiS_DDC_Data = 0x02 << temp;
    SiS_Pr->SiS_DDC_Clk  = 0x01 << temp;

    SiS_SetupDDCN(SiS_Pr);

#ifdef TWDEBUG
    xf86DrvMsg(0, X_INFO, "DDC Port %x Index %x Shift %d\n",
    		SiS_Pr->SiS_DDC_Port, SiS_Pr->SiS_DDC_Index, temp);
#endif
    
    return 0;
}

USHORT
SiS_WriteDABDDC(SiS_Private *SiS_Pr)
{
   if(SiS_SetStart(SiS_Pr)) return 0xFFFF;
   if(SiS_WriteDDC2Data(SiS_Pr, SiS_Pr->SiS_DDC_DeviceAddr)) {
  	return 0xFFFF;
   }
   if(SiS_WriteDDC2Data(SiS_Pr, SiS_Pr->SiS_DDC_SecAddr)) {
   	return 0xFFFF;
   }
   return(0);
}

USHORT
SiS_PrepareReadDDC(SiS_Private *SiS_Pr)
{
   if(SiS_SetStart(SiS_Pr)) return 0xFFFF;
   if(SiS_WriteDDC2Data(SiS_Pr, (SiS_Pr->SiS_DDC_DeviceAddr | 0x01))) {
   	return 0xFFFF;
   }
   return(0);
}

USHORT
SiS_PrepareDDC(SiS_Private *SiS_Pr)
{
   if(SiS_WriteDABDDC(SiS_Pr)) SiS_WriteDABDDC(SiS_Pr);
   if(SiS_PrepareReadDDC(SiS_Pr)) return(SiS_PrepareReadDDC(SiS_Pr));
   return(0);
}

void
SiS_SendACK(SiS_Private *SiS_Pr, USHORT yesno)
{
   SiS_SetSCLKLow(SiS_Pr);
   if(yesno) {
      SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
      		      SiS_Pr->SiS_DDC_Index,
                      SiS_Pr->SiS_DDC_NData,
		      SiS_Pr->SiS_DDC_Data);
   } else {
      SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
      		      SiS_Pr->SiS_DDC_Index,
                      SiS_Pr->SiS_DDC_NData,
		      0);
   }
   SiS_SetSCLKHigh(SiS_Pr);
}

USHORT
SiS_DoProbeDDC(SiS_Private *SiS_Pr)
{
    unsigned char mask, value;
    USHORT  temp, ret=0;
    BOOLEAN failed = FALSE;

    SiS_SetSwitchDDC2(SiS_Pr);
    if(SiS_PrepareDDC(SiS_Pr)) {
         SiS_SetStop(SiS_Pr);
#ifdef TWDEBUG
         xf86DrvMsg(0, X_INFO, "Probe: Prepare failed\n");
#endif
         return(0xFFFF);
    }
    mask = 0xf0;
    value = 0x20;
    if(SiS_Pr->SiS_DDC_DeviceAddr == 0xa0) {
       temp = (unsigned char)SiS_ReadDDC2Data(SiS_Pr, 0);
       SiS_SendACK(SiS_Pr, 0);
       if(temp == 0) {
           mask = 0xff;
	   value = 0xff;
       } else {
           failed = TRUE;
	   ret = 0xFFFF;
#ifdef TWDEBUG
           xf86DrvMsg(0, X_INFO, "Probe: Read 1 failed\n");
#endif
       }
    }
    if(failed == FALSE) {
       temp = (unsigned char)SiS_ReadDDC2Data(SiS_Pr, 0);
       SiS_SendACK(SiS_Pr, 1);
       temp &= mask;
       if(temp == value) ret = 0;
       else {
          ret = 0xFFFF;
#ifdef TWDEBUG
          xf86DrvMsg(0, X_INFO, "Probe: Read 2 failed\n");
#endif
          if(SiS_Pr->SiS_DDC_DeviceAddr == 0xa0) {
             if(temp == 0x30) ret = 0;
          }
       }
    }
    SiS_SetStop(SiS_Pr);
    return(ret);
}

USHORT
SiS_ProbeDDC(SiS_Private *SiS_Pr)
{
   USHORT flag;

   flag = 0x180;
   SiS_Pr->SiS_DDC_DeviceAddr = 0xa0;
   if(!(SiS_DoProbeDDC(SiS_Pr))) flag |= 0x02;
   SiS_Pr->SiS_DDC_DeviceAddr = 0xa2;
   if(!(SiS_DoProbeDDC(SiS_Pr))) flag |= 0x08;
   SiS_Pr->SiS_DDC_DeviceAddr = 0xa6;
   if(!(SiS_DoProbeDDC(SiS_Pr))) flag |= 0x10;
   if(!(flag & 0x1a)) flag = 0;
   return(flag);
}

USHORT
SiS_ReadDDC(SiS_Private *SiS_Pr, USHORT DDCdatatype, unsigned char *buffer)
{
   USHORT flag, length, i;
   unsigned char chksum,gotcha;

   if(DDCdatatype > 4) return 0xFFFF;  

   flag = 0;
   SiS_SetSwitchDDC2(SiS_Pr);
   if(!(SiS_PrepareDDC(SiS_Pr))) {
      length = 127;
      if(DDCdatatype != 1) length = 255;
      chksum = 0;
      gotcha = 0;
      for(i=0; i<length; i++) {
         buffer[i] = (unsigned char)SiS_ReadDDC2Data(SiS_Pr, 0);
	 chksum += buffer[i];
	 gotcha |= buffer[i];
	 SiS_SendACK(SiS_Pr, 0);
      }
      buffer[i] = (unsigned char)SiS_ReadDDC2Data(SiS_Pr, 0);
      chksum += buffer[i];
      SiS_SendACK(SiS_Pr, 1);
      if(gotcha) flag = (USHORT)chksum;
      else flag = 0xFFFF;
   } else {
      flag = 0xFFFF;
   }
   SiS_SetStop(SiS_Pr);
   return(flag);
}

/* Our private DDC functions

   It complies somewhat with the corresponding VESA function
   in arguments and return values.

   Since this is probably called before the mode is changed,
   we use our pre-detected pSiS-values instead of SiS_Pr as
   regards chipset and video bridge type.

   Arguments:
       adaptnum: 0=CRT1, 1=LCD, 2=VGA2
                 CRT2 DDC is only supported on SiS301, 301B, 302B.
       DDCdatatype: 0=Probe, 1=EDID, 2=EDID+VDIF, 3=EDID V2 (P&D), 4=EDID V2 (FPDI-2)
       buffer: ptr to 256 data bytes which will be filled with read data.

   Returns 0xFFFF if error, otherwise
       if DDCdatatype > 0:  Returns 0 if reading OK (included a correct checksum)
       if DDCdatatype = 0:  Returns supported DDC modes

 */
USHORT
SiS_HandleDDC(SiS_Private *SiS_Pr, unsigned long VBFlags, int VGAEngine,
              USHORT adaptnum, USHORT DDCdatatype, unsigned char *buffer)
{
   unsigned char sr1f,cr17=1;
   USHORT result;

   if(adaptnum > 2) return 0xFFFF;
   if(DDCdatatype > 4) return 0xFFFF;
   if((!(VBFlags & VB_VIDEOBRIDGE)) && (adaptnum > 0)) return 0xFFFF;
   if(SiS_InitDDCRegs(SiS_Pr, VBFlags, VGAEngine, adaptnum, DDCdatatype, FALSE) == 0xFFFF) return 0xFFFF;

   sr1f = SiS_GetReg(SiS_Pr->SiS_P3c4,0x1f);
   SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x1f,0x3f,0x04);
   if(VGAEngine == SIS_300_VGA) {
      cr17 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x17) & 0x80;
      if(!cr17) {
         SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x17,0x80);
         SiS_SetReg(SiS_Pr->SiS_P3c4,0x00,0x01);
         SiS_SetReg(SiS_Pr->SiS_P3c4,0x00,0x03);
      }
   }
   if((sr1f) || (!cr17)) {
      SiS_WaitRetrace1(SiS_Pr);
      SiS_WaitRetrace1(SiS_Pr);
      SiS_WaitRetrace1(SiS_Pr);
      SiS_WaitRetrace1(SiS_Pr);
   }

   if(DDCdatatype == 0) {
      result = SiS_ProbeDDC(SiS_Pr);
   } else {
      result = SiS_ReadDDC(SiS_Pr, DDCdatatype, buffer);
   }
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x1f,sr1f);
   if(VGAEngine == SIS_300_VGA) {
      SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x17,0x7f,cr17);
   }
   return result;
}

#ifdef LINUX_XF86

static BOOLEAN
checkedid1(unsigned char *buffer)
{
   /* Check header */
   if((buffer[0] != 0x00) ||
      (buffer[1] != 0xff) ||
      (buffer[2] != 0xff) ||
      (buffer[3] != 0xff) ||
      (buffer[4] != 0xff) ||
      (buffer[5] != 0xff) ||
      (buffer[6] != 0xff) ||
      (buffer[7] != 0x00))
      return FALSE;

   /* Check EDID version and revision */
   if((buffer[0x12] != 1) || (buffer[0x13] > 4)) return FALSE;

   /* Check week of manufacture for sanity */
   if(buffer[0x10] > 53) return FALSE;

   /* Check year of manufacture for sanity */
   if(buffer[0x11] > 40) return FALSE;

   return TRUE;
}

static BOOLEAN
checkedid2(unsigned char *buffer)
{
   USHORT year = buffer[6] | (buffer[7] << 8);

   /* Check EDID version */
   if((buffer[0] & 0xf0) != 0x20) return FALSE;

   /* Check week of manufacture for sanity */
   if(buffer[5] > 53) return FALSE;

   /* Check year of manufacture for sanity */
   if((year != 0) && ((year < 1990) || (year > 2030))) return FALSE;

   return TRUE;
}

/* Sense the LCD parameters (CR36, CR37) via DDC */
/* SiS30x(B) only */
USHORT
SiS_SenseLCDDDC(SiS_Private *SiS_Pr, SISPtr pSiS)
{
   USHORT DDCdatatype, paneltype, flag, xres=0, yres=0;
   USHORT index, myindex, lumsize, numcodes;
   unsigned char cr37=0, seekcode;
   BOOLEAN checkexpand = FALSE;
   int retry, i;
   unsigned char buffer[256];

   for(i=0; i<7; i++) SiS_Pr->CP_DataValid[i] = FALSE;
   SiS_Pr->CP_HaveCustomData = FALSE;
   SiS_Pr->CP_MaxX = SiS_Pr->CP_MaxY = SiS_Pr->CP_MaxClock = 0;

   if(!(pSiS->VBFlags & (VB_301|VB_301B|VB_301C|VB_302B))) return 0;
   if(pSiS->VBFlags & VB_30xBDH) return 0;
  
   if(SiS_InitDDCRegs(SiS_Pr, pSiS->VBFlags, pSiS->VGAEngine, 1, 0, FALSE) == 0xFFFF) return 0;
   
   SiS_Pr->SiS_DDC_SecAddr = 0x00;
   
   /* Probe supported DA's */
   flag = SiS_ProbeDDC(SiS_Pr);
#ifdef TWDEBUG
   xf86DrvMsg(pSiS->pScrn->scrnIndex, X_INFO,
   	"CRT2 DDC capabilities 0x%x\n", flag);
#endif	
   if(flag & 0x10) {
      SiS_Pr->SiS_DDC_DeviceAddr = 0xa6;	/* EDID V2 (FP) */
      DDCdatatype = 4;
   } else if(flag & 0x08) {
      SiS_Pr->SiS_DDC_DeviceAddr = 0xa2;	/* EDID V2 (P&D-D Monitor) */
      DDCdatatype = 3;
   } else if(flag & 0x02) {
      SiS_Pr->SiS_DDC_DeviceAddr = 0xa0;	/* EDID V1 */
      DDCdatatype = 1;
   } else return 0;				/* no DDC support (or no device attached) */
   
   /* Read the entire EDID */
   retry = 2;
   do {
      if(SiS_ReadDDC(SiS_Pr, DDCdatatype, buffer)) {
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	 	"CRT2: DDC read failed (attempt %d), %s\n",
		(3-retry), (retry == 1) ? "giving up" : "retrying");
	 retry--;
	 if(retry == 0) return 0xFFFF;
      } else break;
   } while(1);

#ifdef TWDEBUG
   for(i=0; i<256; i+=16) {
       xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
       	"%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
	buffer[i],    buffer[i+1], buffer[i+2], buffer[i+3],
	buffer[i+4],  buffer[i+5], buffer[i+6], buffer[i+7],
	buffer[i+8],  buffer[i+9], buffer[i+10], buffer[i+11],
	buffer[i+12], buffer[i+13], buffer[i+14], buffer[i+15]);
   }
#endif   
   
   /* Analyze EDID and retrieve LCD panel information */
   paneltype = 0;
   switch(DDCdatatype) {
   case 1:							/* Analyze EDID V1 */
      /* Catch a few clear cases: */
      if(!(checkedid1(buffer))) {
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	 	"CRT2: EDID corrupt\n");
	 return 0;
      }

      if(!(buffer[0x14] & 0x80)) {
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	        "CRT2: Attached display expects analog input (0x%02x)\n",
		buffer[0x14]);
      	 return 0;
      }

      if((buffer[0x18] & 0x18) != 0x08) {
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	 	"CRT2: Attached display is not of RGB but of %s type (0x%02x)\n",
		((buffer[0x18] & 0x18) == 0x00) ? "monochrome/greyscale" :
		  ( ((buffer[0x18] & 0x18) == 0x10) ? "non-RGB multicolor" :
		     "undefined"),
		buffer[0x18]);
	 return 0;
      }

      /* Now analyze the first Detailed Timing Block and see
       * if the preferred timing mode is stored there. If so,
       * check if this is a standard panel for which we already
       * know the timing.
       */

      paneltype = Panel_Custom;
      checkexpand = FALSE;

      if(buffer[0x18] & 0x02) {

         xres = buffer[0x38] | ((buffer[0x3a] & 0xf0) << 4);
         yres = buffer[0x3b] | ((buffer[0x3d] & 0xf0) << 4);

	 SiS_Pr->CP_PreferredX = xres;
	 SiS_Pr->CP_PreferredY = yres;

         switch(xres) {
            case 800:
	        if(yres == 600) {
	     	   paneltype = Panel_800x600;
	     	   checkexpand = TRUE;
	        }
	        break;
            case 1024:
	        if(yres == 768) {
	     	   paneltype = Panel_1024x768;
	     	   checkexpand = TRUE;
	        }
	        break;
	    case 1280:
	        if(yres == 1024) {
	     	   paneltype = Panel_1280x1024;
		   checkexpand = TRUE;
	        } else if(yres == 960) {
	           if(pSiS->VGAEngine == SIS_300_VGA) {
		      paneltype = Panel300_1280x960;
		   } else {
		      paneltype = Panel310_1280x960;
		   }
	        } else if(yres == 768) {
		   if( ((buffer[0x36] | (buffer[0x37] << 8)) == 8100) &&
		       ((buffer[0x39] | ((buffer[0x3a] & 0x0f) << 8)) == (1688 - 1280)) &&
		       ((buffer[0x3c] | ((buffer[0x3d] & 0x0f) << 8)) == (802 - 768)) ) {
	       	      paneltype = Panel_1280x768;
		      checkexpand = FALSE;
		      cr37 |= 0x10;
		   }
	        }
	        break;
	    case 1400:
	        if(pSiS->VGAEngine == SIS_315_VGA) {
	           if(yres == 1050) {
	              paneltype = Panel310_1400x1050;
		      checkexpand = TRUE;
	           }
	        }
      	        break;
#if 0	    /* Treat this as custom, as we have no valid timing data yet */
	    case 1600:
	        if(pSiS->VGAEngine == SIS_315_VGA) {
	           if(yres == 1200) {
	              paneltype = Panel310_1600x1200;
		      checkexpand = TRUE;
	           }
	        }
      	        break;
#endif
         }

	 if(paneltype != Panel_Custom) {
	    if((buffer[0x47] & 0x18) == 0x18) {
	       cr37 |= ((((buffer[0x47] & 0x06) ^ 0x06) << 5) | 0x20);
	    } else {
	       /* What now? There is no digital separate output timing... */
	       xf86DrvMsg(pSiS->pScrn->scrnIndex, X_WARNING,
	       	   "CRT2: Unable to retrieve Sync polarity information\n");
	       cr37 |= 0xc0;  /* Default */
	    }
	 }

      }

      /* If we still don't know what panel this is, we take it
       * as a custom panel and derive the timing data from the
       * detailed timing blocks
       */
      if(paneltype == Panel_Custom) {

         BOOLEAN havesync = FALSE;
	 int i, temp, base = 0x36;
	 unsigned long estpack;
	 unsigned short estx[] = {
	 	720, 720, 640, 640, 640, 640, 800, 800,
		800, 800, 832,1024,1024,1024,1024,1280,
		1152
	 };
	 unsigned short esty[] = {
	 	400, 400, 480, 480, 480, 480, 600, 600,
		600, 600, 624, 768, 768, 768, 768,1024,
		870
	 };

	 paneltype = 0;
	 SiS_Pr->CP_Supports64048075 = TRUE;

	 /* Find the maximum resolution */

	 /* 1. From Established timings */
	 estpack = (buffer[0x23] << 9) | (buffer[0x24] << 1) | ((buffer[0x25] >> 7) & 0x01);
	 for(i=16; i>=0; i--) {
	     if(estpack & (1 << i)) {
	        if(estx[16 - i] > SiS_Pr->CP_MaxX) SiS_Pr->CP_MaxX = estx[16 - i];
		if(esty[16 - i] > SiS_Pr->CP_MaxY) SiS_Pr->CP_MaxY = esty[16 - i];
	     }
	 }

	 /* 2. From Standard Timings */
	 for(i=0x26; i < 0x36; i+=2) {
	    if((buffer[i] != 0x01) && (buffer[i+1] != 0x01)) {
	       temp = (buffer[i] + 31) * 8;
	       if(temp > SiS_Pr->CP_MaxX) SiS_Pr->CP_MaxX = temp;
	       switch((buffer[i+1] & 0xc0) >> 6) {
	       case 0x03: temp = temp * 9 / 16; break;
	       case 0x02: temp = temp * 4 / 5;  break;
	       case 0x01: temp = temp * 3 / 4;  break;
	       }
	       if(temp > SiS_Pr->CP_MaxY) SiS_Pr->CP_MaxY = temp;
	    }
	 }

	 /* Now extract the Detailed Timings and convert them into modes */

         for(i = 0; i < 4; i++, base += 18) {

	    /* Is this a detailed timing block or a monitor descriptor? */
	    if(buffer[base] || buffer[base+1] || buffer[base+2]) {

      	       xres = buffer[base+2] | ((buffer[base+4] & 0xf0) << 4);
               yres = buffer[base+5] | ((buffer[base+7] & 0xf0) << 4);

	       SiS_Pr->CP_HDisplay[i] = xres;
	       SiS_Pr->CP_HSyncStart[i] = xres + (buffer[base+8] | ((buffer[base+11] & 0xc0) << 2));
               SiS_Pr->CP_HSyncEnd[i]   = SiS_Pr->CP_HSyncStart[i] + (buffer[base+9] | ((buffer[base+11] & 0x30) << 4));
	       SiS_Pr->CP_HTotal[i] = xres + (buffer[base+3] | ((buffer[base+4] & 0x0f) << 8));
	       SiS_Pr->CP_HBlankStart[i] = xres + 1;
	       SiS_Pr->CP_HBlankEnd[i] = SiS_Pr->CP_HTotal[i];

	       SiS_Pr->CP_VDisplay[i] = yres;
               SiS_Pr->CP_VSyncStart[i] = yres + (((buffer[base+10] & 0xf0) >> 4) | ((buffer[base+11] & 0x0c) << 2));
               SiS_Pr->CP_VSyncEnd[i] = SiS_Pr->CP_VSyncStart[i] + ((buffer[base+10] & 0x0f) | ((buffer[base+11] & 0x03) << 4));
	       SiS_Pr->CP_VTotal[i] = yres + (buffer[base+6] | ((buffer[base+7] & 0x0f) << 8));
	       SiS_Pr->CP_VBlankStart[i] = yres + 1;
	       SiS_Pr->CP_VBlankEnd[i] = SiS_Pr->CP_VTotal[i];

	       SiS_Pr->CP_Clock[i] = (buffer[base] | (buffer[base+1] << 8)) * 10;

	       SiS_Pr->CP_DataValid[i] = TRUE;

	       /* Sort out invalid timings, interlace and too high clocks */
	       if((SiS_Pr->CP_HDisplay[i] & 7)						||
	          (SiS_Pr->CP_HDisplay[i] > SiS_Pr->CP_HSyncStart[i])  			||
	          (SiS_Pr->CP_HDisplay[i] >= SiS_Pr->CP_HSyncEnd[i])   			||
	          (SiS_Pr->CP_HDisplay[i] >= SiS_Pr->CP_HTotal[i])     			||
	          (SiS_Pr->CP_HSyncStart[i] >= SiS_Pr->CP_HSyncEnd[i]) 			||
	          (SiS_Pr->CP_HSyncStart[i] > SiS_Pr->CP_HTotal[i])    			||
	          (SiS_Pr->CP_HSyncEnd[i] > SiS_Pr->CP_HTotal[i])      			||
	          (SiS_Pr->CP_VDisplay[i] > SiS_Pr->CP_VSyncStart[i])  			||
	          (SiS_Pr->CP_VDisplay[i] >= SiS_Pr->CP_VSyncEnd[i])   			||
	          (SiS_Pr->CP_VDisplay[i] >= SiS_Pr->CP_VTotal[i])     			||
	          (SiS_Pr->CP_VSyncStart[i] > SiS_Pr->CP_VSyncEnd[i])  			||
	          (SiS_Pr->CP_VSyncStart[i] > SiS_Pr->CP_VTotal[i])    			||
	          (SiS_Pr->CP_VSyncEnd[i] > SiS_Pr->CP_VTotal[i])      			||
		  (((pSiS->VBFlags & VB_301C) && (SiS_Pr->CP_Clock[i] > 162500)) ||
	           ((!(pSiS->VBFlags & VB_301C)) && (SiS_Pr->CP_Clock[i] > 108200)))	||
		  (buffer[base+17] & 0x80)) {

	          SiS_Pr->CP_DataValid[i] = FALSE;

	       } else {

	          paneltype = Panel_Custom;

		  SiS_Pr->CP_HaveCustomData = TRUE;

		  if(xres > SiS_Pr->CP_MaxX) SiS_Pr->CP_MaxX = xres;
	          if(yres > SiS_Pr->CP_MaxY) SiS_Pr->CP_MaxY = yres;
		  if(SiS_Pr->CP_Clock[i] > SiS_Pr->CP_MaxClock) SiS_Pr->CP_MaxClock = SiS_Pr->CP_Clock[i];

		  SiS_Pr->CP_Vendor = buffer[9] | (buffer[8] << 8);
		  SiS_Pr->CP_Product = buffer[10] | (buffer[11] << 8);

		  /* By default we drive the LCD at 75Hz in 640x480 mode; if
		   * the panel does not provide this mode, use 60hz
		   */
		  if(!(buffer[0x23] & 0x04)) SiS_Pr->CP_Supports64048075 = FALSE;

	          /* We must assume the panel can scale, since we have
	           * no scaling data
		   */
	          checkexpand = FALSE;
	          cr37 |= 0x10;

	          /* Extract the sync polarisation information. This only works
	           * if the Flags indicate a digital separate output.
	           */
	          if((buffer[base+17] & 0x18) == 0x18) {
		     SiS_Pr->CP_HSync_P[i] = (buffer[base+17] & 0x02) ? TRUE : FALSE;
		     SiS_Pr->CP_VSync_P[i] = (buffer[base+17] & 0x04) ? TRUE : FALSE;
		     SiS_Pr->CP_SyncValid[i] = TRUE;
		     if(!havesync) {
	                cr37 |= ((((buffer[base+17] & 0x06) ^ 0x06) << 5) | 0x20);
			havesync = TRUE;
	   	     }
	          } else {
		     SiS_Pr->CP_SyncValid[i] = FALSE;
		  }
	       }
            }
	 }
	 if(!havesync) {
	    xf86DrvMsg(pSiS->pScrn->scrnIndex, X_WARNING,
	       	   "CRT2: Unable to retrieve Sync polarity information\n");
   	 }
      }

      if(paneltype && checkexpand) {
         /* If any of the Established low-res modes is supported, the
	  * panel can scale automatically. For 800x600 panels, we only 
	  * check the even lower ones.
	  */
	 if(paneltype == Panel_800x600) {
	    if(buffer[0x23] & 0xfc) cr37 |= 0x10;
	 } else {
            if(buffer[0x23])	    cr37 |= 0x10;
	 }
      }
       
      break;
      
   case 3:							/* Analyze EDID V2 */
   case 4:
      index = 0;

      if(!(checkedid2(buffer))) {
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	 	"CRT2: EDID corrupt\n");
	 return 0;
      }

      if((buffer[0x41] & 0x0f) == 0x03) {
         index = 0x42 + 3;
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	 	"CRT2: Display supports TMDS input on primary interface\n");
      } else if((buffer[0x41] & 0xf0) == 0x30) {
         index = 0x46 + 3;
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	 	"CRT2: Display supports TMDS input on secondary interface\n");
      } else {
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	 	"CRT2: Display does not support TMDS video interface (0x%02x)\n", 
		buffer[0x41]);
	 return 0;
      }

      paneltype = Panel_Custom;
      SiS_Pr->CP_MaxX = xres = buffer[0x76] | (buffer[0x77] << 8);
      SiS_Pr->CP_MaxY = yres = buffer[0x78] | (buffer[0x79] << 8);
      switch(xres) {
         case 800:
	     if(yres == 600) {
	     	paneltype = Panel_800x600;
	     	checkexpand = TRUE;
	     }
	     break;
         case 1024:
	     if(yres == 768) {
	     	paneltype = Panel_1024x768;
	     	checkexpand = TRUE;
	     }
	     break;
	 case 1152:
	     if(yres == 768) {
	        if(pSiS->VGAEngine == SIS_300_VGA) {
		   paneltype = Panel300_1152x768;
		} else {
		   paneltype = Panel310_1152x768;
		}
	     	checkexpand = TRUE;
	     }
	     break;
	 case 1280:
	     if(yres == 960) {
	        if(pSiS->VGAEngine == SIS_315_VGA) {
	     	   paneltype = Panel310_1280x960;
		} else {
		   paneltype = Panel300_1280x960;
		}
	     } else if(yres == 1024) {
	     	paneltype = Panel_1280x1024;
		checkexpand = TRUE;
	     }
	     /* 1280x768 treated as custom here */
	     break;
	 case 1400:
	     if(pSiS->VGAEngine == SIS_315_VGA) {
	        if(yres == 1050) {
	           paneltype = Panel310_1400x1050;
		   checkexpand = TRUE;
	        }
	     }
      	     break;
#if 0    /* Treat this one as custom since we have no timing data yet */
	 case 1600:
	     if(pSiS->VGAEngine == SIS_315_VGA) {
	        if(yres == 1200) {
	           paneltype = Panel310_1600x1200;
		   checkexpand = TRUE;
	        }
	     }
      	     break;
#endif
      }

      /* Determine if RGB18 or RGB24 */
      if(index) {
         if((buffer[index] == 0x20) || (buffer[index] == 0x34)) {
	    cr37 |= 0x01;
	 }
      }

      if(checkexpand) {
         /* TODO - for now, we let the panel scale */
	 cr37 |= 0x10;
      }

      /* Now seek 4-Byte Timing codes and extract sync pol info */
      index = 0x80;
      if(buffer[0x7e] & 0x20) {			    /* skip Luminance Table (if provided) */
         lumsize = buffer[0x80] & 0x1f;
	 if(buffer[0x80] & 0x80) lumsize *= 3;
	 lumsize++;  /* luminance header byte */
	 index += lumsize;
      }
      index += (((buffer[0x7e] & 0x1c) >> 2) * 8);   /* skip Frequency Ranges */
      index += ((buffer[0x7e] & 0x03) * 27);         /* skip Detailed Range Limits */
      numcodes = (buffer[0x7f] & 0xf8) >> 3;
      if(numcodes) {
         myindex = index;
 	 seekcode = (xres - 256) / 16;
     	 for(i=0; i<numcodes; i++) {
	    if(buffer[myindex] == seekcode) break;
	    myindex += 4;
	 }
	 if(buffer[myindex] == seekcode) {
	    cr37 |= ((((buffer[myindex + 1] & 0x0c) ^ 0x0c) << 4) | 0x20);
	 } else {
	    xf86DrvMsg(pSiS->pScrn->scrnIndex, X_WARNING,
	        "CRT2: Unable to retrieve Sync polarity information\n");
	 }
      } else {
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_WARNING,
	     "CRT2: Unable to retrieve Sync polarity information\n");
      }

      /* Now seek the detailed timing descriptions for custom panels */
      if(paneltype == Panel_Custom) {

         SiS_Pr->CP_Supports64048075 = TRUE;

         index += (numcodes * 4);
	 numcodes = buffer[0x7f] & 0x07;
	 for(i=0; i<numcodes; i++, index += 18) {
	    xres = buffer[index+2] | ((buffer[index+4] & 0xf0) << 4);
            yres = buffer[index+5] | ((buffer[index+7] & 0xf0) << 4);

	    SiS_Pr->CP_HDisplay[i] = xres;
	    SiS_Pr->CP_HSyncStart[i] = xres + (buffer[index+8] | ((buffer[index+11] & 0xc0) << 2));
            SiS_Pr->CP_HSyncEnd[i] = SiS_Pr->CP_HSyncStart[i] + (buffer[index+9] | ((buffer[index+11] & 0x30) << 4));
	    SiS_Pr->CP_HTotal[i] = xres + (buffer[index+3] | ((buffer[index+4] & 0x0f) << 8));
	    SiS_Pr->CP_HBlankStart[i] = xres + 1;
	    SiS_Pr->CP_HBlankEnd[i] = SiS_Pr->CP_HTotal[i];

	    SiS_Pr->CP_VDisplay[i] = yres;
            SiS_Pr->CP_VSyncStart[i] = yres + (((buffer[index+10] & 0xf0) >> 4) | ((buffer[index+11] & 0x0c) << 2));
            SiS_Pr->CP_VSyncEnd[i] = SiS_Pr->CP_VSyncStart[i] + ((buffer[index+10] & 0x0f) | ((buffer[index+11] & 0x03) << 4));
	    SiS_Pr->CP_VTotal[i] = yres + (buffer[index+6] | ((buffer[index+7] & 0x0f) << 8));
	    SiS_Pr->CP_VBlankStart[i] = yres + 1;
	    SiS_Pr->CP_VBlankEnd[i] = SiS_Pr->CP_VTotal[i];

	    SiS_Pr->CP_Clock[i] = (buffer[index] | (buffer[index+1] << 8)) * 10;

	    SiS_Pr->CP_DataValid[i] = TRUE;

	    if((SiS_Pr->CP_HDisplay[i] & 7)						||
	       (SiS_Pr->CP_HDisplay[i] > SiS_Pr->CP_HSyncStart[i])  			||
	       (SiS_Pr->CP_HDisplay[i] >= SiS_Pr->CP_HSyncEnd[i])   			||
	       (SiS_Pr->CP_HDisplay[i] >= SiS_Pr->CP_HTotal[i])     			||
	       (SiS_Pr->CP_HSyncStart[i] >= SiS_Pr->CP_HSyncEnd[i]) 			||
	       (SiS_Pr->CP_HSyncStart[i] > SiS_Pr->CP_HTotal[i])    			||
	       (SiS_Pr->CP_HSyncEnd[i] > SiS_Pr->CP_HTotal[i])      			||
	       (SiS_Pr->CP_VDisplay[i] > SiS_Pr->CP_VSyncStart[i])  			||
	       (SiS_Pr->CP_VDisplay[i] >= SiS_Pr->CP_VSyncEnd[i])   			||
	       (SiS_Pr->CP_VDisplay[i] >= SiS_Pr->CP_VTotal[i])     			||
	       (SiS_Pr->CP_VSyncStart[i] > SiS_Pr->CP_VSyncEnd[i])  			||
	       (SiS_Pr->CP_VSyncStart[i] > SiS_Pr->CP_VTotal[i])    			||
	       (SiS_Pr->CP_VSyncEnd[i] > SiS_Pr->CP_VTotal[i])      			||
	       (((pSiS->VBFlags & VB_301C) && (SiS_Pr->CP_Clock[i] > 162500)) ||
	        ((!(pSiS->VBFlags & VB_301C)) && (SiS_Pr->CP_Clock[i] > 108200)))	||
	       (buffer[index + 17] & 0x80)) {

	       SiS_Pr->CP_DataValid[i] = FALSE;

	    } else {

	       SiS_Pr->CP_HaveCustomData = TRUE;

	       if(SiS_Pr->CP_Clock[i] > SiS_Pr->CP_MaxClock) SiS_Pr->CP_MaxClock = SiS_Pr->CP_Clock[i];

	       SiS_Pr->CP_HSync_P[i] = (buffer[index + 17] & 0x02) ? TRUE : FALSE;
	       SiS_Pr->CP_VSync_P[i] = (buffer[index + 17] & 0x04) ? TRUE : FALSE;
	       SiS_Pr->CP_SyncValid[i] = TRUE;

	       SiS_Pr->CP_Vendor = buffer[2] | (buffer[1] << 8);
	       SiS_Pr->CP_Product = buffer[3] | (buffer[4] << 8);

	       /* We must assume the panel can scale, since we have
	        * no scaling data
    	        */
	       cr37 |= 0x10;

	    }
	 }

      }

      break;

   }

   /* 1280x960 panels are always RGB24, unable to scale and use
    * high active sync polarity
    */
   if(pSiS->VGAEngine == SIS_315_VGA) {
      if(paneltype == Panel310_1280x960) cr37 &= 0x0e;
   } else {
      if(paneltype == Panel300_1280x960) cr37 &= 0x0e;
   }

   for(i = 0; i < 7; i++) {
      if(SiS_Pr->CP_DataValid[i]) {
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
            "Non-standard LCD timing data no. %d:\n", i);
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	    "   HDisplay %d HSync %d HSyncEnd %d HTotal %d\n",
	    SiS_Pr->CP_HDisplay[i], SiS_Pr->CP_HSyncStart[i],
	    SiS_Pr->CP_HSyncEnd[i], SiS_Pr->CP_HTotal[i]);
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
            "   VDisplay %d VSync %d VSyncEnd %d VTotal %d\n",
            SiS_Pr->CP_VDisplay[i], SiS_Pr->CP_VSyncStart[i],
   	    SiS_Pr->CP_VSyncEnd[i], SiS_Pr->CP_VTotal[i]);
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	    "   Pixel clock: %3.3fMhz\n", (float)SiS_Pr->CP_Clock[i] / 1000);
	 xf86DrvMsg(pSiS->pScrn->scrnIndex, X_INFO,
	    "   To use this, add \"%dx%d\" to the list of Modes in the Screen section\n",
	    SiS_Pr->CP_HDisplay[i],
	    SiS_Pr->CP_VDisplay[i]);
      }
   }

   if(paneltype) {
       if(!SiS_Pr->CP_PreferredX) SiS_Pr->CP_PreferredX = SiS_Pr->CP_MaxX;
       if(!SiS_Pr->CP_PreferredY) SiS_Pr->CP_PreferredY = SiS_Pr->CP_MaxY;
       cr37 &= 0xf1;
       SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x36,0xf0,paneltype);
       SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x37,0xf1,cr37);
       SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x32,0x08);
#ifdef TWDEBUG
       xf86DrvMsgVerb(pSiS->pScrn->scrnIndex, X_PROBED, 3, 
       	"CRT2: [DDC LCD results: 0x%02x, 0x%02x]\n", paneltype, cr37);
#endif	
   }
   return 0;
}
   
USHORT
SiS_SenseVGA2DDC(SiS_Private *SiS_Pr, SISPtr pSiS)
{
   USHORT DDCdatatype,flag;
   BOOLEAN foundcrt = FALSE;
   int retry;
   unsigned char buffer[256];

   if(!(pSiS->VBFlags & (VB_301|VB_301B|VB_301C|VB_302B))) return 0;

   if(SiS_InitDDCRegs(SiS_Pr, pSiS->VBFlags, pSiS->VGAEngine, 2, 0, FALSE) == 0xFFFF) return 0;
   
   SiS_Pr->SiS_DDC_SecAddr = 0x00;
   
   /* Probe supported DA's */
   flag = SiS_ProbeDDC(SiS_Pr);
   if(flag & 0x10) {
      SiS_Pr->SiS_DDC_DeviceAddr = 0xa6;	/* EDID V2 (FP) */
      DDCdatatype = 4;
   } else if(flag & 0x08) {
      SiS_Pr->SiS_DDC_DeviceAddr = 0xa2;	/* EDID V2 (P&D-D Monitor) */
      DDCdatatype = 3;
   } else if(flag & 0x02) {
      SiS_Pr->SiS_DDC_DeviceAddr = 0xa0;	/* EDID V1 */
      DDCdatatype = 1;
   } else {
   	xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
		"Do DDC answer\n");
   	return 0;				/* no DDC support (or no device attached) */
   }

   /* Read the entire EDID */
   retry = 2;
   do {
      if(SiS_ReadDDC(SiS_Pr, DDCdatatype, buffer)) {
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	 	"CRT2: DDC read failed (attempt %d), %s\n",
		(3-retry), (retry == 1) ? "giving up" : "retrying");
	 retry--;
	 if(retry == 0) return 0xFFFF;
      } else break;
   } while(1);

   /* Analyze EDID. We don't have many chances to
    * distinguish a flat panel from a CRT...
    */
   switch(DDCdatatype) {
   case 1:
      if(!(checkedid1(buffer))) {
          xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	  	"CRT2: EDID corrupt\n");
      	  return 0;
      }
      if(buffer[0x14] & 0x80) {			/* Display uses digital input */
          xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	  	"CRT2: Attached display expects digital input\n");
      	  return 0;
      }
      SiS_Pr->CP_Vendor = buffer[9] | (buffer[8] << 8);
      SiS_Pr->CP_Product = buffer[10] | (buffer[11] << 8);
      foundcrt = TRUE;
      break;
   case 3:
   case 4:
      if(!(checkedid2(buffer))) {
          xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	  	"CRT2: EDID corrupt\n");
      	  return 0;
      }
      if( ((buffer[0x41] & 0x0f) != 0x01) &&  	/* Display does not support analog input */
          ((buffer[0x41] & 0x0f) != 0x02) &&
	  ((buffer[0x41] & 0xf0) != 0x10) &&
	  ((buffer[0x41] & 0xf0) != 0x20) ) {
	  xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	     	"CRT2: Attached display does not support analog input (0x%02x)\n",
		buffer[0x41]);
	  return 0;
      }
      SiS_Pr->CP_Vendor = buffer[2] | (buffer[1] << 8);
      SiS_Pr->CP_Product = buffer[3] | (buffer[4] << 8);
      foundcrt = TRUE;
      break;
   }

   if(foundcrt) {
      SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x32,0x10);
   }
   return(0);
}

#endif

void
SiS_SetCH70xxANDOR(SiS_Private *SiS_Pr, USHORT tempax,USHORT tempbh)
{
  USHORT tempbl;

  tempbl = SiS_GetCH70xx(SiS_Pr,(tempax & 0x00FF));
  tempbl = (((tempbl & tempbh) << 8) | tempax);
  SiS_SetCH70xx(SiS_Pr,tempbl);
}

/* Generic I2C functions for Chrontel & DDC --------- */

void
SiS_SetSwitchDDC2(SiS_Private *SiS_Pr)
{
  SiS_SetSCLKHigh(SiS_Pr);
  SiS_WaitRetrace1(SiS_Pr);

  SiS_SetSCLKLow(SiS_Pr);
  SiS_WaitRetrace1(SiS_Pr);
}

USHORT
SiS_ReadDDC1Bit(SiS_Private *SiS_Pr)
{
   SiS_WaitRetrace1(SiS_Pr);
   return((SiS_GetReg(SiS_Pr->SiS_P3c4,0x11) & 0x02) >> 1);
}

/* Set I2C start condition */
/* This is done by a SD high-to-low transition while SC is high */
USHORT
SiS_SetStart(SiS_Private *SiS_Pr)
{
  if(SiS_SetSCLKLow(SiS_Pr)) return 0xFFFF;			           /* (SC->low)  */
  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
    		  SiS_Pr->SiS_DDC_Index,
                  SiS_Pr->SiS_DDC_NData,
		  SiS_Pr->SiS_DDC_Data);             			   /* SD->high */
  if(SiS_SetSCLKHigh(SiS_Pr)) return 0xFFFF;			           /* SC->high */
  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
  		  SiS_Pr->SiS_DDC_Index,
                  SiS_Pr->SiS_DDC_NData,
		  0x00);                             			   /* SD->low = start condition */
  if(SiS_SetSCLKHigh(SiS_Pr)) return 0xFFFF;			           /* (SC->low) */
  return 0;
}

/* Set I2C stop condition */
/* This is done by a SD low-to-high transition while SC is high */
USHORT
SiS_SetStop(SiS_Private *SiS_Pr)
{
  if(SiS_SetSCLKLow(SiS_Pr)) return 0xFFFF;			           /* (SC->low) */
  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
  	          SiS_Pr->SiS_DDC_Index,
                  SiS_Pr->SiS_DDC_NData,
		  0x00);          		   			   /* SD->low   */
  if(SiS_SetSCLKHigh(SiS_Pr)) return 0xFFFF;			           /* SC->high  */
  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
  		  SiS_Pr->SiS_DDC_Index,
                  SiS_Pr->SiS_DDC_NData,
		  SiS_Pr->SiS_DDC_Data);  	   			   /* SD->high = stop condition */
  if(SiS_SetSCLKHigh(SiS_Pr)) return 0xFFFF;			           /* (SC->high) */
  return 0;
}

/* Write 8 bits of data */
USHORT
SiS_WriteDDC2Data(SiS_Private *SiS_Pr, USHORT tempax)
{
  USHORT i,flag,temp;

  flag = 0x80;
  for(i=0; i<8; i++) {
    SiS_SetSCLKLow(SiS_Pr);				                      /* SC->low */
    if(tempax & flag) {
      SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
      		      SiS_Pr->SiS_DDC_Index,
                      SiS_Pr->SiS_DDC_NData,
		      SiS_Pr->SiS_DDC_Data);            		      /* Write bit (1) to SD */
    } else {
      SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
      		      SiS_Pr->SiS_DDC_Index,
                      SiS_Pr->SiS_DDC_NData,
		      0x00);                            		      /* Write bit (0) to SD */
    }
    SiS_SetSCLKHigh(SiS_Pr);				                      /* SC->high */
    flag >>= 1;
  }
  temp = SiS_CheckACK(SiS_Pr);				                      /* Check acknowledge */
  return(temp);
}

USHORT
SiS_ReadDDC2Data(SiS_Private *SiS_Pr, USHORT tempax)
{
  USHORT i,temp,getdata;

  getdata=0;
  for(i=0; i<8; i++) {
    getdata <<= 1;
    SiS_SetSCLKLow(SiS_Pr);
    SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
    		    SiS_Pr->SiS_DDC_Index,
                    SiS_Pr->SiS_DDC_NData,
		    SiS_Pr->SiS_DDC_Data);
    SiS_SetSCLKHigh(SiS_Pr);
    temp = SiS_GetReg(SiS_Pr->SiS_DDC_Port,SiS_Pr->SiS_DDC_Index);
    if(temp & SiS_Pr->SiS_DDC_Data) getdata |= 0x01;
  }
  return(getdata);
}

USHORT
SiS_SetSCLKLow(SiS_Private *SiS_Pr)
{
  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
  		  SiS_Pr->SiS_DDC_Index,
                  SiS_Pr->SiS_DDC_NClk,
		  0x00);      					/* SetSCLKLow()  */
  SiS_DDC2Delay(SiS_Pr,SiS_I2CDELAYSHORT);
  return 0;
}

USHORT
SiS_SetSCLKHigh(SiS_Private *SiS_Pr)
{
  USHORT temp, watchdog=1000;

  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
  		  SiS_Pr->SiS_DDC_Index,
                  SiS_Pr->SiS_DDC_NClk,
		  SiS_Pr->SiS_DDC_Clk);  			/* SetSCLKHigh()  */
  do {
    temp = SiS_GetReg(SiS_Pr->SiS_DDC_Port,SiS_Pr->SiS_DDC_Index);
  } while((!(temp & SiS_Pr->SiS_DDC_Clk)) && --watchdog);
  if (!watchdog) {
#ifdef TWDEBUG
        xf86DrvMsg(0, X_INFO, "SetClkHigh failed\n");
#endif
  	return 0xFFFF;
  }
  SiS_DDC2Delay(SiS_Pr,SiS_I2CDELAYSHORT);
  return 0;
}

/* Check I2C acknowledge */
/* Returns 0 if ack ok, non-0 if ack not ok */
USHORT
SiS_CheckACK(SiS_Private *SiS_Pr)
{
  USHORT tempah;

  SiS_SetSCLKLow(SiS_Pr);				           /* (SC->low) */
  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
  		  SiS_Pr->SiS_DDC_Index,
                  SiS_Pr->SiS_DDC_NData,
		  SiS_Pr->SiS_DDC_Data);     			   /* (SD->high) */
  SiS_SetSCLKHigh(SiS_Pr);				           /* SC->high = clock impulse for ack */
  tempah = SiS_GetReg(SiS_Pr->SiS_DDC_Port,SiS_Pr->SiS_DDC_Index); /* Read SD */
  SiS_SetSCLKLow(SiS_Pr);				           /* SC->low = end of clock impulse */
  if(tempah & SiS_Pr->SiS_DDC_Data) return(1);			   /* Ack OK if bit = 0 */
  else return(0);
}

/* End of I2C functions ----------------------- */


/* =============== SiS 315/330 O.E.M. ================= */

#ifdef SIS315H

static USHORT
GetRAMDACromptr(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
  USHORT romptr;

  if(HwInfo->jChipType < SIS_330) {
     romptr = ROMAddr[0x128] | (ROMAddr[0x129] << 8);
     if(SiS_Pr->SiS_VBType & VB_SIS301B302B)
        romptr = ROMAddr[0x12a] | (ROMAddr[0x12b] << 8);
  } else {
     romptr = ROMAddr[0x1a8] | (ROMAddr[0x1a9] << 8);
     if(SiS_Pr->SiS_VBType & VB_SIS301B302B)
        romptr = ROMAddr[0x1aa] | (ROMAddr[0x1ab] << 8);
  }
  return(romptr);
}

static USHORT
GetLCDromptr(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
  USHORT romptr;

  if(HwInfo->jChipType < SIS_330) {
     romptr = ROMAddr[0x120] | (ROMAddr[0x121] << 8);
     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)
        romptr = ROMAddr[0x122] | (ROMAddr[0x123] << 8);
  } else {
     romptr = ROMAddr[0x1a0] | (ROMAddr[0x1a1] << 8);
     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)
        romptr = ROMAddr[0x1a2] | (ROMAddr[0x1a3] << 8);
  }
  return(romptr);
}

static USHORT
GetTVromptr(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
  USHORT romptr;

  if(HwInfo->jChipType < SIS_330) {
     romptr = ROMAddr[0x114] | (ROMAddr[0x115] << 8);
     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)
        romptr = ROMAddr[0x11a] | (ROMAddr[0x11b] << 8);
  } else {
     romptr = ROMAddr[0x194] | (ROMAddr[0x195] << 8);
     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)
        romptr = ROMAddr[0x19a] | (ROMAddr[0x19b] << 8);
  }
  return(romptr);
}

static USHORT
GetLCDPtrIndexBIOS(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  USHORT index;

  if((IS_SIS650) && (SiS_Pr->SiS_VBType & VB_SIS301LV302LV)) {
     if(!(SiS_IsNotM650orLater(SiS_Pr, HwInfo))) {
        if((index = SiS_GetReg(SiS_Pr->SiS_P3d4,0x36) & 0xf0)) {
	   index >>= 4;
	   index *= 3;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) index += 2;
           else if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) index++;
           return index;
	}
     }
  }

  index = SiS_Pr->SiS_LCDResInfo & 0x0F;
  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050)      index -= 5;
  else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) index -= 6;
  index--;
  index *= 3;
  if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) index += 2;
  else if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) index++;
  return index;
}

static USHORT
GetLCDPtrIndex(SiS_Private *SiS_Pr)
{
  USHORT index;

  index = SiS_Pr->SiS_LCDResInfo & 0x0F;
  index--;
  index *= 3;
  if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) index += 2;
  else if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) index++;

  return index;
}

static USHORT
GetTVPtrIndex(SiS_Private *SiS_Pr)
{
  USHORT index;

  index = 0;
  if(SiS_Pr->SiS_TVMode & TVSetPAL) index = 1;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) index = 2;

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) index = 0;

  index <<= 1;

  if((SiS_Pr->SiS_VBInfo & SetInSlaveMode) &&
     (SiS_Pr->SiS_TVMode & TVSetTVSimuMode)) {
     index++;
  }

  return index;
}

static ULONG
GetOEMTVPtr661_2(SiS_Private *SiS_Pr)
{
   USHORT index = 0, temp = 0;

   if(SiS_Pr->SiS_TVMode & TVSetPAL)   index = 1;
   if(SiS_Pr->SiS_TVMode & TVSetPALM)  index = 2;
   if(SiS_Pr->SiS_TVMode & TVSetPALN)  index = 3;
   if(SiS_Pr->SiS_TVMode & TVSetNTSCJ) index = 6;
   if(SiS_Pr->SiS_TVMode & TVSetNTSC1024) {
      index = 4;
      if(SiS_Pr->SiS_TVMode & TVSetPALM)  index++;
      if(SiS_Pr->SiS_TVMode & TVSetNTSCJ) index = 7;
   }

   if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
      if((!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) ||
         (SiS_Pr->SiS_TVMode & TVSetTVSimuMode)) {
	 index += 8;
	 temp++;
      }
      temp += 0x0100;
   }
   return(ULONG)(index | (temp << 16));
}

static int
GetOEMTVPtr661(SiS_Private *SiS_Pr)
{
   int index = 0;

   if(SiS_Pr->SiS_TVMode & TVSetPAL)       index = 2;
   if(SiS_Pr->SiS_TVMode & TVSetHiVision)  index = 4;
   if(SiS_Pr->SiS_TVMode & TVSetYPbPr525i) index = 6;
   if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p) index = 8;
   if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p) index = 10;

   if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode) index++;

   return index;
}

static void
SetDelayComp(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo, USHORT ModeNo)
{
  UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
  USHORT delay=0,index,myindex,temp,romptr=0;
  BOOLEAN dochiptest = TRUE;

  /* Find delay (from ROM, internal tables, PCI subsystem) */

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC) {			/* ------------ VGA */
     
     if((ROMAddr) && SiS_Pr->SiS_UseROM) {
        romptr = GetRAMDACromptr(SiS_Pr, HwInfo);
	if(!romptr) return;
	delay = ROMAddr[romptr];
     } else {
        delay = 0x04;
        if(SiS_Pr->SiS_VBType & VB_SIS301B302B) {
	   if(IS_SIS650) {
	      delay = 0x0a;
	   } else if(IS_SIS740) {
	      delay = 0x00;
	   } else if(HwInfo->jChipType < SIS_330) {
	      delay = 0x0c;
	   } else {
	      delay = 0x0c;
	   }
	}
        if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
           delay = 0x00;
	}
     }

  } else if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD|SetCRT2ToLCDA)) {  /* ----------	LCD/LCDA */

     BOOLEAN gotitfrompci = FALSE;

     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_PanelCustom) return;

     /* Could we detect a PDC for LCD? If yes, use it */

     if(SiS_Pr->PDC) {
        if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2d,0x0f,((SiS_Pr->PDC & 0x0f) << 4));
	}
        return;
     }

     /* This is a piece of typical SiS crap: They code the OEM LCD
      * delay into the code, at no defined place in the BIOS.
      * We now have to start doing a PCI subsystem check here.
      */

     switch(SiS_Pr->SiS_CustomT) {
     case CUT_COMPAQ1280:
     case CUT_COMPAQ12802:
	if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
	   gotitfrompci = TRUE;
	   dochiptest = FALSE;
	   delay = 0x03;
	}
	break;
     case CUT_CLEVO1400:
     case CUT_CLEVO14002:
	/* if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) { */
	   gotitfrompci = TRUE;
	   dochiptest = FALSE;
	   delay = 0x02;
	/* } */
	break;
     case CUT_CLEVO1024:
     case CUT_CLEVO10242:
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
	   gotitfrompci = TRUE;
	   dochiptest = FALSE;
	   delay = 0x33;
	   SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2D,delay);
	   delay &= 0x0f;
	}
	break;
     }

     /* Could we find it through the PCI ID? If no, use ROM or table */

     if(!gotitfrompci) {

        index = GetLCDPtrIndexBIOS(SiS_Pr, HwInfo);
        myindex = GetLCDPtrIndex(SiS_Pr);

        if(IS_SIS650 && (SiS_Pr->SiS_VBType & VB_SIS301LV302LV)) {

           if(SiS_IsNotM650orLater(SiS_Pr, HwInfo)) {

              if((ROMAddr) && SiS_Pr->SiS_UseROM) {
	         /* Always use the second pointer on 650; some BIOSes */
                 /* still carry old 301 data at the first location    */
	         /* romptr = ROMAddr[0x120] | (ROMAddr[0x121] << 8);  */
	         /* if(SiS_Pr->SiS_VBType & VB_SIS302LV)              */
	         romptr = ROMAddr[0x122] | (ROMAddr[0x123] << 8);
	         if(!romptr) return;
	         delay = ROMAddr[(romptr + index)];
	      } else {
                 delay = SiS310_LCDDelayCompensation_650301LV[myindex];
	      }

          } else {

             delay = SiS310_LCDDelayCompensation_651301LV[myindex];
	     if(SiS_Pr->SiS_VBType & (VB_SIS302LV | VB_SIS302ELV))
	        delay = SiS310_LCDDelayCompensation_651302LV[myindex];

          }

        } else if((ROMAddr) && SiS_Pr->SiS_UseROM &&
	          (SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1280x1024)) {

	   /* Data for 1280x1024 wrong in BIOS */
           romptr = GetLCDromptr(SiS_Pr, HwInfo);
	   if(!romptr) return;
	   delay = ROMAddr[(romptr + index)];

        } else if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {

	   if(IS_SIS740) delay = 0x03;
	   else          delay = 0x00;

	} else {

           delay = SiS310_LCDDelayCompensation_301[myindex];
	   if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	      if(IS_SIS740) delay = 0x01;
	      else          delay = SiS310_LCDDelayCompensation_650301LV[myindex];
	   } else if(SiS_Pr->SiS_VBType & VB_SIS301B302B) {
	      if(IS_SIS740) delay = 0x01;
	      else          delay = SiS310_LCDDelayCompensation_3xx301B[myindex];
	   }

        }

     }  /* got it from PCI */

     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
	SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2D,0x0F,((delay << 4) & 0xf0));
	dochiptest = FALSE;
     }
     
  } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {			/* ------------ TV */

     index = GetTVPtrIndex(SiS_Pr);
     
     if(IS_SIS650 && (SiS_Pr->SiS_VBType & VB_SIS301LV302LV)) {

        if(SiS_IsNotM650orLater(SiS_Pr,HwInfo)) {

           if((ROMAddr) && SiS_Pr->SiS_UseROM) {
	      /* Always use the second pointer on 650; some BIOSes */
              /* still carry old 301 data at the first location    */
              /* romptr = ROMAddr[0x114] | (ROMAddr[0x115] << 8);  */
	      /* if(SiS_Pr->SiS_VBType & VB_SIS302LV) */
	      romptr = ROMAddr[0x11a] | (ROMAddr[0x11b] << 8);
	      if(!romptr) return;
	      delay = ROMAddr[romptr + index];

	   } else {

	      delay = SiS310_TVDelayCompensation_301B[index];

	   }

        } else {

           switch(SiS_Pr->SiS_CustomT) {
	   case CUT_COMPAQ1280:
	   case CUT_COMPAQ12802:
	   case CUT_CLEVO1400:
	   case CUT_CLEVO14002:
	      delay = 0x02;
	      dochiptest = FALSE;
	      break;
	   case CUT_CLEVO1024:
	   case CUT_CLEVO10242:
	      delay = 0x03;
	      dochiptest = FALSE;
   	      break;
	   default:
              delay = SiS310_TVDelayCompensation_651301LV[index];
	      if(SiS_Pr->SiS_VBType & VB_SIS302LV) {
	         delay = SiS310_TVDelayCompensation_651302LV[index];
	      }
	   }
        }

     } else if((ROMAddr) && SiS_Pr->SiS_UseROM) {

        romptr = GetTVromptr(SiS_Pr, HwInfo);
	if(!romptr) return;
	delay = ROMAddr[romptr + index];

     } else if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {

        delay = SiS310_TVDelayCompensation_LVDS[index];

     } else {

	delay = SiS310_TVDelayCompensation_301[index];
        if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
	   if(IS_SIS740) {
	      delay = SiS310_TVDelayCompensation_740301B[index];
	   } else {
              delay = SiS310_TVDelayCompensation_301B[index];
	   }
	}

     }

     if(SiS_GetReg(SiS_Pr->SiS_Part1Port,0x13) & 0x04) {  /* LCDA */
	delay &= 0x0f;
	dochiptest = FALSE;
     }
    
  } else return;

  /* Write delay */

  if(SiS_Pr->SiS_VBType & VB_SISVB) {

     if(IS_SIS650 && (SiS_Pr->SiS_VBType & VB_SIS301LV302LV) && dochiptest) {

        temp = (SiS_GetReg(SiS_Pr->SiS_P3d4,0x36) & 0xf0) >> 4;
        if(temp == 8) {		/* 1400x1050 BIOS (COMPAL) */
	   delay &= 0x0f;
	   delay |= 0xb0;
        } else if(temp == 6) {
           delay &= 0x0f;
	   delay |= 0xc0;
        } else if(temp > 7) {	/* 1280x1024 BIOS (which one?) */
	   delay = 0x35;
        }
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2D,delay);

     } else {

        SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2D,0xF0,delay);

     }

  } else {  /* LVDS */

     if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
        SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2D,0xF0,delay);
     } else {
        if(IS_SIS650 && (SiS_Pr->SiS_IF_DEF_CH70xx != 0)) {
           delay <<= 4;
           SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2D,0x0F,delay);
        } else {
           SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2D,0xF0,delay);
        }
     }

  }

}

static void
SetAntiFlicker(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
               USHORT ModeNo,USHORT ModeIdIndex)
{
  UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
  USHORT index,temp,temp1,romptr=0;

  if(SiS_Pr->SiS_TVMode & (TVSetYPbPr750p|TVSetYPbPr525p)) return;

  if(ModeNo<=0x13)
     index = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].VB_StTVFlickerIndex;
  else
     index = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].VB_ExtTVFlickerIndex;

  temp = GetTVPtrIndex(SiS_Pr);
  temp >>= 1;  	  /* 0: NTSC/YPbPr, 1: PAL, 2: HiTV */
  temp1 = temp;

  if(ROMAddr && SiS_Pr->SiS_UseROM) {
     if(HwInfo->jChipType >= SIS_661) {
	romptr = ROMAddr[0x260] | (ROMAddr[0x261] << 8);
	temp1 = GetOEMTVPtr661(SiS_Pr);
        temp1 >>= 1;
     } else if(HwInfo->jChipType >= SIS_330) {
        romptr = ROMAddr[0x192] | (ROMAddr[0x193] << 8);
     } else {
        romptr = ROMAddr[0x112] | (ROMAddr[0x113] << 8);
     }
  }

  if(romptr) {
     temp1 <<= 1;
     temp = ROMAddr[romptr + temp1 + index];
  } else {
     temp = SiS310_TVAntiFlick1[temp][index];
  }
  temp <<= 4;

  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x0A,0x8f,temp);  /* index 0A D[6:4] */
}

static void
SetEdgeEnhance(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
               USHORT ModeNo,USHORT ModeIdIndex)
{
  UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
  USHORT index,temp,temp1,romptr=0;

  temp = GetTVPtrIndex(SiS_Pr);
  temp >>= 1;              	/* 0: NTSC/YPbPr, 1: PAL, 2: HiTV */
  temp1 = temp;

  if(ModeNo<=0x13)
     index = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].VB_StTVEdgeIndex;
  else
     index = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].VB_ExtTVEdgeIndex;

  if(ROMAddr && SiS_Pr->SiS_UseROM) {
     if(HwInfo->jChipType >= SIS_661) {
	romptr = ROMAddr[0x26c] | (ROMAddr[0x26d] << 8);
	temp1 = GetOEMTVPtr661(SiS_Pr);
        temp1 >>= 1;
     } else if(HwInfo->jChipType >= SIS_330) {
        romptr = ROMAddr[0x1a4] | (ROMAddr[0x1a5] << 8);
     } else {
        romptr = ROMAddr[0x124] | (ROMAddr[0x125] << 8);
     }
  }

  if(romptr) {
     temp1 <<= 1;
     temp = ROMAddr[romptr + temp1 + index];
  } else {
     temp = SiS310_TVEdge1[temp][index];
  }
  temp <<= 5;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x3A,0x1F,temp);  /* index 0A D[7:5] */
}

static void
SetYFilter(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
           USHORT ModeNo,USHORT ModeIdIndex)
{
  UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
  USHORT index, myindex, oldindex,temp, i, j, flag1 = 0, flag2 = 0, romptr = 0;
  ULONG  lindex;

  if(ModeNo<=0x13) {
    index =  SiS_Pr->SiS_SModeIDTable[ModeIdIndex].VB_StTVYFilterIndex;
  } else {
    index =  SiS_Pr->SiS_EModeIDTable[ModeIdIndex].VB_ExtTVYFilterIndex;
  }

  oldindex = index;

  if((HwInfo->jChipType >= SIS_661) && ROMAddr && SiS_Pr->SiS_UseROM) {
     if(ModeNo > 0x13) {
        index =  SiS_Pr->SiS_EModeIDTable[ModeIdIndex].VB_ExtTVYFilterIndexROM661;
     }
     lindex = GetOEMTVPtr661_2(SiS_Pr);
     if(lindex & 0x00ff0000) flag1 = 1;
     if(lindex & 0xff000000) flag2 = 1;
     lindex &= 0xffff;

     /* NTSC-J: Use PAL filters */
     if(SiS_Pr->SiS_TVMode & TVSetNTSCJ) lindex = 1;

     romptr = ROMAddr[0x268] | (ROMAddr[0x269] << 8);
     if(flag1) myindex = index * 7;
     else      myindex = index << 2;

     if(romptr) {
        romptr += (lindex << 1);
        romptr = (ROMAddr[romptr] | (ROMAddr[romptr+1] << 8)) + myindex;
	if(romptr) {
           if((!flag1) && (flag2)) {
	      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x35,0x00);
	      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x36,0x00);
	      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x37,0x00);
	      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x38,ROMAddr[romptr++]);
           } else {
	      for(i=0x35; i<=0x38; i++) {
                 SiS_SetReg(SiS_Pr->SiS_Part2Port,i,ROMAddr[romptr++]);
              }
           }
           if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
	      for(j=0, i=0x48; i<=0x4a; i++, j++) {
                 SiS_SetReg(SiS_Pr->SiS_Part2Port,i,ROMAddr[romptr++]);
              }
           }
           return;
	}
     }
  }

  index = oldindex;

  temp = GetTVPtrIndex(SiS_Pr);
  temp >>= 1;  			/* 0: NTSC/YPbPr, 1: PAL, 2: HiTV */

  if(SiS_Pr->SiS_TVMode & TVSetNTSCJ)	     temp = 1;  /* NTSC-J uses PAL */
  else if(SiS_Pr->SiS_TVMode & TVSetPALM)    temp = 3;  /* PAL-M */
  else if(SiS_Pr->SiS_TVMode & TVSetPALN)    temp = 4;  /* PAL-N */
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) temp = 1;  /* HiVision uses PAL */

  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
     for(i=0x35, j=0; i<=0x38; i++, j++) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS310_TVYFilter2[temp][index][j]);
     }
     for(i=0x48; i<=0x4A; i++, j++) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS310_TVYFilter2[temp][index][j]);
     }
  } else {
     for(i=0x35, j=0; i<=0x38; i++, j++) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS310_TVYFilter1[temp][index][j]);
     }
  }
}

static void
SetPhaseIncr(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
             USHORT ModeNo,USHORT ModeIdIndex)
{
  UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
  USHORT index,temp,i,j,resinfo,romptr=0;
  ULONG  lindex;

  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) return;

  /* NTSC-J data not in BIOS, and already set in SetGroup2 */
  if(SiS_Pr->SiS_TVMode & TVSetNTSCJ) return;

  if(HwInfo->jChipType >= SIS_661) {
     lindex = GetOEMTVPtr661_2(SiS_Pr) & 0xffff;
     lindex <<= 2;
     if((ROMAddr) && SiS_Pr->SiS_UseROM) {
        romptr = ROMAddr[0x264] | (ROMAddr[0x265] << 8);
     }
     if(romptr) {
	romptr += lindex;
	for(j=0, i=0x31; i<=0x34; i++, j++) {
           SiS_SetReg(SiS_Pr->SiS_Part2Port,i,ROMAddr[romptr + j]);
        }
     } else {
        for(j=0, i=0x31; i<=0x34; i++, j++) {
           SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS661_TVPhase[lindex + j]);
        }
     }
     return;
  }

  /* PAL-M, PAL-N not in BIOS, and already set in SetGroup2 */
  if(SiS_Pr->SiS_TVMode & (TVSetPALM | TVSetPALN)) return;

  if(ModeNo<=0x13) {
     resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  } else {
     resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  }

  temp = GetTVPtrIndex(SiS_Pr);
  /* 0: NTSC Graphics, 1: NTSC Text,    2: PAL Graphics,
   * 3: PAL Text,      4: HiTV Graphics 5: HiTV Text
   */
  if((ROMAddr) && SiS_Pr->SiS_UseROM) {
     romptr = ROMAddr[0x116] | (ROMAddr[0x117] << 8);
     if(HwInfo->jChipType >= SIS_330) {
        romptr = ROMAddr[0x196] | (ROMAddr[0x197] << 8);
     }
     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
        romptr = ROMAddr[0x11c] | (ROMAddr[0x11d] << 8);
	if(HwInfo->jChipType >= SIS_330) {
	   romptr = ROMAddr[0x19c] | (ROMAddr[0x19d] << 8);
	}
	if((SiS_Pr->SiS_VBInfo & SetInSlaveMode) && (!(SiS_Pr->SiS_TVMode & TVSetTVSimuMode))) {
	   romptr = ROMAddr[0x116] | (ROMAddr[0x117] << 8);
	   if(HwInfo->jChipType >= SIS_330) {
              romptr = ROMAddr[0x196] | (ROMAddr[0x197] << 8);
           }
	}
     }
  }
  if(romptr) {
     romptr += (temp << 2);
     for(j=0, i=0x31; i<=0x34; i++, j++) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,i,ROMAddr[romptr + j]);
     }
  } else {
     index = temp % 2;
     temp >>= 1;          /* 0:NTSC, 1:PAL, 2:HiTV */
     for(j=0, i=0x31; i<=0x34; i++, j++) {
        if(!(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV))
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS310_TVPhaseIncr1[temp][index][j]);
        else if((!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) || (SiS_Pr->SiS_TVMode & TVSetTVSimuMode))
           SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS310_TVPhaseIncr2[temp][index][j]);
        else
           SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS310_TVPhaseIncr1[temp][index][j]);
     }
  }

  if((SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) && (!(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision))) {
     if((!(SiS_Pr->SiS_TVMode & (TVSetPAL | TVSetYPbPr525p | TVSetYPbPr750p))) && (ModeNo > 0x13)) {
        if((resinfo == SIS_RI_640x480) ||
	   (resinfo == SIS_RI_800x600)) {
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,0x31,0x21);
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,0x32,0xf0);
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,0x33,0xf5);
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,0x34,0x7f);
	} else if(resinfo == SIS_RI_1024x768) {
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,0x31,0x1e);
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,0x32,0x8b);
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,0x33,0xfb);
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,0x34,0x7b);
	}
     }
  }
}

void
SiS_OEM310Setting(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
                  USHORT ModeNo,USHORT ModeIdIndex)
{
   SetDelayComp(SiS_Pr,HwInfo,ModeNo);

   if(SiS_Pr->UseCustomMode) return;

   if((SiS_Pr->SiS_VBType & VB_SISVB) && (SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) {
      SetAntiFlicker(SiS_Pr,HwInfo,ModeNo,ModeIdIndex);
      SetPhaseIncr(SiS_Pr,HwInfo,ModeNo,ModeIdIndex);
      SetYFilter(SiS_Pr,HwInfo,ModeNo,ModeIdIndex);
      if(!(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)) {
         SetEdgeEnhance(SiS_Pr,HwInfo,ModeNo,ModeIdIndex);
      }
   }
}

static void
SetDelayComp661(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo, USHORT ModeNo,
                USHORT ModeIdIndex, USHORT RTI)
{
   UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
   USHORT delay = 0, romptr = 0, index;
   UCHAR  *myptr = NULL;
   UCHAR  temp;

   if(!(SiS_Pr->SiS_VBInfo & (SetCRT2ToTV | SetCRT2ToLCD | SetCRT2ToLCDA | SetCRT2ToRAMDAC)))
      return;

   delay = SiS_Pr->SiS_RefIndex[RTI].Ext_PDC;

   delay &= 0xf0;
   delay >>= 4;
   if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) delay <<= 12;  /* BIOS: 8, wrong */

   if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
      index = GetOEMTVPtr661(SiS_Pr);
      if((ROMAddr) && SiS_Pr->SiS_UseROM) {
         romptr = ROMAddr[0x25c] | (ROMAddr[0x25d] << 8);
         if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
            romptr = ROMAddr[0x25e] | (ROMAddr[0x25f] << 8);
         }
      }
      if(romptr) myptr = &ROMAddr[romptr];
      if(!myptr) {
         myptr = (UCHAR *)SiS_TVDelay661_301;
	 if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
	    myptr = (UCHAR *)SiS_TVDelay661_301B;
	 }
      }
      delay = myptr[index];
      if(SiS_GetReg(SiS_Pr->SiS_Part1Port,0x13) & 0x04) delay >>= 4;  /* Should test dual edge */
   } else if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
      if(SiS_Pr->PDC) {
         delay = SiS_Pr->PDC & 0x0f;
	 if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
            delay |= (delay << 12);
         }
      } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_PanelCustom) {
         delay = 0x4444;  /* TEST THIS */
      } else if(!(SiS_Pr->SiS_LCDInfo & LCDPass11)) {
         myptr = GetLCDStructPtr661(SiS_Pr, HwInfo);
         if(myptr) delay = myptr[4];
         else delay = 0x44;
         if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
            delay |= (delay << 8);
         }
      }
   }

   temp = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x2d);
   if(SiS_Pr->SiS_VBInfo & (SetCRT2ToTV | SetCRT2ToLCD | SetCRT2ToRAMDAC)) {
      temp &= 0xf0;
      temp |= (delay & 0x000f);
   }
   if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
      temp &= 0x0f;
      temp |= ((delay & 0xf000) >> 8);
   }
   SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2d,temp);
}

static void
SetCRT2SyncDither661(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo, USHORT ModeNo, USHORT RTI)
{
   USHORT infoflag;
   UCHAR temp;

   infoflag = SiS_Pr->SiS_RefIndex[RTI].Ext_InfoFlag;
   if(ModeNo <= 0x13) {
      infoflag = SiS_GetRegByte(SiS_Pr->SiS_P3ca+2);
   }
   infoflag &= 0xc0;
   if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
      temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x37);
      if(temp & 0x20) infoflag = temp;
      if(temp & 0x01) infoflag |= 0x01;
   }
   if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
      temp = 0x0c;
      if(infoflag & 0x01) temp ^= 0x14;  /* BIOS: 18, wrong */
      temp |= (infoflag >> 6);
      SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x1a,0xe0,temp);
   }
   if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
      temp = 0;
      if(infoflag & 0x01) temp |= 0x80;
      SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x1a,0x7f,temp);
      temp = 0x30;
      if(infoflag & 0x01) temp = 0x20;
      infoflag &= 0xc0;
      temp |= infoflag;
      SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0x0f,temp);
   }
}

static void
SetPanelParms661(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
   UCHAR *myptr;

   if(SiS_Pr->SiS_VBType & (VB_SIS301LV | VB_SIS302LV | VB_SIS302ELV)) {
      if(SiS_Pr->LVDSHL != -1) {
         SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x24,0xfc,SiS_Pr->LVDSHL);
      }
   }

   myptr = GetLCDStructPtr661(SiS_Pr, HwInfo);
   if(myptr) {
      if(SiS_Pr->SiS_VBType & (VB_SIS301LV | VB_SIS302LV | VB_SIS302ELV)) {
         if(SiS_Pr->LVDSHL == -1) {
            SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x24,0xE0,myptr[1] & 0x1f);
	 } else {
	    SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x24,0xE3,myptr[1] & 0x1c);
	 }
      }
      SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x0d,0x3f,myptr[2] & 0xc0);
   }
}

void
SiS_OEM661Setting(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
                  USHORT ModeNo,USHORT ModeIdIndex, USHORT RRTI)
{
   if(SiS_Pr->SiS_VBType & VB_SISVB) {

      SetDelayComp661(SiS_Pr,HwInfo,ModeNo,ModeIdIndex,RRTI);

      if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
         SetCRT2SyncDither661(SiS_Pr,HwInfo,ModeNo,RRTI);
         SetPanelParms661(SiS_Pr,HwInfo);
      }

      if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
         SetPhaseIncr(SiS_Pr,HwInfo,ModeNo,ModeIdIndex);
         SetYFilter(SiS_Pr,HwInfo,ModeNo,ModeIdIndex);
         SetAntiFlicker(SiS_Pr,HwInfo,ModeNo,ModeIdIndex);
         if(!(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)) {
            SetEdgeEnhance(SiS_Pr,HwInfo,ModeNo,ModeIdIndex);
         }
      }
   }
}

/* FinalizeLCD
 * This finalizes some CRT2 registers for the very panel used.
 * If we have a backup if these registers, we use it; otherwise
 * we set the register according to most BIOSes. However, this
 * function looks quite different in every BIOS, so you better
 * pray that we have a backup...
 */
void
SiS_FinalizeLCD(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
                PSIS_HW_INFO HwInfo)
{
  USHORT tempcl,tempch,tempbl,tempbh,tempbx,tempax,temp;
  USHORT resinfo,modeflag;

  if(!(SiS_Pr->SiS_VBType & VB_SIS301LV302LV)) return;

  if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
     if(SiS_Pr->LVDSHL != -1) {
        SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x24,0xfc,SiS_Pr->LVDSHL);
     }
  }

  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_PanelCustom) return;
  if(SiS_Pr->UseCustomMode) return;

  switch(SiS_Pr->SiS_CustomT) {
  case CUT_COMPAQ1280:
  case CUT_COMPAQ12802:
  case CUT_CLEVO1400:
  case CUT_CLEVO14002:
     return;
  }

  if(ModeNo <= 0x13) {
     resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
     modeflag =  SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  } else {
     resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
     modeflag =  SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
  }

  if(IS_SIS650) {
     if(!(SiS_GetReg(SiS_Pr->SiS_P3d4, 0x5f) & 0xf0)) {
        if(SiS_Pr->SiS_CustomT == CUT_CLEVO1024) {
	   SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1e,0x02);
	} else {
           SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1e,0x03);
	}
     }
  }

  if(SiS_Pr->SiS_CustomT == CUT_CLEVO1024) {
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
        /* Maybe all panels? */
        if(SiS_Pr->LVDSHL == -1) {
           SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x24,0xfc,0x01);
	}
	return;
     }
  }

  if(SiS_Pr->SiS_CustomT == CUT_CLEVO10242) {
     if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
	   if(SiS_Pr->LVDSHL == -1) {
	      /* Maybe all panels? */
              SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x24,0xfc,0x01);
	   }
	   if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
	      tempch = SiS_GetReg(SiS_Pr->SiS_P3d4,0x36) >> 4;
	      if(tempch == 3) {
	         SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x02);
	         SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1b,0x25);
	         SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1c,0x00);
	         SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1d,0x1b);
	      }
	   }
	   return;
	}
     }
  }

  if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
#ifdef SET_EMI
	if(SiS_Pr->SiS_VBType & (VB_SIS302LV | VB_SIS302ELV)) {
	   SiS_SetReg(SiS_Pr->SiS_Part4Port,0x2a,0x00);
	   SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x30,0x0c);
	   SiS_SetReg(SiS_Pr->SiS_Part4Port,0x34,0x10);
	}
#endif
     } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
        if(SiS_Pr->LVDSHL == -1) {
           /* Maybe ACER only? */
           SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x24,0xfc,0x01);
	}
     }
     tempch = SiS_GetReg(SiS_Pr->SiS_P3d4,0x36) >> 4;
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
	if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
	   SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1f,0x76);
	} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
	   if(tempch == 0x03) {
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x02);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1b,0x25);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1c,0x00);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1d,0x1b);
	   }
	   if((SiS_Pr->Backup == TRUE) && (SiS_Pr->Backup_Mode == ModeNo)) {
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x14,SiS_Pr->Backup_14);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x15,SiS_Pr->Backup_15);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x16,SiS_Pr->Backup_16);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x17,SiS_Pr->Backup_17);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,SiS_Pr->Backup_18);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x19,SiS_Pr->Backup_19);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1a,SiS_Pr->Backup_1a);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1b,SiS_Pr->Backup_1b);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1c,SiS_Pr->Backup_1c);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1d,SiS_Pr->Backup_1d);
	   } else if(!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {	/* 1.10.8w */
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x14,0x90);
	      if(ModeNo <= 0x13) {
	         SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x11);
		 if((resinfo == 0) || (resinfo == 2)) return;
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x18);
		 if((resinfo == 1) || (resinfo == 3)) return;
	      }
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x02);
	      if((ModeNo > 0x13) && (resinfo == SIS_RI_1024x768)) {
	         SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x02);  /* 1.10.7u */
#if 0
	         tempbx = 806;  /* 0x326 */			 /* other older BIOSes */
		 tempbx--;
		 temp = tempbx & 0xff;
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1b,temp);
		 temp = (tempbx >> 8) & 0x03;
		 SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x1d,0xf8,temp);
#endif
	      }
	   } else if(ModeNo <= 0x13) {
	      if(ModeNo <= 1) {
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x70);
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x19,0xff);
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1b,0x48);
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1d,0x12);
	      }
	      if(!(modeflag & HalfDCLK)) {
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x14,0x20);
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x15,0x1a);
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x16,0x28);
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x17,0x00);
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x4c);
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x19,0xdc);
		 if(ModeNo == 0x12) {
		    switch(tempch) {
		       case 0:
			  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x95);
			  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x19,0xdc);
			  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1a,0x10);
			  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1b,0x95);
			  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1c,0x48);
			  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1d,0x12);
			  break;
		       case 2:
			  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x95);
			  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1b,0x48);
			  break;
		       case 3:
			  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1b,0x95);
			  break;
		    }
		 }
	      }
	   }
	}
     } else {
        tempcl = tempbh = SiS_GetReg(SiS_Pr->SiS_Part2Port,0x01);
	tempcl &= 0x0f;
	tempbh &= 0x70;
	tempbh >>= 4;
	tempbl = SiS_GetReg(SiS_Pr->SiS_Part2Port,0x04);
	tempbx = (tempbh << 8) | tempbl;
	if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
	   if((resinfo == SIS_RI_1024x768) || (!(SiS_Pr->SiS_LCDInfo & DontExpandLCD))) {
	      if(SiS_Pr->SiS_SetFlag & LCDVESATiming) {
	      	 tempbx = 770;
	      } else {
	         if(tempbx > 770) tempbx = 770;
		 if(SiS_Pr->SiS_VGAVDE < 600) {
		    tempax = 768 - SiS_Pr->SiS_VGAVDE;
		    tempax >>= 4;  				 /* 1.10.7w; 1.10.6s: 3;  */
		    if(SiS_Pr->SiS_VGAVDE <= 480)  tempax >>= 4; /* 1.10.7w; 1.10.6s: < 480; >>=1; */
		    tempbx -= tempax;
		 }
	      }
	   } else return;
	}
	temp = tempbx & 0xff;
	SiS_SetReg(SiS_Pr->SiS_Part2Port,0x04,temp);
	temp = ((tempbx & 0xff00) >> 4) | tempcl;
	SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x01,0x80,temp);
     }
  }
}

#endif

/*  =================  SiS 300 O.E.M. ================== */

#ifdef SIS300

void
SetOEMLCDData2(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
               USHORT ModeNo,USHORT ModeIdIndex, USHORT RefTabIndex)
{
  USHORT crt2crtc=0, modeflag, myindex=0;
  UCHAR  temp;
  int i;

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     crt2crtc = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     crt2crtc = SiS_Pr->SiS_RefIndex[RefTabIndex].Ext_CRT2CRTC;
  }

  crt2crtc &= 0x3f;

  if(SiS_Pr->SiS_CustomT == CUT_BARCO1024) {
     SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x13,0xdf);
  }

  if(SiS_Pr->SiS_CustomT == CUT_BARCO1366) {
     if(modeflag & HalfDCLK) myindex = 1;

     if(SiS_Pr->SiS_SetFlag & LowModeTests) {
        for(i=0; i<7; i++) {
           if(barco_p1[myindex][crt2crtc][i][0]) {
	      SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,
	                      barco_p1[myindex][crt2crtc][i][0],
	   	   	      barco_p1[myindex][crt2crtc][i][2],
			      barco_p1[myindex][crt2crtc][i][1]);
	   }
        }
     }
     temp = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00);
     if(temp & 0x80) {
        temp = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x18);
        temp++;
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,temp);
     }
  }
}

static USHORT
GetOEMLCDPtr(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo, int Flag)
{
  UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
  USHORT tempbx=0,romptr=0;
  UCHAR customtable300[] = {
  	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
  };
  UCHAR customtable630[] = {
  	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
  };

  if(HwInfo->jChipType == SIS_300) {

    tempbx = (SiS_GetReg(SiS_Pr->SiS_P3d4,0x36) & 0x0f) - 2;
    if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) tempbx += 4;
    if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
       if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx += 3;
    }
    if((ROMAddr) && SiS_Pr->SiS_UseROM) {
       if(ROMAddr[0x235] & 0x80) {
          tempbx = SiS_Pr->SiS_LCDTypeInfo;
          if(Flag) {
	     romptr = ROMAddr[0x255] | (ROMAddr[0x256] << 8);
	     if(romptr) {
	        tempbx = ROMAddr[romptr + SiS_Pr->SiS_LCDTypeInfo];
	     } else {
	        tempbx = customtable300[SiS_Pr->SiS_LCDTypeInfo];
	     }
             if(tempbx == 0xFF) return 0xFFFF;
          }
	  tempbx <<= 1;
	  if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) tempbx++;
       }
    }

  } else {

    if(Flag) {
       if((ROMAddr) && SiS_Pr->SiS_UseROM) {
          romptr = ROMAddr[0x255] | (ROMAddr[0x256] << 8);
	  if(romptr) {
	     tempbx = ROMAddr[romptr + SiS_Pr->SiS_LCDTypeInfo];
	  } else {
	     tempbx = 0xff;
	  }
       } else {
          tempbx = customtable630[SiS_Pr->SiS_LCDTypeInfo];
       }
       if(tempbx == 0xFF) return 0xFFFF;
       tempbx <<= 2;
       if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) tempbx += 2;
       if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx++;
       return tempbx;
    }
    tempbx = SiS_Pr->SiS_LCDTypeInfo << 2;
    if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) tempbx += 2;
    if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx++;
  }
  return tempbx;
}

static void
SetOEMLCDDelay(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
               USHORT ModeNo,USHORT ModeIdIndex)
{
  UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
  USHORT index,temp,romptr=0;

  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_PanelCustom) return;

  if((ROMAddr) && SiS_Pr->SiS_UseROM) {
     if(!(ROMAddr[0x237] & 0x01)) return;
     if(!(ROMAddr[0x237] & 0x02)) return;
     romptr = ROMAddr[0x24b] | (ROMAddr[0x24c] << 8);
  }

  /* The Panel Compensation Delay should be set according to tables
   * here. Unfortunately, various BIOS versions don't case about
   * a uniform way using eg. ROM byte 0x220, but use different
   * hard coded delays (0x04, 0x20, 0x18) in SetGroup1().
   * Thus we don't set this if the user select a custom pdc or if
   * we otherwise detected a valid pdc.
   */
  if(SiS_Pr->PDC) return;

  temp = GetOEMLCDPtr(SiS_Pr,HwInfo, 0);

  if(SiS_Pr->UseCustomMode)
     index = 0;
  else
     index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex].VB_LCDDelayIndex;

  if(HwInfo->jChipType != SIS_300) {
     if(romptr) {
	romptr += (temp * 2);
	romptr = ROMAddr[romptr] | (ROMAddr[romptr + 1] << 8);
	romptr += index;
	temp = ROMAddr[romptr];
     } else {
	if(SiS_Pr->SiS_VBType & VB_SISVB) {
    	   temp = SiS300_OEMLCDDelay2[temp][index];
	} else {
           temp = SiS300_OEMLCDDelay3[temp][index];
        }
     }
  } else {
     if((ROMAddr) && SiS_Pr->SiS_UseROM && (ROMAddr[0x235] & 0x80)) {
	if(romptr) {
	   romptr += (temp * 2);
	   romptr = ROMAddr[romptr] | (ROMAddr[romptr + 1] << 8);
	   romptr += index;
	   temp = ROMAddr[romptr];
	} else {
	   temp = SiS300_OEMLCDDelay5[temp][index];
	}
     } else {
        if((ROMAddr) && SiS_Pr->SiS_UseROM) {
	   romptr = ROMAddr[0x249] | (ROMAddr[0x24a] << 8);
	   if(romptr) {
	      romptr += (temp * 2);
	      romptr = ROMAddr[romptr] | (ROMAddr[romptr + 1] << 8);
	      romptr += index;
	      temp = ROMAddr[romptr];
	   } else {
	      temp = SiS300_OEMLCDDelay4[temp][index];
	   }
	} else {
	   temp = SiS300_OEMLCDDelay4[temp][index];
	}
     }
  }
  temp &= 0x3c;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,~0x3C,temp);  /* index 0A D[6:4] */
}

static void
SetOEMLCDData(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
              USHORT ModeNo,USHORT ModeIdIndex)
{
#if 0  /* Unfinished; Data table missing */
  UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
  USHORT index,temp;

  if((ROMAddr) && SiS_Pr->SiS_UseROM) {
     if(!(ROMAddr[0x237] & 0x01)) return;
     if(!(ROMAddr[0x237] & 0x04)) return;
     /* No rom pointer in BIOS header! */
  }

  temp = GetOEMLCDPtr(SiS_Pr,HwInfo, 1);
  if(temp = 0xFFFF) return;

  index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex]._VB_LCDHIndex;
  for(i=0x14, j=0; i<=0x17; i++, j++) {
      SiS_SetReg(SiS_Pr->SiS_Part1Port,i,SiS300_LCDHData[temp][index][j]);
  }
  SiS_SetRegANDOR(SiS_SiS_Part1Port,0x1a, 0xf8, (SiS300_LCDHData[temp][index][j] & 0x07));

  index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex]._VB_LCDVIndex;
  SiS_SetReg(SiS_SiS_Part1Port,0x18, SiS300_LCDVData[temp][index][0]);
  SiS_SetRegANDOR(SiS_SiS_Part1Port,0x19, 0xF0, SiS300_LCDVData[temp][index][1]);
  SiS_SetRegANDOR(SiS_SiS_Part1Port,0x1A, 0xC7, (SiS300_LCDVData[temp][index][2] & 0x38));
  for(i=0x1b, j=3; i<=0x1d; i++, j++) {
      SiS_SetReg(SiS_Pr->SiS_Part1Port,i,SiS300_LCDVData[temp][index][j]);
  }
#endif
}

static USHORT
GetOEMTVPtr(SiS_Private *SiS_Pr)
{
  USHORT index;

  index = 0;
  if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode))  index += 4;
  if(SiS_Pr->SiS_VBType & VB_SISVB) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToSCART)  index += 2;
     else if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) index += 3;
     else if(SiS_Pr->SiS_TVMode & TVSetPAL)   index += 1;
  } else {
     if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) index += 2;
     if(SiS_Pr->SiS_TVMode & TVSetPAL)        index += 1;
  }
  return index;
}

static void
SetOEMTVDelay(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
              USHORT ModeNo,USHORT ModeIdIndex)
{
  UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
  USHORT index,temp,romptr=0;

  if((ROMAddr) && SiS_Pr->SiS_UseROM) {
     if(!(ROMAddr[0x238] & 0x01)) return;
     if(!(ROMAddr[0x238] & 0x02)) return;
     romptr = ROMAddr[0x241] | (ROMAddr[0x242] << 8);
  }

  temp = GetOEMTVPtr(SiS_Pr);

  index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex].VB_TVDelayIndex;

  if(romptr) {
     romptr += (temp * 2);
     romptr = ROMAddr[romptr] | (ROMAddr[romptr + 1] << 8);
     romptr += index;
     temp = ROMAddr[romptr];
  } else {
     if(SiS_Pr->SiS_VBType & VB_SISVB) {
        temp = SiS300_OEMTVDelay301[temp][index];
     } else {
        temp = SiS300_OEMTVDelayLVDS[temp][index];
     }
  }
  temp &= 0x3c;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,~0x3C,temp);  /* index 0A D[6:4] */
}

static void
SetOEMAntiFlicker(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
                  USHORT ModeNo, USHORT ModeIdIndex)
{
  UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
  USHORT index,temp,romptr=0;

  if((ROMAddr) && SiS_Pr->SiS_UseROM) {
     if(!(ROMAddr[0x238] & 0x01)) return;
     if(!(ROMAddr[0x238] & 0x04)) return;
     romptr = ROMAddr[0x243] | (ROMAddr[0x244] << 8);
  }

  temp = GetOEMTVPtr(SiS_Pr);

  index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex].VB_TVFlickerIndex;

  if(romptr) {
     romptr += (temp * 2);
     romptr = ROMAddr[romptr] | (ROMAddr[romptr + 1] << 8);
     romptr += index;
     temp = ROMAddr[romptr];
  } else {
     temp = SiS300_OEMTVFlicker[temp][index];
  }
  temp &= 0x70;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x0A,0x8F,temp);  /* index 0A D[6:4] */
}

static void
SetOEMPhaseIncr(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
                USHORT ModeNo,USHORT ModeIdIndex)
{
  UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
  USHORT index,i,j,temp,romptr=0;

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) return;

  if(SiS_Pr->SiS_TVMode & (TVSetNTSC1024 | TVSetNTSCJ | TVSetPALM | TVSetPALN)) return;

  if((ROMAddr) && SiS_Pr->SiS_UseROM) {
     if(!(ROMAddr[0x238] & 0x01)) return;
     if(!(ROMAddr[0x238] & 0x08)) return;
     romptr = ROMAddr[0x245] | (ROMAddr[0x246] << 8);
  }

  temp = GetOEMTVPtr(SiS_Pr);

  index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex].VB_TVPhaseIndex;

  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
     for(i=0x31, j=0; i<=0x34; i++, j++) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS300_Phase2[temp][index][j]);
     }
  } else {
     if(romptr) {
        romptr += (temp * 2);
	romptr = ROMAddr[romptr] | (ROMAddr[romptr + 1] << 8);
	romptr += (index * 4);
        for(i=0x31, j=0; i<=0x34; i++, j++) {
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,i,ROMAddr[romptr + j]);
	}
     } else {
        for(i=0x31, j=0; i<=0x34; i++, j++) {
           SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS300_Phase1[temp][index][j]);
	}
     }
  }
}

static void
SetOEMYFilter(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
              USHORT ModeNo,USHORT ModeIdIndex)
{
  UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
  USHORT index,temp,i,j,romptr=0;

  if(SiS_Pr->SiS_VBInfo & (SetCRT2ToSCART | SetCRT2ToHiVision | SetCRT2ToYPbPr525750)) return;

  if((ROMAddr) && SiS_Pr->SiS_UseROM) {
     if(!(ROMAddr[0x238] & 0x01)) return;
     if(!(ROMAddr[0x238] & 0x10)) return;
     romptr = ROMAddr[0x247] | (ROMAddr[0x248] << 8);
  }

  temp = GetOEMTVPtr(SiS_Pr);

  if(SiS_Pr->SiS_TVMode & TVSetPALM)      temp = 8;
  else if(SiS_Pr->SiS_TVMode & TVSetPALN) temp = 9;
  /* NTSCJ uses NTSC filters */

  index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex].VB_TVYFilterIndex;

  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
      for(i=0x35, j=0; i<=0x38; i++, j++) {
       	SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS300_Filter2[temp][index][j]);
      }
      for(i=0x48; i<=0x4A; i++, j++) {
     	SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS300_Filter2[temp][index][j]);
      }
  } else {
      if((romptr) && (!(SiS_Pr->SiS_TVMode & (TVSetPALM|TVSetPALN)))) {
         romptr += (temp * 2);
	 romptr = ROMAddr[romptr] | (ROMAddr[romptr + 1] << 8);
	 romptr += (index * 4);
	 for(i=0x35, j=0; i<=0x38; i++, j++) {
       	    SiS_SetReg(SiS_Pr->SiS_Part2Port,i,ROMAddr[romptr + j]);
         }
      } else {
         for(i=0x35, j=0; i<=0x38; i++, j++) {
       	    SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS300_Filter1[temp][index][j]);
         }
      }
  }
}

static USHORT
SiS_SearchVBModeID(SiS_Private *SiS_Pr, USHORT *ModeNo)
{
   USHORT ModeIdIndex;
   UCHAR VGAINFO = SiS_Pr->SiS_VGAINFO;

   if(*ModeNo <= 5) *ModeNo |= 1;

   for(ModeIdIndex=0; ; ModeIdIndex++) {
      if(SiS_Pr->SiS_VBModeIDTable[ModeIdIndex].ModeID == *ModeNo) break;
      if(SiS_Pr->SiS_VBModeIDTable[ModeIdIndex].ModeID == 0xFF)    return 0;
   }

   if(*ModeNo != 0x07) {
      if(*ModeNo > 0x03) return ModeIdIndex;
      if(VGAINFO & 0x80) return ModeIdIndex;
      ModeIdIndex++;
   }

   if(VGAINFO & 0x10) ModeIdIndex++;   /* 400 lines */
	                               /* else 350 lines */
   return ModeIdIndex;
}

void
SiS_OEM300Setting(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
		  USHORT ModeNo, USHORT ModeIdIndex, USHORT RefTableIndex)
{
  USHORT OEMModeIdIndex=0;

  if(!SiS_Pr->UseCustomMode) {
     OEMModeIdIndex = SiS_SearchVBModeID(SiS_Pr,&ModeNo);
     if(!(OEMModeIdIndex)) return;
  }

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
     SetOEMLCDDelay(SiS_Pr, HwInfo, ModeNo, OEMModeIdIndex);
     if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
        SetOEMLCDData(SiS_Pr, HwInfo, ModeNo, OEMModeIdIndex);
     }
  }
  if(SiS_Pr->UseCustomMode) return;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
     SetOEMTVDelay(SiS_Pr, HwInfo, ModeNo,OEMModeIdIndex);
     if(SiS_Pr->SiS_VBType & VB_SISVB) {
        SetOEMAntiFlicker(SiS_Pr, HwInfo, ModeNo, OEMModeIdIndex);
    	SetOEMPhaseIncr(SiS_Pr, HwInfo, ModeNo, OEMModeIdIndex);
       	SetOEMYFilter(SiS_Pr, HwInfo, ModeNo, OEMModeIdIndex);
     }
  }
}
#endif

