/*
 * linux/net/sunrpc/xdr.c
 *
 * Generic XDR support.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/pagemap.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/msg_prot.h>

/*
 * XDR functions for basic NFS types
 */
u32 *
xdr_encode_netobj(u32 *p, const struct xdr_netobj *obj)
{
	unsigned int	quadlen = XDR_QUADLEN(obj->len);

	p[quadlen] = 0;		/* zero trailing bytes */
	*p++ = htonl(obj->len);
	memcpy(p, obj->data, obj->len);
	return p + XDR_QUADLEN(obj->len);
}

u32 *
xdr_decode_netobj_fixed(u32 *p, void *obj, unsigned int len)
{
	if (ntohl(*p++) != len)
		return NULL;
	memcpy(obj, p, len);
	return p + XDR_QUADLEN(len);
}

u32 *
xdr_decode_netobj(u32 *p, struct xdr_netobj *obj)
{
	unsigned int	len;

	if ((len = ntohl(*p++)) > XDR_MAX_NETOBJ)
		return NULL;
	obj->len  = len;
	obj->data = (u8 *) p;
	return p + XDR_QUADLEN(len);
}

u32 *
xdr_encode_array(u32 *p, const char *array, unsigned int len)
{
	int quadlen = XDR_QUADLEN(len);

	p[quadlen] = 0;
	*p++ = htonl(len);
	memcpy(p, array, len);
	return p + quadlen;
}

u32 *
xdr_encode_string(u32 *p, const char *string)
{
	return xdr_encode_array(p, string, strlen(string));
}

u32 *
xdr_decode_string(u32 *p, char **sp, int *lenp, int maxlen)
{
	unsigned int	len;
	char		*string;

	if ((len = ntohl(*p++)) > maxlen)
		return NULL;
	if (lenp)
		*lenp = len;
	if ((len % 4) != 0) {
		string = (char *) p;
	} else {
		string = (char *) (p - 1);
		memmove(string, p, len);
	}
	string[len] = '\0';
	*sp = string;
	return p + XDR_QUADLEN(len);
}

u32 *
xdr_decode_string_inplace(u32 *p, char **sp, int *lenp, int maxlen)
{
	unsigned int	len;

	if ((len = ntohl(*p++)) > maxlen)
		return NULL;
	*lenp = len;
	*sp = (char *) p;
	return p + XDR_QUADLEN(len);
}

void
xdr_encode_pages(struct xdr_buf *xdr, struct page **pages, unsigned int base,
		 unsigned int len)
{
	xdr->pages = pages;
	xdr->page_base = base;
	xdr->page_len = len;

	if (len & 3) {
		struct iovec *iov = xdr->tail;
		unsigned int pad = 4 - (len & 3);

		iov->iov_base = (void *) "\0\0\0";
		iov->iov_len  = pad;
		len += pad;
	}
	xdr->len += len;
}


/*
 * Realign the iovec if the server missed out some reply elements
 * (such as post-op attributes,...)
 * Note: This is a simple implementation that assumes that
 *            len <= iov->iov_len !!!
 *       The RPC header (assumed to be the 1st element in the iov array)
 *            is not shifted.
 */
void xdr_shift_iovec(struct iovec *iov, int nr, size_t len)
{
	struct iovec *pvec;

	for (pvec = iov + nr - 1; nr > 1; nr--, pvec--) {
		struct iovec *svec = pvec - 1;

		if (len > pvec->iov_len) {
			printk(KERN_DEBUG "RPC: Urk! Large shift of short iovec.\n");
			return;
		}
		memmove((char *)pvec->iov_base + len, pvec->iov_base,
			pvec->iov_len - len);

		if (len > svec->iov_len) {
			printk(KERN_DEBUG "RPC: Urk! Large shift of short iovec.\n");
			return;
		}
		memcpy(pvec->iov_base,
		       (char *)svec->iov_base + svec->iov_len - len, len);
	}
}

/*
 * Zero the last n bytes in an iovec array of 'nr' elements
 */
void xdr_zero_iovec(struct iovec *iov, int nr, size_t n)
{
	struct iovec *pvec;

	for (pvec = iov + nr - 1; n && nr > 0; nr--, pvec--) {
		if (n < pvec->iov_len) {
			memset((char *)pvec->iov_base + pvec->iov_len - n, 0, n);
			n = 0;
		} else {
			memset(pvec->iov_base, 0, pvec->iov_len);
			n -= pvec->iov_len;
		}
	}
}

/*
 * Map a struct xdr_buf into an iovec array.
 */
int xdr_kmap(struct iovec *iov_base, struct xdr_buf *xdr, unsigned int base)
{
	struct iovec	*iov = iov_base;
	struct page	**ppage = xdr->pages;
	unsigned int	len, pglen = xdr->page_len;

	len = xdr->head[0].iov_len;
	if (base < len) {
		iov->iov_len = len - base;
		iov->iov_base = (char *)xdr->head[0].iov_base + base;
		iov++;
		base = 0;
	} else
		base -= len;

	if (pglen == 0)
		goto map_tail;
	if (base >= pglen) {
		base -= pglen;
		goto map_tail;
	}
	if (base || xdr->page_base) {
		pglen -= base;
		base  += xdr->page_base;
		ppage += base >> PAGE_CACHE_SHIFT;
		base &= ~PAGE_CACHE_MASK;
	}
	do {
		len = PAGE_CACHE_SIZE;
		iov->iov_base = kmap(*ppage);
		if (base) {
			iov->iov_base += base;
			len -= base;
			base = 0;
		}
		if (pglen < len)
			len = pglen;
		iov->iov_len = len;
		iov++;
		ppage++;
	} while ((pglen -= len) != 0);
map_tail:
	if (xdr->tail[0].iov_len) {
		iov->iov_len = xdr->tail[0].iov_len - base;
		iov->iov_base = (char *)xdr->tail[0].iov_base + base;
		iov++;
	}
	return (iov - iov_base);
}

void xdr_kunmap(struct xdr_buf *xdr, unsigned int base)
{
	struct page	**ppage = xdr->pages;
	unsigned int	pglen = xdr->page_len;

	if (!pglen)
		return;
	if (base > xdr->head[0].iov_len)
		base -= xdr->head[0].iov_len;
	else
		base = 0;

	if (base >= pglen)
		return;
	if (base || xdr->page_base) {
		pglen -= base;
		ppage += base >> PAGE_CACHE_SHIFT;
	}
	for (;;) {
		flush_dcache_page(*ppage);
		flush_page_to_ram(*ppage);
		kunmap(*ppage);
		if (pglen <= PAGE_CACHE_SIZE)
			break;
		pglen -= PAGE_CACHE_SIZE;
		ppage++;
	}
}
