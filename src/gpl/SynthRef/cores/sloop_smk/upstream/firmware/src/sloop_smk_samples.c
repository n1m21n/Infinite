/* SPDX-License-Identifier: GPL-3.0-only */
/* Physical SMK slots. No formatting at boot; never use FM-1's addresses. */
static smk37_sample_upload_t sloop_upload[3];
static const uint8_t *sloop_sample_xip(unsigned k){
    return sloop_samples_owned && k<3 ? (const uint8_t *)(uintptr_t)FL_XIP(smk37_sample_address(k)) : empty_sample;
}
static int sloop_sample_erase(uint32_t a){
    if(!flash_ok || song.playing || transport_req || (a&4095u) ||
       !(sloop_samples_owned || sloop_samples_formatting) || !SMK37_SAMPLE_RANGE(a,4096))return -1;
    uint32_t took_us;
    sloop_storage_silence();return fl_erase4k(a,&took_us);
}
static int sloop_sample_program(uint32_t a,const void *p,uint32_t n){
    if(!flash_ok || song.playing || transport_req || !(sloop_samples_owned || sloop_samples_formatting) ||
       !(SMK37_SAMPLE_RANGE(a,n) || (sloop_samples_formatting && SMK37_SAMPLE_JOURNAL_RANGE(a,n))))return -1;
    return fl_write(a,p,n);
}
static const smk37_store_io_t sloop_sample_io={sloop_storage_read_raw,sloop_sample_erase,sloop_sample_program};
static void sloop_sample_unpublish(unsigned k){
    fm1_irq_off();usr_nz[k]=0;for(unsigned j=0;j<16;j++)usr_zone[k][j].n=0;++smp_user_gen;fm1_irq_on();
}
static int sloop_sample_initialize_request(void){
    if(!flash_ok || song.playing || transport_req)return -1;
    uint32_t crc[3];uint8_t block[256];
    for(unsigned k=0;k<3;k++){
        uint32_t c=~0u;
        for(unsigned p=0;p<SMK37_SAMPLE_SIZE;p+=256){
            if(sloop_storage_read_raw(smk37_sample_address(k)+p,block,256))return -1;
            c=smk37_profile_crc(block,256,c);
        }crc[k]=~c;
    }
    sloop_samples_formatting=1;
    int rc=smk37_sample_initialize(&sloop_sample_io,sloop_samples_unit,crc);
    sloop_samples_formatting=0;
    sloop_samples_owned=smk37_sample_owner_state(&sloop_sample_io,sloop_samples_unit)==2;
    for(unsigned k=0;k<4;k++)smp_user_scan(k);
    return rc;
}
