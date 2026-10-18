/* Actual xHCI rings/control builder with a host register/DMA boundary.
 * Include the implementation to inspect private ring state without adding
 * a production debug API. Live enumeration is separately tested in QEMU/VZ.
 * Created by Jonas Lee. */
#include "../kernel/src/arch/aarch64/xhci.c"
#include "test_support.h"

#define CAP 0x50001000ULL
#define OP (CAP+0x20)
static uint8_t pool[64*4096] __attribute__((aligned(4096)));
static unsigned allocated,slots,cached_slots,control_checks;
static uint32_t run;
static bool reject_command,drop_command;

void *pmm_alloc_bytes(size_t bytes)
{
    unsigned size=(unsigned)((bytes+4095)&~4095u);
    if (allocated+size>sizeof(pool)) return NULL;
    void *p=pool+allocated; allocated+=size; memset(p,0,size); return p;
}
int pci_find(uint16_t vendor,uint16_t device,PciDev *out)
{
    if (vendor!=0x106B || device!=0x1A06) return -1;
    memset(out,0,sizeof(*out)); out->bars[0]=CAP; return 0;
}
void pci_enable(const PciDev *p) { (void)p; }
void usb_hid_report(UsbHid *h,const uint8_t *r,size_t n) { (void)h; (void)r; (void)n; }
void usb_hid_release(UsbHid *h) { (void)h; }
void usb_hid_tick(UsbHid *h,uint64_t now) { (void)h; (void)now; }
bool usb_hid_parse_pointer(UsbHid *h,const uint8_t *r,size_t n)
{ (void)h; (void)r; (void)n; return false; }

static void completion(unsigned type,unsigned slot,uintptr_t pointer,unsigned code)
{
    Trb *event=&host->events[host->event_next];
    event->lo=(uint32_t)pointer; event->hi=(uint32_t)((uint64_t)pointer>>32);
    event->status=code<<24;
    event->control=TRB_TYPE(type)|(slot<<24)|(type==32 ? 1u<<16 : 0)|host->event_cycle;
}
uint32_t mmio_read32(uintptr_t a)
{
    if (a==CAP) return 0x01100020;
    if (a==CAP+4) return 16; /* no ports, command-ring-only fixture */
    if (a==CAP+0x10) return 1;
    if (a==CAP+0x14) return 0x940;
    if (a==CAP+0x18) return 0x520;
    if (a==OP) return run;
    if (a==OP+4) return run&1 ? 0 : 1;
    if (a==OP+8) return 1;
    return 0;
}
void mmio_write32(uintptr_t a,uint32_t v)
{
    if (a==OP) run=v&1; /* reset completes immediately */
    if (a==OP+0x38) slots=v;
    if (a==OP+0x30) cached_slots=slots; /* VZ samples length at DCBAAP write */
    if (host && a==host->db && !drop_command) {
        completion(33,0,host->command_wait,reject_command?5:1);
    }
    if (host && a==host->db+4 && host->control_wait) {
        Trb *status=(Trb *)host->control_wait;
        Trb *data=status-1, *setup=status-2;
        bool has_data=((data->control>>10)&63)==3;
        if (!has_data) setup=data;
        CHECK(((setup->control>>10)&63)==2 && (setup->control&(1u<<6)) &&
              !(setup->control&CHAIN), "Setup stage uses immediate data and ends its own TD");
        if (has_data) CHECK(!(data->control&CHAIN) && data->status==18,
                            "single Data stage is not chained to Status");
        bool in=(setup->lo&0x80)!=0;
        CHECK(((status->control>>10)&63)==4 && (status->control&IOC) &&
              !(status->control&CHAIN) &&
              ((status->control>>16)&1)==(unsigned)(!has_data || !in),
              "Status has IOC and opposite direction (IN for no-data request)");
        control_checks++;
        completion(32,1,host->control_wait,1);
    }
}
uint8_t mmio_read8(uintptr_t a) { (void)a; return 0; }
uint16_t mmio_read16(uintptr_t a) { (void)a; return 0; }
void mmio_write8(uintptr_t a,uint8_t v) { (void)a; (void)v; }
void mmio_write16(uintptr_t a,uint16_t v) { (void)a; (void)v; }

int main(void)
{
    xhci_init();
    CHECK(host && host->ready && !host->failed, "actual controller init and No Op complete");
    CHECK(cached_slots==16, "MaxSlotsEn precedes DCBAAP publication");
    CHECK(host->event_next==1, "command completion consumed from event ring");
    Ring ring;
    CHECK(ring_init(&ring), "DMA ring allocated at runtime");
    for (unsigned i=0;i<65536;i++) {
        unsigned index=ring.next,cycle=ring.cycle;
        uintptr_t p=enqueue(&ring,0x123456789ULL,i,TRB_TYPE(1));
        Trb *trb=(Trb *)p;
        if (p!=(uintptr_t)&ring.trbs[index] || (trb->control&1)!=cycle ||
            trb->lo!=0x23456789u || trb->hi!=1) {
            CHECK(false,"TRB ring pointer, address and cycle survive repeated wrapping"); break;
        }
        if (index==254) {
            Trb *link=&ring.trbs[255];
            if (((link->control>>10)&63)!=6 || !(link->control&2) ||
                (link->control&1)!=cycle || ring.cycle==cycle) {
                CHECK(false,"Link TRB toggles the cycle exactly at ring capacity"); break;
            }
        }
    }
    CHECK(ring.next==65536%255, "ring reserves link entry instead of wrapping at 256");
    UsbDevice *device=&host->devices[0];
    device->slot=1; device->data=pmm_alloc_bytes(4096);
    CHECK(ring_init(&device->ep0), "control ring allocated");
    CHECK(control(device,0x80,6,0x0100,0,18), "control IN round trip");
    CHECK(control(device,0,9,1,0,0), "control no-data round trip");
    CHECK(control_checks==2, "both control stage forms reached DMA boundary");
    reject_command=true;
    CHECK(!command(23,0,0,NULL) && !host->failed, "command error returned without false success");
    reject_command=false; drop_command=true;
    CHECK(!command(23,0,0,NULL) && host->failed, "lost command completion stops reuse at deadline");
    return finish();
}
