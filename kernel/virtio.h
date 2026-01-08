//
// virtio device definitions.
// for both the mmio interface, and virtio descriptors.
// only tested with qemu.
//
// the virtio spec:
// https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.pdf
//

// virtio mmio control registers, mapped starting at 0x10001000.
// from qemu virtio_mmio.h
#define VIRTIO_MMIO_MAGIC_VALUE		0x000 // 0x74726976
#define VIRTIO_MMIO_VERSION		0x004 // version; should be 2
#define VIRTIO_MMIO_DEVICE_ID		0x008 // device type; 1 is net, 2 is disk
#define VIRTIO_MMIO_VENDOR_ID		0x00c // 0x554d4551
#define VIRTIO_MMIO_DEVICE_FEATURES	0x010
#define VIRTIO_MMIO_DRIVER_FEATURES	0x020
#define VIRTIO_MMIO_QUEUE_SEL		0x030 // select queue, write-only
#define VIRTIO_MMIO_QUEUE_NUM_MAX	0x034 // max size of current queue, read-only
#define VIRTIO_MMIO_QUEUE_NUM		0x038 // size of current queue, write-only
#define VIRTIO_MMIO_QUEUE_READY		0x044 // ready bit
#define VIRTIO_MMIO_QUEUE_NOTIFY	0x050 // write-only
#define VIRTIO_MMIO_INTERRUPT_STATUS	0x060 // read-only
#define VIRTIO_MMIO_INTERRUPT_ACK	0x064 // write-only
#define VIRTIO_MMIO_STATUS		0x070 // read/write
#define VIRTIO_MMIO_QUEUE_DESC_LOW	0x080 // physical address for descriptor table, write-only
#define VIRTIO_MMIO_QUEUE_DESC_HIGH	0x084
#define VIRTIO_MMIO_DRIVER_DESC_LOW	0x090 // physical address for available ring, write-only
#define VIRTIO_MMIO_DRIVER_DESC_HIGH	0x094
#define VIRTIO_MMIO_DEVICE_DESC_LOW	0x0a0 // physical address for used ring, write-only
#define VIRTIO_MMIO_DEVICE_DESC_HIGH	0x0a4

// status register bits, from qemu virtio_config.h
#define VIRTIO_CONFIG_S_ACKNOWLEDGE	1
#define VIRTIO_CONFIG_S_DRIVER		2
#define VIRTIO_CONFIG_S_DRIVER_OK	4
#define VIRTIO_CONFIG_S_FEATURES_OK	8

// device feature bits
#define VIRTIO_BLK_F_RO              5	/* Disk is read-only */
#define VIRTIO_BLK_F_SCSI            7	/* Supports scsi command passthru */
#define VIRTIO_BLK_F_CONFIG_WCE     11	/* Writeback mode available in config */
#define VIRTIO_BLK_F_MQ             12	/* support more than one vq */
#define VIRTIO_F_ANY_LAYOUT         27
#define VIRTIO_RING_F_INDIRECT_DESC 28
#define VIRTIO_RING_F_EVENT_IDX     29

// this many virtio descriptors.
// must be a power of two.
#define NUM 8

// a single descriptor, from the spec.
struct virtq_desc {
    uint64 addr;
    uint32 len;
    uint16 flags;
    uint16 next;
} __attribute__((packed));
#define VRING_DESC_F_NEXT  1 // chained with another descriptor
#define VRING_DESC_F_WRITE 2 // device writes (vs read)

// the (entire) avail ring, from the spec.
struct virtq_avail {
    uint16 flags;
    uint16 idx;
    uint16 ring[NUM];
    uint16 unused;
} __attribute__((packed));

// one entry in the "used" ring, with which the
// device tells the driver about completed requests.
struct virtq_used_elem {
    uint32 id;
    uint32 len;
} __attribute__((packed));


struct virtq_used {
    uint16 flags;
    uint16 idx;
    struct virtq_used_elem ring[NUM];
} __attribute__((packed));

// these are specific to virtio block devices, e.g. disks,
// described in Section 5.2 of the spec.

#define VIRTIO_BLK_T_IN  0 // read the disk
#define VIRTIO_BLK_T_OUT 1 // write the disk

// the format of the first descriptor in a disk request.
// to be followed by two more descriptors containing
// the block, and a one-byte status.
struct virtio_blk_req {
  uint32 type; // VIRTIO_BLK_T_IN or ..._OUT
  uint32 reserved;
  uint64 sector;
};

// Command Codes (Input)
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO    0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D  0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF      0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT         0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH      0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106

// Response Codes (Output)
#define VIRTIO_GPU_RESP_OK_NODATA          0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO    0x1101
#define VIRTIO_GPU_RESP_OK_CAPSET_INFO     0x1102
#define VIRTIO_GPU_RESP_OK_CAPSET          0x1103

#define VIRTQ_DESC_F_NEXT  1 // Next field contains linked descriptor index
#define VIRTQ_DESC_F_WRITE 2 // Device writes (we read)

// Struct are all copied from the specs
// All commands to GPU start with this header
struct virtio_gpu_ctrl_hdr {
    uint32 type;
    uint32 flags;
    uint64 fence_id;
    uint32 ctx_id;
    uint32 padding;
} __attribute__((packed));

// Basic rectangle definition
struct virtio_gpu_rect {
    uint32 x;
    uint32 y;
    uint32 width;
    uint32 height;
} __attribute__((packed));

// Allocatr resource_id for new image
struct virtio_gpu_resource_create_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32 resource_id;
    uint32 format;
    uint32 width;
    uint32 height;
} __attribute__((packed));

// A page of RAM to send
struct virtio_gpu_mem_entry {
    uint64 addr;
    uint32 length;
    uint32 padding;
} __attribute__((packed));

// Tell the GPU where to look in RAM for resource_id
struct virtio_gpu_resource_attach_backing {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32 resource_id;
    uint32 nr_entries;
    struct virtio_gpu_mem_entry entries[1];
} __attribute__((packed));

// Set scanout (link GPU handle to the display)
struct virtio_gpu_set_scanout {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32 scanout_id;
    uint32 resource_id;
} __attribute__((packed));

// Transfer/dlush
struct virtio_gpu_transfer_to_host_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint64 offset;
    uint32 resource_id;
    uint32 padding;
} __attribute__((packed));

struct virtio_gpu_resource_flush {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32 resource_id;
    uint32 padding;
} __attribute__((packed));

// Virtio input config
#define VIRTIO_INPUT_CFG_UNSET 0x00
#define VIRTIO_INPUT_CFG_ID_NAME 0x01
#define VIRTIO_INPUT_CFG_ID_SERIAL 0x02
#define VIRTIO_INPUT_CFG_ID_DEVIDS 0x03
#define VIRTIO_INPUT_CFG_PROP_BITS 0x10
#define VIRTIO_INPUT_CFG_EV_BITS 0x11
#define VIRTIO_INPUT_CFG_ABS_INFO 0x12

#define EV_KEY 0x01

struct virtio_input_absinfo {
    uint32 min;
    uint32 max;
    uint32 fuzz;
    uint32 flat;
    uint32 res;
} __attribute__((packed));

struct virtio_input_devids {
    uint16 bustype;
    uint16 vendor;
    uint16 product;
    uint16 version;
} __attribute__((packed));

struct virtio_input_config {
    uint8 select;
    uint8 subsel;
    uint8 size;
    uint8 reserved[5];
    union {
        char string[128];
        uint8  bitmap[128];
        struct virtio_input_absinfo abs;
        struct virtio_input_devids ids;
    } u;
} __attribute__((packed));

struct virtio_input_event {
    uint16 type;
    uint16 code;
    uint32 value;
} __attribute__((packed));