/*
 * include/linux/pagevec.h
 *
 * In many places it is efficient to batch an operation up against multiple
 * pages.  A pagevec is a multipage container which is used for that.
 */

#define PAGEVEC_SIZE	16

struct page;
struct address_space;

struct pagevec {
	unsigned nr;
	struct page *pages[PAGEVEC_SIZE];
};

void __pagevec_release(struct pagevec *pvec);
void __pagevec_release_nonlru(struct pagevec *pvec);
void __pagevec_free(struct pagevec *pvec);
void __pagevec_lru_add(struct pagevec *pvec);
void lru_add_drain(void);
void pagevec_deactivate_inactive(struct pagevec *pvec);
void pagevec_strip(struct pagevec *pvec);
unsigned int pagevec_lookup(struct pagevec *pvec, struct address_space *mapping,
		pgoff_t start, unsigned int nr_pages);

static inline void pagevec_init(struct pagevec *pvec)
{
	pvec->nr = 0;
}

static inline unsigned pagevec_count(struct pagevec *pvec)
{
	return pvec->nr;
}

static inline unsigned pagevec_space(struct pagevec *pvec)
{
	return PAGEVEC_SIZE - pvec->nr;
}

/*
 * Add a page to a pagevec.  Returns the number of slots still available.
 */
static inline unsigned pagevec_add(struct pagevec *pvec, struct page *page)
{
	pvec->pages[pvec->nr++] = page;
	return pagevec_space(pvec);
}

static inline void pagevec_release(struct pagevec *pvec)
{
	if (pagevec_count(pvec))
		__pagevec_release(pvec);
}

static inline void pagevec_release_nonlru(struct pagevec *pvec)
{
	if (pagevec_count(pvec))
		__pagevec_release_nonlru(pvec);
}

static inline void pagevec_free(struct pagevec *pvec)
{
	if (pagevec_count(pvec))
		__pagevec_free(pvec);
}

static inline void pagevec_lru_add(struct pagevec *pvec)
{
	if (pagevec_count(pvec))
		__pagevec_lru_add(pvec);
}
