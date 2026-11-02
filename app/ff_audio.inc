// core_bridge.c内で使用する、既知FF2/FF3 ROM専用の接続。
static bool ff_audio_write(Emulator* e,uint16_t addr,uint8_t value,void* userdata)
{
    WiizCore* core=userdata;
    const unsigned pc=e->s.c.PC.val;
    const unsigned bank=pc>=0x8000?e->s.m.prg8k_bank[(pc-0x8000)/8192]:~0u;
    if(core->profile==123)
    {
        // ドライバー自身の更新は転送せず、ゲーム側の要求だけを取り出す。
        if(bank!=54 && bank!=55)
        {
            if(addr==0x7f40)sound_event(core,1,value);
            if(addr==0x7f42)sound_event(core,2,value);
            if(addr==0x7f43)sound_event(core,3,value);
            if(addr==0x7f49)sound_event(core,4,value);
        }
    }
    else
    {
        if(bank==26 && addr==0xc0 && pc==0x9804)++core->music_steps;
        if(bank==11 && addr==0xcc && pc==0xba73)++core->effect_steps;
        if(addr==0xe0 && bank!=26 && bank!=27 && (value&0xc0))sound_event(core,1,value);
        if(addr==0xe5 && pc==0xdb44)sound_event(core,2,0);
        if(addr==0xe5 && pc==0xdb5b)sound_event(core,3,0);
        if(addr==0xe5 && pc==0xc937)sound_event(core,4,0);
        if(addr==0xe5 && pc==0xde6b)sound_event(core,7,0);
        if(addr==0x6f60 && bank==11 && pc==0xba33)sound_event(core,6,value);
    }
    return ff_input_write(e,addr,value,userdata);
}
static void ff_audio_apu(Emulator* e,uint16_t addr,uint8_t value,void* userdata)
{
    WiizCore* core=userdata;
    const unsigned pc=e->s.c.PC.val;
    if(core->profile==123)
    {
        if(pc==0x801f && addr==0x4015 && e->s.m.prg8k_bank[0]==54)++core->music_steps;
        return;
    }
    const unsigned bank=pc>=0x8000?e->s.m.prg8k_bank[(pc-0x8000)/8192]/2:~0u;
    if(bank==13 || bank==5)return;
    if(addr==0x400c && pc==0xc135)sound_event(core,9,e->s.c.ram[0xf0]);
    else if(addr==0x400c && pc==0xc14c)sound_event(core,10,e->s.c.ram[0xf0]);
    else if(addr==0x4000 && pc==0xd918)sound_event(core,8,1);
    else if(addr==0x4000 && pc==0xc4a5)sound_event(core,8,2);
    else if(addr==0x4015 && value==0)sound_event(core,8,2);
    else if(addr==0x400c && pc==0xd064){sound_event(core,11,0);sound_event(core,5,0);}
    else if(addr==0x400c && pc==0xc8d5)sound_event(core,12,0);
    else if(addr==0x400c && (value==0 || value==0x30))sound_event(core,11,1);
}
static int ff_audio_player(WiizCore* core,unsigned profile)
{
    uint8_t* ram=core->e->s.c.ram;
    core->profile=profile;core->sound_role=2;
    if(profile==122)
    {
        memcpy(ram+0x700,ff2_boot,sizeof(ff2_boot));
        memcpy(ram+0x780,ff2_loop,sizeof(ff2_loop));
        memcpy(ram+0x600,ff2_wrappers,sizeof(ff2_wrappers));
        memcpy(ram+0x7f8,ff2_nmi,sizeof(ff2_nmi));
        ram[0x7f2]=0x4c;ram[0x7fb]=0x40;
    }
    else
    {
        memcpy(ram+0x700,ff3_boot,sizeof(ff3_boot));
        memcpy(ram+0x780,ff3_loop,sizeof(ff3_loop));
        memcpy(ram+0x600,ff3_wrappers,sizeof(ff3_wrappers));
        memcpy(ram+0x7f0,ff3_nmi,sizeof(ff3_nmi));
        ram[0x7e0]=0x4c;
    }
    return 1;
}
unsigned wiiz_core_music_steps(WiizCore* core){return core->music_steps;}
int wiiz_core_ff_clock(WiizCore* core,unsigned music_steps)
{
    if((core->profile!=122 && core->profile!=123) || core->sound_role!=2 || music_steps>8)return 0;
    core->e->s.c.ram[0x4f8]=(uint8_t)music_steps;
    // ゲーム側のSEが先に終了しても、等速側は独立して最後まで更新する。
    if(core->profile==122)core->e->s.c.ram[0x4f9]=1;
    return 1;
}
