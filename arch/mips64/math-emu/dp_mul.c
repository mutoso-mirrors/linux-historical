/* IEEE754 floating point arithmetic
 * double precision: common utilities
 */
/*
 * MIPS floating point support
 * Copyright (C) 1994-2000 Algorithmics Ltd.  All rights reserved.
 * http://www.algor.co.uk
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 */


#include "ieee754dp.h"

ieee754dp ieee754dp_mul(ieee754dp x, ieee754dp y)
{
	COMPXDP;
	COMPYDP;

	CLEARCX;

	EXPLODEXDP;
	EXPLODEYDP;

	switch (CLPAIR(xc, yc)) {
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_SNAN):
		return ieee754dp_nanxcpt(ieee754dp_bestnan(x, y), "mul", x,
					 y);

	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_SNAN):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_SNAN):
		return ieee754dp_nanxcpt(y, "mul", x, y);

	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_SNAN, IEEE754_CLASS_INF):
		return ieee754dp_nanxcpt(x, "mul", x, y);

	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_QNAN):
		return ieee754dp_bestnan(x, y);

	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_QNAN):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_QNAN):
		return y;

	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_QNAN, IEEE754_CLASS_INF):
		return x;


		/* Infinity handling */

	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_INF):
		SETCX(IEEE754_INVALID_OPERATION);
		return ieee754dp_xcpt(ieee754dp_indef(), "mul", x, y);

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_INF):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_INF, IEEE754_CLASS_INF):
		return ieee754dp_inf(xs ^ ys);

	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_NORM):
	case CLPAIR(IEEE754_CLASS_ZERO, IEEE754_CLASS_DNORM):
	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_ZERO):
	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_ZERO):
		return ieee754dp_zero(xs ^ ys);


	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_DNORM):
		DPDNORMX;

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_DNORM):
		DPDNORMY;
		break;

	case CLPAIR(IEEE754_CLASS_DNORM, IEEE754_CLASS_NORM):
		DPDNORMX;
		break;

	case CLPAIR(IEEE754_CLASS_NORM, IEEE754_CLASS_NORM):
		break;
	}
	/* rm = xm * ym, re = xe+ye basicly */
	assert(xm & DP_HIDDEN_BIT);
	assert(ym & DP_HIDDEN_BIT);
	{
		int re = xe + ye;
		int rs = xs ^ ys;
		unsigned long long rm;

		/* shunt to top of word */
		xm <<= 64 - (DP_MBITS + 1);
		ym <<= 64 - (DP_MBITS + 1);

		/* multiply 32bits xm,ym to give high 32bits rm with stickness
		 */

		/* 32 * 32 => 64 */
#define DPXMULT(x,y)	((unsigned long long)(x) * (unsigned long long)y)

		{
			unsigned lxm = xm;
			unsigned hxm = xm >> 32;
			unsigned lym = ym;
			unsigned hym = ym >> 32;
			unsigned long long lrm;
			unsigned long long hrm;

			lrm = DPXMULT(lxm, lym);
			hrm = DPXMULT(hxm, hym);

			{
				unsigned long long t = DPXMULT(lxm, hym);
				{
					unsigned long long at =
					    lrm + (t << 32);
					hrm += at < lrm;
					lrm = at;
				}
				hrm = hrm + (t >> 32);
			}

			{
				unsigned long long t = DPXMULT(hxm, lym);
				{
					unsigned long long at =
					    lrm + (t << 32);
					hrm += at < lrm;
					lrm = at;
				}
				hrm = hrm + (t >> 32);
			}
			rm = hrm | (lrm != 0);
		}

		/*
		 * sticky shift down to normal rounding precision
		 */
		if ((signed long long) rm < 0) {
			rm =
			    (rm >> (64 - (DP_MBITS + 1 + 3))) |
			    ((rm << (DP_MBITS + 1 + 3)) != 0);
			re++;
		} else {
			rm =
			    (rm >> (64 - (DP_MBITS + 1 + 3 + 1))) |
			    ((rm << (DP_MBITS + 1 + 3 + 1)) != 0);
		}
		assert(rm & (DP_HIDDEN_BIT << 3));
		DPNORMRET2(rs, re, rm, "mul", x, y);
	}
}
