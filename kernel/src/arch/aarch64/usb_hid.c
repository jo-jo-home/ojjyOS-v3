/* Original bounded HID report decoder. Created by Jonas Lee. */
#include "usb_hid.h"
#include "virtio_devices.h"
#include "drivers/input.h"
#include "string.h"

/* USB keyboard usages -> the existing evdev decoder (ASCII/modifier and
 * repeat policy is shared with virtio-input, never duplicated here). */
static const uint8_t usage_key[128] = {
    [4]=30,[5]=48,[6]=46,[7]=32,[8]=18,[9]=33,[10]=34,[11]=35,[12]=23,
    [13]=36,[14]=37,[15]=38,[16]=50,[17]=49,[18]=24,[19]=25,[20]=16,
    [21]=19,[22]=31,[23]=20,[24]=22,[25]=47,[26]=17,[27]=45,[28]=21,[29]=44,
    [30]=2,[31]=3,[32]=4,[33]=5,[34]=6,[35]=7,[36]=8,[37]=9,[38]=10,[39]=11,
    [40]=28,[41]=1,[42]=14,[43]=15,[44]=57,[45]=12,[46]=13,[47]=26,[48]=27,
    [49]=43,[50]=43,[51]=39,[52]=40,[53]=41,[54]=51,[55]=52,[56]=53,[57]=58,
    [58]=59,[59]=60,[60]=61,[61]=62,[62]=63,[63]=64,[64]=65,[65]=66,
    [66]=67,[67]=68,[68]=87,[69]=88,[71]=70,[73]=110,[74]=102,[75]=104,
    [76]=111,[77]=107,[78]=109,[79]=106,[80]=105,[81]=108,[82]=103,
    [83]=69,[85]=55,[86]=74,[87]=78,[88]=96,
};
static const uint8_t modifier_key[8] = {29,42,56,125,97,54,100,126};

static bool contains(const uint8_t keys[8], uint8_t key)
{
    if (!key) return false;
    for (unsigned i=2; i<8; i++) if (keys[i]==key) return true;
    return false;
}
static void key_event(uint8_t usage, int value)
{
    if (usage < sizeof(usage_key) && usage_key[usage])
        virtio_input_handle_key(usage_key[usage], value);
}
static void keyboard_report(UsbHid *hid, const uint8_t *data, size_t len)
{
    if (len < 8) return;
    /* ErrorRollOver/POSTFail/ErrorUndefined reports are not key releases. */
    for (unsigned i=2; i<8; i++) if (data[i] && data[i]<=3) return;
    for (unsigned i=0; i<8; i++) {
        if ((data[0]^hid->previous_keys[0]) & (1u<<i))
            virtio_input_handle_key(modifier_key[i], (data[0]>>i)&1);
    }
    for (unsigned i=2; i<8; i++) {
        uint8_t key=hid->previous_keys[i];
        if (key && !contains(data,key)) key_event(key,0);
    }
    for (unsigned i=2; i<8; i++) {
        uint8_t key=data[i];
        bool duplicate=false;
        for (unsigned j=2; j<i; j++) if (data[j]==key) duplicate=true;
        if (key && !duplicate && !contains(hid->previous_keys,key)) key_event(key,1);
    }
    memcpy(hid->previous_keys,data,8);
}

typedef struct {
    uint16_t page;
    uint8_t id, size;
    uint16_t count;
    int32_t min, max;
} HidGlobals;

static int32_t signed_item(uint32_t v, unsigned bytes)
{
    if (bytes==1) return (int8_t)v;
    if (bytes==2) return (int16_t)v;
    return (int32_t)v;
}

bool usb_hid_parse_pointer(UsbHid *hid, const uint8_t *d, size_t len)
{
    memset(hid,0,sizeof(*hid));
    if (!d || !len || len>4096) return false;
    HidGlobals g={0}, stack[4];
    unsigned depth=0, nusage=0;
    uint32_t usages[32], usage_min=0, usage_max=0;
    bool range=false;
    uint16_t bits[256]={0};
    for (size_t at=0; at<len;) {
        uint8_t prefix=d[at++];
        if (prefix==0xFE) return false; /* long items not supported */
        unsigned bytes=prefix&3;
        if (bytes==3) bytes=4;
        if (bytes>len-at) return false;
        uint32_t value=0;
        for (unsigned j=0;j<bytes;j++) value|=(uint32_t)d[at++]<<(8*j);
        unsigned type=(prefix>>2)&3, tag=prefix>>4;
        if (type==1) {
            switch (tag) {
            case 0: if (value>65535) return false; g.page=(uint16_t)value; break;
            case 1: g.min=signed_item(value,bytes); break;
            case 2: g.max=g.min<0 ? signed_item(value,bytes) : (int32_t)value; break;
            case 7: if (value>32) return false; g.size=(uint8_t)value; break;
            case 8:
                if (!value || value>255) return false;
                g.id=(uint8_t)value; hid->has_ids=true;
                if (!bits[g.id]) bits[g.id]=8;
                break;
            case 9: if (value>256) return false; g.count=(uint16_t)value; break;
            case 10: if (depth==4) return false; stack[depth++]=g; break;
            case 11: if (!depth) return false; g=stack[--depth]; break;
            default: break;
            }
        } else if (type==2) {
            if (tag==0) {
                if (nusage==32) return false;
                usages[nusage++]=bytes==4 ? value : ((uint32_t)g.page<<16)|value;
            } else if (tag==1) { usage_min=value; range=true; }
            else if (tag==2) usage_max=value;
        } else if (type==0) {
            if (tag==8) { /* Input: constants still consume bits. */
                if (!g.size || (uint32_t)bits[g.id]+g.size*g.count>32768) return false;
                for (unsigned j=0;j<g.count;j++) {
                    uint32_t usage=0;
                    if (j<nusage) usage=usages[j];
                    else if (range && usage_max>=usage_min && j<=usage_max-usage_min)
                        usage=((uint32_t)g.page<<16)|(usage_min+j);
                    uint16_t page=(uint16_t)(usage>>16), u=(uint16_t)usage;
                    if (!(value&1) && (value&2) &&
                        ((page==1 && (u==0x30 || u==0x31 || u==0x38)) ||
                         (page==9 && u>=1 && u<=5))) {
                        if (hid->field_count==HID_MAX_FIELDS) return false;
                        hid->fields[hid->field_count++]=(HidField){
                            .bit=bits[g.id],.page=page,.usage=u,.size=g.size,
                            .report_id=g.id,.relative=(value&4)!=0,.min=g.min,.max=g.max};
                    }
                    bits[g.id]+=g.size;
                }
            }
            nusage=0; range=false; usage_min=usage_max=0;
        }
    }
    /* Coordinates must occur in the same report; reject relative mice. */
    for (unsigned i=0;i<hid->field_count;i++) {
        HidField *x=&hid->fields[i];
        if (x->page!=1 || x->usage!=0x30 || x->relative || x->max<=x->min) continue;
        for (unsigned j=0;j<hid->field_count;j++) {
            HidField *y=&hid->fields[j];
            if (y->page==1 && y->usage==0x31 && !y->relative && y->max>y->min &&
                x->report_id==y->report_id) return true;
        }
    }
    return false;
}

static int32_t field_value(const HidField *f, const uint8_t *data)
{
    uint32_t value=0;
    for (unsigned j=0;j<f->size;j++) {
        unsigned b=f->bit+j;
        value|=((data[b/8]>>(b%8))&1u)<<j;
    }
    if (f->min<0 && f->size<32 && (value&(1u<<(f->size-1))))
        value|=~((1u<<f->size)-1);
    return (int32_t)value;
}

void usb_hid_report(UsbHid *hid, const uint8_t *data, size_t len)
{
    if (!hid || !data || !len) return;
    if (hid->keyboard) { keyboard_report(hid,data,len); return; }
    uint8_t id=hid->has_ids ? data[0] : 0;
    /* Reject a truncated report atomically, not after posting its X axis. */
    for (unsigned i=0;i<hid->field_count;i++) {
        HidField *f=&hid->fields[i];
        if (f->report_id==id && (size_t)f->bit+f->size>len*8) return;
    }
    int32_t x=0,y=0,w=0,width,height;
    input_get_bounds(&width,&height);
    bool have_x=false,have_y=false;
    uint8_t buttons=hid->buttons;
    for (unsigned i=0;i<hid->field_count;i++) {
        HidField *f=&hid->fields[i];
        if (f->report_id!=id) continue;
        int32_t v=field_value(f,data);
        if (f->page==9) {
            unsigned mask=1u<<(f->usage-1);
            if (v) buttons|=mask; else buttons&=~mask;
        } else if (f->usage==0x38) w=v;
        else if (!f->relative && f->max>f->min) {
            int64_t clamped=MAX(f->min,MIN(v,f->max));
            int32_t bound=f->usage==0x30 ? width : height;
            int32_t position=bound>1 ? (int32_t)((clamped-f->min)*(bound-1)/((int64_t)f->max-f->min)) : 0;
            if (f->usage==0x30) { x=position; have_x=true; }
            else { y=position; have_y=true; }
        }
    }
    if (have_x && have_y) input_post_mouse_absolute(x,y);
    for (unsigned i=0;i<5;i++) if ((buttons^hid->buttons)&(1u<<i))
        input_post_mouse_button((buttons&(1u<<i)) ? INPUT_EVENT_MOUSE_BUTTON_DOWN :
            INPUT_EVENT_MOUSE_BUTTON_UP,(MouseButton)i);
    hid->buttons=buttons;
    if (w) input_post_mouse_scroll(0,w);
}

void usb_hid_tick(UsbHid *hid, uint64_t now)
{
    if (!hid->keyboard || now-hid->repeat_at<35) return;
    hid->repeat_at=now;
    for (unsigned i=2;i<8;i++) key_event(hid->previous_keys[i],2);
}
void usb_hid_release(UsbHid *hid)
{
    if (hid->keyboard) {
        const uint8_t empty[8]={0}; keyboard_report(hid,empty,sizeof(empty));
    }
    for (unsigned i=0;i<5;i++) if (hid->buttons&(1u<<i))
        input_post_mouse_button(INPUT_EVENT_MOUSE_BUTTON_UP,(MouseButton)i);
    hid->buttons=0;
}
