/* Included only with FRONT_CPU_TAIL. All state refers to one exclusive frame. */
#include "front_tail.h"
static int front_tail_active,front_tail_used;
#define TAIL_SMALL __attribute__((optimize("Os")))
static TAIL_SMALL void front_tail_begin(uintptr_t base,struct front_control *f) {
    volatile struct front_tail_host *h=FRONT_TAIL_PTR(front_tail_host,base,FRONT_TAIL_HOST_OFF);
    struct front_tail_remote *r=FRONT_TAIL_PTR(front_tail_remote,base,FRONT_TAIL_REMOTE_OFF);
    inv_dcache_range(base+FRONT_TAIL_OFF+FRONT_TAIL_HOST_OFF,64);
    front_tail_active=h->magic==FRONT_TAIL_MAGIC && h->timestamp==f->timestamp;
    front_tail_used=0;
    if(!front_tail_active)return;
    memset(r,0,sizeof(*r));r->magic=FRONT_TAIL_MAGIC;r->timestamp=f->timestamp;
    clean_dcache_range((uintptr_t)r,64);
}
static TAIL_SMALL unsigned front_tail_offer(void *memory,const struct lk_tpu_level *a,const struct lk_tpu_level *b,
                                          unsigned levels,const struct front_point *points,unsigned count) {
    uintptr_t base=(uintptr_t)memory;
    if(!front_tail_active || front_tail_used || count<FRONT_TAIL_MIN_POINTS)return 0;
    /* Only right-camera temporal LK, after both cameras' top-up is complete. */
    if(a[0].planes[0]==b[0].planes[0] || ((a[0].planes[0]-FRONT_PYR_OFF)/FRONT_CAM_BYTES)%2!=1 ||
       ((b[0].planes[0]-FRONT_PYR_OFF)/FRONT_CAM_BYTES)%2!=1)return 0;
    volatile struct front_tail_host *h=FRONT_TAIL_PTR(front_tail_host,base,FRONT_TAIL_HOST_OFF);
    inv_dcache_range((uintptr_t)h,64);
    if(h->state!=FRONT_TAIL_AVAILABLE)return 0;
    struct front_tail_task *task=FRONT_TAIL_PTR(front_tail_task,base,FRONT_TAIL_TASK_OFF);
    struct front_tail_remote *r=FRONT_TAIL_PTR(front_tail_remote,base,FRONT_TAIL_REMOTE_OFF);
    task->timestamp=h->timestamp;task->first=count-FRONT_TAIL_POINTS;task->count=FRONT_TAIL_POINTS;task->levels=levels;
    memcpy(task->previous,a,levels*sizeof(*a));memcpy(task->current,b,levels*sizeof(*b));
    for(unsigned i=0;i<FRONT_TAIL_POINTS;++i){task->input[i].x=points[task->first+i].x;task->input[i].y=points[task->first+i].y;}
    clean_dcache_range((uintptr_t)task,sizeof(*task));
    r->points=FRONT_TAIL_POINTS;r->state=FRONT_TAIL_OFFERED;clean_dcache_range((uintptr_t)r,64);
    front_tail_used=1;return FRONT_TAIL_POINTS;
}
static TAIL_SMALL int front_tail_join(uintptr_t base,struct lk_tpu_point *output,unsigned char *valid) {
    volatile struct front_tail_host *h=FRONT_TAIL_PTR(front_tail_host,base,FRONT_TAIL_HOST_OFF);
    struct front_tail_remote *r=FRONT_TAIL_PTR(front_tail_remote,base,FRONT_TAIL_REMOTE_OFF);
    uint64_t start=probe_clock(),poll=0;
    for(;;){uint64_t now=probe_clock();
        if(now-poll>=625u){inv_dcache_range((uintptr_t)h,64);poll=now;}
        if(h->state==FRONT_TAIL_HOST_DONE)break;
        if(h->state==FRONT_TAIL_HOST_FAILED)return -71;
        if(now-start>12500000u)return -72;
    }
    r->wait_ticks=probe_clock()-start;
    struct front_tail_result *result=FRONT_TAIL_PTR(front_tail_result,base,FRONT_TAIL_RESULT_OFF);
    inv_dcache_range((uintptr_t)result,sizeof(*result));
    memcpy(output,result->output,sizeof(result->output));memcpy(valid,result->valid,sizeof(result->valid));
    return 0;
}
static TAIL_SMALL void front_tail_end(uintptr_t base,int error) {
    if(!front_tail_active)return;
    struct front_tail_remote *r=FRONT_TAIL_PTR(front_tail_remote,base,FRONT_TAIL_REMOTE_OFF);
    r->error=error;r->state=FRONT_TAIL_FINISHED;clean_dcache_range((uintptr_t)r,64);
}
#undef TAIL_SMALL
