#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "virtio.h"

// Adapted from https://github.com/rafaelRiv/osblog/blob/master/risc_v/src/input.rs

#define NUM_EVENTS 16
#define KQ_BUF_SIZE 64

void repopulate_event(uint16 desc_idx);

// Global Driver State
struct {
    uint64 base_addr;
    struct virtq_desc *event_desc;
	struct virtq_avail *event_avail;
    struct virtq_used *event_used;
    struct spinlock lock;
    struct virtq_desc *status_desc;
	struct virtq_avail *status_avail;
    struct virtq_used *status_used;
    uint16 event_idx;
    uint16 event_ack_used_idx;
    struct virtio_input_event *event_buffer;
    uint16 status_ack_used_idx;
    struct virtio_input_event KEY_EVENT[NUM_EVENTS];
} input;

struct key_press {
    uint16 code;
    uint32 val;
};

struct {
    struct key_press buf[KQ_BUF_SIZE];
    uint32 read_idx, write_idx;
    struct spinlock lock;
} kqueue;

// Helpers to interact with registers
static uint32 reg_read(uint32 offset) {
    return *(volatile uint32 *)(input.base_addr + offset);
}

static void reg_write(uint32 offset, uint32 val) {
    *(volatile uint32 *)(input.base_addr + offset) = val;
}

__attribute__((aligned(PGSIZE)))
char event_queue_page[PGSIZE * 2];

__attribute__((aligned(PGSIZE)))
char status_queue_page[PGSIZE * 2];

void kqueue_push(uint16 code, uint32 val) {
    acquire(&kqueue.lock);
    if ((kqueue.write_idx + 1) % KQ_BUF_SIZE == kqueue.read_idx % KQ_BUF_SIZE) {
        release(&kqueue.lock);
        return;
    }
    kqueue.buf[kqueue.write_idx % KQ_BUF_SIZE].code = code;
    kqueue.buf[kqueue.write_idx % KQ_BUF_SIZE].val = val;
    kqueue.write_idx++;
    release(&kqueue.lock);
}

int kqueue_pop(uint16 *code, uint32 *val) {
    acquire(&kqueue.lock);
    if (kqueue.read_idx == kqueue.write_idx) {
        release(&kqueue.lock);
        return 0;
    }
    *code = kqueue.buf[kqueue.read_idx % KQ_BUF_SIZE].code;
    *val = kqueue.buf[kqueue.read_idx % KQ_BUF_SIZE].val;
    kqueue.read_idx++;
    release(&kqueue.lock);
    return 1;
}

// Initialise input
void virtio_input_init(void) {

    initlock(&input.lock, "virtio_input");
    initlock(&kqueue.lock, "kqueu");

    // Find the input
    input.base_addr = 0;
    for(int i = 0; i < 8; i++) {
        uint64 addr = 0x10001000 + (i * 0x1000);
        uint32 magic = *(volatile uint32 *)(addr + VIRTIO_MMIO_MAGIC_VALUE);
        uint32 device_id = *(volatile uint32 *)(addr + VIRTIO_MMIO_DEVICE_ID);
        
        if(magic == 0x74726976 && device_id == 18) {
            input.base_addr = addr;
            printf("virtio_input: Found at %p\n", (void *) addr);
            break;
        }
    }
    if (input.base_addr == 0) {
        panic("virtio_input: not found");
    }
    
    // Setup Ring Memory
    input.event_desc = (struct virtq_desc *) event_queue_page;
    input.event_avail = (struct virtq_avail *) (event_queue_page + NUM_EVENTS * sizeof(struct virtq_desc));
    input.event_used = (struct virtq_used *) (event_queue_page + PGSIZE);
    input.status_desc = (struct virtq_desc *) status_queue_page;
    input.status_avail = (struct virtq_avail *) (status_queue_page + NUM_EVENTS * sizeof(struct virtq_desc));
    input.status_used = (struct virtq_used *) (status_queue_page + PGSIZE);

    // Reset Device
    reg_write(VIRTIO_MMIO_STATUS, 0);
    reg_write(VIRTIO_MMIO_STATUS, VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER);

    // Features
    uint64 features = *(volatile uint64 *)(input.base_addr + VIRTIO_MMIO_DEVICE_FEATURES);
    *(volatile uint64 *)(input.base_addr + VIRTIO_MMIO_DRIVER_FEATURES) = features;
    reg_write(VIRTIO_MMIO_STATUS, reg_read(VIRTIO_MMIO_STATUS) | VIRTIO_CONFIG_S_FEATURES_OK);

    if(!(reg_read(VIRTIO_MMIO_STATUS) & VIRTIO_CONFIG_S_FEATURES_OK))
        panic("virtio_gpu: features fail");

    // Config Queue 0
    reg_write(VIRTIO_MMIO_QUEUE_SEL, 0);
    if(reg_read(VIRTIO_MMIO_QUEUE_READY)) panic("virtio_gpu: queue ready?");

    reg_write(VIRTIO_MMIO_QUEUE_NUM, NUM_EVENTS);

    uint64 desc_pa = (uint64) input.event_desc;
    uint64 avail_pa = (uint64) input.event_avail;
    uint64 used_pa = (uint64) input.event_used;

    reg_write(VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32) desc_pa);
    reg_write(VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32) (desc_pa >> 32));
    reg_write(VIRTIO_MMIO_DRIVER_DESC_LOW, (uint32) avail_pa);
    reg_write(VIRTIO_MMIO_DRIVER_DESC_HIGH, (uint32) (avail_pa >> 32));
    reg_write(VIRTIO_MMIO_DEVICE_DESC_LOW, (uint32) used_pa);
    reg_write(VIRTIO_MMIO_DEVICE_DESC_HIGH, (uint32) (used_pa >> 32));

    reg_write(VIRTIO_MMIO_QUEUE_READY, 1);

    // Config Queue 1 (not gonna use it but whatever)
    reg_write(VIRTIO_MMIO_QUEUE_SEL, 1);

    desc_pa = (uint64) input.status_desc;
    avail_pa = (uint64) input.status_avail;
    used_pa = (uint64) input.status_used;

    reg_write(VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32) desc_pa);
    reg_write(VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32) (desc_pa >> 32));
    reg_write(VIRTIO_MMIO_DRIVER_DESC_LOW, (uint32) avail_pa);
    reg_write(VIRTIO_MMIO_DRIVER_DESC_HIGH, (uint32) (avail_pa >> 32));
    reg_write(VIRTIO_MMIO_DEVICE_DESC_LOW, (uint32) used_pa);
    reg_write(VIRTIO_MMIO_DEVICE_DESC_HIGH, (uint32) (used_pa >> 32));

    reg_write(VIRTIO_MMIO_QUEUE_READY, 1);

    // Driver OK
    reg_write(VIRTIO_MMIO_STATUS, reg_read(VIRTIO_MMIO_STATUS) | VIRTIO_CONFIG_S_DRIVER_OK);

    for (int i = 0; i < NUM_EVENTS; i++) {
        repopulate_event(i);
    }

    printf("virtio_input: configured\n");
}

void repopulate_event(uint16 desc_idx) {
    struct virtq_desc *desc = input.event_desc;
    struct virtq_avail *avail = input.event_avail;

    desc[desc_idx].addr = (uint64) &input.KEY_EVENT[desc_idx];
    desc[desc_idx].flags = VIRTQ_DESC_F_WRITE;
    desc[desc_idx].len = sizeof(struct virtio_input_event);
    desc[desc_idx].next = 0;

    avail -> ring[avail -> idx % NUM_EVENTS] = desc_idx;
    __sync_synchronize();
    avail -> idx++;
    __sync_synchronize();

    input.event_idx++;
}

void virtio_input_poll(void) {
    acquire(&(input.lock));
    struct virtq_used *used = input.event_used;
    int elem_processed = 0;
    while (used -> idx != input.event_ack_used_idx && elem_processed < NUM_EVENTS) {
        struct virtq_used_elem elem = used -> ring[input.event_ack_used_idx % NUM_EVENTS];
        uint16 id = elem.id;
        struct virtio_input_event *event = &input.KEY_EVENT[id];
        if (event -> type == EV_KEY && event -> value != 2) {
            kqueue_push(event -> code, event -> value);
        }
        repopulate_event(elem.id);
        input.event_ack_used_idx++;
        elem_processed++;
    }

    if (elem_processed > 0) {
        reg_write(VIRTIO_MMIO_QUEUE_NOTIFY, 0);
    }
    release(&(input.lock));
}
