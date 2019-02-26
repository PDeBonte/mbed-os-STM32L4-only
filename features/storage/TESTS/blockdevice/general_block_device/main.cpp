/* mbed Microcontroller Library
 * Copyright (c) 2018 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS //Required for PRIu64
#endif

#include "mbed.h"
#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"
#include "mbed_trace.h"
#include <inttypes.h>
#include <stdlib.h>
#include "BufferedBlockDevice.h"
#include "BlockDevice.h"

#if COMPONENT_SPIF
#include "SPIFBlockDevice.h"
#endif

#if COMPONENT_QSPIF
#include "QSPIFBlockDevice.h"
#endif

#if COMPONENT_DATAFLASH
#include "DataFlashBlockDevice.h"
#endif

#if COMPONENT_SD
#include "SDBlockDevice.h"
#endif

#if COMPONENT_FLASHIAP
#include "FlashIAPBlockDevice.h"
#endif

using namespace utest::v1;

#define TEST_BLOCK_COUNT 10
#define TEST_ERROR_MASK 16
#define TEST_NUM_OF_THREADS 5

const struct {
    const char *name;
    bd_size_t (BlockDevice::*method)() const;
} ATTRS[] = {
    {"read size",    &BlockDevice::get_read_size},
    {"program size", &BlockDevice::get_program_size},
    {"erase size",   &BlockDevice::get_erase_size},
    {"total size",   &BlockDevice::size},
};

enum bd_type {
    spif = 0,
    qspif,
    dataflash,
    sd,
    flashiap,
    default_bd
};

uint8_t bd_arr[5] = {0};

static uint8_t test_iteration = 0;

static SingletonPtr<PlatformMutex> _mutex;

BlockDevice *block_device = NULL;

#if COMPONENT_FLASHIAP
static inline uint32_t align_up(uint32_t val, uint32_t size)
{
    return (((val - 1) / size) + 1) * size;
}
#endif

static BlockDevice *get_bd_instance(uint8_t bd_type)
{
    switch (bd_arr[bd_type]) {
        case spif: {
#if COMPONENT_SPIF
            static SPIFBlockDevice default_bd(
                MBED_CONF_SPIF_DRIVER_SPI_MOSI,
                MBED_CONF_SPIF_DRIVER_SPI_MISO,
                MBED_CONF_SPIF_DRIVER_SPI_CLK,
                MBED_CONF_SPIF_DRIVER_SPI_CS,
                MBED_CONF_SPIF_DRIVER_SPI_FREQ
            );
            return &default_bd;
#endif
            break;
        }
        case qspif: {
#if COMPONENT_QSPIF
            static QSPIFBlockDevice default_bd(
                MBED_CONF_QSPIF_QSPI_IO0,
                MBED_CONF_QSPIF_QSPI_IO1,
                MBED_CONF_QSPIF_QSPI_IO2,
                MBED_CONF_QSPIF_QSPI_IO3,
                MBED_CONF_QSPIF_QSPI_SCK,
                MBED_CONF_QSPIF_QSPI_CSN,
                MBED_CONF_QSPIF_QSPI_POLARITY_MODE,
                MBED_CONF_QSPIF_QSPI_FREQ
            );
            return &default_bd;
#endif
            break;
        }
        case dataflash: {
#if COMPONENT_DATAFLASH
            static DataFlashBlockDevice default_bd(
                MBED_CONF_DATAFLASH_SPI_MOSI,
                MBED_CONF_DATAFLASH_SPI_MISO,
                MBED_CONF_DATAFLASH_SPI_CLK,
                MBED_CONF_DATAFLASH_SPI_CS
            );
            return &default_bd;
#endif
            break;
        }
        case sd: {
#if COMPONENT_SD
            static SDBlockDevice default_bd(
                MBED_CONF_SD_SPI_MOSI,
                MBED_CONF_SD_SPI_MISO,
                MBED_CONF_SD_SPI_CLK,
                MBED_CONF_SD_SPI_CS
            );
            return &default_bd;
#endif
            break;
        }
        case flashiap: {
#if COMPONENT_FLASHIAP
#if (MBED_CONF_FLASHIAP_BLOCK_DEVICE_SIZE == 0) && (MBED_CONF_FLASHIAP_BLOCK_DEVICE_BASE_ADDRESS == 0xFFFFFFFF)

            size_t flash_size;
            uint32_t start_address;
            uint32_t bottom_address;
            mbed::FlashIAP flash;

            int ret = flash.init();
            if (ret != 0) {
                return NULL;
            }

            //Find the start of first sector after text area
            bottom_address = align_up(FLASHIAP_APP_ROM_END_ADDR, flash.get_sector_size(FLASHIAP_APP_ROM_END_ADDR));
            start_address = flash.get_flash_start();
            flash_size = flash.get_flash_size();

            ret = flash.deinit();

            static FlashIAPBlockDevice default_bd(bottom_address, start_address + flash_size - bottom_address);

#else

            static FlashIAPBlockDevice default_bd;

#endif
            return &default_bd;
#endif
            break;
        }
    }
    return NULL;
}

// Mutex is protecting rand() per srand for buffer writing and verification.
// Mutex is also protecting printouts for clear logs.
// Mutex is NOT protecting Block Device actions: erase/program/read - which is the purpose of the multithreaded test!
void basic_erase_program_read_test(BlockDevice *block_device, bd_size_t block_size, uint8_t *write_block,
                                   uint8_t *read_block, unsigned addrwidth, int thread_num)
{
    int err = 0;
    _mutex->lock();

    // Make sure block address per each test is unique
    static unsigned block_seed = 1;
    srand(block_seed++);

    // Find a random block
    int threaded_rand_number = (rand() * TEST_NUM_OF_THREADS) + thread_num;
    bd_addr_t block = (threaded_rand_number * block_size) % block_device->size();

    // Flashiap boards with inconsistent sector size will not align with random start addresses
    if (bd_arr[test_iteration] == flashiap) {
        block = 0;
    }

    // Use next random number as temporary seed to keep
    // the address progressing in the pseudorandom sequence
    unsigned seed = rand();

    // Fill with random sequence
    srand(seed);
    for (bd_size_t i_ind = 0; i_ind < block_size; i_ind++) {
        write_block[i_ind] = 0xff & rand();
    }
    // Write, sync, and read the block
    utest_printf("test  %0*llx:%llu...\n", addrwidth, block, block_size);

    // Thread test for flashiap write to the same sector, so all write/read/erase actions should be locked
    if (bd_arr[test_iteration] != flashiap) {
        _mutex->unlock();
    }

    err = block_device->erase(block, block_size);
    TEST_ASSERT_EQUAL(0, err);

    err = block_device->program(write_block, block, block_size);
    TEST_ASSERT_EQUAL(0, err);

    err = block_device->read(read_block, block, block_size);
    TEST_ASSERT_EQUAL(0, err);

    if (bd_arr[test_iteration] != flashiap) {
        _mutex->lock();
    }

    // Check that the data was unmodified
    srand(seed);
    int val_rand;
    for (bd_size_t i_ind = 0; i_ind < block_size; i_ind++) {
        val_rand = rand();
        if ((0xff & val_rand) != read_block[i_ind]) {
            utest_printf("\n Assert Failed Buf Read - block:size: %llx:%llu \n", block, block_size);
            utest_printf("\n pos: %llu, exp: %02x, act: %02x, wrt: %02x \n", i_ind, (0xff & val_rand),
                         read_block[i_ind],
                         write_block[i_ind]);
        }
        TEST_ASSERT_EQUAL(0xff & val_rand, read_block[i_ind]);
    }
    _mutex->unlock();
}

void test_init_bd()
{
    utest_printf("\nTest Init block device.\n");

    block_device = get_bd_instance(test_iteration);

    TEST_SKIP_UNLESS_MESSAGE(block_device != NULL, "no block device found.");

    int err = block_device->init();
    TEST_ASSERT_EQUAL(0, err);
}

void test_random_program_read_erase()
{
    utest_printf("\nTest Random Program Read Erase Starts..\n");

    TEST_SKIP_UNLESS_MESSAGE(block_device != NULL, "no block device found.");

    for (unsigned atr = 0; atr < sizeof(ATTRS) / sizeof(ATTRS[0]); atr++) {
        static const char *prefixes[] = {"", "k", "M", "G"};
        for (int i_ind = 3; i_ind >= 0; i_ind--) {
            bd_size_t size = (block_device->*ATTRS[atr].method)();
            if (size >= (1ULL << 10 * i_ind)) {
                utest_printf("%s: %llu%sbytes (%llubytes)\n",
                             ATTRS[atr].name, size >> 10 * i_ind, prefixes[i_ind], size);
                break;
            }
        }
    }

    bd_size_t block_size = block_device->get_erase_size();
    unsigned addrwidth = ceil(log(float(block_device->size() - 1)) / log(float(16))) + 1;

    uint8_t *write_block = new (std::nothrow) uint8_t[block_size];
    uint8_t *read_block = new (std::nothrow) uint8_t[block_size];

    if (!write_block || !read_block) {
        utest_printf("Not enough memory for test\n");
        goto end;
    }

    for (int b = 0; b < TEST_BLOCK_COUNT; b++) {
        basic_erase_program_read_test(block_device, block_size, write_block, read_block, addrwidth, 0);
    }

end:
    delete[] write_block;
    delete[] read_block;
}

static void test_thread_job(void *block_device_ptr)
{
    static int thread_num = 0;
    _mutex->lock();
    int block_num = thread_num++;
    _mutex->unlock();
    BlockDevice *block_device = (BlockDevice *)block_device_ptr;

    bd_size_t block_size = block_device->get_erase_size();
    unsigned addrwidth = ceil(log(float(block_device->size() - 1)) / log(float(16))) + 1;

    uint8_t *write_block = new (std::nothrow) uint8_t[block_size];
    uint8_t *read_block = new (std::nothrow) uint8_t[block_size];

    if (!write_block || !read_block) {
        utest_printf("Not enough memory for test\n");
        goto end;
    }

    for (int b = 0; b < TEST_BLOCK_COUNT; b++) {
        basic_erase_program_read_test(block_device, block_size, write_block, read_block, addrwidth, block_num);
    }

end:
    delete[] write_block;
    delete[] read_block;
}

void test_multi_threads()
{
    utest_printf("\nTest Multi Threaded Erase/Program/Read Starts..\n");

    TEST_SKIP_UNLESS_MESSAGE(block_device != NULL, "no block device found.");

    char *dummy = new (std::nothrow) char[TEST_NUM_OF_THREADS * OS_STACK_SIZE];
    TEST_SKIP_UNLESS_MESSAGE(dummy, "Not enough memory for test.\n");
    delete[] dummy;

    for (unsigned atr = 0; atr < sizeof(ATTRS) / sizeof(ATTRS[0]); atr++) {
        static const char *prefixes[] = {"", "k", "M", "G"};
        for (int i_ind = 3; i_ind >= 0; i_ind--) {
            bd_size_t size = (block_device->*ATTRS[atr].method)();
            if (size >= (1ULL << 10 * i_ind)) {
                utest_printf("%s: %llu%sbytes (%llubytes)\n",
                             ATTRS[atr].name, size >> 10 * i_ind, prefixes[i_ind], size);
                break;
            }
        }
    }

    rtos::Thread bd_thread[TEST_NUM_OF_THREADS];

    osStatus threadStatus;
    int i_ind;

    for (i_ind = 0; i_ind < TEST_NUM_OF_THREADS; i_ind++) {
        threadStatus = bd_thread[i_ind].start(callback(test_thread_job, (void *)block_device));
        if (threadStatus != 0) {
            utest_printf("Thread %d Start Failed!\n", i_ind + 1);
        }
    }

    for (i_ind = 0; i_ind < TEST_NUM_OF_THREADS; i_ind++) {
        bd_thread[i_ind].join();
    }
}

void test_erase_functionality()
{
    utest_printf("\nTest BlockDevice::get_erase_value()..\n");

    // Test flow:
    //  1. Write data to selected region
    //    - Known starting point
    //  2. Erase selected region
    //  3. Read erased region and compare with get_erase_value()

    TEST_SKIP_UNLESS_MESSAGE(block_device != NULL, "no block device found.");

    // Check erase value
    int erase_value_int = block_device->get_erase_value();
    utest_printf("block_device->get_erase_value()=%d\n", erase_value_int);
    TEST_SKIP_UNLESS_MESSAGE(erase_value_int >= 0, "Erase not supported in this block device. Test skipped.");

    // Assuming that get_erase_value() returns byte value as documentation mentions
    // "If get_erase_value() returns a non-negative byte value" for unknown case.
    TEST_ASSERT(erase_value_int <= 255);
    uint8_t erase_value = (uint8_t)erase_value_int;

    // Determine data_buf_size
    bd_size_t erase_size = block_device->get_erase_size();
    TEST_ASSERT(erase_size > 0);
    bd_size_t data_buf_size = erase_size;

    // Determine start_address
    bd_addr_t start_address = rand();            // low 32 bytes
    start_address += (uint64_t)rand() << 32;     // high 32 bytes
    start_address %= block_device->size() - data_buf_size - erase_size; //  fit all data + alignment reserve
    start_address += erase_size;                 // add alignment reserve
    start_address -= start_address % erase_size; // align with erase_block
    utest_printf("start_address=0x%016" PRIx64 "\n", start_address);

    // Flashiap boards with inconsistent sector size will not align with random start addresses
    if (bd_arr[test_iteration] == flashiap) {
        start_address = 0;
    }

    // Allocate buffer for write test data
    uint8_t *data_buf = (uint8_t *)malloc(data_buf_size);
    TEST_SKIP_UNLESS_MESSAGE(data_buf, "Not enough memory for test.\n");

    // Allocate buffer for read test data
    uint8_t *out_data_buf = (uint8_t *)malloc(data_buf_size);
    TEST_SKIP_UNLESS_MESSAGE(out_data_buf, "Not enough memory for test.\n");

    // First must Erase given memory region
    utest_printf("erasing given memory region\n");
    int err = block_device->erase(start_address, data_buf_size);
    TEST_ASSERT_EQUAL(0, err);

    // Write random data to selected region to make sure data is not accidentally set to "erased" value.
    // With this pre-write, the test case will fail even if block_device->erase() is broken.
    for (bd_size_t i = 0; i < data_buf_size; i++) {
        data_buf[i] = (uint8_t) rand();
    }

    utest_printf("writing given memory region\n");
    err = block_device->program((const void *)data_buf, start_address, data_buf_size);
    TEST_ASSERT_EQUAL(0, err);

    // Read written memory region to verify it contains information
    memset(out_data_buf, 0, data_buf_size);
    utest_printf("reading written memory region\n");
    err = block_device->read((void *)out_data_buf, start_address, data_buf_size);
    TEST_ASSERT_EQUAL(0, err);

    // Verify erased memory region
    utest_printf("verifying written memory region\n");
    for (bd_size_t i = 0; i < data_buf_size; i++) {
        TEST_ASSERT_EQUAL(out_data_buf[i], data_buf[i]);
    }

    // Erase given memory region
    utest_printf("erasing written memory region\n");
    err = block_device->erase(start_address, data_buf_size);
    TEST_ASSERT_EQUAL(0, err);

    // Read erased memory region
    utest_printf("reading erased memory region\n");
    memset(out_data_buf, 0, data_buf_size);
    err = block_device->read((void *)out_data_buf, start_address, data_buf_size);
    TEST_ASSERT_EQUAL(0, err);

    // Verify erased memory region
    utest_printf("verifying erased memory region\n");
    for (bd_size_t i = 0; i < data_buf_size; i++) {
        TEST_ASSERT_EQUAL(erase_value, out_data_buf[i]);
    }

    free(data_buf);
    free(out_data_buf);
}

void test_contiguous_erase_write_read()
{
    utest_printf("\nTest Contiguous Erase/Program/Read Starts..\n");

    TEST_SKIP_UNLESS_MESSAGE(block_device != NULL, "no block device found.");

    // Flashiap boards with inconsistent sector size will not align with random start addresses
    if (bd_arr[test_iteration] == flashiap) {
        return;
    }

    // Test flow:
    //  1. Erase whole test area
    //    - Tests contiguous erase
    //  2. Write smaller memory area
    //    - Tests contiguous sector writes
    //  3. Return step 2 for whole erase region

    // Test parameters
    bd_size_t erase_size = block_device->get_erase_size();
    TEST_ASSERT(erase_size > 0);
    bd_size_t program_size = block_device->get_program_size();
    TEST_ASSERT(program_size > 0);
    utest_printf("erase_size=%" PRId64 "\n", erase_size);
    utest_printf("program_size=%" PRId64 "\n", program_size);
    utest_printf("block_device->size()=%" PRId64 "\n", block_device->size());

    // Determine write/read buffer size
    //  start write_read_buf_size from 1% block_device->size()
    bd_size_t write_read_buf_size = block_device->size() / 100; // 1%, 10k=100, 100k=1k, 1MB=10k, 32MB=32k
    //  try to limit write_read_buf_size to 10k. If program_size*2 is larger than 10k, that will be used instead.
    if (write_read_buf_size > 10000) {
        write_read_buf_size = 10000;
    }
    //  2 program_size blocks is minimum for contiguous write/read test
    if (write_read_buf_size < program_size * 2) {
        write_read_buf_size = program_size * 2; // going over 10k
    }
    bd_size_t contiguous_write_read_blocks_per_region = write_read_buf_size /
                                                        program_size; // 2 is minimum to test contiguous write
    write_read_buf_size = contiguous_write_read_blocks_per_region * program_size;
    utest_printf("contiguous_write_read_blocks_per_region=%" PRIu64 "\n", contiguous_write_read_blocks_per_region);
    utest_printf("write_read_buf_size=%" PRIu64 "\n", write_read_buf_size);

    // Determine test region count
    int contiguous_write_read_regions = TEST_BLOCK_COUNT;
    utest_printf("contiguous_write_read_regions=%d\n", contiguous_write_read_regions);

    // Determine whole erase size
    bd_size_t contiguous_erase_size = write_read_buf_size * contiguous_write_read_regions;
    contiguous_erase_size -= contiguous_erase_size % erase_size; // aligned to erase_size
    contiguous_erase_size += erase_size; // but larger than write/read size * regions
    utest_printf("contiguous_erase_size=%" PRIu64 "\n", contiguous_erase_size);

    // Determine starting address
    bd_addr_t start_address = rand();            // low 32 bytes
    start_address += (uint64_t)rand() << 32;     // high 32 bytes
    start_address %= block_device->size() - contiguous_erase_size - erase_size; //  fit all data + alignment reserve
    start_address += erase_size;                 // add alignment reserve
    start_address -= start_address % erase_size; // align with erase_block
    bd_addr_t stop_address = start_address + write_read_buf_size * contiguous_write_read_regions;
    utest_printf("start_address=0x%016" PRIx64 "\n", start_address);
    utest_printf("stop_address=0x%016" PRIx64 "\n", stop_address);

    // Allocate write/read buffer
    uint8_t *write_read_buf = (uint8_t *)malloc(write_read_buf_size);
    if (write_read_buf == NULL) {
        TEST_SKIP_MESSAGE("not enough memory for test");
    }
    utest_printf("write_read_buf_size=%" PRIu64 "\n", (uint64_t)write_read_buf_size);

    // Must Erase the whole region first
    utest_printf("erasing memory, from 0x%" PRIx64 " of size 0x%" PRIx64 "\n", start_address, contiguous_erase_size);
    int err = block_device->erase(start_address, contiguous_erase_size);
    TEST_ASSERT_EQUAL(0, err);

    // Pre-fill the to-be-erased region. By pre-filling the region,
    // we can be sure the test will not pass if the erase doesn't work.
    for (bd_size_t offset = 0; start_address + offset < stop_address; offset += write_read_buf_size) {
        for (size_t i = 0; i < write_read_buf_size; i++) {
            write_read_buf[i] = (uint8_t)rand();
        }
        utest_printf("pre-filling memory, from 0x%" PRIx64 " of size 0x%" PRIx64 "\n", start_address + offset,
                     write_read_buf_size);
        err = block_device->program((const void *)write_read_buf, start_address + offset, write_read_buf_size);
        TEST_ASSERT_EQUAL(0, err);
    }

    // Erase the whole region again
    utest_printf("erasing memory, from 0x%" PRIx64 " of size 0x%" PRIx64 "\n", start_address, contiguous_erase_size);
    err = block_device->erase(start_address, contiguous_erase_size);
    TEST_ASSERT_EQUAL(0, err);

    // Loop through all write/read regions
    int region = 0;
    for (; start_address < stop_address; start_address += write_read_buf_size) {
        utest_printf("\nregion #%d start_address=0x%016" PRIx64 "\n", region++, start_address);

        // Generate test data
        unsigned int seed = rand();
        utest_printf("generating test data, seed=%u\n", seed);
        srand(seed);
        for (size_t i = 0; i < write_read_buf_size; i++) {
            write_read_buf[i] = (uint8_t)rand();
        }

        // Write test data
        utest_printf("writing test data\n");
        err = block_device->program((const void *)write_read_buf, start_address, write_read_buf_size);
        TEST_ASSERT_EQUAL(0, err);

        // Read test data
        memset(write_read_buf, 0, (size_t)write_read_buf_size);
        utest_printf("reading test data\n");
        err = block_device->read(write_read_buf, start_address, write_read_buf_size);
        TEST_ASSERT_EQUAL(0, err);

        // Verify read data
        utest_printf("verifying test data\n");
        srand(seed);
        for (size_t i = 0; i < write_read_buf_size; i++) {
            uint8_t expected_value = (uint8_t)rand();
            if (write_read_buf[i] != expected_value) {
                utest_printf("data verify failed, write_read_buf[%d]=%" PRIu8 " and not %" PRIu8 "\n",
                             i, write_read_buf[i], expected_value);
            }
            TEST_ASSERT_EQUAL(write_read_buf[i], expected_value);
        }
        utest_printf("verify OK\n");
    }

    free(write_read_buf);
}

void test_program_read_small_data_sizes()
{
    utest_printf("\nTest program-read small data sizes, from 1 to 7 bytes..\n");

    TEST_SKIP_UNLESS_MESSAGE(block_device != NULL, "no block device found.");

    bd_size_t erase_size = block_device->get_erase_size();
    bd_size_t program_size = block_device->get_program_size();
    bd_size_t read_size = block_device->get_read_size();
    TEST_ASSERT(program_size > 0);

    // See that we have enough memory for buffered block device
    char *dummy = new (std::nothrow) char[program_size + read_size];
    TEST_SKIP_UNLESS_MESSAGE(dummy, "Not enough memory for test.\n");
    delete[] dummy;

    // use BufferedBlockDevice for better handling of block devices program and read
    BufferedBlockDevice *buff_block_device = new BufferedBlockDevice(block_device);

    // BlockDevice initialization
    int err = buff_block_device->init();
    TEST_ASSERT_EQUAL(0, err);

    const char write_buffer[] = "1234567";
    char read_buffer[7] = {};

    // Determine starting address
    bd_addr_t start_address = 0;

    for (int i = 1; i <= 7; i++) {
        err = buff_block_device->erase(start_address, erase_size);
        TEST_ASSERT_EQUAL(0, err);

        err = buff_block_device->program((const void *)write_buffer, start_address, i);
        TEST_ASSERT_EQUAL(0, err);

        err = buff_block_device->sync();
        TEST_ASSERT_EQUAL(0, err);

        err = buff_block_device->read(read_buffer, start_address, i);
        TEST_ASSERT_EQUAL(0, err);

        err = memcmp(write_buffer, read_buffer, i);
        TEST_ASSERT_EQUAL(0, err);
    }

    // BlockDevice deinitialization
    err = buff_block_device->deinit();
    TEST_ASSERT_EQUAL(0, err);

    delete buff_block_device;
}


void test_unaligned_erase_blocks()
{

    utest_printf("\nTest Unaligned Erase Starts..\n");

    TEST_SKIP_UNLESS_MESSAGE(block_device != NULL, "no block device found.");

    TEST_SKIP_UNLESS_MESSAGE(block_device->get_erase_value() != -1, "block device has no erase functionality.");

    bd_addr_t addr = 0;
    bd_size_t sector_erase_size = block_device->get_erase_size(addr);
    unsigned addrwidth = ceil(log(float(block_device->size() - 1)) / log(float(16))) + 1;

    utest_printf("\ntest  %0*llx:%llu...", addrwidth, addr, sector_erase_size);

    //unaligned start address
    addr += 1;
    int err = block_device->erase(addr, sector_erase_size - 1);
    TEST_ASSERT_NOT_EQUAL(0, err);

    err = block_device->erase(addr, sector_erase_size);
    TEST_ASSERT_NOT_EQUAL(0, err);

    err = block_device->erase(addr, 1);
    TEST_ASSERT_NOT_EQUAL(0, err);

    //unaligned end address
    addr = 0;

    err = block_device->erase(addr, 1);
    TEST_ASSERT_NOT_EQUAL(0, err);

    err = block_device->erase(addr, sector_erase_size + 1);
    TEST_ASSERT_NOT_EQUAL(0, err);

    //erase size exceeds flash device size
    err = block_device->erase(addr, block_device->size() + 1);
    TEST_ASSERT_NOT_EQUAL(0, err);

    // Valid erase
    err = block_device->erase(addr, sector_erase_size);
    TEST_ASSERT_EQUAL(0, err);
}

void test_deinit_bd()
{
    utest_printf("\nTest deinit block device.\n");

    test_iteration++;

    TEST_SKIP_UNLESS_MESSAGE(block_device != NULL, "no block device found.");

    int err = block_device->deinit();
    TEST_ASSERT_EQUAL(0, err);

    block_device = NULL;
}

void test_get_type_functionality()
{
    utest_printf("\nTest get blockdevice type..\n");

    block_device = BlockDevice::get_default_instance();
    TEST_SKIP_UNLESS_MESSAGE(block_device != NULL, "no block device found.");

    const char *bd_type = block_device->get_type();
    TEST_ASSERT_NOT_EQUAL(0, bd_type);

#if COMPONENT_QSPIF
    TEST_ASSERT_EQUAL(0, strcmp(bd_type, "QSPIF"));
#elif COMPONENT_SPIF
    TEST_ASSERT_EQUAL(0, strcmp(bd_type, "SPIF"));
#elif COMPONENT_DATAFLASH
    TEST_ASSERT_EQUAL(0, strcmp(bd_type, "DATAFLASH"));
#elif COMPONENT_SD
    TEST_ASSERT_EQUAL(0, strcmp(bd_type, "SD"));
#elif COMPONET_FLASHIAP
    TEST_ASSERT_EQUAL(0, strcmp(bd_type, "FLASHIAP"));
#endif
}

utest::v1::status_t greentea_failure_handler(const Case *const source, const failure_t reason)
{
    greentea_case_failure_abort_handler(source, reason);
    return STATUS_CONTINUE;
}

typedef struct {
    const char *description;
    const case_handler_t case_handler;
    const case_failure_handler_t failure_handler;
} template_case_t;

template_case_t template_cases[] = {
    {"Testing Init block device", test_init_bd, greentea_failure_handler},
    {"Testing read write random blocks", test_random_program_read_erase, greentea_failure_handler},
    {"Testing multi threads erase program read", test_multi_threads, greentea_failure_handler},
    {"Testing contiguous erase, write and read", test_contiguous_erase_write_read, greentea_failure_handler},
    {"Testing BlockDevice erase functionality", test_erase_functionality, greentea_failure_handler},
    {"Testing program read small data sizes", test_program_read_small_data_sizes, greentea_failure_handler},
    {"Testing unaligned erase blocks", test_unaligned_erase_blocks, greentea_failure_handler},
    {"Testing Deinit block device", test_deinit_bd, greentea_failure_handler},
};

template_case_t def_template_case = {"Testing get type functionality", test_get_type_functionality, greentea_failure_handler};

utest::v1::status_t greentea_test_setup(const size_t number_of_cases)
{
    return greentea_test_setup_handler(number_of_cases);
}

int get_bd_count()
{
    int count = 0;

#if COMPONENT_SPIF
    bd_arr[count++] = spif;           //0
#endif
#if COMPONENT_QSPIF
    bd_arr[count++] = qspif;          //1
#endif
#if COMPONENT_DATAFLASH
    bd_arr[count++] = dataflash;      //2
#endif
#if COMPONENT_SD
    bd_arr[count++] = sd;             //3
#endif
#if COMPONENT_FLASHIAP
    bd_arr[count++] = flashiap;       //4
#endif

    return count;
}

static const char *prefix[] = {"SPIF ", "QSPIF ", "DATAFLASH ", "SD ", "FLASHIAP ", "DEFAULT "};

int main()
{
    GREENTEA_SETUP(3000, "default_auto");

    // We want to replicate our test cases to different types
    size_t num_cases = sizeof(template_cases) / sizeof(template_case_t);
    size_t total_num_cases = 0;

    int bd_count = get_bd_count();

    void *raw_mem = new (std::nothrow) uint8_t[(bd_count * num_cases + 1) * sizeof(Case)];
    Case *cases = static_cast<Case *>(raw_mem);

    for (int j = 0; j < bd_count; j++) {
        for (size_t i = 0; i < num_cases; i++) {
            char desc[128], *desc_ptr;
            sprintf(desc, "%s%s", prefix[bd_arr[j]], template_cases[i].description);
            desc_ptr = new char[strlen(desc) + 1];
            strcpy(desc_ptr, desc);
            new (&cases[total_num_cases]) Case((const char *) desc_ptr, template_cases[i].case_handler,
                                               template_cases[i].failure_handler);
            total_num_cases++;
        }

        //Add test_get_type_functionality once, runs on default blockdevice
        if (j == bd_count - 1) {
            char desc[128], *desc_ptr;
            sprintf(desc, "%s%s", prefix[default_bd], def_template_case.description);
            desc_ptr = new char[strlen(desc) + 1];
            strcpy(desc_ptr, desc);
            new (&cases[total_num_cases]) Case((const char *) desc_ptr, def_template_case.case_handler,
                                               def_template_case.failure_handler);
            total_num_cases++;
        }
    }



    Specification specification(greentea_test_setup, cases, total_num_cases,
                                greentea_test_teardown_handler, (test_failure_handler_t)greentea_failure_handler);

    return !Harness::run(specification);
}
