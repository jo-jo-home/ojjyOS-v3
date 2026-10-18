/* Original, polled xHCI driver for directly connected USB HID devices.
 * One command/control TD in flight, one report per endpoint. DMA storage
 * lives for the boot, including failed slots: never free an owned buffer.
 * No hubs, streams, isochronous traffic or hot-plug enumeration yet.
 * Created by Jonas Lee. */
#include "xhci.h"
#include "pci.h"
#include "mmio.h"
#include "serial.h"
#include "memory.h"
#include "string.h"
#include "virtio.h"
#include "usb_hid.h"

#define RING_SIZE 256
#define MAX_USB_DEVICES 8
#define TRB_TYPE(n) ((uint32_t)(n)<<10)
#define IOC (1u<<5)
#define CHAIN (1u<<4)

typedef struct { uint32_t lo,hi,status,control; } Trb;
typedef struct { Trb *trbs; unsigned next; uint32_t cycle; } Ring;
typedef struct {
    unsigned port,slot,speed,dci,packet,interface,context_size;
    uint8_t *input,*output,*data,*report;
    Ring ep0,interrupt;
    UsbHid hid;
    bool active;
} UsbDevice;
typedef struct {
    uintptr_t cap,op,db,ir;
    unsigned max_slots,max_ports,context_size,event_next;
    uint32_t event_cycle;
    Ring commands;
    Trb *events;
    uint64_t *dcbaa,*erst;
    UsbDevice devices[MAX_USB_DEVICES];
    unsigned device_count;
    uintptr_t command_wait,control_wait;
    unsigned command_code,command_slot,control_slot,control_code,control_remaining;
    bool ready,failed;
} Host;
static Host *host;

static uint64_t address(const void *p) { return (uint64_t)(uintptr_t)p; }
static void write64(uintptr_t reg,uint64_t v)
{
    mmio_write32(reg,(uint32_t)v); mmio_write32(reg+4,(uint32_t)(v>>32));
}
static bool wait_bits(uintptr_t reg,uint32_t mask,uint32_t value,unsigned ms)
{
    uint64_t end=virtio_now_ms()+ms;
    do {
        if ((mmio_read32(reg)&mask)==value) return true;
    } while (virtio_now_ms()<end);
    serial_printf("[XHCI] Register wait timed out at %p (value %x mask %x wanted %x)\n",
        (void *)reg,mmio_read32(reg),mask,value);
    return false;
}
static bool ring_init(Ring *r)
{
    r->trbs=pmm_alloc_bytes(4096);
    r->next=0; r->cycle=1;
    return r->trbs!=NULL;
}
static uintptr_t enqueue(Ring *r,uint64_t p,uint32_t status,uint32_t control)
{
    Trb *t=&r->trbs[r->next++];
    t->lo=(uint32_t)p; t->hi=(uint32_t)(p>>32); t->status=status;
    virtio_memory_barrier();
    t->control=control|r->cycle;
    if (r->next==RING_SIZE-1) {
        Trb *link=&r->trbs[RING_SIZE-1];
        link->lo=(uint32_t)address(r->trbs); link->hi=(uint32_t)(address(r->trbs)>>32);
        link->status=0;
        virtio_memory_barrier();
        link->control=TRB_TYPE(6)|(1u<<1)|(control&CHAIN)|r->cycle; /* Link, Toggle Cycle */
        r->next=0; r->cycle^=1;
    }
    return (uintptr_t)t;
}
static void doorbell(unsigned slot,unsigned target)
{
    virtio_memory_barrier(); mmio_write32(host->db+4*slot,target);
}
static void queue_report(UsbDevice *d)
{
    enqueue(&d->interrupt,address(d->report),d->packet,TRB_TYPE(1)|IOC|(1u<<2));
    doorbell(d->slot,d->dci);
}
static void events(void)
{
    /* Budget bounds a main-loop harvest even if the host floods events. */
    for (unsigned budget=0;budget<RING_SIZE;budget++) {
        Trb *event=&host->events[host->event_next];
        uint32_t ctrl=*(volatile uint32_t *)&event->control;
        if ((ctrl&1)!=host->event_cycle) break;
        virtio_memory_barrier();
        unsigned type=(ctrl>>10)&63, slot=ctrl>>24, ep=(ctrl>>16)&31;
        unsigned code=event->status>>24;
        uintptr_t pointer=(uintptr_t)((uint64_t)event->lo|((uint64_t)event->hi<<32));
        if (type==33 && pointer==host->command_wait) {
            host->command_code=code; host->command_slot=slot;
        } else if (type==32 && slot==host->control_slot && ep==1 && host->control_wait) {
            if (code!=1 && code!=13) host->control_code=code;
            else if (pointer==host->control_wait) host->control_code=1;
            else if (code==13) host->control_remaining=event->status&0xFFFFFF;
        } else if (type==32) {
            for (unsigned i=0;i<host->device_count;i++) {
                UsbDevice *d=&host->devices[i];
                if (!d->active || d->slot!=slot || d->dci!=ep) continue;
                unsigned remaining=event->status&0xFFFFFF;
                if ((code==1 || code==13) && remaining<=d->packet) {
                    usb_hid_report(&d->hid,d->report,d->packet-remaining);
                    queue_report(d);
                } else {
                    d->active=false; usb_hid_release(&d->hid);
                    serial_printf("[XHCI] Slot %d endpoint %d stopped, completion %d\n",
                        (int)slot,(int)ep,(int)code);
                }
            }
        } else if (type==34) {
            unsigned port=(unsigned)(pointer>>24)&255;
            if (port && port<=host->max_ports) {
                uintptr_t reg=host->op+0x400+16*(port-1);
                uint32_t ps=mmio_read32(reg);
                /* Only power/wake enables and observed W1C change bits. */
                mmio_write32(reg,ps&((1u<<9)|(7u<<25)|0xFE0000u));
                if (!(ps&1)) for (unsigned i=0;i<host->device_count;i++) {
                    UsbDevice *d=&host->devices[i];
                    if (d->port==port && d->active) {
                        d->active=false; usb_hid_release(&d->hid);
                    }
                }
            }
        } else if (type==37) {
            host->failed=true;
            serial_printf("[XHCI] Host controller event %d\n",(int)code);
        }
        if (++host->event_next==RING_SIZE) { host->event_next=0; host->event_cycle^=1; }
        write64(host->ir+0x18,address(&host->events[host->event_next])|8); /* ERDP, EHB */
    }
}
static bool command(unsigned type,uint64_t pointer,uint32_t flags,unsigned *slot)
{
    if (host->failed) return false;
    host->command_code=0; host->command_slot=0;
    host->command_wait=enqueue(&host->commands,pointer,0,TRB_TYPE(type)|flags);
    doorbell(0,0);
    uint64_t end=virtio_now_ms()+1000;
    while (!host->command_code && !host->failed && virtio_now_ms()<end) events();
    unsigned code=host->command_code;
    if (slot) *slot=host->command_slot;
    host->command_wait=0;
    if (code!=1) {
        serial_printf("[XHCI] Command %d failed, completion %d\n",(int)type,(int)code);
        if (!code) host->failed=true; /* completion may still own ring entries */
        return false;
    }
    return true;
}
static bool control(UsbDevice *d,uint8_t request_type,uint8_t request,
                    uint16_t value,uint16_t index,uint16_t length)
{
    if (length>4096 || host->failed) return false;
    bool in=(request_type&0x80)!=0;
    uint64_t setup=request_type|((uint64_t)request<<8)|((uint64_t)value<<16)|
                   ((uint64_t)index<<32)|((uint64_t)length<<48);
    /* Each control stage is its own TD. CH chains TRBs within a stage,
     * not Setup -> Data -> Status (each is one TRB here). */
    enqueue(&d->ep0,setup,8,TRB_TYPE(2)|(1u<<6)|
            (length ? ((in?3u:2u)<<16) : 0));
    if (length) enqueue(&d->ep0,address(d->data),length,
                         TRB_TYPE(3)|(1u<<2)|(in?(1u<<16):0)); /* ISP: observe short reads */
    host->control_code=0; host->control_slot=d->slot; host->control_remaining=0;
    host->control_wait=enqueue(&d->ep0,0,0,TRB_TYPE(4)|IOC|
                              ((!length || !in)?(1u<<16):0));
    doorbell(d->slot,1);
    uint64_t end=virtio_now_ms()+1000;
    while (!host->control_code && !host->failed && virtio_now_ms()<end) events();
    unsigned code=host->control_code;
    host->control_wait=0;
    if (code!=1) {
        serial_printf("[USB] Slot %d request %x:%x failed, completion %d\n",
            (int)d->slot,(unsigned)request_type,(unsigned)request,(int)code);
        /* Do not queue another TD into an uncertain/possibly halted EP0. */
        return false;
    }
    if (host->control_remaining) {
        serial_printf("[USB] Slot %d short descriptor read (%d bytes missing)\n",
                      (int)d->slot,(int)host->control_remaining);
        return false;
    }
    return true;
}
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static void ctx_dequeue(uint32_t *ctx,Ring *r)
{
    uint64_t p=address(r->trbs)|1;
    ctx[2]=(uint32_t)p; ctx[3]=(uint32_t)(p>>32);
}
static bool enumerate(unsigned port,uint32_t port_status)
{
    if (host->device_count==MAX_USB_DEVICES) return false;
    UsbDevice *d=&host->devices[host->device_count++];
    d->port=port; d->speed=(port_status>>10)&15;
    d->context_size=host->context_size;
    d->input=pmm_alloc_bytes(4096); d->output=pmm_alloc_bytes(4096);
    d->data=pmm_alloc_bytes(4096); d->report=pmm_alloc_bytes(4096);
    if (!d->input || !d->output || !d->data || !d->report ||
        !ring_init(&d->ep0) || !ring_init(&d->interrupt)) return false;
    if (!command(9,0,0,&d->slot) || !d->slot || d->slot>host->max_slots) return false;
    host->dcbaa[d->slot]=address(d->output);
    uint32_t *input=(uint32_t *)d->input;
    uint32_t *slot=(uint32_t *)(d->input+d->context_size);
    uint32_t *ep0=(uint32_t *)(d->input+2*d->context_size);
    input[1]=3; /* add slot and default control endpoint */
    slot[0]=(d->speed<<20)|(1u<<27); slot[1]=port<<16;
    unsigned mps=d->speed==4 ? 512 : d->speed==3 ? 64 : 8;
    ep0[1]=(mps<<16)|(4u<<3)|(3u<<1); ctx_dequeue(ep0,&d->ep0); ep0[4]=8;
    if (!command(11,address(d->input),d->slot<<24,NULL)) return false;
    if (!control(d,0x80,6,0x0100,0,8)) return false;
    if (d->data[0]<18 || d->data[1]!=1) return false;
    unsigned reported=d->speed==4 ? (d->data[7]<=9 ? 1u<<d->data[7] : 0) : d->data[7];
    if (reported!=8 && reported!=16 && reported!=32 && reported!=64 && reported!=512) return false;
    if (reported!=mps) {
        memset(d->input,0,4096); input[1]=2;
        memcpy(ep0,d->output+d->context_size,d->context_size);
        ep0[1]=(ep0[1]&0xFFFF)|(reported<<16);
        if (!command(13,address(d->input),d->slot<<24,NULL)) return false;
    }
    if (!control(d,0x80,6,0x0100,0,18)) return false;
    serial_printf("[USB] Port %d slot %d speed %d device %x:%x\n",
        (int)port,(int)d->slot,(int)d->speed,(unsigned)le16(d->data+8),(unsigned)le16(d->data+10));
    if (!control(d,0x80,6,0x0200,0,9)) return false;
    unsigned total=le16(d->data+2);
    if (total<9 || total>4096) return false;
    if (!control(d,0x80,6,0x0200,0,(uint16_t)total)) return false;
    uint8_t configuration=d->data[5];
    unsigned endpoint=0,interval=0,report_length=0;
    bool hid_interface=false,keyboard=false;
    for (unsigned at=0;at+2<=total;) {
        const uint8_t *desc=d->data+at;
        unsigned len=desc[0],type=desc[1];
        if (len<2 || at+len>total) return false;
        if (type==4 && len>=9) {
            if (endpoint) break; /* one HID interface per directly attached device */
            hid_interface=desc[5]==3 && desc[3]==0;
            keyboard=hid_interface && desc[6]==1 && desc[7]==1;
            d->interface=desc[2]; report_length=0;
        } else if (type==0x21 && hid_interface && len>=9) {
            for (unsigned n=0;n<desc[5] && 6+3*n+3<=len;n++)
                if (desc[6+3*n]==0x22) report_length=le16(desc+7+3*n);
        } else if (type==5 && hid_interface && len>=7 && (desc[2]&0x80) && (desc[3]&3)==3) {
            endpoint=desc[2]; d->packet=le16(desc+4)&0x7FF; interval=desc[6];
        }
        at+=len;
    }
    if (!endpoint || !d->packet || d->packet>1024 || !interval) return false;
    d->hid.keyboard=keyboard;
    if (!keyboard) {
        if (!report_length || report_length>4096 ||
            !control(d,0x81,6,0x2200,(uint16_t)d->interface,(uint16_t)report_length) ||
            !usb_hid_parse_pointer(&d->hid,d->data,report_length)) {
            serial_printf("[USB] No supported absolute-pointer report descriptor\n");
            return false;
        }
    }
    if (!control(d,0,9,configuration,0,0)) return false;
    if (keyboard && (!control(d,0x21,11,0,(uint16_t)d->interface,0) ||
                     !control(d,0x21,10,0,(uint16_t)d->interface,0))) return false;
    d->dci=2*(endpoint&15)+1;
    if (d->dci<3 || d->dci>31) return false;
    memset(d->input,0,4096);
    input[1]=1u|(1u<<d->dci);
    memcpy(slot,d->output,d->context_size);
    slot[0]=(slot[0]&0x07FFFFFF)|(d->dci<<27);
    uint32_t *ep=(uint32_t *)(d->input+(d->dci+1)*d->context_size);
    unsigned xinterval=0;
    if (d->speed>=3) xinterval=MIN(interval,16)-1;
    else { unsigned frames=interval; while (frames>1 && xinterval<7) { frames>>=1; xinterval++; } xinterval+=3; }
    /* bInterval is the requested maximum service period. Poll FS/HS HID
     * at <=1ms for low latency: an 8/16ms endpoint plus one pending report
     * cannot drain QEMU's keyboard FIFO during sustained fast transitions.
     * Do not round a full-speed maximum period up to a slower power of two. */
    if (d->speed!=2) xinterval=MIN(xinterval,3);
    ep[0]=xinterval<<16;
    ep[1]=(d->packet<<16)|(7u<<3)|(3u<<1);
    ctx_dequeue(ep,&d->interrupt);
    ep[4]=d->packet|(d->packet<<16);
    if (!command(12,address(d->input),d->slot<<24,NULL)) return false;
    d->active=true;
    queue_report(d);
    serial_printf("[USB-HID] %s attached (port %d slot %d ep %d packet %d interval_us %d bInterval %d)\n",
        keyboard?"Boot keyboard":"Absolute pointer",(int)port,(int)d->slot,(int)d->dci,(int)d->packet,
        (int)(125u<<xinterval),(int)interval);
    return true;
}

void xhci_init(void)
{
    if (host) return;
    PciDev pci;
    if (pci_find(0x106b, 0x1a06, &pci) != 0 &&
        pci_find(0x1b36, 0x000d, &pci) != 0) return;
    pci_enable(&pci);
    uintptr_t cap=pci.bars[0];
    if (!cap || cap>=(8ULL<<30)) return;
    uint32_t version=mmio_read32(cap);
    unsigned caplen=version&255;
    if (caplen<0x20 || (version>>16)<0x100 || (version>>16)>0x120) return;
    host=pmm_alloc_bytes(sizeof(*host));
    if (!host) return;
    host->cap=cap; host->op=cap+caplen;
    host->db=cap+(mmio_read32(cap+0x14)&~3u);
    host->ir=cap+(mmio_read32(cap+0x18)&~31u)+0x20;
    uint32_t hcs1=mmio_read32(cap+4),hcs2=mmio_read32(cap+8),hcc=mmio_read32(cap+0x10);
    host->max_slots=hcs1&255; host->max_ports=hcs1>>24;
    host->context_size=(hcc&4)?64:32;
    serial_printf("[XHCI] v%x at %p slots=%d ports=%d context=%d\n",version>>16,
        (void *)cap,(int)host->max_slots,(int)host->max_ports,(int)host->context_size);
    /* BIOS ownership handoff, when the optional legacy capability exists. */
    unsigned ext=(hcc>>16)*4;
    for (unsigned guard=0;ext && guard<64;guard++) {
        uint32_t value=mmio_read32(cap+ext);
        serial_printf("[XHCI] Extended capability off=%x value=%x\n",ext,value);
        if ((value&255)==1) {
            mmio_write32(cap+ext,value|(1u<<24));
            if (!wait_bits(cap+ext,1u<<16,0,1000)) return;
        }
        unsigned next=(value>>8)&255;
        if (!next) break;
        ext+=next*4;
        if (ext>65532) return;
    }
    serial_printf("[XHCI] Halting controller\n");
    mmio_write32(host->op,0);
    if (!wait_bits(host->op+4,1,1,1000)) return;
    mmio_write32(host->op,2);
    if (!wait_bits(host->op,2,0,1000) || !wait_bits(host->op+4,1u<<11,0,1000)) return;
    serial_printf("[XHCI] Controller reset complete\n");
    if (!(mmio_read32(host->op+8)&1) || !host->max_slots) return;
    host->dcbaa=pmm_alloc_bytes(4096); host->erst=pmm_alloc_bytes(4096);
    host->events=pmm_alloc_bytes(4096);
    if (!host->dcbaa || !host->erst || !host->events || !ring_init(&host->commands)) return;
    unsigned scratch=((hcs2>>27)&31)|(((hcs2>>21)&31)<<5);
    if (scratch) {
        if (scratch>128) return;
        uint64_t *array=pmm_alloc_bytes(4096);
        if (!array) return;
        for (unsigned i=0;i<scratch;i++) {
            array[i]=address(pmm_alloc_bytes(4096));
            if (!array[i]) return;
        }
        host->dcbaa[0]=address(array);
    }
    host->event_cycle=1;
    host->erst[0]=address(host->events); host->erst[1]=RING_SIZE;
    /* VZ sizes its cached DCBAA mapping when DCBAAP is written. Publish
     * MaxSlotsEn first, or Address Device sees a one-entry (slot-0) array. */
    mmio_write32(host->op+0x38,host->max_slots);
    write64(host->op+0x30,address(host->dcbaa));
    write64(host->op+0x18,address(host->commands.trbs)|1);
    mmio_write32(host->ir,0); /* polled, no MSI/INTx */
    mmio_write32(host->ir+4,0);
    mmio_write32(host->ir+8,1);
    write64(host->ir+0x10,address(host->erst));
    write64(host->ir+0x18,address(host->events));
    virtio_memory_barrier();
    mmio_write32(host->op,1);
    serial_printf("[XHCI] Rings installed; starting\n");
    if (!wait_bits(host->op+4,1,0,1000)) return;
    if (!command(23,0,0,NULL)) return; /* No Op validates both rings. */
    unsigned attached=0;
    for (unsigned port=1;port<=host->max_ports && !host->failed;port++) {
        uintptr_t reg=host->op+0x400+16*(port-1);
        uint32_t ps=mmio_read32(reg);
        if (!(ps&1)) continue;
        /* Reset even a firmware-enabled port: its old USB address cannot
         * survive our controller reset/slot table replacement. */
        {
            mmio_write32(reg,(ps&((1u<<9)|(7u<<25)))|(((ps>>10)&15)==4 ? (1u<<31) : (1u<<4)));
            if (!wait_bits(reg,(1u<<4)|(1u<<31),0,500) || !wait_bits(reg,2,2,500)) continue;
        }
        ps=mmio_read32(reg);
        if (enumerate(port,ps)) attached++;
    }
    host->ready=!host->failed;
    serial_printf("[XHCI] %d USB HID device(s) attached\n",(int)attached);
}
void xhci_poll(void)
{
    if (!host || !host->ready || host->failed) return;
    events();
    for (unsigned i=0;i<host->device_count;i++)
        if (host->devices[i].active) usb_hid_tick(&host->devices[i].hid,virtio_now_ms());
}
