#include <driver/virtio.h>
#include <fs/block_device.h>
#include <common/string.h>
#include <kernel/printk.h>

extern u32 LBA;

/**
    @brief a simple implementation of reading a block from SD card.

    @param[in] block_no the block number to read
    @param[out] buffer the buffer to store the data
 */
static void sd_read(usize block_no, u8 *buffer)
{
    Buf b;
    b.block_no = (u32)block_no + LBA;
    b.flags = 0;
    virtio_blk_rw(&b);
    memcpy(buffer, b.data, BLOCK_SIZE);
}

/**
    @brief a simple implementation of writing a block to SD card.

    @param[in] block_no the block number to write
    @param[in] buffer the buffer to store the data
 */
static void sd_write(usize block_no, u8 *buffer) {
    Buf b;
    b.block_no = (u32)block_no;
    b.flags = B_DIRTY | B_VALID;
    memcpy(b.data, buffer, BLOCK_SIZE);
    virtio_blk_rw(&b);
}

/**
    @brief the in-memory copy of the super block.

    We may need to read the super block multiple times, so keep a copy of it in
    memory.

    @note the super block, in our lab, is always read-only, so we don't need to
    write it back.
 */
static u8 sblock_data[BLOCK_SIZE];

BlockDevice block_device;

void print_super_block()
{
    SuperBlock *sblock = (SuperBlock *)sblock_data;
    printk("Super Block:\n---------------------\n");
    printk("num_blocks: %u\n", sblock->num_blocks);
    printk("num_data_blocks: %u\n", sblock->num_data_blocks);
    printk("num_inodes: %u\n", sblock->num_inodes);
    printk("num_log_blocks: %u\n", sblock->num_log_blocks);
    printk("log_start: %u\n", sblock->log_start);
    printk("inode_start: %u\n", sblock->inode_start);
    printk("bitmap_start: %u\n", sblock->bitmap_start);
    printk("---------------------\n");
}

void init_block_device() {
    sd_read(1, sblock_data);
    print_super_block();
    block_device.read = sd_read;
    block_device.write = sd_write;
}

const SuperBlock *get_super_block() { return (const SuperBlock *)sblock_data; }