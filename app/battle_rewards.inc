// 既知の日本語版ROMに限定した補助機能。設定はステートと独立。
static int reward_factor(unsigned value)
{
    return value==1 || value==4 || value==8 || value==16 || value==64 || value==128;
}
int wiiz_core_max_skills(WiizCore* core,unsigned enabled)
{
    if(enabled>1 || (enabled && core->profile!=122 && core->profile!=123))return 0;
    core->max_skills=enabled;return 1;
}
int wiiz_core_free_jobs(WiizCore* core,unsigned enabled)
{
    if(enabled>1 || (enabled && core->profile!=123))return 0;
    core->free_jobs=enabled;return 1;
}
// 原作の表・ROM・キャパシティ残高は保ち、判定と消費の読取値だけを0にする。
static uint8_t ff3_job_read(Emulator* e,uint16_t addr)
{
    WiizCore* core=e->ram_write_user_data;
    const uint8_t value=core->prg_ram_read(e,addr);
    if(core->free_jobs && core->sound_role==1 && core->profile==123 &&
       e->s.m.prg8k_bank[1]==0x3d && e->s.c.X<23 && addr==0x7200+e->s.c.X &&
       ((e->s.c.PC.val==0xae56 && e->s.c.opcode==0xdd) ||
        (e->s.c.PC.val==0xab9b && e->s.c.opcode==0xfd)))return 0;
    return value;
}
int wiiz_core_rewards(WiizCore* core,unsigned experience,unsigned gold)
{
    if(!reward_factor(experience) || !reward_factor(gold))return 0;
    if(core->profile==122 && experience!=1)return 0;
    if(core->profile!=3 && core->profile!=4 && core->profile!=122 && core->profile!=123)
        return experience==1 && gold==1;
    core->reward_exp=experience;core->reward_gold=gold;return 1;
}
static unsigned reward_read(Emulator* e,unsigned addr,unsigned size)
{
    unsigned result=0;
    for(unsigned i=0;i<size;++i)
        result|=(unsigned)(addr<0x800?e->s.c.ram[addr+i]:e->s.c.prg_ram[addr+i-0x6000])<<(8*i);
    return result;
}
static void reward_write(Emulator* e,unsigned addr,unsigned size,unsigned value)
{
    for(unsigned i=0;i<size;++i)
    {
        if(addr<0x800)e->s.c.ram[addr+i]=(uint8_t)(value>>(8*i));
        else e->s.c.prg_ram[addr+i-0x6000]=(uint8_t)(value>>(8*i));
    }
}
static unsigned reward_scale(Emulator* e,unsigned addr,unsigned size,unsigned factor,unsigned limit)
{
    const unsigned original=reward_read(e,addr,size);
    const uint64_t product=(uint64_t)original*(factor?factor:1);
    const unsigned result=product>limit?limit:(unsigned)product;
    reward_write(e,addr,size,result);return result;
}
static int battle_rewards(WiizCore* core,unsigned addr,unsigned value)
{
    if(core->sound_role!=1 || (!core->max_skills && core->reward_exp<=1 && core->reward_gold<=1))return 0;
    Emulator* e=core->e;
    const unsigned pc=e->s.c.PC.val;
    const unsigned bank=pc>=0x8000?e->s.m.prg8k_bank[(pc-0x8000)/8192]:~0u;
    // 最後のキャラクターの成長処理が完了したINCの確定書込だけで反映する。
    if(core->max_skills && value==4 && core->profile==122 && bank/2==5 && pc==0xa7df && addr==0x9e)
    {
        for(unsigned character=0;character<4;++character)
        {
            const unsigned offset=character*0x40;
            for(unsigned weapon=0;weapon<8;++weapon)
                reward_write(e,0x6200+offset+weapon*2,2,15);
            for(unsigned spell=0;spell<16;++spell)
                if(reward_read(e,0x6130+offset+spell,1))
                    reward_write(e,0x6210+offset+spell*2,2,15);
        }
    }
    if(core->max_skills && value==4 && core->profile==123 && bank==0x35 && pc==0xbf72 && addr==0x52)
    {
        for(unsigned character=0;character<4;++character)
            reward_write(e,0x6110+character*0x40,2,98);
    }
    if(core->profile==122 && bank/2==5 && pc==0xa4ce && addr==0x62 && core->reward_gold>1)
    {
        const unsigned amount=reward_scale(e,0x7cc0,2,core->reward_gold,65535);
        e->s.c.A=(uint8_t)amount;e->s.c.ram[addr]=(uint8_t)amount;return 1;
    }
    if(core->profile==3 && bank/2==1)
    {
        // 生存者への分配後、メッセージと各人への加算に同じ値を渡す。
        if(pc==0xbd6b && addr==4 && core->reward_exp>1)
            reward_scale(e,0x5d,2,core->reward_exp,65535);
        if(pc==0xbe8c && addr==0x63f && core->reward_gold>1)
        {
            const unsigned amount=reward_scale(e,0x60,2,core->reward_gold,65535);
            e->s.c.A=(uint8_t)amount;e->s.c.ram[addr]=(uint8_t)amount;return 1;
        }
    }
    if(core->profile==4 && bank/2==18)
    {
        unsigned amount;
        if(pc==0x87ff && addr==0xfd && core->reward_exp>1)
            // 累積経験値の上限9,999,999との加算でも24ビットが循環しない範囲。
            amount=reward_scale(e,0x7203,3,core->reward_exp,0xffffff-9999999);
        else if(pc==0x8869 && addr==0x6f && core->reward_gold>1)
            amount=reward_scale(e,0x7201,2,core->reward_gold,65535);
        else return 0;
        e->s.c.A=(uint8_t)amount;e->s.c.ram[addr]=(uint8_t)amount;return 1;
    }
    if(core->profile==123 && bank==0x35 && pc==0xbcfc && addr==0x7570 && value==4)
    {
        // 原作の人数割り・端数処理を保ち、報酬表示の16ビット上限で飽和。
        if(core->reward_exp>1)reward_scale(e,0x7424,3,core->reward_exp,65535);
        if(core->reward_gold>1)reward_scale(e,0x7427,3,core->reward_gold,65535);
    }
    return 0;
}
