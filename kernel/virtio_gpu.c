#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "virtio.h"

void virtio_gpu_start(void);
void virtio_gpu_send(void *cmd, uint32 cmd_len, void *resp, uint32 resp_len);

// Global framebuffer for contiguous physical memory for mmap
__attribute__((aligned(PGSIZE))) 
uchar gpu_buffer[320 * 200 * 4];

// The base address of the GPU device
void *gpu_base = 0;

// Copied the virtio_disk.c struct
struct {
  struct virtq_desc *desc;
  struct virtq_avail *avail;
  struct virtq_used *used;
  int free[NUM];
  uint16 used_idx;
  struct spinlock lock;
} gpuq;

// Find the VirtIO GPU on the MMIO bus
void
virtio_gpu_init(void)
{
  uint32 *pad;
  
  initlock(&gpuq.lock, "virtio_gpu");

  // QEMU maps VirtIO devices at 0x10001000, 0x10002000, ...
  for(void *base = (void *) VIRTIO0; base < (void *) (VIRTIO0 + 8 * 0x1000); base += 0x1000){
    pad = (uint32 *)base;
    if(pad[VIRTIO_MMIO_MAGIC_VALUE] != 0x74726976 || pad[VIRTIO_MMIO_VERSION] != 1){
      continue;
    }
    if(pad[VIRTIO_MMIO_DEVICE_ID] == 16){
      gpu_base = base;
      break;
    }
  }

  if(!gpu_base) {
    printf("virtio_gpu: not found!\n");
    return;
  }

  #define R(reg) (*(volatile uint32 *) (gpu_base + (reg)))

  // negotiate features
  R(VIRTIO_MMIO_STATUS) = 0;
  R(VIRTIO_MMIO_STATUS) |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  R(VIRTIO_MMIO_STATUS) |= VIRTIO_CONFIG_S_DRIVER;
  R(VIRTIO_MMIO_STATUS) |= VIRTIO_CONFIG_S_FEATURES_OK;
  R(VIRTIO_MMIO_QUEUE_SEL) = 0;
  
  if(R(VIRTIO_MMIO_QUEUE_READY))
    panic("virtio_gpu: queue 0 ready");

  R(VIRTIO_MMIO_QUEUE_NUM) = NUM;

  // allocate and zero queue memory.
  gpuq.desc = kalloc();
  gpuq.avail = kalloc();
  gpuq.used = kalloc();
  if(!gpuq.desc || !gpuq.avail || !gpuq.used)
    panic("virtio gpu kalloc");

  memset(gpuq.desc, 0, PGSIZE);
  memset(gpuq.avail, 0, PGSIZE);
  memset(gpuq.used, 0, PGSIZE);

  // write physical addresses.
  R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)gpuq.desc;
  R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)gpuq.desc >> 32;
  R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)gpuq.avail;
  R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)gpuq.avail >> 32;
  R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)gpuq.used;
  R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)gpuq.used >> 32;

  // queue is ready.
  R(VIRTIO_MMIO_QUEUE_READY) = 0x1;

  // all NUM descriptors start out unused.
  for(int i = 0; i < NUM; i++)
    gpuq.free[i] = 1;

  R(VIRTIO_MMIO_STATUS) |= VIRTIO_CONFIG_S_DRIVER_OK;

  virtio_gpu_start();
}

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 200
#define SCREEN_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT * 4)

uint64 gpu_framebuffer_pa = 0;

// Helper: Send a command and wait for response
void
virtio_gpu_send(void *cmd, uint32 cmd_len, void *resp, uint32 resp_len)
{
    struct virtq_desc *desc = gpuq.desc;
    struct virtq_avail *avail = gpuq.avail;
    struct virtq_used *used = gpuq.used;

    acquire(&gpuq.lock);

    // Allocate descriptors, one for CMD (read-only for GPU), one for RESP (write-only for GPU)
    int idx[2];
    for(int i = 0; i < 2; i++){
        int found = -1;
        for(int j = 0; j < NUM; j++){
            if(gpuq.free[j]){
                gpuq.free[j] = 0;
                found = j;
                break;
            }
        }
        if(found == -1) panic("virtio_gpu: no descriptors");
        idx[i] = found;
    }

    // Fill Descriptors
    // Desc 0: command
    desc[idx[0]].addr = (uint64) cmd;
    desc[idx[0]].len = cmd_len;
    desc[idx[0]].flags = VIRTQ_DESC_F_NEXT;
    desc[idx[0]].next = idx[1];

    // Desc 1: response
    desc[idx[1]].addr = (uint64) resp;
    desc[idx[1]].len = resp_len;
    desc[idx[1]].flags = VIRTQ_DESC_F_WRITE;
    desc[idx[1]].next = 0;

    // Submit to Avail Ring
    avail->ring[avail->idx % NUM] = idx[0];
    __sync_synchronize();
    avail->idx++;
    __sync_synchronize(); 

    // Notify Device
    R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

    // Poll for completion
    static uint16 last_used_idx = 0;
    while(used->idx == last_used_idx) {
    }
    last_used_idx++;

    // Free Descriptors
    gpuq.free[idx[0]] = 1;
    gpuq.free[idx[1]] = 1;

    release(&gpuq.lock);
}

void
virtio_gpu_start(void)
{
    struct virtio_gpu_resp_display_info resp_info;
    struct virtio_gpu_ctrl_hdr resp_hdr;

    // Get display info
    struct virtio_gpu_ctrl_hdr cmd_info;
    memset(&cmd_info, 0, sizeof(cmd_info));
    cmd_info.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
    virtio_gpu_send(&cmd_info, sizeof(cmd_info), &resp_info, sizeof(resp_info));
    
    printf("virtio_gpu: display info type %d (expected %d)\n", 
            resp_info.hdr.type, VIRTIO_GPU_RESP_OK_DISPLAY_INFO);


    // Create 2D resrouce
    struct virtio_gpu_resource_create_2d cmd_create;
    memset(&cmd_create, 0, sizeof(cmd_create));
    cmd_create.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cmd_create.resource_id = 1;
    cmd_create.format = 1;
    cmd_create.width = SCREEN_WIDTH;
    cmd_create.height = SCREEN_HEIGHT;
    virtio_gpu_send(&cmd_create, sizeof(cmd_create), &resp_hdr, sizeof(resp_hdr));


    // Allocate backing store
    struct virtio_gpu_resource_attach_backing cmd_attach;
    memset(&cmd_attach, 0, sizeof(cmd_attach));
    cmd_attach.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    cmd_attach.resource_id = 1;

    int n_pages = (sizeof(gpu_buffer) + PGSIZE - 1) / PGSIZE;
    cmd_attach.nr_entries = n_pages;

    uint64 start_pa = (uint64) gpu_buffer; // VA == PA for kernel

    gpu_framebuffer_pa = start_pa; 

    for(int i = 0; i < n_pages; i++){
        cmd_attach.entries[i].addr = start_pa + (i * PGSIZE);
        cmd_attach.entries[i].length = PGSIZE;
    }

    virtio_gpu_send(&cmd_attach, 
        sizeof(cmd_attach.hdr) + sizeof(uint32)*2 + sizeof(struct virtio_gpu_mem_entry)*n_pages, 
        &resp_hdr, sizeof(resp_hdr));

    // Set scanout to link to screen
    struct virtio_gpu_set_scanout cmd_scan;
    memset(&cmd_scan, 0, sizeof(cmd_scan));
    cmd_scan.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    cmd_scan.resource_id = 1;
    cmd_scan.scanout_id = 0;
    cmd_scan.r.width = SCREEN_WIDTH;
    cmd_scan.r.height = SCREEN_HEIGHT;
    virtio_gpu_send(&cmd_scan, sizeof(cmd_scan), &resp_hdr, sizeof(resp_hdr));
    
    printf("virtio_gpu: initialized 320x200\n");
}

// Return the physical address of the framebuffer
uint64
virtio_gpu_get_framebuffer_addr(void)
{
  return gpu_framebuffer_pa;
}


void
virtio_gpu_flush(void)
{
  struct virtio_gpu_resource_flush cmd_flush;
  struct virtio_gpu_transfer_to_host_2d cmd_transfer;
  struct virtio_gpu_ctrl_hdr resp;

  // Copy from Backing Store to GPU VRAM
  memset(&cmd_transfer, 0, sizeof(cmd_transfer));
  cmd_transfer.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
  cmd_transfer.resource_id = 1;
  cmd_transfer.r.width = SCREEN_WIDTH;
  cmd_transfer.r.height = SCREEN_HEIGHT;
  virtio_gpu_send(&cmd_transfer, sizeof(cmd_transfer), &resp, sizeof(resp));

  // Draw VRAM to Screen
  memset(&cmd_flush, 0, sizeof(cmd_flush));
  cmd_flush.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
  cmd_flush.resource_id = 1;
  cmd_flush.r.width = SCREEN_WIDTH;
  cmd_flush.r.height = SCREEN_HEIGHT;
  virtio_gpu_send(&cmd_flush, sizeof(cmd_flush), &resp, sizeof(resp));
}