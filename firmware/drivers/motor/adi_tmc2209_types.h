#pragma once
#include <zephyr/kernel.h>

#define DATA_LENGTH 4       // Number of bytes in the data portion of a packet
#define WR_PACKET_LENGTH 8  // packet length when writing to the IC
#define RD_PACKET_LENGTH 4  // packet length when requesting a read from the IC
#define RP_PACKET_LENGTH 8  // packet length when receiving a reply from the IC

#define REG_GCONF 0x00U
#define REG_GSTAT 0x01U
#define REG_IFCNT 0x02U
#define REG_NODECONF 0x03U
#define REG_IOIN 0x06U

#define REG_IHOLD_IRUN 0x10U

#define REG_CHOPCONF 0x6CU

struct __attribute__((packed)) IOIN_data_fields {
    uint32_t enn : 1;
    uint32_t : 1;
    uint32_t ms1 : 1;
    uint32_t ms2 : 1;
    uint32_t diag : 1;
    uint32_t : 1;
    uint32_t pdn_uart : 1;
    uint32_t step : 1;
    uint32_t spread_en : 1;
    uint32_t dir : 1;
    uint32_t : 14;
    uint32_t version : 8;
};

BUILD_ASSERT(sizeof(struct IOIN_data_fields) == DATA_LENGTH);

typedef enum rw_bit {
    READ = 0,
    WRITE = 1,
} rw_bit_t;

struct __attribute__((packed)) write_datagram_fields {
    uint8_t sync : 4;      // invariably 0b0101 = 0x5
    uint8_t reserved : 4;  // Doesn't matter but contributes to CRC

    uint8_t address;  // 0, 1, 2, or 3

    uint8_t reg_address : 7;
    rw_bit_t rw : 1;  // 1 for write

    uint8_t data[DATA_LENGTH];  // Data to write

    uint8_t crc;
};

typedef union write_datagram {
    struct write_datagram_fields fields;
    uint8_t raw[WR_PACKET_LENGTH];
} write_datagram_t;

BUILD_ASSERT(sizeof(struct write_datagram_fields) == sizeof(write_datagram_t));
BUILD_ASSERT(sizeof(write_datagram_t) == WR_PACKET_LENGTH);

struct __attribute__((packed)) read_datagram_fields {
    uint8_t sync : 4;      // invariably 0x5
    uint8_t reserved : 4;  // Doesn't matter but contributes to CRC

    uint8_t address;  // 0, 1, 2, or 3

    uint8_t reg_address : 7;
    rw_bit_t rw : 1;  // 0 for read

    uint8_t crc;
};

typedef union read_datagram {
    struct read_datagram_fields fields;
    uint8_t raw[DATA_LENGTH];
} read_datagram_t;

BUILD_ASSERT(sizeof(struct read_datagram_fields) == sizeof(read_datagram_t));
BUILD_ASSERT(sizeof(read_datagram_t) == RD_PACKET_LENGTH);

struct __attribute__((packed)) reply_datagram_fields {
    uint8_t sync : 4;      // invariably 0b0101
    uint8_t reserved : 4;  // Doesn't matter but contributes to CRC

    uint8_t address;  // master address: always 0b11111111

    uint8_t reg_address : 7;
    rw_bit_t rw : 1;  // 0 for read

    uint8_t data[DATA_LENGTH];  // Data read

    uint8_t crc;
};

typedef union reply_datagram {
    struct reply_datagram_fields fields;
    uint8_t raw[RP_PACKET_LENGTH];
} read_reply_datagram_t;

BUILD_ASSERT(sizeof(struct reply_datagram_fields) == sizeof(read_reply_datagram_t));
BUILD_ASSERT(sizeof(read_reply_datagram_t) == RP_PACKET_LENGTH);

/* Register addresses and contents tables */
/* Bytes are sent MSB first (so the bytes look backwards from the datasheet, but the bits are in the right order) */

struct __attribute__((packed)) GCONF_data_fields {
    uint8_t : 8;
    uint8_t : 8;

    uint8_t multistep_filt : 1;
    uint8_t test_mode_DO_NOT_USE : 1;
    uint8_t : 6;

    uint8_t i_scale_analog : 1;
    uint8_t internal_Rsense : 1;
    uint8_t en_spreadcycle : 1;
    uint8_t shaft : 1;
    uint8_t index_otpw : 1;
    uint8_t index_step : 1;
    uint8_t pdn_disable : 1;
    uint8_t mstep_reg_select : 1;
};

BUILD_ASSERT(sizeof(struct GCONF_data_fields) == DATA_LENGTH);

struct __attribute__((packed)) GSTAT_data_fields {
    uint8_t : 8;
    uint8_t : 8;
    uint8_t : 8;

    uint8_t reset : 1;
    uint8_t drv_err : 1;
    uint8_t uv_cp : 1;
};

BUILD_ASSERT(sizeof(struct GSTAT_data_fields) == DATA_LENGTH);

struct __attribute__((packed)) IHOLD_IRUN_data_fields {
    uint8_t : 8;
    uint8_t iholddelay : 4;  // 0 = instant power down, 1..15 = n * 2^18 clocks
    uint8_t : 4;
    uint8_t irun : 5;  // 0 = 1/32 ... 31 = 32/32
    uint8_t : 3;
    uint8_t ihold : 5;  // 0 = 1/32 ... 31 = 32/32
    uint8_t : 3;
};

BUILD_ASSERT(sizeof(struct IHOLD_IRUN_data_fields) == DATA_LENGTH);

struct __attribute__((packed)) NODECONF_data_fields {
    uint8_t : 8;
    uint8_t : 8;
    uint8_t send_delay : 4;  // delay before reply is sent
    uint8_t : 4;
    uint8_t : 8;
};

BUILD_ASSERT(sizeof(struct NODECONF_data_fields) == DATA_LENGTH);

struct __attribute__((packed)) CHOPCONF_data_fields {
    uint8_t mres : 4;
    uint8_t intpol : 1;
    uint8_t dedge : 1;
    uint8_t diss2g : 1;
    uint8_t dss2vs : 1;

    uint8_t tbl1 : 1;
    uint8_t vsense : 1;
    uint8_t : 6;

    uint8_t hend1 : 1;
    uint8_t hend2 : 1;
    uint8_t hend3 : 1;
    uint8_t : 4;
    uint8_t tbl0 : 1;

    uint8_t toff : 4;
    uint8_t hstrt : 3;
    uint8_t hend0 : 1;
};

BUILD_ASSERT(sizeof(struct CHOPCONF_data_fields) == DATA_LENGTH);
