/*
 * QEMU SMBus EEPROM device
 *
 * Copyright (c) 2007 Arastra, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/core/boards.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/smbus_slave.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/i2c/smbus_eeprom.h"
#include "qom/object.h"

//#define DEBUG

#define TYPE_SMBUS_EEPROM "smbus-eeprom"

OBJECT_DECLARE_SIMPLE_TYPE(SMBusEEPROMDevice, SMBUS_EEPROM)

#define SMBUS_EEPROM_SIZE 256

struct SMBusEEPROMDevice {
    SMBusDevice smbusdev;
    uint8_t data[SMBUS_EEPROM_SIZE];
    uint8_t *init_data;
    uint8_t offset;
    bool accessed;
};

static uint8_t eeprom_receive_byte(SMBusDevice *dev)
{
    SMBusEEPROMDevice *eeprom = SMBUS_EEPROM(dev);
    uint8_t *data = eeprom->data;
    uint8_t val = data[eeprom->offset++];

    eeprom->accessed = true;
#ifdef DEBUG
    printf("eeprom_receive_byte: addr=0x%02x val=0x%02x\n",
           dev->i2c.address, val);
#endif
    return val;
}

static int eeprom_write_data(SMBusDevice *dev, uint8_t *buf, uint8_t len)
{
    SMBusEEPROMDevice *eeprom = SMBUS_EEPROM(dev);
    uint8_t *data = eeprom->data;

    eeprom->accessed = true;
#ifdef DEBUG
    printf("eeprom_write_byte: addr=0x%02x cmd=0x%02x val=0x%02x\n",
           dev->i2c.address, buf[0], buf[1]);
#endif
    /* len is guaranteed to be > 0 */
    eeprom->offset = buf[0];
    buf++;
    len--;

    for (; len > 0; len--) {
        data[eeprom->offset] = *buf++;
        eeprom->offset = (eeprom->offset + 1) % SMBUS_EEPROM_SIZE;
    }

    return 0;
}

static bool smbus_eeprom_vmstate_needed(void *opaque)
{
    MachineClass *mc = MACHINE_GET_CLASS(qdev_get_machine());
    SMBusEEPROMDevice *eeprom = opaque;

    return (eeprom->accessed || smbus_vmstate_needed(&eeprom->smbusdev)) &&
        !mc->smbus_no_migration_support;
}

static const VMStateDescription vmstate_smbus_eeprom = {
    .name = "smbus-eeprom",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = smbus_eeprom_vmstate_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_SMBUS_DEVICE(smbusdev, SMBusEEPROMDevice),
        VMSTATE_UINT8_ARRAY(data, SMBusEEPROMDevice, SMBUS_EEPROM_SIZE),
        VMSTATE_UINT8(offset, SMBusEEPROMDevice),
        VMSTATE_BOOL(accessed, SMBusEEPROMDevice),
        VMSTATE_END_OF_LIST()
    }
};

/*
 * Reset the EEPROM contents to the initial state on a reset.  This
 * isn't really how an EEPROM works, of course, but the general
 * principle of QEMU is to restore function on reset to what it would
 * be if QEMU was stopped and started.
 *
 * The proper thing to do would be to have a backing blockdev to hold
 * the contents and restore that on startup, and not do this on reset.
 * But until that time, act as if we had been stopped and restarted.
 */
static void smbus_eeprom_reset(DeviceState *dev)
{
    SMBusEEPROMDevice *eeprom = SMBUS_EEPROM(dev);

    memcpy(eeprom->data, eeprom->init_data, SMBUS_EEPROM_SIZE);
    eeprom->offset = 0;
}

static void smbus_eeprom_realize(DeviceState *dev, Error **errp)
{
    SMBusEEPROMDevice *eeprom = SMBUS_EEPROM(dev);

    smbus_eeprom_reset(dev);
    if (eeprom->init_data == NULL) {
        error_setg(errp, "init_data cannot be NULL");
    }
}

static void smbus_eeprom_class_initfn(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SMBusDeviceClass *sc = SMBUS_DEVICE_CLASS(klass);

    dc->realize = smbus_eeprom_realize;
    device_class_set_legacy_reset(dc, smbus_eeprom_reset);
    sc->receive_byte = eeprom_receive_byte;
    sc->write_data = eeprom_write_data;
    dc->vmsd = &vmstate_smbus_eeprom;
    /* Reason: init_data */
    dc->user_creatable = false;
}

static const TypeInfo smbus_eeprom_types[] = {
    {
        .name          = TYPE_SMBUS_EEPROM,
        .parent        = TYPE_SMBUS_DEVICE,
        .instance_size = sizeof(SMBusEEPROMDevice),
        .class_init    = smbus_eeprom_class_initfn,
    },
};

DEFINE_TYPES(smbus_eeprom_types)

void smbus_eeprom_init_one(I2CBus *smbus, uint8_t address, uint8_t *eeprom_buf)
{
    DeviceState *dev;

    dev = qdev_new(TYPE_SMBUS_EEPROM);
    qdev_prop_set_uint8(dev, "address", address);
    /* FIXME: use an array of byte or block backend property? */
    SMBUS_EEPROM(dev)->init_data = eeprom_buf;
    qdev_realize_and_unref(dev, (BusState *)smbus, &error_fatal);
}

void smbus_eeprom_init(I2CBus *smbus, int nb_eeprom,
                       const uint8_t *eeprom_spd, int eeprom_spd_size)
{
    int i;
     /* XXX: make this persistent */

    assert(nb_eeprom <= 8);
    uint8_t *eeprom_buf = g_malloc0(8 * SMBUS_EEPROM_SIZE);
    if (eeprom_spd_size > 0) {
        memcpy(eeprom_buf, eeprom_spd, eeprom_spd_size);
    }

    eeprom_buf[0]=0x92;
eeprom_buf[1]=0x10;
eeprom_buf[2]=0x0B;
eeprom_buf[3]=0x03;
eeprom_buf[4]=0x06;
eeprom_buf[5]=0x21;
eeprom_buf[6]=0x02;
eeprom_buf[7]=0x09;
eeprom_buf[8]=0x03;
eeprom_buf[9]=0x52;
eeprom_buf[0x0a]=0x01;
eeprom_buf[0x0b]=0x08;
eeprom_buf[0x0c]=0x0A;
eeprom_buf[0x0d]=0x00;
eeprom_buf[0x0e]=0xFE;
eeprom_buf[0x0f]=0x00;
eeprom_buf[0x10]=0x5A;
eeprom_buf[0x11]=0x78;
eeprom_buf[0x12]=0x5A;
eeprom_buf[0x13]=0x30;
eeprom_buf[0x14]=0x5A;
eeprom_buf[0x15]=0x11;
eeprom_buf[0x16]=0x0E;
eeprom_buf[0x17]=0x81;
eeprom_buf[0x18]=0x20;
eeprom_buf[0x19]=0x08;
eeprom_buf[0x1a]=0x3C;
eeprom_buf[0x1b]=0x3C;
eeprom_buf[0x1c]=0x00;
eeprom_buf[0x1d]=0xF0;
eeprom_buf[0x1e]=0x83;
eeprom_buf[0x1f]=0x81;
eeprom_buf[0x3c]=0x0F;
eeprom_buf[0x3d]=0x11;
eeprom_buf[0x3e]=0x65;
eeprom_buf[0x3f]=0x00;
eeprom_buf[0x70]=0x00;
eeprom_buf[0x71]=0x00;
eeprom_buf[0x72]=0x00;
eeprom_buf[0x73]=0x00;
eeprom_buf[0x74]=0x00;
eeprom_buf[0x75]=0x01;
eeprom_buf[0x76]=0x98;
eeprom_buf[0x77]=0x07;
eeprom_buf[0x78]=0x25;
eeprom_buf[0x79]=0x18;
eeprom_buf[0x7a]=0x00;
eeprom_buf[0x7b]=0x00;
eeprom_buf[0x7c]=0x00;
eeprom_buf[0x7d]=0x00;
eeprom_buf[0x7e]=0x3D;
eeprom_buf[0x7f]=0xA7;
eeprom_buf[0x80]=0x4B;
eeprom_buf[0x81]=0x48;
eeprom_buf[0x82]=0x58;
eeprom_buf[0x83]=0x31;
eeprom_buf[0x84]=0x36;
eeprom_buf[0x85]=0x30;
eeprom_buf[0x86]=0x30;
eeprom_buf[0x87]=0x43;
eeprom_buf[0x88]=0x39;
eeprom_buf[0x89]=0x53;
eeprom_buf[0x8a]=0x33;
eeprom_buf[0x8b]=0x4C;
eeprom_buf[0x8c]=0x2F;
eeprom_buf[0x8d]=0x33;
eeprom_buf[0x8e]=0x32;
eeprom_buf[0x8f]=0x47;
eeprom_buf[0x90]=0x20;
eeprom_buf[0x91]=0x20;
eeprom_buf[0x92]=0x00;
eeprom_buf[0x93]=0x00;
eeprom_buf[0x94]=0x00;
eeprom_buf[0x95]=0x00;
eeprom_buf[0xfe]=0x00;
eeprom_buf[0xff]=0x5A;
for (i = 0; i < nb_eeprom; i++) {
        smbus_eeprom_init_one(smbus, 0x50 + i,
                              eeprom_buf + (i * SMBUS_EEPROM_SIZE));
    }
}

/* Generate SDRAM SPD EEPROM data describing a module of type and size */
uint8_t *spd_data_generate(enum sdram_type type, ram_addr_t ram_size)
{
    uint8_t *spd;
    uint8_t nbanks;
    uint16_t density;
    uint32_t size;
    int min_log2, max_log2, sz_log2;
    int i;

    switch (type) {
    case SDR:
        min_log2 = 2;
        max_log2 = 9;
        break;
    case DDR:
        min_log2 = 5;
        max_log2 = 12;
        break;
    case DDR2:
        min_log2 = 7;
        max_log2 = 14;
        break;
    default:
        g_assert_not_reached();
    }
    size = ram_size >> 20; /* work in terms of megabytes */
    sz_log2 = 31 - clz32(size);
    size = 1U << sz_log2;
    assert(ram_size == size * MiB);
    assert(sz_log2 >= min_log2);

    nbanks = 1;
    while (sz_log2 > max_log2 && nbanks < 8) {
        sz_log2--;
        nbanks *= 2;
    }

    assert(size == (1ULL << sz_log2) * nbanks);

    /* split to 2 banks if possible to avoid a bug in MIPS Malta firmware */
    if (nbanks == 1 && sz_log2 > min_log2) {
        sz_log2--;
        nbanks++;
    }

    density = 1ULL << (sz_log2 - 2);
    switch (type) {
    case DDR2:
        density = (density & 0xe0) | (density >> 8 & 0x1f);
        break;
    case DDR:
        density = (density & 0xf8) | (density >> 8 & 0x07);
        break;
    case SDR:
    default:
        density &= 0xff;
        break;
    }

    spd = g_malloc0(256);
    spd[0] = 128;   /* data bytes in EEPROM */
    spd[1] = 8;     /* log2 size of EEPROM */
    spd[2] = type;
    spd[3] = 13;    /* row address bits */
    spd[4] = 10;    /* column address bits */
    spd[5] = (type == DDR2 ? nbanks - 1 : nbanks);
    spd[6] = 64;    /* module data width */
                    /* reserved / data width high */
    spd[8] = 4;     /* interface voltage level */
    spd[9] = 0x25;  /* highest CAS latency */
    spd[10] = 1;    /* access time */
                    /* DIMM configuration 0 = non-ECC */
    spd[12] = 0x82; /* refresh requirements */
    spd[13] = 8;    /* primary SDRAM width */
                    /* ECC SDRAM width */
    spd[15] = (type == DDR2 ? 0 : 1); /* reserved / delay for random col rd */
    spd[16] = 12;   /* burst lengths supported */
    spd[17] = 4;    /* banks per SDRAM device */
    spd[18] = 12;   /* ~CAS latencies supported */
    spd[19] = (type == DDR2 ? 0 : 1); /* reserved / ~CS latencies supported */
    spd[20] = 2;    /* DIMM type / ~WE latencies */
    spd[21] = (type < DDR2 ? 0x20 : 0); /* module features */
                    /* memory chip features */
    spd[23] = 0x12; /* clock cycle time @ medium CAS latency */
                    /* data access time */
                    /* clock cycle time @ short CAS latency */
                    /* data access time */
    spd[27] = 20;   /* min. row precharge time */
    spd[28] = 15;   /* min. row active row delay */
    spd[29] = 20;   /* min. ~RAS to ~CAS delay */
    spd[30] = 45;   /* min. active to precharge time */
    spd[31] = density;
    spd[32] = 20;   /* addr/cmd setup time */
    spd[33] = 8;    /* addr/cmd hold time */
    spd[34] = 20;   /* data input setup time */
    spd[35] = 8;    /* data input hold time */
    spd[36] = (type == DDR2 ? 13 << 2 : 0); /* min. write recovery time */

    /* checksum */
    for (i = 0; i < 63; i++) {
        spd[63] += spd[i];
    }
    return spd;
}
