/* SPDX-License-Identifier: MIT */
/* Real Mu block + controller path; only MMIO, DMA and media are synthetic. */
#include "AppleNvmeBlockCore.h"
#include "AppleNvmeControllerCore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#define DMA UINT64_C(0x100000000)
#define CAPACITY 8192u
#define NS_BLOCKS (UINT64_C(0x100000000) + 1024u)
struct fixture {
    struct ntasi_ans_controller c;
    struct ntasi_ans_block_device b;
    struct ntasi_ans_sqe asq[64],isq[64];
    struct ntasi_ans_cqe acq[64],icq[64];
    struct ntasi_ans_tcb atcb[64],itcb[64];
    unsigned char bounce[CAPACITY];
    uint32_t cc,sector,reads,fail_read,mode,wbar,rbar,services;
    uint64_t lbas[256]; uint32_t blocks[256];
    uint64_t prp2[256];
};
static unsigned checks;
static void need(int ok,const char *why){checks++;if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
static unsigned char pattern(uint64_t lba,size_t i){return (unsigned char)(lba*37u+i*13u+(i>>8));}
static uint32_t read32(void *o,uint32_t reg){
    struct fixture *f=o;
    if(reg==NTASI_ANS_REG_BOOT_STATUS)return NTASI_ANS_BOOT_STATUS_OK;
    if(reg==NTASI_ANS_REG_CC)return f->cc;
    if(reg==NTASI_ANS_REG_CSTS)return (f->cc&NTASI_ANS_CC_EN?NTASI_ANS_CSTS_RDY:0)|((f->cc&NTASI_ANS_CC_SHN_MASK)?NTASI_ANS_CSTS_SHST_DONE<<NTASI_ANS_CSTS_SHST_SHIFT:0);
    if(reg==NTASI_ANS_REG_NVMMU_TCB_STAT)return f->mode==3 && f->reads==f->fail_read ? 1:0;
    return 0;
}
static void write32(void *o,uint32_t reg,uint32_t value){
    struct fixture *f=o;
    if(reg==NTASI_ANS_REG_CC){f->cc=value;return;}
    if(reg!=ntasi_ans_sq_db_off(true) && reg!=ntasi_ans_sq_db_off(false))return;
    int admin=reg==ntasi_ans_sq_db_off(true);
    struct ntasi_ans_controller_queue *q=admin?&f->c.admin:&f->c.io;
    struct ntasi_ans_sqe *s=&((struct ntasi_ans_sqe *)q->memory.commands)[value];
    struct ntasi_ans_tcb *t=&q->memory.tcbs[value];
    need(f->wbar>0,"write barrier before doorbell");
    need(t->opcode==s->opcode && t->command_id==s->tag && t->length==(uint16_t)s->cdw12 && t->prp1==s->prp1 && t->prp2==s->prp2,"TCB equals actual SQE");
    if(admin && s->opcode==NTASI_ANS_ADMIN_CMD_IDENTIFY){
        need(s->prp1==DMA && !s->prp2,"identify DMA ownership");memset(f->bounce,0,4096);
        uint64_t n=NS_BLOCKS;memcpy(f->bounce,&n,8);f->bounce[0x82]=f->sector==4096?12:9;
    }else if(!admin){
        need(s->opcode==NTASI_ANS_CMD_READ && s->nsid==1,"only expected READ data path");
        uint64_t lba=((uint64_t)s->cdw11<<32)|s->cdw10;uint32_t n=(s->cdw12&0xffffu)+1,bytes=n*f->sector;
        need(f->reads<256,"trace capacity");f->lbas[f->reads]=lba;f->blocks[f->reads]=n;f->prp2[f->reads]=s->prp2;f->reads++;
        need(!(s->cdw12&0xffff0000u) && bytes<=CAPACITY && s->prp1==DMA,"READ NLB and first DMA page");
        need(s->prp2==(bytes>4096?DMA+4096:0),"direct second page iff needed");
        need(t->dma_flags==NTASI_ANS_DMA_FROM_DEVICE,"READ DMA direction");
        need(lba<NS_BLOCKS && n<=NS_BLOCKS-lba,"media range");
        for(uint32_t k=0;k<n;k++)for(uint32_t j=0;j<f->sector;j++)f->bounce[k*f->sector+j]=pattern(lba+k,j);
        if(f->mode==4 && f->reads==f->fail_read)return;
    }
    struct ntasi_ans_cqe *e=&q->memory.completions[q->completion.head];memset(e,0,sizeof(*e));e->tag=s->tag;e->status=q->completion.phase;
    if(!admin && f->reads==f->fail_read){if(f->mode==1)e->status|=2;if(f->mode==2)e->tag++;}
}
static void write64(void *o,uint32_t reg,uint64_t value){write32(o,reg,(uint32_t)value);write32(o,reg+4,(uint32_t)(value>>32));}
static void wbar(void *o){((struct fixture *)o)->wbar++;}
static void rbar(void *o){((struct fixture *)o)->rbar++;}
static void service(void *o){((struct fixture *)o)->services++;}
static int execute(void *o,bool admin,const struct ntasi_ans_sqe *s,enum ntasi_ans_dma_direction d,uint64_t *r){struct fixture *f=o;return ntasi_ans_controller_execute(&f->c,admin?&f->c.admin:&f->c.io,s,d,r);}
static struct fixture *start(uint32_t sector,int batch){
    struct fixture *f=calloc(1,sizeof(*f));need(f!=NULL,"fixture allocation");f->sector=sector;
    struct ntasi_ans_queue_memory a={f->asq,f->acq,f->atcb,0x100000,0x104000,0x108000},i={f->isq,f->icq,f->itcb,0x10c000,0x110000,0x114000};
    struct ntasi_ans_controller_ops ops={read32,write32,write64,rbar,wbar,service};
    need(ntasi_ans_controller_start_variant(&f->c,&ops,&ntasi_ans_hw_t8142,f,64,8,&a,&i)==0,"real T8142 controller start");
    need(ntasi_ans_block_device_init(&f->b,execute,f,f->bounce,DMA,CAPACITY)==0,"block initialize");
    need(ntasi_ans_block_identify(&f->b,1)==0,"real identify command");
#ifndef BASELINE
    if(batch)need(ntasi_ans_block_set_read_batch(&f->b,CAPACITY)==0,"explicit batching enable");
#else
    (void)batch;
#endif
    return f;
}
static void finish(struct fixture *f){need(ntasi_ans_controller_stop(&f->c)==0,"real queue delete and shutdown");need(!f->c.enabled && !f->c.io_queues_created,"shutdown disabled");free(f);}
static void healthy(uint32_t sector,uint64_t lba,uint32_t n,int batch,const char *file){
    struct fixture *f=start(sector,batch);size_t bytes=(size_t)n*sector;unsigned char *p=malloc(bytes+2);need(p!=NULL,"output allocation");memset(p,0xa5,bytes+2);
    need(ntasi_ans_block_read(&f->b,lba,n,p+1,bytes)==0,"real block read success");
    for(uint32_t k=0;k<n;k++)for(uint32_t j=0;j<sector;j++)need(p[1+(size_t)k*sector+j]==pattern(lba+k,j),"exact byte oracle");
    need(p[0]==0xa5 && p[bytes+1]==0xa5,"caller canaries");uint32_t per=batch?CAPACITY/sector:1;
    need(f->reads==(n+per-1)/per,"bounded sequential READ command reduction");
    uint64_t next=lba;for(uint32_t k=0;k<f->reads;k++){need(f->lbas[k]==next,"sequential LBA trace");next+=f->blocks[k];}
    need(next==lba+n && f->rbar>0,"complete read/barrier trace");
    if(file){FILE *x=fopen(file,"wb");need(x!=NULL,"artifact create");need(fwrite(p+1,1,bytes,x)==bytes,"artifact bytes");fclose(x);}
    printf("{\"case\":\"healthy\",\"sector\":%u,\"lba\":%"PRIu64",\"blocks\":%u,\"batch\":%d,\"commands\":%u}\n",sector,lba,n,batch,f->reads);free(p);finish(f);
}
int main(int argc,char **argv){
    need(argc==2,"output artifact argument");healthy(4096,7,5,1,argv[1]);healthy(4096,3,1,1,NULL);healthy(4096,3,2,1,NULL);healthy(4096,UINT64_C(0xffffffff),3,1,NULL);healthy(512,9,33,1,NULL);healthy(512,9,17,0,NULL);
    struct fixture *f=start(4096,1);unsigned char out[5*4096];
    for(unsigned k=0;k<80;k++)need(ntasi_ans_block_read(&f->b,k,1,out,sizeof(out))==0,"completion ring wrap");finish(f);
    for(unsigned mode=1;mode<=4;mode++){
        f=start(4096,1);f->mode=mode;f->fail_read=2;memset(out,0xa5,sizeof(out));unsigned before=f->services;
        int rc=ntasi_ans_block_read(&f->b,11,5,out,sizeof(out));int want=mode==1?-8:mode==2?-7:mode==3?-9:-6;
        need(rc==want && f->reads==2,"fault stops exact command boundary");
        for(size_t k=0;k<8192;k++)need(out[k]==pattern(11+k/4096,k%4096),"completed prefix retained");
        for(size_t k=8192;k<sizeof(out);k++)need(out[k]==0xa5,"failed chunk and tail untouched");
        need(mode!=4 || f->services-before<=24,"unchanged bounded polling");printf("{\"case\":\"failure\",\"mode\":%u,\"status\":%d,\"commands\":%u}\n",mode,rc,f->reads);free(f);
    }
#ifndef BASELINE
    f=start(4096,0);unsigned n=f->reads;struct ntasi_ans_block_device saved=f->b;
    f->b.identified=false;need(ntasi_ans_block_set_read_batch(&f->b,8192)<0,"enable before identify refuses");f->b=saved;
    need(ntasi_ans_block_set_read_batch(&f->b,0)<0 && ntasi_ans_block_set_read_batch(&f->b,12288)<0,"unsupported batch sizes refuse");
    f->b.dma_size=4096;need(ntasi_ans_block_set_read_batch(&f->b,8192)<0,"small bounce refuses");f->b=saved;
    f->b.dma_address=UINT64_MAX-4095;need(ntasi_ans_block_set_read_batch(&f->b,8192)<0,"DMA overflow refuses");f->b=saved;
    f->b.dma_address++;need(ntasi_ans_block_set_read_batch(&f->b,8192)<0,"unaligned DMA refuses");f->b=saved;
    f->b.media.block_size=8192;need(ntasi_ans_block_set_read_batch(&f->b,8192)<0,"unsupported media refuses");f->b=saved;
    need(ntasi_ans_block_read(&f->b,NS_BLOCKS-1,2,out,sizeof(out))<0,"range refuses");
    need(ntasi_ans_block_read(&f->b,0,2,out,4096)<0,"short caller buffer refuses");
    need(ntasi_ans_block_read(&f->b,0,1,NULL,4096)<0,"NULL caller refuses");
    need(ntasi_ans_block_read(&f->b,0,UINT64_MAX,out,sizeof(out))<0,"size overflow refuses");
    need(ntasi_ans_block_read(&f->b,0,0,NULL,0)==0,"zero request no-op");need(f->reads==n,"invalid requests never submit");
    need(ntasi_ans_block_set_read_batch(&f->b,8192)==0 && ntasi_ans_block_identify(&f->b,1)==0,"reidentify success");
    need(ntasi_ans_block_read(&f->b,0,2,out,sizeof(out))==0 && f->reads==n+2,"reidentify revokes old batching admission");finish(f);
#endif
    printf("{\"verdict\":\"PASS\",\"assertions\":%u}\n",checks);return 0;
}
