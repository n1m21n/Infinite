/* SPDX-License-Identifier: GPL-3.0-only */
/* Same upstream sample command IDs/wire format, separately bounded storage. */
static unsigned sloop_sample_finish(unsigned k,const uint8_t *a,unsigned n){
    smp_user_hdr_t *h=(void *)ed_smp_buf;
    if(ed_unpack7(a,n,ed_smp_buf,sizeof *h)!=sizeof *h)return 1;
    if(h->magic!=SMP_USER_MAGIC || h->version!=1 || !h->nz || h->nz>16 ||
       !h->data_len || h->data_len>SMP_USER_SIZE-SMP_USER_DATA)return 2;
    for(unsigned j=0;j<h->nz;j++)if(!smp_zone_ok(&h->zone[j],h->data_len))return 2;
    if(usr_nz[k])return memcmp(h,smp_user_xip(k),sizeof *h)?2:0;
    if(!sloop_upload[k].active || sloop_upload[k].next!=SMP_USER_DATA+h->data_len)return 6;
    if(st_crc32(smp_user_xip(k)+SMP_USER_DATA,h->data_len)!=h->crc)return 3;
    uint32_t a0=smk37_sample_address(k);uint8_t check[256];
    /* Publish magic last; never present an incomplete upload to the engine. */
    if(sloop_sample_program(a0+256,ed_smp_buf+256,sizeof *h-256) ||
       sloop_storage_read_raw(a0+256,check,sizeof *h-256) || memcmp(check,ed_smp_buf+256,sizeof *h-256) ||
       sloop_sample_program(a0+4,ed_smp_buf+4,252) ||
       sloop_storage_read_raw(a0+4,check,252) || memcmp(check,ed_smp_buf+4,252) ||
       sloop_sample_program(a0,ed_smp_buf,4))return 4;
    smp_user_scan(k);sloop_upload[k].active=0;
    return usr_nz[k]?0:5;
}
static int sloop_ed_samples(unsigned cmd,const uint8_t *a,unsigned n){
    if(cmd<ED_SMP_BEGIN || cmd>ED_SMP_INFO)return 0;
    if(cmd==ED_SMP_INFO){
        unsigned slots=sloop_samples_owned?3:0;
        ed_b(slots);ed_b(slots?80:0);
        for(unsigned k=0;k<slots;k++){
            const smp_user_hdr_t *h=(const void *)smp_user_xip(k);char name[9]={0};
            for(unsigned j=0;usr_nz[k]&&j<8;j++)name[j]=h->name[j]>=32&&h->name[j]<127?h->name[j]:0;
            ed_b(usr_nz[k]);ed_str(name,8);ed_b(usr_nz[k]?(h->data_len+1023)/1024:0);
        }return 1;
    }
    unsigned k=n?a[0]:127,off=n>=4?(unsigned)a[1]|(unsigned)a[2]<<7|(unsigned)a[3]<<14:0;
    unsigned rc=1;
    if(k<3 && sloop_samples_owned && flash_ok){
        if(song.playing || transport_req)rc=7;
        else if((cmd==ED_SMP_BEGIN || cmd==ED_SMP_ERASE) && n==1){
            sloop_sample_unpublish(k);
            rc=smk37_sample_begin(&sloop_sample_io,sloop_upload,k,1);
            if(cmd==ED_SMP_ERASE)sloop_upload[k].active=0;
        }else if(cmd==ED_SMP_WRITE && n>=5){
            unsigned len=ed_unpack7(a+4,n-4,ed_smp_buf,256);
            rc=smk37_sample_write(&sloop_sample_io,sloop_upload,k,off,ed_smp_buf,len,1);
        }else if(cmd==ED_SMP_END && n>=2)rc=sloop_sample_finish(k,a+1,n-1);
    }
    ed_b(k);
    if(cmd==ED_SMP_WRITE){ed_b(off);ed_b(off>>7);ed_b(off>>14);}
    ed_b(rc);return 1;
}
