#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "virtio.h"

// Adapted from https://github.com/rafaelRiv/osblog/blob/master/risc_v/src/gpu.rs

// Global Driver State
struct {
    uint64 base_addr;
    struct spinlock lock;
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    int free[NUM];
    uint16 used_idx; 
    
} gpu;

// Queue Memory
__attribute__((aligned(4096)))
char gpu_queue_page[4096 * 2];

// Framebuffer
__attribute__((aligned(4096)))
uchar gpu_buffer[640 * 400 * 4]; 


// Command buffer
struct virtio_gpu_resource_create_2d    cmd_create;
struct virtio_gpu_resource_attach_backing cmd_attach;
struct virtio_gpu_set_scanout           cmd_scanout;
struct virtio_gpu_transfer_to_host_2d   cmd_transfer;
struct virtio_gpu_resource_flush        cmd_flush;
struct virtio_gpu_ctrl_hdr              cmd_resp;


// Helpers to interact with registers
static uint32 reg_read(uint32 offset) {
    return *(volatile uint32 *)(gpu.base_addr + offset);
}

static void reg_write(uint32 offset, uint32 val) {
    *(volatile uint32 *)(gpu.base_addr + offset) = val;
}

// Send a command and wait for response
void
virtio_gpu_send(void *cmd, uint32 cmd_len, void *resp, uint32 resp_len)
{
    acquire(&gpu.lock);

    // Allocate descriptors
    int idx[2];
    for(int i = 0; i < 2; i++){
        int found = -1;
        for(int j = 0; j < NUM; j++){
            if(gpu.free[j]){
                gpu.free[j] = 0;
                found = j;
                break;
            }
        }
        if(found == -1) panic("virtio_gpu: no descriptors");
        idx[i] = found;
    }

    // Set up descriptors
    struct virtq_desc *desc = gpu.desc;
    struct virtq_avail *avail = gpu.avail;
    struct virtq_used *used = gpu.used;

    // Desc 0: Command (Read-Only for GPU)
    desc[idx[0]].addr = (uint64) cmd;
    desc[idx[0]].len = cmd_len;
    desc[idx[0]].flags = VIRTQ_DESC_F_NEXT;
    desc[idx[0]].next = idx[1];

    // Desc 1: Response (Write-Only for GPU)
    desc[idx[1]].addr = (uint64) resp;
    desc[idx[1]].len = resp_len;
    desc[idx[1]].flags = VIRTQ_DESC_F_WRITE;
    desc[idx[1]].next = 0;

    // Submit
    avail->ring[avail->idx % NUM] = idx[0];
    __sync_synchronize();
    avail->idx++;
    __sync_synchronize();

    // Notify
    reg_write(VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    // Poll for completion
    while(used->idx == gpu.used_idx) {
        __sync_synchronize(); 
    }
    gpu.used_idx++;

    gpu.free[idx[0]] = 1;
    gpu.free[idx[1]] = 1;

    release(&gpu.lock);
}


// Initialise GPU
void virtio_gpu_init(void) {

    initlock(&gpu.lock, "virtio_gpu");

    // Find the GPU
    gpu.base_addr = 0;
    for(int i = 0; i < 8; i++) {
        uint64 addr = 0x10001000 + (i * 0x1000);
        uint32 magic = *(volatile uint32 *)(addr + VIRTIO_MMIO_MAGIC_VALUE);
        uint32 device_id = *(volatile uint32 *)(addr + VIRTIO_MMIO_DEVICE_ID);
        
        if(magic == 0x74726976 && device_id == 16) {
            gpu.base_addr = addr;
            printf("virtio_gpu: Found at %p\n", (void *) addr);
            break;
        }
    }
    if (gpu.base_addr == 0) {
        panic("virtio_gpu: not found");
    }

    // Setup Ring Memory
    gpu.desc = (struct virtq_desc *) gpu_queue_page;
    gpu.avail = (struct virtq_avail *) (gpu_queue_page + NUM * sizeof(struct virtq_desc));
    gpu.used = (struct virtq_used *) (gpu_queue_page + 4096);

    // Mark all descriptors free
    for (int i = 0; i < NUM; i++) {
        gpu.free[i] = 1;
    }
    gpu.used_idx = 0;

    // Reset Device
    reg_write(VIRTIO_MMIO_STATUS, 0);
    reg_write(VIRTIO_MMIO_STATUS, VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER);

    // Features
    uint64 features = *(volatile uint64 *)(gpu.base_addr + VIRTIO_MMIO_DEVICE_FEATURES);
    *(volatile uint64 *)(gpu.base_addr + VIRTIO_MMIO_DRIVER_FEATURES) = features;
    reg_write(VIRTIO_MMIO_STATUS, reg_read(VIRTIO_MMIO_STATUS) | VIRTIO_CONFIG_S_FEATURES_OK);

    if(!(reg_read(VIRTIO_MMIO_STATUS) & VIRTIO_CONFIG_S_FEATURES_OK))
        panic("virtio_gpu: features fail");

    // Config Queue 0
    reg_write(VIRTIO_MMIO_QUEUE_SEL, 0);
    if(reg_read(VIRTIO_MMIO_QUEUE_READY)) panic("virtio_gpu: queue ready?");

    reg_write(VIRTIO_MMIO_QUEUE_NUM, NUM);

    uint64 desc_pa = (uint64) gpu.desc;
    uint64 avail_pa = (uint64) gpu.avail;
    uint64 used_pa = (uint64) gpu.used;

    reg_write(VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32)desc_pa);
    reg_write(VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32)(desc_pa >> 32));
    reg_write(VIRTIO_MMIO_DRIVER_DESC_LOW, (uint32)avail_pa);
    reg_write(VIRTIO_MMIO_DRIVER_DESC_HIGH, (uint32)(avail_pa >> 32));
    reg_write(VIRTIO_MMIO_DEVICE_DESC_LOW, (uint32)used_pa);
    reg_write(VIRTIO_MMIO_DEVICE_DESC_HIGH, (uint32)(used_pa >> 32));

    reg_write(VIRTIO_MMIO_QUEUE_READY, 1);

    // setup fb
    // Create resource
    cmd_create.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cmd_create.resource_id = 1;
    cmd_create.format = 1;
    cmd_create.width = 640;
    cmd_create.height = 400;
    virtio_gpu_send(&cmd_create, sizeof(cmd_create), &cmd_resp, sizeof(cmd_resp));

    // Attach backing
    cmd_attach.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    cmd_attach.resource_id = 1;
    cmd_attach.nr_entries = 1;
    cmd_attach.entries[0].addr = (uint64) gpu_buffer;
    cmd_attach.entries[0].length = 640 * 400 * 4;
    virtio_gpu_send(&cmd_attach, sizeof(cmd_attach), &cmd_resp, sizeof(cmd_resp));

    // Set scanout
    cmd_scanout.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    cmd_scanout.r.x = 0; cmd_scanout.r.y = 0;
    cmd_scanout.r.width = 640; cmd_scanout.r.height = 400;
    cmd_scanout.scanout_id = 0;
    cmd_scanout.resource_id = 1;
    virtio_gpu_send(&cmd_scanout, sizeof(cmd_scanout), &cmd_resp, sizeof(cmd_resp));

    // Driver OK
    reg_write(VIRTIO_MMIO_STATUS, reg_read(VIRTIO_MMIO_STATUS) | VIRTIO_CONFIG_S_DRIVER_OK);

    printf("virtio_gpu: display initialized 640x400\n");
}


// Called by sys_flushfb
void virtio_gpu_flush(void)
{
    // Transfer (RAM -> VRAM)
    cmd_transfer.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    cmd_transfer.r.x = 0; cmd_transfer.r.y = 0;
    cmd_transfer.r.width = 640; cmd_transfer.r.height = 400;
    cmd_transfer.offset = 0;
    cmd_transfer.resource_id = 1;
    
    virtio_gpu_send(&cmd_transfer, sizeof(cmd_transfer), &cmd_resp, sizeof(cmd_resp));

    // Flush (VRAM -> Screen)
    cmd_flush.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    cmd_flush.r.x = 0; cmd_flush.r.y = 0;
    cmd_flush.r.width = 640; cmd_flush.r.height = 400;
    cmd_flush.resource_id = 1;

    virtio_gpu_send(&cmd_flush, sizeof(cmd_flush), &cmd_resp, sizeof(cmd_resp));
}