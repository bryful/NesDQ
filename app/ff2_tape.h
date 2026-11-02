#pragma once
#include <stdint.h>

enum { FF2_TAPE_CAPACITY=8192, FF2_TAPE_MAGIC=0x32544646 };
#define FF2_TIMED UINT32_C(0x80000000)
typedef struct
{
    uint32_t magic,version,source_active,source_start;
    uint32_t playing,age,head,count,prefix,resume_music,resume_effect;
    uint32_t pending_count,pending[42];
    uint32_t events[FF2_TAPE_CAPACITY];
} Ff2Tape;

static unsigned ff2_event_type(uint32_t event){return (event>>8)&255;}
static int ff2_raw_type(unsigned type){return (type>=32 && type<=47) || type==53;}
static int ff2_timed_valid(uint32_t event)
{
    const unsigned type=ff2_event_type(event);
    return (event&FF2_TIMED) && (type==1 || type==6 || type==14 || type==15 || ff2_raw_type(type));
}
static int ff2_tape_valid(const Ff2Tape* tape,unsigned role)
{
    if(tape->magic!=FF2_TAPE_MAGIC || tape->version!=1 || tape->source_active>1 || tape->playing>1 ||
       tape->age>32767 || tape->head>tape->count || tape->count>FF2_TAPE_CAPACITY || tape->pending_count>42 ||
       tape->prefix>tape->pending_count || tape->resume_music>8 || tape->resume_effect>8 ||
       (role==1 && tape->playing) || (role==2 && tape->source_active))return 0;
    unsigned previous=0;
    for(unsigned i=tape->head;i<tape->count;++i)
    {
        const uint32_t event=tape->events[i];
        const unsigned type=ff2_event_type(event),age=(event>>16)&32767;
        if(!ff2_timed_valid(event) || (i>tape->head && age<previous) || (type==14 && i+1!=tape->count) ||
           (type==15 && ((event&15)>8 || ((event>>4)&15)>8)))return 0;
        previous=age;
    }
    for(unsigned i=0;i<tape->pending_count;++i)
    {
        const uint32_t event=tape->pending[i];
        const unsigned type=ff2_event_type(event);
        if(role==1 && (event&FF2_TIMED))
        {
            if(!ff2_timed_valid(event) || (type==15 && ((event&15)>8 || ((event>>4)&15)>8)))return 0;
        }
        else if((event>>8)<1 || ((event>>8)>12 && !(role==1 && event==(13u<<8))))return 0;
    }
    return 1;
}
