
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
typedef unsigned long u_long; typedef uint64_t bus_addr_t, bus_size_t, paddr_t; typedef uintptr_t vaddr_t; typedef void *bus_dma_tag_t;
struct vmspace { int dummy; }; struct vm_map { int d; };
#define PAGE_SIZE 4096UL
#define PGOFSET (PAGE_SIZE-1)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define VMSPACE_IS_KERNEL_P(vm) 1
#define vtophys(v) ((bus_addr_t)(v))
#define pmap_extract(a,b,c) 0
#define vm_map_pmap(m) 0
typedef struct { bus_addr_t ds_addr; bus_size_t ds_len; } bus_dma_segment_t;
struct map { bus_size_t _dm_boundary, dm_maxsegsz, _dm_bounce_thresh; int _dm_segcnt; bus_dma_segment_t dm_segs[64]; };
typedef struct map *bus_dmamap_t;
#define NV 64
static unsigned long vbase = 0x10000000UL, mpage[NV];
unsigned long rumpcomp_pci_virt_to_mach(void *v) {
  unsigned long a=(unsigned long)v, i=(a-vbase)/PAGE_SIZE; return mpage[i]+(a&PGOFSET); }

int
load_old(bus_dma_tag_t t, bus_dmamap_t map,
	void *buf, bus_size_t buflen, struct vmspace *vm, int flags,
	bus_addr_t *lastaddrp, int *segp, int first)
{
	bus_size_t sgsize;
	bus_addr_t curaddr, lastaddr, baddr, bmask;
	vaddr_t vaddr = (vaddr_t)buf;
	int seg;

//	printf("%s(%p,%p,%p,%u,%p,%#x,%p,%p,%u)\n", __func__,
//	    t, map, buf, buflen, vm, flags, lastaddrp, segp, first);

	lastaddr = *lastaddrp;
	bmask = ~(map->_dm_boundary - 1);

	for (seg = *segp; buflen > 0 ; ) {
		/*
		 * Get the physical address for this segment.
		 */
		if (!VMSPACE_IS_KERNEL_P(vm))
			(void) pmap_extract(vm_map_pmap(&vm->vm_map),
			    vaddr, (void *)&curaddr);
		else
			curaddr = vtophys(vaddr);

		/*
		 * If we're beyond the bounce threshold, notify
		 * the caller.
		 */
		if (map->_dm_bounce_thresh != 0 &&
		    curaddr >= map->_dm_bounce_thresh)
			return (EINVAL);

		/*
		 * Compute the segment size, and adjust counts.
		 */
		sgsize = PAGE_SIZE - ((u_long)vaddr & PGOFSET);
		if (buflen < sgsize)
			sgsize = buflen;
		sgsize = MIN(sgsize, map->dm_maxsegsz);

		/*
		 * Make sure we don't cross any boundaries.
		 */
		if (map->_dm_boundary > 0) {
			baddr = (curaddr + map->_dm_boundary) & bmask;
			if (sgsize > (baddr - curaddr))
				sgsize = (baddr - curaddr);
		}

		/*
		 * Insert chunk into a segment, coalescing with
		 * the previous segment if possible.
		 */
		if (first) {
			map->dm_segs[seg].ds_addr
			    = rumpcomp_pci_virt_to_mach((void *)(uintptr_t)curaddr);
			map->dm_segs[seg].ds_len = sgsize;
			first = 0;
		} else {
			if (curaddr == lastaddr &&
			    (map->dm_segs[seg].ds_len + sgsize) <=
			     map->dm_maxsegsz &&
			    (map->_dm_boundary == 0 ||
			     (map->dm_segs[seg].ds_addr & bmask) ==
			     (rumpcomp_pci_virt_to_mach((void*)(uintptr_t)curaddr)&bmask)))
				map->dm_segs[seg].ds_len += sgsize;
			else {
				if (++seg >= map->_dm_segcnt)
					break;
				map->dm_segs[seg].ds_addr =
				    rumpcomp_pci_virt_to_mach((void *)(uintptr_t)curaddr);
				map->dm_segs[seg].ds_len = sgsize;
			}
		}

		lastaddr = curaddr + sgsize;
		vaddr += sgsize;
		buflen -= sgsize;
	}

	*segp = seg;
	*lastaddrp = lastaddr;

	/*
	 * Did we fit?
	 */
	if (buflen != 0)
		return (EFBIG);		/* XXX better return value here? */

	return (0);
}

int
load_new(bus_dma_tag_t t, bus_dmamap_t map,
	void *buf, bus_size_t buflen, struct vmspace *vm, int flags,
	bus_addr_t *lastaddrp, int *segp, int first)
{
	bus_size_t sgsize;
	bus_addr_t curaddr, lastaddr, baddr, bmask, maddr;
	vaddr_t vaddr = (vaddr_t)buf;
	int seg;

//	printf("%s(%p,%p,%p,%u,%p,%#x,%p,%p,%u)\n", __func__,
//	    t, map, buf, buflen, vm, flags, lastaddrp, segp, first);

	lastaddr = *lastaddrp;
	bmask = ~(map->_dm_boundary - 1);

	for (seg = *segp; buflen > 0 ; ) {
		/*
		 * Get the physical address for this segment.
		 */
		if (!VMSPACE_IS_KERNEL_P(vm))
			(void) pmap_extract(vm_map_pmap(&vm->vm_map),
			    vaddr, (void *)&curaddr);
		else
			curaddr = vtophys(vaddr);

		/*
		 * If we're beyond the bounce threshold, notify
		 * the caller.
		 */
		if (map->_dm_bounce_thresh != 0 &&
		    curaddr >= map->_dm_bounce_thresh)
			return (EINVAL);

		/*
		 * CURADDR is a host virtual address: the device needs the
		 * machine address, and that is also what boundaries and
		 * contiguity are about, since pages that are contiguous in
		 * virtual memory need not be in machine memory.
		 */
		maddr = rumpcomp_pci_virt_to_mach((void *)(uintptr_t)curaddr);

		/*
		 * Compute the segment size, and adjust counts.
		 */
		sgsize = PAGE_SIZE - ((u_long)vaddr & PGOFSET);
		if (buflen < sgsize)
			sgsize = buflen;
		sgsize = MIN(sgsize, map->dm_maxsegsz);

		/*
		 * Make sure we don't cross any boundaries.
		 */
		if (map->_dm_boundary > 0) {
			baddr = (maddr + map->_dm_boundary) & bmask;
			if (sgsize > (baddr - maddr))
				sgsize = (baddr - maddr);
		}

		/*
		 * Insert chunk into a segment, coalescing with
		 * the previous segment if possible.
		 */
		if (first) {
			map->dm_segs[seg].ds_addr = maddr;
			map->dm_segs[seg].ds_len = sgsize;
			first = 0;
		} else {
			if (maddr == map->dm_segs[seg].ds_addr +
			     map->dm_segs[seg].ds_len &&
			    (map->dm_segs[seg].ds_len + sgsize) <=
			     map->dm_maxsegsz &&
			    (map->_dm_boundary == 0 ||
			     (map->dm_segs[seg].ds_addr & bmask) ==
			     (maddr & bmask)))
				map->dm_segs[seg].ds_len += sgsize;
			else {
				if (++seg >= map->_dm_segcnt)
					break;
				map->dm_segs[seg].ds_addr = maddr;
				map->dm_segs[seg].ds_len = sgsize;
			}
		}

		lastaddr = curaddr + sgsize;
		vaddr += sgsize;
		buflen -= sgsize;
	}

	*segp = seg;
	*lastaddrp = lastaddr;

	/*
	 * Did we fit?
	 */
	if (buflen != 0)
		return (EFBIG);		/* XXX better return value here? */

	return (0);
}


static int check(const char *name, struct map *m, int nseg, unsigned long buf, unsigned long len, int *crossbad) {
  unsigned long off=0; int bad=0;
  for (int s=0; s<=nseg; s++) {
    bus_addr_t a=m->dm_segs[s].ds_addr; bus_size_t l=m->dm_segs[s].ds_len;
    if (((a) & ~0xffffUL) != ((a+l-1) & ~0xffffUL)) (*crossbad)++;
    for (bus_size_t k=0;k<l;k++,off++)
      if (a+k != rumpcomp_pci_virt_to_mach((void*)(buf+off))) { bad++; break; }
  }
  if (off != len) bad++;
  return bad;
}
int main(void) {
  srand(1); long wrong_old=0, wrong_new=0, cross_old=0, cross_new=0, trials=200000;
  for (long t=0;t<trials;t++) {
    /* physical pages: runs of contiguous pages, scattered in a 1 MiB window */
    unsigned long m = 0x79700000UL + (rand()%256)*PAGE_SIZE;
    for (int i=0;i<NV;i++) { if (rand()%3==0) m = 0x79700000UL + (rand()%256)*PAGE_SIZE; mpage[i]=m; m+=PAGE_SIZE; }
    unsigned long buf = vbase + (rand()%8)*PAGE_SIZE + (rand()%8)*512, len = 512*(1+rand()%64);
    struct map mo={0x10000,0x10000,0,64}, mn=mo; bus_addr_t la; int seg; int c;
    seg=0; la=0; if (load_old(0,&mo,(void*)buf,len,0,0,&la,&seg,1)==0) { c=0; if (check("old",&mo,seg,buf,len,&c)) wrong_old++; cross_old+=c!=0; }
    seg=0; la=0; if (load_new(0,&mn,(void*)buf,len,0,0,&la,&seg,1)==0) { c=0; if (check("new",&mn,seg,buf,len,&c)) wrong_new++; cross_new+=c!=0; }
  }
  printf("trials %ld: old: wrong mapping %ld, crosses 64K %ld; new: wrong mapping %ld, crosses 64K %ld\n", trials, wrong_old, cross_old, wrong_new, cross_new);
  return wrong_new || cross_new;
}
