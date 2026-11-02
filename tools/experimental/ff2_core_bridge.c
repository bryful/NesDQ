#include "core_bridge.h"
#include "emulator.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ff2_harness.h"
#include "ff2_tape.h"
struct WiizCore { Emulator* e; unsigned buttons; float audio[4096]; size_t audio_count; unsigned sound_commands[3]; int sound_role; unsigned roster_editable,title_ready;
    unsigned queue[42], queue_count, queue_overflow, trace;
    unsigned input_speed,profile,music_steps,effect_steps; Ff2Tape* ff2;
    void (*audio_observer)(void*,unsigned,unsigned,unsigned,unsigned,unsigned); void* audio_observer_data; };
static void audio_probe_write(Emulator* e,uint16_t addr,uint8_t value,void* userdata)
{
    WiizCore* core=userdata;
    const unsigned pc=e->s.c.PC.val;
    const unsigned bank=pc>=0x8000?e->s.m.prg8k_bank[(pc-0x8000)/8192]:~0u;
    if(core->audio_observer)core->audio_observer(core->audio_observer_data,e->s.p.frame,bank,pc,addr,value);
}
void wiiz_core_audio_observer(WiizCore* core,void (*observer)(void*,unsigned,unsigned,unsigned,unsigned,unsigned),void* data)
{
    core->audio_observer=observer;core->audio_observer_data=data;
    core->e->apu_write_observer=observer?audio_probe_write:NULL;
    core->e->apu_write_user_data=core;
}
static int ff2_init(WiizCore* core)
{
    if(!core->ff2)
    {
        core->ff2=calloc(1,sizeof(Ff2Tape));
        if(!core->ff2)return 0;
        core->ff2->magic=FF2_TAPE_MAGIC;core->ff2->version=1;
    }
    return 1;
}
static bool ff_input_write(Emulator* e,uint16_t addr,uint8_t value,void* userdata)
{
    WiizCore* core=userdata;
    if(core->input_speed<=1)return true;
    const unsigned pc=e->s.c.PC.val;
    const unsigned bank=pc>=0x8000 && pc<0xc000?e->s.m.prg8k_bank[(pc-0x8000)/8192]/2:15;
    const int ff3=core->profile==123;
    if(addr==(ff3?0x14:0x36) && (pc==(ff3?0xfbd2:0xfc5c) || pc==(ff3?0xfbea:0xfc74)))
    {
        e->s.c.ram[addr]=(uint8_t)(value*core->input_speed);return false;
    }
    if(bank==(ff3?30:14))
    {
        if(addr==0xa1 && pc==(ff3?0x9184:0x96a2))core->sound_commands[0]=2*(core->input_speed-1);
        if(addr==0x47 && pc==(ff3?0x9198:0x96b6))
        {
            if(core->sound_commands[0])
            {
                --core->sound_commands[0];e->s.c.PC.val=ff3?0x91a0:0x96c2;return false;
            }
            core->sound_commands[0]=core->input_speed-1;
        }
    }
    return true;
}
void wiiz_core_input_profile(WiizCore* core,unsigned profile)
{
    core->profile=profile;core->sound_role=0;
    core->e->ram_write_filter=ff_input_write;core->e->ram_write_user_data=core;
    core->e->apu_write_observer=NULL;core->e->apu_write_user_data=NULL;
}
static void sound_timed_event(WiizCore* core,unsigned type,unsigned value,unsigned age)
{
    if(age>32767 || core->queue_count>=42){core->queue_overflow=1;return;}
    core->queue[core->queue_count++]=FF2_TIMED|(age<<16)|(type<<8)|value;
}
static void sound_event(WiizCore* core,unsigned type,unsigned value)
{
    if(core->profile==22 && core->ff2 && core->ff2->source_active && type!=13)
    {
        sound_timed_event(core,type,value,core->e->s.p.frame-core->ff2->source_start);return;
    }
    if(core->queue_count>=42){core->queue_overflow=1;return;}
    core->queue[core->queue_count++]=(type<<8)|value;
    if(core->trace)printf("SOUND type=%u value=%02x pc=%04x SP=%02x\n",type,value,core->e->s.c.PC.val,core->e->s.c.S);
}
static int ff2_mailbox(WiizCore* core)
{
    static const unsigned routines[]={0,FF2_REQUEST_ADDR,0xdb2e,0xdb45,FF2_BEEP_ADDR,0xd05f,FF2_BATTLE_ADDR,0xde67,
        FF2_MUTE_ADDR,FF2_WATER_ADDR,FF2_WIND_ADDR,FF2_STOP_ADDR,FF2_DOOR_ADDR};
    Ff2Tape* tape=core->ff2;
    unsigned events[42],count=0;
    const unsigned prefix=tape->playing?tape->prefix:0;
    if(prefix>core->queue_count)return 0;
    for(unsigned i=0;i<prefix;++i)events[count++]=core->queue[i];
    core->queue_count-=prefix;memmove(core->queue,core->queue+prefix,core->queue_count*sizeof(unsigned));tape->prefix=0;
    while(tape->playing && tape->head<tape->count)
    {
        const unsigned event=tape->events[tape->head],type=ff2_event_type(event),age=(event>>16)&32767;
        if(age>tape->age)break;
        if(type==14)
        {
            tape->playing=0;tape->head=tape->count=0;
            core->e->s.c.ram[0x4f8]=(uint8_t)tape->resume_music;
            core->e->s.c.ram[0x4f9]=(uint8_t)tape->resume_effect;
            break;
        }
        if(type==15)
        {
            core->e->s.c.ram[0x4f8]=(uint8_t)(event&15);
            core->e->s.c.ram[0x4f9]=(uint8_t)((event>>4)&15);
        }
        else
        {
            if(count==42)return 0;
            events[count++]=event&65535;
        }
        ++tape->head;
    }
    if(!tape->playing)
    {
        if(count+core->queue_count>42)return 0;
        for(unsigned i=0;i<core->queue_count;++i)events[count++]=core->queue[i];
        core->queue_count=0;
    }
    for(unsigned i=0;i<count;++i)
    {
        const unsigned event=events[i],type=ff2_event_type(event);
        if(!ff2_raw_type(type) && (type<1 || type>12))return 0;
        const unsigned target=ff2_raw_type(type)?0x200+(type-32)*4:routines[type];
        core->e->s.c.ram[0x501+i*3]=(uint8_t)target;
        core->e->s.c.ram[0x502+i*3]=(uint8_t)(target>>8);
        core->e->s.c.ram[0x503+i*3]=(uint8_t)event;
    }
    core->e->s.c.ram[0x500]=(uint8_t)(count*3);
    return 1;
}
static bool dq4_write(Emulator* e,uint16_t addr,uint8_t value,void* userdata)
{
    WiizCore* core=userdata;
    const unsigned pc=e->s.c.PC.val;
    if(core->sound_role!=1)return true;
    if(core->profile==22)
    {
        const unsigned bank=pc>=0x8000 && pc<0xc000?e->s.m.prg8k_bank[(pc-0x8000)/8192]/2:15;
        if(bank==13 && addr==0xc0 && pc==0x9804)
        {
            ++core->music_steps;
            if(core->trace>=3)printf("MUSIC frame=%u\n",e->s.p.frame);
        }
        if(bank==5 && addr==0xcc && pc==0xba73)++core->effect_steps;
        if(addr==0xe0 && bank!=13 && (value&0xc0))sound_event(core,1,value);
        if(!core->ff2->source_active)
        {
            if(addr==0xe5 && pc==0xdb44)sound_event(core,2,0);
            if(addr==0xe5 && pc==0xdb5b)sound_event(core,3,0);
            if(addr==0xe5 && pc==0xc937)sound_event(core,4,0);
            if(addr==0xe5 && pc==0xde6b)sound_event(core,7,0);
        }
        if(addr==0x6f60 && bank==5 && pc==0xba33)sound_event(core,6,value);
        if(core->input_speed>1 && addr==0x36 && (pc==0xfc5c || pc==0xfc74))
        {
            e->s.c.ram[addr]=(uint8_t)(value*core->input_speed);return false;
        }
        if(bank==14 && core->input_speed>1)
        {
            if(addr==0xa1 && pc==0x96a2)core->sound_commands[0]=2*(core->input_speed-1);
            if(addr==0x47 && pc==0x96b6)
            {
                if(core->sound_commands[0])
                {
                    --core->sound_commands[0];
                    // この回は待機カウンターを進めず、元ルーチンの「入力なし」へ戻す。
                    e->s.c.PC.val=0x96c2;return false;
                }
                core->sound_commands[0]=core->input_speed-1;
            }
        }
        return true;
    }
    if(core->profile==3)
    {
        const unsigned bank=pc>=0x8000 && pc<0xc000?e->s.m.prg8k_bank[(pc-0x8000)/8192]/2:15;
        if(core->input_speed>1 && (addr==0x18 || addr==0x19) && pc==0xcb57)
        {
            const unsigned previous=e->s.c.ram[addr];
            if((previous==0 && value==7) || (previous==1 && value==11))
            {
                // 共通メニューの描き直し待ち2フレームも元の実時間へ合わせる。
                e->s.c.ram[addr]=(uint8_t)(value*core->input_speed+2*(core->input_speed-1));return false;
            }
        }
        if(bank!=1)return true;
        if(addr==0x6f0 && pc==0x8675)
        {
            const unsigned sp=e->s.c.S;
            const unsigned ret=e->s.c.ram[0x100+((sp+2)&255)] | (e->s.c.ram[0x100+((sp+3)&255)]<<8);
            if(ret==0x85c3 || ret==0x85ae)sound_event(core,ret==0x85c3?1:2,e->s.c.ram[0x100+((sp+1)&255)]);
        }
        if(addr==0xfa && pc==0x854a)sound_event(core,3,0);
        if(addr==0xfa && pc==0x85a2)sound_event(core,5,0);
        return true;
    }
    // 元の長押し状態は保ち、共通メニューの待ち時間だけを実時間に合わせる。
    if(core->input_speed>1 && pc>=0x8000 && pc<0xc000 && e->s.m.prg8k_bank[(pc-0x8000)/8192]/2==18)
    {
        if(addr==0x3cd && pc==0xbceb)
        {
            e->s.c.ram[addr]=(uint8_t)(value+e->s.c.X*(core->input_speed-1));return false;
        }
        if(addr==0x3e2 && pc==0xbd06)
        {
            e->s.c.ram[addr]=(uint8_t)(value+15*(core->input_speed-1));return false;
        }
    }
    if(addr==0xbf && pc==0xf07f)
    {
        const unsigned sp=e->s.c.S;
        const unsigned ret=e->s.c.ram[0x100+((sp+2)&255)] | (e->s.c.ram[0x100+((sp+3)&255)]<<8);
        if(ret==0xef5b || ret==0xf05e)sound_event(core,ret==0xef5b?1:2,e->s.c.ram[0x100+((sp+1)&255)]);
    }
    if(addr==0xbb && pc==0xf0a8)sound_event(core,5,0);
    if(addr==0xbb && pc==0xf05b)sound_event(core,3,0);
    if(addr==0xb0 && pc==0xeedf)sound_event(core,4,value&15);
    return true;
}
static void input(SystemInput* out, void* userdata, bool strobe) {
    (void)strobe;
    const unsigned b=((WiizCore*)userdata)->buttons;
    memset(out, 0, sizeof(*out));
    out->port[0].type = out->port[1].type = CONTROLLER_JOYPAD;
    JoypadInput* pad = &out->port[0].joyp;
    pad->A = !!(b & PAD_A); pad->B = !!(b & PAD_B);
    pad->select = !!(b & PAD_SELECT); pad->start = !!(b & PAD_START);
    pad->up = !!(b & PAD_UP); pad->down = !!(b & PAD_DOWN);
    pad->left = !!(b & PAD_LEFT); pad->right = !!(b & PAD_RIGHT);
}
WiizCore* wiiz_core_new(uint8_t* rom, size_t size) {
    if(size<16 || memcmp(rom,"NES\x1a",4)!=0 || ((rom[7]&0x0c)!=8 && rom[8]>8))
    {
        fprintf(stderr,"Invalid or unsupported ROM header (PRG RAM capacity)\n");return NULL;
    }
    WiizCore* core = calloc(1, sizeof(*core));
    if (!core) return NULL;
    core->input_speed=1;
    EmulatorInit init = {0};
    init.rom.data = rom; init.rom.size = size;
    init.audio_frequency = 48000; init.audio_frames = 512;
    init.ram_init = RAM_INIT_FCEUX;
    core->e = emulator_new(&init);
    if (!core->e) { free(core); return NULL; }
    // iNES形式で未指定のバッテリーRAMは8 KiBとして扱う。
    if (core->e->ci.has_bat_ram && !core->e->ci.prgram_bytes) {
        core->e->ci.prgram_bytes = 8192;
        core->e->ci.prgram8k_banks = 1;
        core->e->ci.prgram512b_banks = 16;
    }
    emulator_set_joypad_callback(core->e, input, core);

    return core;
}
void wiiz_core_delete(WiizCore* core) {
    if (!core) return;
    // The upstream destructor releases PPU storage and Emulator, but not audio data.
    free(core->e->audio_buffer.data);
    emulator_delete(core->e);
    free(core->ff2);
    free(core);
}
void wiiz_core_input(WiizCore* core, unsigned buttons) { core->buttons = buttons;if(buttons)core->roster_editable=0; }
unsigned wiiz_core_roster_editable(WiizCore* core){return core->roster_editable;}
unsigned wiiz_core_title_ready(WiizCore* core) {return core->title_ready;}
void wiiz_core_reset(WiizCore* core) {
    core->title_ready=0;core->roster_editable=0;
    core->buttons = 0;core->queue_count=core->queue_overflow=0;
    if(core->ff2){memset(core->ff2,0,sizeof(Ff2Tape));core->ff2->magic=FF2_TAPE_MAGIC;core->ff2->version=1;}
    emulator_set_reset(core->e, true);
    emulator_set_reset(core->e, false);
}
int wiiz_core_frame(WiizCore* core) {
    Emulator* e = core->e;
    core->audio_count = 0;
    core->music_steps=core->effect_steps=0;
    if(core->queue_overflow)return 0;
    unsigned tape_advance=0;
    const unsigned mailbox=core->profile==3 || core->profile==22?0x500:0x600,data=core->profile==3 || core->profile==22?0x501:0x680;
    if(core->sound_role==2 && core->profile==22 && e->s.c.ram[mailbox]==0)
    {
        if(!ff2_mailbox(core))return 0;
        tape_advance=core->ff2->playing;
    }
    else if(core->sound_role==2 && core->profile!=22 && core->queue_count && e->s.c.ram[mailbox]==0)
    {
        static const unsigned dq4[]={0,0xef59,0xf05c,0xf055,0xeed3,0xf088};
        static const unsigned dq3[]={0,0x85c1,0x85ac,0x8542,0x7e3,0x8581};
        static const unsigned ff2[]={0,FF2_REQUEST_ADDR,0xdb2e,0xdb45,FF2_BEEP_ADDR,0xd05f,FF2_BATTLE_ADDR,0xde67,
            FF2_MUTE_ADDR,FF2_WATER_ADDR,FF2_WIND_ADDR,FF2_STOP_ADDR,FF2_DOOR_ADDR};
        const unsigned* routines=core->profile==22?ff2:core->profile==3?dq3:dq4;
        for(unsigned i=0;i<core->queue_count;++i)
        {
            const unsigned event=core->queue[i],target=routines[event>>8];
            e->s.c.ram[data+i*3]=(uint8_t)target;e->s.c.ram[data+1+i*3]=(uint8_t)(target>>8);e->s.c.ram[data+2+i*3]=(uint8_t)event;
        }
        e->s.c.ram[mailbox]=(uint8_t)(core->queue_count*3);core->queue_count=0;
    }
    Ticks until = emulator_get_ticks(e) + e->master_ticks_per_frame * 2;
    for (;;) {
        const EmulatorEvent event = emulator_run_until(e, until);
        if (event & EMULATOR_EVENT_AUDIO_BUFFER_FULL) {
            AudioBuffer* ab = emulator_get_audio_buffer(e);
            size_t count = ab->frames;
            if (core->audio_count + count > 4096) return 0;
            memcpy(core->audio + core->audio_count, ab->data, count * sizeof(float));
            core->audio_count += count;
        }
        if (event & EMULATOR_EVENT_INVALID_OPCODE) return 0;
        if (event & EMULATOR_EVENT_NEW_FRAME)
        {
            if(core->profile==22 && core->sound_role==1 && core->ff2->source_active)
            {
                const unsigned age=e->s.p.frame-1-core->ff2->source_start;
                if(core->music_steps>8 || core->effect_steps>8)return 0;
                sound_timed_event(core,15,core->music_steps|(core->effect_steps<<4),age);
                if(e->s.c.ram[0x6c]==0)
                {
                    sound_timed_event(core,14,0,age+1);core->ff2->source_active=0;
                }
            }
            if(tape_advance && core->ff2->playing && ++core->ff2->age>32767)return 0;
            return !core->queue_overflow;
        }
        if (event & EMULATOR_EVENT_UNTIL_TICKS) return 0;
    }
}
void wiiz_core_pixels(WiizCore* core, uint32_t* rgba) { emulator_convert_frame_buffer(core->e, rgba); }
const float* wiiz_core_audio(WiizCore* core, size_t* count) { *count = core->audio_count; return core->audio; }
size_t wiiz_core_sram_size(WiizCore* core) { return core->e->ci.has_bat_ram ? core->e->ci.prgram_bytes : 0; }
int wiiz_core_sram_read(WiizCore* core, uint8_t* bytes, size_t size) {
    FileData data = {bytes, size}; return emulator_write_prg_ram(core->e, &data) == OK;
}
int wiiz_core_sram_write(WiizCore* core, uint8_t* bytes, size_t size) {
    FileData data = {bytes, size}; return emulator_read_prg_ram(core->e, &data) == OK;
}
unsigned wiiz_core_mapper(WiizCore* core) { return core->e->ci.mapper; }
/* Existing upstream S serializer + non-S presentation/resampler adapter state.
   No native pointers are written to disk. Snapshot version is ABI-specific. */
typedef struct {
    uint32_t magic, version, state_bytes, audio_prefix_bytes, audio_capacity, audio_position;
    uint32_t buttons, sound_role, sound_commands[3];
} WiizSnapshot;
size_t wiiz_core_state_size(WiizCore* core) {
    return sizeof(WiizSnapshot)+sizeof(S)+sizeof(FrameBuffer)+offsetof(AudioBuffer,data)+
        (core->e->audio_buffer.end-core->e->audio_buffer.data)*sizeof(float)+(core->profile==22?sizeof(Ff2Tape):0);
}
int wiiz_core_state_write(WiizCore* core, uint8_t* bytes, size_t size) {
    if (size != wiiz_core_state_size(core) || core->queue_overflow || (core->profile==22 && !core->ff2)) return 0;
    AudioBuffer* ab=&core->e->audio_buffer;
    WiizSnapshot header={0};
    header.magic=0x31535a57;header.version=1;header.state_bytes=sizeof(S);
    header.audio_prefix_bytes=offsetof(AudioBuffer,data);header.audio_capacity=(uint32_t)(ab->end-ab->data);
    header.audio_position=(uint32_t)(ab->position-ab->data);header.buttons=core->buttons;header.sound_role=core->sound_role;
    memcpy(header.sound_commands,core->sound_commands,sizeof(header.sound_commands));
    // 既存の予約フィールドを使用し、旧ステートのサイズを維持する。
    header.sound_commands[2]=core->input_speed;
    header.sound_commands[1]=core->profile;
    memset(bytes,0,size);memcpy(bytes,&header,sizeof(header));bytes+=sizeof(header);
    FileData state;emulator_init_state_file_data(&state);
    const int ok=emulator_write_state(core->e,&state)==OK;
    if(ok)memcpy(bytes,state.data,state.size);free(state.data);if(!ok)return 0;
    bytes+=sizeof(S);memcpy(bytes,core->e->frame_buffer,sizeof(FrameBuffer));bytes+=sizeof(FrameBuffer);
    memcpy(bytes,ab,offsetof(AudioBuffer,data));bytes+=offsetof(AudioBuffer,data);
    memcpy(bytes,ab->data,header.audio_position*sizeof(float));
    if(core->profile==22)
    {
        Ff2Tape extra=*core->ff2;extra.pending_count=core->queue_count;
        memcpy(extra.pending,core->queue,core->queue_count*sizeof(unsigned));
        if(!ff2_tape_valid(&extra,core->sound_role))return 0;
        bytes+=header.audio_capacity*sizeof(float);memcpy(bytes,&extra,sizeof(extra));
    }
    return 1;
}
int wiiz_core_state_read(WiizCore* core, const uint8_t* bytes, size_t size) {
    if (size != wiiz_core_state_size(core)) return 0;
    WiizSnapshot header;memcpy(&header,bytes,sizeof(header));bytes+=sizeof(header);
    Ff2Tape extra;
    if((header.sound_commands[1]==22)!=(core->profile==22))return 0;
    if(core->profile==22)
    {
        if(!core->ff2 || header.sound_role!=(unsigned)core->sound_role)return 0;
        memcpy(&extra,bytes-sizeof(header)+size-sizeof(extra),sizeof(extra));
        if(!ff2_tape_valid(&extra,core->sound_role))return 0;
    }
    AudioBuffer* ab=&core->e->audio_buffer;
    if(header.magic!=0x31535a57 || header.version!=1 || header.state_bytes!=sizeof(S) ||
       header.audio_prefix_bytes!=offsetof(AudioBuffer,data) || header.audio_capacity!=(uint32_t)(ab->end-ab->data) ||
       header.audio_position>header.audio_capacity || header.sound_role>2 || header.buttons>255 || header.sound_commands[2]>6 || (header.sound_commands[1]!=0 && header.sound_commands[1]!=3 && header.sound_commands[1]!=4 && header.sound_commands[1]!=22 && header.sound_commands[1]!=122 && header.sound_commands[1]!=123))return 0;
    AudioBuffer prefix={0};
    memcpy(&prefix,bytes+sizeof(S)+sizeof(FrameBuffer),offsetof(AudioBuffer,data));
    if(prefix.frequency!=48000 || prefix.frames!=512 || prefix.bufferi<0 || prefix.bufferi>=128 ||
       prefix.freq_counter>=core->e->cpu_ticks_per_second)return 0;
    FileData state;emulator_init_state_file_data(&state);memcpy(state.data,bytes,sizeof(S));
    const int ok=emulator_read_state(core->e,&state)==OK;free(state.data);if(!ok)return 0;
    bytes+=sizeof(S);memcpy(core->e->frame_buffer,bytes,sizeof(FrameBuffer));bytes+=sizeof(FrameBuffer);
    memcpy(ab,bytes,offsetof(AudioBuffer,data));bytes+=offsetof(AudioBuffer,data);
    memcpy(ab->data,bytes,header.audio_capacity*sizeof(float));ab->position=ab->data+header.audio_position;
    core->buttons=header.buttons;core->audio_count=0;
    core->input_speed=header.sound_commands[2]?header.sound_commands[2]:1;
    if(header.sound_commands[1])core->profile=header.sound_commands[1];
    core->queue_count=core->queue_overflow=0;
    memcpy(core->sound_commands,header.sound_commands,sizeof(core->sound_commands));
    if(core->profile==22)
    {
        *core->ff2=extra;core->queue_count=extra.pending_count;
        memcpy(core->queue,extra.pending,core->queue_count*sizeof(unsigned));
    }

    return 1;
}

static void ff2_apu_write(Emulator* e,uint16_t addr,uint8_t value,void* userdata)
{
    WiizCore* core=userdata;
    const unsigned pc=e->s.c.PC.val;
    const unsigned bank=pc>=0x8000 && pc<0xc000?e->s.m.prg8k_bank[(pc-0x8000)/8192]/2:15;
    if(bank==13 || bank==5)return;
    if(bank==3 && e->s.c.ram[0x6c] && !core->ff2->source_active)
    {
        sound_event(core,13,0);core->ff2->source_active=1;core->ff2->source_start=e->s.p.frame;
    }
    if(core->ff2->source_active)
    {
        const unsigned type=32+(addr-0x4000);
        if(!ff2_raw_type(type)){core->queue_overflow=1;return;}
        sound_event(core,type,value);
    }
    else if(addr==0x400c && pc==0xc135)sound_event(core,9,e->s.c.ram[0xf0]);
    else if(addr==0x400c && pc==0xc14c)sound_event(core,10,e->s.c.ram[0xf0]);
    else if(addr==0x4000 && pc==0xd918)sound_event(core,8,1);
    else if(addr==0x4000 && pc==0xc4a5)sound_event(core,8,2);
    else if(addr==0x4015 && value==0)sound_event(core,8,2);
    else if(addr==0x400c && pc==0xd064){sound_event(core,11,0);sound_event(core,5,0);}
    else if(addr==0x400c && pc==0xc8d5)sound_event(core,12,0);
    else if(addr==0x400c && (value==0 || value==0x30))sound_event(core,11,1);
    if(core->trace<2)return;
    const unsigned sp=e->s.c.S;
    const unsigned ret=e->s.c.ram[0x100+((sp+1)&255)] | (e->s.c.ram[0x100+((sp+2)&255)]<<8);
    printf("APU frame=%u bank=%u pc=%04x addr=%04x value=%02x return=%04x\n",e->s.p.frame,bank,pc,addr,value,ret);
}
void wiiz_core_profile_source(WiizCore* core,unsigned profile,int trace)
{
    core->profile=profile;core->sound_role=1;core->trace=trace;
    if(profile==22 && !ff2_init(core)){core->queue_overflow=1;return;}
    core->e->ram_write_filter=dq4_write;core->e->ram_write_user_data=core;
    core->e->apu_write_observer=profile==22?ff2_apu_write:NULL;
    core->e->apu_write_user_data=core;
}
void wiiz_core_dq4_source(WiizCore* core,int trace){wiiz_core_profile_source(core,4,trace);}
unsigned wiiz_core_take_sound(WiizCore* core)
{
    if(!core->queue_count)return 0;
    const unsigned result=core->queue[0];--core->queue_count;
    memmove(core->queue,core->queue+1,core->queue_count*sizeof(unsigned));return result;
}
int wiiz_core_sound_command(WiizCore* core,unsigned event)
{
    if(core->profile==22 && core->sound_role==2)
    {
        Ff2Tape* tape=core->ff2;
        const unsigned type=ff2_event_type(event);
        if(event&FF2_TIMED)
        {
            if(!tape->playing || !ff2_timed_valid(event) || (type==15 && ((event&15)>8 || ((event>>4)&15)>8)))return 0;
            if(tape->count && ((tape->events[tape->count-1]>>16)&32767)>((event>>16)&32767))return 0;
            if(tape->count && ff2_event_type(tape->events[tape->count-1])==14)return 0;
            if(tape->count==FF2_TAPE_CAPACITY && tape->head)
            {
                tape->count-=tape->head;memmove(tape->events,tape->events+tape->head,tape->count*sizeof(uint32_t));tape->head=0;
            }
            if(tape->count==FF2_TAPE_CAPACITY)return 0;
            tape->events[tape->count++]=event;return 1;
        }
        if(type==13 && (event&255)==0)
        {
            tape->playing=1;tape->age=tape->head=tape->count=0;tape->prefix=core->queue_count;
            tape->resume_music=core->e->s.c.ram[0x4f8];tape->resume_effect=core->e->s.c.ram[0x4f9];
            core->e->s.c.ram[0x4fc]=0;return 1;
        }
        if(type>=2 && type<=12 && type!=8 && type!=11)
            tape->playing=tape->head=tape->count=tape->prefix=0;
        if(tape->playing && (type==8 || type==11) && core->queue_count && core->queue[core->queue_count-1]==event)return 1;
    }
    if((event>>8)<1 || (event>>8)>(core->profile==22?12u:5u) || core->queue_count>=42)return 0;
    core->queue[core->queue_count++]=event;return 1;
}
void wiiz_core_input_speed(WiizCore* core,unsigned buttons,unsigned speed)
{
    if(speed<1 || speed>6)speed=1;
    if(core->sound_role==1 && core->profile!=22 && core->input_speed!=speed)
    {
        // 切替時に残っている待ち時間も、元の実時間を保って換算する。
        const unsigned previous=core->input_speed?core->input_speed:1;
        const uint8_t now=core->e->s.c.ram[0x50c];
        const unsigned timers[]={core->profile==3?0x18u:0x3cdu,core->profile==3?0x19u:0x3e2u};
        for(unsigned i=0;i<2;++i)
        {
            const unsigned addr=timers[i];
            const unsigned remaining=core->profile==3?core->e->s.c.ram[addr]:(uint8_t)(core->e->s.c.ram[addr]-now);
            if(remaining && remaining<128)
            {
                unsigned adjusted=(remaining*speed+previous-1)/previous;
                if(adjusted>127)adjusted=127;
                core->e->s.c.ram[addr]=(uint8_t)((core->profile==3?0:now)+adjusted);
            }
        }
    }
    if((core->profile==22 || core->profile==122 || core->profile==123) && core->input_speed!=speed)
    {
        const unsigned previous=core->input_speed?core->input_speed:1;
        const unsigned timer=core->profile==123?0x14:0x36;
        core->e->s.c.ram[timer]=(uint8_t)((core->e->s.c.ram[timer]*speed+previous-1)/previous);
        core->sound_commands[0]=(core->sound_commands[0]*speed+previous-1)/previous;
    }
    core->buttons=buttons;core->input_speed=speed;
}
unsigned wiiz_core_peek(WiizCore* core,unsigned addr)
{
    if(addr<0x800)return core->e->s.c.ram[addr];
    if(addr>=0x6000 && addr<0x8000)return core->e->s.c.prg_ram[addr-0x6000];
    return 0;
}

int wiiz_core_dq4_player(WiizCore* core)
{
    core->profile=4;
    static const uint8_t boot[]={0x78,0xd8,0xa2,0xff,0x9a,0xa9,0x0,0x8d,0x0,0x20,0x8d,0x1,0x20,0xa2,0x0,0x95,0x0,0x9d,0x0,0x1,0x9d,0x0,0x2,0x9d,0x0,0x3,0x9d,0x0,0x4,0x9d,0x0,0x5,0x9d,0x0,0x6,0xe8,0xd0,0xe9,0xa9,0xe,0x20,0x18,0xc1,0xa9,0x10,0x20,0x2f,0xc1,0xa9,0x19,0x8d,0x86,0x6e,0x20,0x91,0xff,0x20,0x88,0xf0,0xa9,0x80,0x8d,0x0,0x20,0x4c,0x40,0x7};
    memcpy(core->e->s.c.ram+0x700,boot,sizeof(boot));
    static const uint8_t nmi[]={0x48,0x8a,0x48,0x98,0x48,0xa2,0x0,0x8e,0x1f,0x6,0xec,0x0,0x6,0xb0,0x1e,0xbd,0x80,0x6,0x8d,0xe1,0x7,0xbd,0x81,0x6,0x8d,0xe2,0x7,0xbd,0x82,0x6,0x20,0xe0,0x7,0xae,0x1f,0x6,0xe8,0xe8,0xe8,0x8e,0x1f,0x6,0x4c,0x8a,0x7,0xa9,0x0,0x8d,0x0,0x6,0x20,0x85,0xe6,0x68,0xa8,0x68,0xaa,0x68,0x40};
    memcpy(core->e->s.c.ram+0x780,nmi,sizeof(nmi));
    core->sound_role=2;
    core->e->s.c.ram[0x7e0]=0x4c;core->e->s.c.ram[0x7f0]=0x40;
    return 1;
}

unsigned wiiz_core_sound_pending(WiizCore* core){return core->queue_count;}
unsigned wiiz_core_sound_at(WiizCore* core,unsigned index){return index<core->queue_count?core->queue[index]:0;}

int wiiz_core_profile_player(WiizCore* core,unsigned profile)
{
    if(profile==4)return wiiz_core_dq4_player(core);
    if(profile==22)
    {
        core->profile=22;core->sound_role=2;
        if(!ff2_init(core))return 0;
        memcpy(core->e->s.c.ram+0x700,ff2_boot,sizeof(ff2_boot));
        memcpy(core->e->s.c.ram+0x780,ff2_loop,sizeof(ff2_loop));
        memcpy(core->e->s.c.ram+0x7f8,ff2_nmi,sizeof(ff2_nmi));
        memcpy(core->e->s.c.ram+0x600,ff2_wrappers,sizeof(ff2_wrappers));
        for(unsigned reg=0;reg<=0x15;++reg)
        {
            const unsigned address=0x200+reg*4;
            core->e->s.c.ram[address]=0x8d;core->e->s.c.ram[address+1]=(uint8_t)reg;
            core->e->s.c.ram[address+2]=0x40;core->e->s.c.ram[address+3]=0x60;
        }
        core->e->s.c.ram[0x7f2]=0x4c;core->e->s.c.ram[0x7fb]=0x40;
        return 1;
    }
    if(profile!=3)return 0;
    core->profile=3;core->sound_role=2;
    static const uint8_t dq3_boot[]={0x78,0xd8,0xa2,0xff,0x9a,0xa9,0x0,0x8d,0x0,0x20,0x8d,0x1,0x20,0xa2,0x0,0x95,0x0,0x9d,0x0,0x1,0x9d,0x0,0x2,0x9d,0x0,0x3,0x9d,0x0,0x4,0x9d,0x0,0x5,0x9d,0x0,0x6,0xe8,0xd0,0xe9,0xa9,0x80,0x8d,0x0,0x80,0xa9,0x1,0x20,0xbd,0xff,0xa9,0xff,0x8d,0xf0,0x6,0x20,0x81,0x85,0xa9,0x80,0x8d,0x0,0x20,0x4c,0x3d,0x7};
    memcpy(core->e->s.c.ram+0x700,dq3_boot,sizeof(dq3_boot));
    static const uint8_t dq3_nmi[]={0x48,0x8a,0x48,0x98,0x48,0xa2,0x0,0x8e,0x7f,0x5,0xec,0x0,0x5,0xb0,0x1e,0xbd,0x1,0x5,0x8d,0xe1,0x7,0xbd,0x2,0x5,0x8d,0xe2,0x7,0xbd,0x3,0x5,0x20,0xe0,0x7,0xae,0x7f,0x5,0xe8,0xe8,0xe8,0x8e,0x7f,0x5,0x4c,0x8a,0x7,0xa9,0x0,0x8d,0x0,0x5,0x20,0x0,0x80,0x68,0xa8,0x68,0xaa,0x68,0x40};
    memcpy(core->e->s.c.ram+0x780,dq3_nmi,sizeof(dq3_nmi));
    core->e->s.c.ram[0x7e0]=0x4c;core->e->s.c.ram[0x7e3]=0x60;core->e->s.c.ram[0x7f0]=0x40;
    return 1;
}
unsigned wiiz_core_current_music(WiizCore* core)
{
    if(core->profile==22)return core->e->s.c.prg_ram[0xf25];
    return core->e->s.c.ram[core->profile==3?0x6f0:0xbf]>>1;
}
unsigned wiiz_core_music_steps(WiizCore* core){return core->music_steps;}
unsigned wiiz_core_effect_steps(WiizCore* core){return core->effect_steps;}
int wiiz_core_sound_clock(WiizCore* core,unsigned music_steps,unsigned effect_steps)
{
    if(core->profile!=22 || core->sound_role!=2 || music_steps>8 || effect_steps>8)return 0;
    core->ff2->resume_music=music_steps;core->ff2->resume_effect=effect_steps;
    if(!core->ff2->playing)
    {
        core->e->s.c.ram[0x4f8]=(uint8_t)music_steps;
        core->e->s.c.ram[0x4f9]=(uint8_t)effect_steps;
    }
    return 1;
}
