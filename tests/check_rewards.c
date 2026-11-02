#include "../app/core_bridge.c"

static void require(int ok,const char* message)
{
    if(!ok){fprintf(stderr,"FAIL: %s\n",message);exit(1);}
}
static void check(const uint8_t* source,size_t size,unsigned profile,unsigned factor,int gold,unsigned amount)
{
    uint8_t* rom=malloc(size);memcpy(rom,source,size);
    unsigned bank,start,end,addr,out,width;
    const uint8_t dq3map[]={0xa9,1,0x20,0xbd,0xff};
    const uint8_t ff2map[]={0xa9,5,0x20,3,0xfe};
    const uint8_t dq4map[]={0xa9,0x0e,0x20,0x18,0xc1,0xa9,0x10,0x20,0x2f,0xc1,0xa9,0x12,0x20,0x91,0xff};
    const uint8_t ff3map[]={0xa9,7,0x8d,0,0x80,0xa9,0x35,0x8d,1,0x80};
    const uint8_t* map;size_t map_size;
    if(profile==122)
    {
        bank=5;start=0xa4c8;end=0xa4e7;addr=0x7cc0;out=0x62;width=2;map=ff2map;map_size=sizeof(ff2map);
    }
    else if(profile==3)
    {
        bank=1;start=gold?0xbe87:0xbd67;end=gold?0xbea0:0xbd79;
        addr=gold?0x60:0x5d;out=gold?0xce:0x63f;width=2;map=dq3map;map_size=sizeof(dq3map);
    }
    else if(profile==4)
    {
        bank=18;start=gold?0x8864:0x87fa;end=gold?0x8877:0x8811;
        addr=gold?0x7201:0x7203;out=gold?0x6f:0xfd;width=gold?2:3;map=dq4map;map_size=sizeof(dq4map);
    }
    else
    {
        bank=0x35;start=0xbcf9;end=0xbd22;addr=gold?0x7427:0x7424;out=addr;width=3;map=ff3map;map_size=sizeof(ff3map);
    }
    const size_t offset=16+(profile==123?bank*8192+(end&8191):bank*16384+(end&16383));
    require(offset+3<size,"ROM range");rom[offset]=0x4c;rom[offset+1]=0xe0;rom[offset+2]=7;
    if(profile==3 && gold)
    {
        const size_t zero_exit=16+bank*16384+(0xbeea&16383);
        rom[zero_exit]=0x4c;rom[zero_exit+1]=0xe0;rom[zero_exit+2]=7;
    }
    if(profile==4)
    {
        const size_t zero_exit=16+bank*16384+((gold?0x8889:0x8848)&16383);
        rom[zero_exit]=0x4c;rom[zero_exit+1]=0xe0;rom[zero_exit+2]=7;
    }
    rom[size-4]=0;rom[size-3]=7;rom[size-6]=0xf0;rom[size-5]=7;
    WiizCore* core=wiiz_core_new(rom,size);require(core!=NULL,"create");
    wiiz_core_profile_source(core,profile,0);require(wiiz_core_rewards(core,gold?1:factor,gold?factor:1),"set factors");
    require(!wiiz_core_rewards(core,2,1) && !wiiz_core_rewards(core,1,32),"reject unsupported factors");
    Emulator* e=core->e;
    uint8_t* boot=e->s.c.ram+0x700;
    const uint8_t prefix[]={0x78,0xd8,0xa2,0xff,0x9a};memcpy(boot,prefix,sizeof(prefix));
    memcpy(boot+sizeof(prefix),map,map_size);size_t n=sizeof(prefix)+map_size;
    boot[n++]=0x4c;boot[n++]=0xc0;boot[n++]=7;
    const uint8_t wait[]={0xad,0xee,7,0xf0,0xfb,0x4c,(uint8_t)start,(uint8_t)(start>>8)};
    memcpy(e->s.c.ram+0x7c0,wait,sizeof(wait));
    const uint8_t done[]={0xa9,1,0x8d,0xef,7,0x4c,0xe5,7};
    memcpy(e->s.c.ram+0x7e0,done,sizeof(done));e->s.c.ram[0x7f0]=0x40;
    e->s.c.ram[0x7ee]=e->s.c.ram[0x7ef]=0;
    reward_write(e,out,width,0);reward_write(e,addr,width,amount);
    if(profile==3 && !gold)e->s.c.ram[0x5f]=0;
    if(profile==123)reward_write(e,0x7570,1,3);
    if(profile>=122)reward_write(e,0x601c,3,123);
    if(profile==122)require(!wiiz_core_rewards(core,4,1),"FF2 excludes experience");
    require(wiiz_core_frame(core),"boot frame");
    require(e->s.m.prg8k_bank[(start-0x8000)/8192]==(profile==123?bank:bank*2+(start-0x8000)/8192),"mapped bank");
    size_t state_size=wiiz_core_state_size(core);uint8_t* state=malloc(state_size);
    require(wiiz_core_state_write(core,state,state_size),"save before reward");
    for(unsigned repeat=0;repeat<2;++repeat)
    {
        if(repeat)require(wiiz_core_state_read(core,state,state_size),"restore before reward");
        e->s.c.ram[0x7ee]=1;require(wiiz_core_frame(core),"reward frame");
        require(e->s.c.ram[0x7ef]==1,"original reward instructions completed");
        unsigned limit=profile==4 && !gold?0xffffff-9999999:65535;
        unsigned expected=amount*factor;if(expected>limit)expected=limit;
        unsigned actual=reward_read(e,out,width);
        if(actual!=expected)fprintf(stderr,"profile=%u factor=%u gold=%d amount=%u got=%u expected=%u\n",profile,factor,gold,amount,actual,expected);
        require(actual==expected,"scaled reward");
        if(profile>=122 && gold)require(reward_read(e,0x601c,3)==123+expected,"gil added");
        require(wiiz_core_frame(core),"idle after award");require(reward_read(e,out,width)==expected,"no repeated award");
    }
    free(state);wiiz_core_delete(core);free(rom);
}
static void checkMaxSkills(const uint8_t* source,size_t size,unsigned profile,unsigned enabled)
{
    uint8_t* rom=malloc(size);memcpy(rom,source,size);
    const int ff2=profile==122;
    const unsigned start=ff2?0xa7dd:0xbf70,end=ff2?0xa7e8:0xbf7b;
    const size_t offset=16+(ff2?5*16384+(end&16383):0x35*8192+(end&8191));
    rom[offset]=0x4c;rom[offset+1]=0xe0;rom[offset+2]=7;
    rom[size-4]=0;rom[size-3]=7;
    WiizCore* core=wiiz_core_new(rom,size);require(core!=NULL,"skill core");
    wiiz_core_profile_source(core,profile,0);
    require(wiiz_core_max_skills(core,enabled),"skill setting");
    require(!wiiz_core_max_skills(core,2),"reject invalid skill setting");
    Emulator* e=core->e;
    for(unsigned i=0;i<8192;++i)e->s.c.prg_ram[i]=(uint8_t)((i*7+3)&255);
    if(ff2)for(unsigned c=0;c<4;++c)for(unsigned spell=0;spell<16;++spell)
        e->s.c.prg_ram[0x130+c*0x40+spell]=spell%2?(uint8_t)(0xc0+spell):0;
    uint8_t expected[8192];memcpy(expected,e->s.c.prg_ram,sizeof(expected));
    if(enabled)for(unsigned c=0;c<4;++c)
    {
        if(ff2)
        {
            for(unsigned w=0;w<8;++w){expected[0x200+c*0x40+w*2]=15;expected[0x201+c*0x40+w*2]=0;}
            for(unsigned spell=0;spell<16;++spell)if(spell%2)
            {expected[0x210+c*0x40+spell*2]=15;expected[0x211+c*0x40+spell*2]=0;}
        }
        else {expected[0x110+c*0x40]=98;expected[0x111+c*0x40]=0;}
    }
    const uint8_t ff2boot[]={0x78,0xd8,0xa2,0xff,0x9a,0xa9,5,0x20,3,0xfe,0x4c,0xc0,7};
    const uint8_t ff3boot[]={0x78,0xd8,0xa2,0xff,0x9a,0xa9,7,0x8d,0,0x80,0xa9,0x35,0x8d,1,0x80,0x4c,0xc0,7};
    memcpy(e->s.c.ram+0x700,ff2?ff2boot:ff3boot,ff2?sizeof(ff2boot):sizeof(ff3boot));
    const uint8_t wait[]={0xad,0xee,7,0xf0,0xfb,0x4c,(uint8_t)start,(uint8_t)(start>>8)};
    const uint8_t done[]={0xa9,1,0x8d,0xef,7,0x4c,0xe5,7};
    memcpy(e->s.c.ram+0x7c0,wait,sizeof(wait));memcpy(e->s.c.ram+0x7e0,done,sizeof(done));
    e->s.c.ram[0x7ee]=e->s.c.ram[0x7ef]=0;e->s.c.ram[ff2?0x9e:0x52]=3;
    require(wiiz_core_frame(core),"skill boot");
    uint8_t before[8192];memcpy(before,e->s.c.prg_ram,sizeof(before));
    const unsigned saved_pc=e->s.c.PC.val;
    e->s.c.PC.val=(uint16_t)(start+2);
    battle_rewards(core,ff2?0x9e:0x52,3);
    require(memcmp(before,e->s.c.prg_ram,sizeof(before))==0,"no skill change before last character");
    e->s.c.PC.val=(uint16_t)(start+3);
    battle_rewards(core,ff2?0x9e:0x52,4);
    require(memcmp(before,e->s.c.prg_ram,sizeof(before))==0,"no skill change outside victory instruction");
    e->s.c.PC.val=(uint16_t)saved_pc;
    size_t state_size=wiiz_core_state_size(core);uint8_t* state=malloc(state_size);
    require(wiiz_core_state_write(core,state,state_size),"skill save before");
    for(unsigned repeat=0;repeat<2;++repeat)
    {
        if(repeat)require(wiiz_core_state_read(core,state,state_size),"skill restore before");
        e->s.c.ram[0x7ee]=1;require(wiiz_core_frame(core),"skill victory");
        require(e->s.c.ram[0x7ef]==1,"skill victory completed");
        require(memcmp(expected,e->s.c.prg_ram,sizeof(expected))==0,"only selected skills changed");
    }
    require(wiiz_core_max_skills(core,0),"disable skill switch");
    require(wiiz_core_frame(core),"skill idle");
    require(memcmp(expected,e->s.c.prg_ram,sizeof(expected))==0,"disable preserves earned skills");
    free(state);wiiz_core_delete(core);free(rom);
}
static void checkFreeJobs(const uint8_t* source,size_t size,unsigned enabled,unsigned capacity,
                          unsigned level,int equipped,unsigned mode,uint8_t* result)
{
    uint8_t* rom=malloc(size);memcpy(rom,source,size);
    // 新ジョブの必要レベルを20に固定し、CP以外の制限も原作命令で検査する。
    rom[16+0x39*8192+8+1]=20;
    if(mode==1)
    {
        const size_t end=16+0x3d*8192+(0xab9e&8191);
        rom[end]=0x4c;rom[end+1]=0xe0;rom[end+2]=7;
    }
    rom[size-4]=0;rom[size-3]=7;
    WiizCore* core=wiiz_core_new(rom,size);require(core!=NULL,"job core");
    wiiz_core_profile_source(core,123,0);
    require(wiiz_core_free_jobs(core,enabled),"free job setting");
    require(!wiiz_core_free_jobs(core,2),"invalid free job setting");
    Emulator* e=core->e;memset(e->s.c.prg_ram,0,8192);
    reward_write(e,0x601b,1,capacity);reward_write(e,0x6101,1,level);
    reward_write(e,0x6200,1,equipped?1:0);reward_write(e,0x7201,1,40);
    e->s.c.ram[0x7f]=0;e->s.c.ram[0x8f]=1;
    const uint8_t boot[]={0x78,0xd8,0xa2,0xff,0x9a,0xa9,6,0x8d,0,0x80,0xa9,0x3c,0x8d,1,0x80,
        0xa9,7,0x8d,0,0x80,0xa9,0x3d,0x8d,1,0x80,0x4c,0xc0,7};
    const unsigned target=mode==0?0xae23:mode==1?0xab92:0xab8f;
    const uint8_t wait[]={0xad,0xee,7,0xf0,0xfb,0x20,(uint8_t)target,(uint8_t)(target>>8),
        0x8d,0xec,7,0xa9,0,0x69,0,0x8d,0xed,7,0x4c,0xe0,7};
    const uint8_t done[]={0xa9,1,0x8d,0xef,7,0x4c,0xe5,7};
    memcpy(e->s.c.ram+0x700,boot,sizeof(boot));memcpy(e->s.c.ram+0x7c0,wait,sizeof(wait));
    memcpy(e->s.c.ram+0x7e0,done,sizeof(done));
    e->s.c.ram[0x7ee]=e->s.c.ram[0x7ef]=0;
    require(wiiz_core_frame(core),"job boot");
    size_t state_size=wiiz_core_state_size(core);uint8_t* state=malloc(state_size);
    require(wiiz_core_state_write(core,state,state_size),"job save");
    for(unsigned repeat=0;repeat<2;++repeat)
    {
        if(repeat)require(wiiz_core_state_read(core,state,state_size),"job restore");
        e->s.c.ram[0x7ee]=1;require(wiiz_core_frame(core),"job execute");
        require(e->s.c.ram[0x7ef]==1,"job routine completed");
        require(reward_read(e,0x7201,1)==40,"cached CP cost preserved");
        if(mode==0)
        {
            const int allowed=level>=20 && !equipped && (enabled || capacity>=40);
            if(e->s.c.ram[0x7ed]!=(unsigned)allowed)fprintf(stderr,"job enabled=%u cp=%u level=%u equip=%d got=%u expected=%u\n",enabled,capacity,level,equipped,e->s.c.ram[0x7ed],allowed);
            require(e->s.c.ram[0x7ed]==(unsigned)allowed,"original eligibility with CP override");
            require(reward_read(e,0x601b,1)==capacity,"eligibility does not change capacity");
            if(!allowed)require(e->s.c.ram[0x7ec]==(level<20?0x3a:equipped?0x71:0x72),"original rejection reason");
        }
        else
        {
            require(reward_read(e,0x601b,1)==(enabled?capacity:capacity-40),"job capacity consumption");
            if(mode==2)require(reward_read(e,0x6100,1)==1,"actual job changed");
        }
    }
    // キャッシュを再計算せず、同じ保存状態でOn/Offを切り替える。
    if(mode==0 && level>=20 && !equipped && capacity==0)
    {
        require(wiiz_core_state_read(core,state,state_size),"job toggle restore");
        require(wiiz_core_free_jobs(core,!enabled),"job toggle");
        e->s.c.ram[0x7ee]=1;require(wiiz_core_frame(core),"job toggle execute");
        require(e->s.c.ram[0x7ed]==!enabled,"job toggle applies to cached cost immediately");
    }
    if(result)memcpy(result,e->s.c.prg_ram,8192);
    free(state);wiiz_core_delete(core);free(rom);
}
int main(int argc,char** argv)
{
    require(argc==5,"provide DQ3, DQ4, FF3, FF2 ROMs");
    const unsigned profiles[]={3,4,123,122},factors[]={1,4,8,16,64,128};
    for(unsigned game=0;game<4;++game)
    {
        FILE* f=fopen(argv[game+1],"rb");require(f!=NULL,"open ROM");fseek(f,0,SEEK_END);size_t size=ftell(f);rewind(f);
        uint8_t* rom=malloc(size);require(fread(rom,1,size,f)==size,"read ROM");fclose(f);
        if(game==0 && size==262288 && rom[6]==0x23)
        {
            size=262160;memset(rom+8,0,8);rom[8]=1;
        }
        for(unsigned i=0;i<6;++i)for(int gold=0;gold<2;++gold)
        {
            if(profiles[game]==122 && !gold)continue;
            check(rom,size,profiles[game],factors[i],gold,0);
            check(rom,size,profiles[game],factors[i],gold,23);
            if(factors[i]>1)check(rom,size,profiles[game],factors[i],gold,60000);
        }
        if(profiles[game]>=122)
        {checkMaxSkills(rom,size,profiles[game],0);checkMaxSkills(rom,size,profiles[game],1);}
        if(profiles[game]==123)
        {
            for(unsigned enabled=0;enabled<=1;++enabled)
            {
                checkFreeJobs(rom,size,enabled,0,20,0,0,NULL);
                checkFreeJobs(rom,size,enabled,40,20,0,0,NULL);
                checkFreeJobs(rom,size,enabled,0,19,0,0,NULL);
                checkFreeJobs(rom,size,enabled,0,20,1,0,NULL);
                checkFreeJobs(rom,size,enabled,40,20,0,1,NULL);
                checkFreeJobs(rom,size,enabled,255,20,0,1,NULL);
            }
            uint8_t normal[8192],free_job[8192];
            checkFreeJobs(rom,size,0,40,20,0,2,normal);
            checkFreeJobs(rom,size,1,0,20,0,2,free_job);
            require(memcmp(normal,free_job,sizeof(normal))==0,"free job result matches original funded change");
            puts("PASS free jobs: zero CP, no charge, original restrictions, toggle, restore, full job change");
        }
        free(rom);printf("profile=%u reward factors, max skills, saturation, restore passed\n",profiles[game]);
    }
    return 0;
}
