// CMA-backed memory adapter for the SDK's static librve; no camera/media module.
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

struct heap_alloc { uint64_t len; uint32_t fd, fd_flags; uint64_t heap_flags; };
struct dma_sync { uint64_t flags; };
#define HEAP_ALLOC _IOWR('R', 0, struct heap_alloc)
#define BUF_SYNC _IOW('b', 0, struct dma_sync)
struct allocation { void *virt; uint64_t phys; size_t size; int fd; struct allocation *next; };
static struct allocation *allocations;

static struct allocation *find(void *addr) {
    for (struct allocation *a=allocations; a; a=a->next)
        if ((uintptr_t)addr >= (uintptr_t)a->virt && (uintptr_t)addr < (uintptr_t)a->virt+a->size) return a;
    return NULL;
}
static int sync_buffer(void *addr, int end) {
    struct allocation *a=find(addr);
    if (!a) { errno=EINVAL; return -1; }
    struct dma_sync s={.flags=3u | (end ? 4u : 0u)};
    return ioctl(a->fd, BUF_SYNC, &s);
}
static struct allocation *allocate(uint32_t size) {
    struct allocation *a=calloc(1,sizeof(*a));
    if (!a) return NULL;
    a->size=((size_t)size+4095)&~(size_t)4095;
    int heap=open("/dev/rk_dma_heap/rk-dma-heap-cma", O_RDWR|O_CLOEXEC);
    if (heap<0) { perror("open CMA heap"); free(a); return NULL; }
    struct heap_alloc arg={.len=a->size,.fd_flags=O_RDWR|O_CLOEXEC};
    int rc=ioctl(heap,HEAP_ALLOC,&arg); close(heap);
    if (rc) { perror("CMA allocation"); free(a); return NULL; }
    a->fd=arg.fd;
    a->virt=mmap(NULL,a->size,PROT_READ|PROT_WRITE,MAP_SHARED,a->fd,0);
    if (a->virt==MAP_FAILED) { perror("CMA mmap"); close(a->fd); free(a); return NULL; }
    a->next=allocations; allocations=a;
    if (sync_buffer(a->virt,0)) goto fail;
    memset(a->virt,0,a->size);
    if (sync_buffer(a->virt,1)) goto fail;
    // Experimental adapter for this SDK: its RK heap has no GET_PHYS ioctl,
    // and inactive-CMA PFN mappings are absent from pagemap. The heap's procfs
    // report exposes the physical range indexed by the DMA-buf inode. Keep the
    // fd alive and verify both inode and range size before handing it to RVE.
    struct stat info;
    if (fstat(a->fd,&info)) goto fail;
    FILE *report=fopen("/proc/rk_dma_heap/dma_heap_info","r");
    if (!report) goto fail;
    char line[512];int matching=0,found=0;
    while (fgets(line,sizeof(line),report)) {
        char *inode=strstr(line,"i_ino = ");
        if(inode) matching=strtoull(inode+8,NULL,10)==(unsigned long long)info.st_ino;
        char *range=strchr(line,'[');
        unsigned long long start=0,end=0;
        if(matching && range && sscanf(range,"[%llx-%llx]",&start,&end)==2) {
            if(end>=start && end-start+1==a->size) {a->phys=start;found=1;}
            break;
        }
    }
    fclose(report);
    if(!found){errno=EFAULT;goto fail;}
    return a;
fail:
    perror("CMA mapping/physical range/sync"); allocations=a->next;
    munmap(a->virt,a->size); close(a->fd); free(a); return NULL;
}

int RK_MPI_MMZ_Alloc(void **handle,uint32_t size,uint32_t flags) {
    (void)flags; struct allocation *a=allocate(size); *handle=a; return a?0:-1;
}
int RK_MPI_MMZ_Free(void *handle) {
    struct allocation *a=handle, **link=&allocations;
    while (*link && *link!=a) link=&(*link)->next;
    if (!*link) return -1;
    *link=a->next; munmap(a->virt,a->size); close(a->fd); free(a); return 0;
}
uint64_t RK_MPI_MB_Handle2PhysAddr(void *handle) { return ((struct allocation*)handle)->phys; }
void *RK_MPI_MB_Handle2VirAddr(void *handle) { return ((struct allocation*)handle)->virt; }
void *RK_MPI_MB_VirAddr2Handle(void *addr) { return find(addr); }
uint64_t RK_MPI_MMZ_GetSize(void *handle) { return ((struct allocation*)handle)->size; }
int RK_MPI_MMZ_Handle2Fd(void *handle) { return ((struct allocation*)handle)->fd; }
int RK_MPI_MMZ_FlushCacheVaddrStart(void *addr,uint32_t size,uint32_t flags) { (void)size;(void)flags;return sync_buffer(addr,0); }
int RK_MPI_MMZ_FlushCacheVaddrEnd(void *addr,uint32_t size,uint32_t flags) { (void)size;(void)flags;return sync_buffer(addr,1); }
int rve_drm_init(void) { return 0; }
int rve_drm_deinit(void) { return 0; }
int RVE_DRM_MmzAlloc(uint64_t *phys,void **virt,uint32_t size) {
    struct allocation *a=allocate(size);
    if (!a) return -1;
    *phys=a->phys; *virt=a->virt; return 0;
}
int RVE_DRM_MmzFree(void *virt) { return RK_MPI_MMZ_Free(find(virt)); }
int RVE_DRM_MmzFlush_Start(void *virt) { return sync_buffer(virt,0); }
int RVE_DRM_MmzFlush_End(void *virt) { return sync_buffer(virt,1); }
int RVE_SYS_MmzAlloc(uint64_t *phys,void **virt,uint32_t size) { return RVE_DRM_MmzAlloc(phys,virt,size); }
int RVE_SYS_MmzAlloc_Cached(uint64_t *phys,void **virt,uint32_t size) { return RVE_DRM_MmzAlloc(phys,virt,size); }
int RVE_SYS_MmzFree(void *virt) { return RVE_DRM_MmzFree(virt); }
int RVE_SYS_MmzFlush_Start(void *virt) { return sync_buffer(virt,0); }
int RVE_SYS_MmzFlush_End(void *virt) { return sync_buffer(virt,1); }
