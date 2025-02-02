/*
 * This file is part of the Serial Flash Universal Driver Library.
 *
 * Copyright (c) 2018, zylx, <qgyhd1234@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Function: Portable interface for each platform.
 * Created on: 2018-11-23
 */

#include <sfud.h>
#include <stdarg.h>
#include <stdio.h>
#include <stm32h7xx_hal.h>
#include <stm32h7xx_hal_gpio.h>
#include "octospi.h"
#include "spi.h"
#include <string.h>

static const char *const TAG = "SFUD";

sfud_err qspi_write_read(
    const sfud_spi *spi,
    const uint8_t *write_buf, size_t write_size,
    uint8_t *read_buf, size_t read_size
);

sfud_err qspi_read(
    const struct __sfud_spi *spi, uint32_t addr,
    sfud_qspi_read_cmd_format *qspi_read_cmd_format,
    uint8_t *read_buf, size_t read_size
);

typedef struct {
    union {
        OSPI_HandleTypeDef *ospi_handle;
        SPI_HandleTypeDef *spi_handle;
    };
    uint32_t memory_mapped_addr;
    GPIO_TypeDef *cs_gpiox;
    uint16_t cs_gpio_pin;
} spi_user_data, *spi_user_data_t;

sfud_err qspi_send_then_recv(
    const sfud_spi *spi,
    const void *send_buf, size_t send_length,
    void *recv_buf, size_t recv_length
);

sfud_err qspi_entry_memory_mapped_mode(sfud_flash *flash);

sfud_err qspi_exit_memory_mapped_mode(sfud_flash *flash);


spi_user_data ospi1 = {
    .ospi_handle = &hospi1,
    .memory_mapped_addr = OCTOSPI1_BASE,
    .cs_gpiox = NULL,
    .cs_gpio_pin = 0
};
spi_user_data spi2 = {
    .spi_handle = &hspi2,
    .cs_gpiox = NULL,
    .cs_gpio_pin = 0
};

static char log_buf[256];

void sfud_log_info(const char *format, ...);

void sfud_log_debug(const char *file, const long line, const char *format, ...);

static void spi_lock(const sfud_spi *spi) {
    __disable_irq();
}

static void spi_unlock(const sfud_spi *spi) {
    __enable_irq();
}

/**
 * SPI write data then read data
 */
sfud_err qspi_write_read(
    const sfud_spi *spi,
    const uint8_t *write_buf, size_t write_size,
    uint8_t *read_buf, size_t read_size
) {
    sfud_err result = SFUD_SUCCESS;

    spi_user_data_t spi_dev = (spi_user_data_t) spi->user_data;

    if (write_size) {
        SFUD_ASSERT(write_buf);
    }
    if (read_size) {
        SFUD_ASSERT(read_buf);
    }

    bool in_memory_mapping = (spi_dev->ospi_handle->Instance->CR & OCTOSPI_CR_FMODE_Msk) == OCTOSPI_CR_FMODE;
//    uint8_t *write_ram_buf = NULL;
    if (in_memory_mapping) {
////        log_d("Memory Mapped Mode, Exit it");
//        // check the data to trans is in ext flash or not
//        if ((uint32_t) write_buf > spi_dev->memory_mapped_addr) {
//            return SFUD_ERR_WRITE;
////            log_d("write_buf is in ext flash, copy it to ram");
////            write_ram_buf = malloc(write_size);
////            memcpy(write_ram_buf, write_buf, write_size);
////            write_buf = write_ram_buf;
//        }
//        qspi_exit_memory_mapped_mode(spi_dev->ospi_handle);
        elog_e(TAG, "should not write when in memory mapping mode");
        return SFUD_ERR_WRITE;
    }

    /* reset cs pin */
    if (spi_dev->cs_gpiox != NULL)
        HAL_GPIO_WritePin(spi_dev->cs_gpiox, spi_dev->cs_gpio_pin, GPIO_PIN_RESET);

    if (write_size && read_size) {        /* read data */
        qspi_send_then_recv(spi, write_buf, write_size, read_buf, read_size);
    } else if (write_size) {        /* send data */
        qspi_send_then_recv(spi, write_buf, write_size, NULL, 0);
    }

    /* set cs pin */
    if (spi_dev->cs_gpiox != NULL)
        HAL_GPIO_WritePin(spi_dev->cs_gpiox, spi_dev->cs_gpio_pin, GPIO_PIN_SET);

//    if (in_memory_mapping) {
////        log_d("Enter Memory Mapped Mode");
//        qspi_entry_memory_mapped_mode(0);
//    }

//    if (write_ram_buf) {
//        free(write_ram_buf);
//    }

    return result;
}

/**
 * QSPI fast read data
 */
sfud_err qspi_read(
    const struct __sfud_spi *spi, uint32_t addr,
    sfud_qspi_read_cmd_format *qspi_read_cmd_format,
    uint8_t *read_buf, size_t read_size
) {
    sfud_err result = SFUD_SUCCESS;
    spi_user_data_t spi_dev = (spi_user_data_t) spi->user_data;

    if ((spi_dev->ospi_handle->Instance->CR & OCTOSPI_CR_FMODE_Msk) == OCTOSPI_CR_FMODE) {
        // in memory-mapping mode, just read it
        memcpy(read_buf, (uint8_t *) (spi_dev->memory_mapped_addr + addr), read_size);
        return result;
    }

    OSPI_RegularCmdTypeDef Cmdhandler;
    memset(&Cmdhandler, 0, sizeof(Cmdhandler));

    Cmdhandler.OperationType = HAL_OSPI_OPTYPE_COMMON_CFG;             // 通用配置
    Cmdhandler.FlashId = HAL_OSPI_FLASH_ID_1;                    // flash ID

    /* set cmd struct */
    Cmdhandler.Instruction = qspi_read_cmd_format->instruction;
    switch (qspi_read_cmd_format->instruction_lines) {
    case 0:
        Cmdhandler.InstructionMode = HAL_OSPI_INSTRUCTION_NONE;
        break;
    case 1:
        Cmdhandler.InstructionMode = HAL_OSPI_INSTRUCTION_1_LINE;
        break;
    case 2:
        Cmdhandler.InstructionMode = HAL_OSPI_INSTRUCTION_2_LINES;
        break;
    case 4:
        Cmdhandler.InstructionMode = HAL_OSPI_INSTRUCTION_4_LINES;
        break;
    case 8:
        Cmdhandler.InstructionMode = HAL_OSPI_INSTRUCTION_8_LINES;
        break;
    default:
        break;
    }
    Cmdhandler.InstructionSize = HAL_OSPI_INSTRUCTION_8_BITS;            // 指令长度8位
    Cmdhandler.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;       // 禁止指令DTR模式


    Cmdhandler.Address = addr;
    switch (qspi_read_cmd_format->address_lines) {
    case 0:
        Cmdhandler.AddressMode = HAL_OSPI_ADDRESS_NONE;
        break;
    case 1:
        Cmdhandler.AddressMode = HAL_OSPI_ADDRESS_1_LINE;
        break;
    case 2:
        Cmdhandler.AddressMode = HAL_OSPI_ADDRESS_2_LINES;
        break;
    case 4:
        Cmdhandler.AddressMode = HAL_OSPI_ADDRESS_4_LINES;
        break;
    case 8:
        Cmdhandler.AddressMode = HAL_OSPI_ADDRESS_8_LINES;
        break;
    default:
        break;
    }
    Cmdhandler.AddressSize = HAL_OSPI_ADDRESS_24_BITS;
    Cmdhandler.AddressDtrMode = HAL_OSPI_ADDRESS_DTR_DISABLE;

    Cmdhandler.AlternateBytes = 0;
    Cmdhandler.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    Cmdhandler.AlternateBytesSize = 0;
    Cmdhandler.AlternateBytesDtrMode = HAL_OSPI_ALTERNATE_BYTES_DTR_DISABLE;   // 禁止替字节DTR模式

    switch (qspi_read_cmd_format->data_lines) {
    case 0:
        Cmdhandler.DataMode = HAL_OSPI_DATA_NONE;
        break;
    case 1:
        Cmdhandler.DataMode = HAL_OSPI_DATA_1_LINE;
        break;
    case 2:
        Cmdhandler.DataMode = HAL_OSPI_DATA_2_LINES;
        break;
    case 4:
        Cmdhandler.DataMode = HAL_OSPI_DATA_4_LINES;
        break;
    case 8:
        Cmdhandler.DataMode = HAL_OSPI_DATA_8_LINES;
        break;
    default:
        break;
    }
    Cmdhandler.NbData = read_size;
    Cmdhandler.DataDtrMode = HAL_OSPI_DATA_DTR_DISABLE;

    Cmdhandler.DummyCycles = qspi_read_cmd_format->dummy_cycles;

    Cmdhandler.DQSMode = HAL_OSPI_DQS_DISABLE;                   // 不使用DQS
    Cmdhandler.SIOOMode = HAL_OSPI_SIOO_INST_EVERY_CMD;

    // 写配置
    if (HAL_OSPI_Command(spi_dev->ospi_handle, &Cmdhandler, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
//        sfud_log_info("qspi send cmd failed(%d)!", spi_dev->ospi_handle->ErrorCode);
        return SFUD_ERR_READ;
    }

    if (HAL_OSPI_Receive(spi_dev->ospi_handle, read_buf, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
//        sfud_log_info("qspi recv data failed(%d)!", spi_dev->ospi_handle->ErrorCode);
        result = SFUD_ERR_READ;
    }

    return result;
}

static sfud_err spi_write_read(
    const sfud_spi *spi,
    const uint8_t *write_buf, size_t write_size,
    uint8_t *read_buf, size_t read_size) {
    sfud_err result = SFUD_SUCCESS;
    spi_user_data_t spi_dev = (spi_user_data_t) spi->user_data;

    if (write_size) {
        SFUD_ASSERT(write_buf);
    }
    if (read_size) {
        SFUD_ASSERT(read_buf);
    }

    uint32_t buf_size = write_size + read_size;

    if (buf_size == 0) {
        return SFUD_ERR_WRITE;
    }

    uint8_t *send_buf = (uint8_t *) malloc(buf_size);
    uint8_t *recv_buf = (uint8_t *) malloc(buf_size);

    if (!send_buf || !recv_buf) {
        elog_e("SFUD", "malloc failed");
        return SFUD_ERR_WRITE;
    }

    memset(send_buf, SFUD_DUMMY_DATA, buf_size);
    memcpy(send_buf, write_buf, write_size);

    HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET);

    if (HAL_OK != HAL_SPI_TransmitReceive(spi_dev->spi_handle, send_buf, recv_buf, buf_size, 1000)) {
        free(send_buf);
        free(recv_buf);
        return SFUD_ERR_TIMEOUT;
    }

    HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);

    memcpy(read_buf, recv_buf + write_size, read_size);

    free(send_buf);
    free(recv_buf);

    return result;
}

/* about 100 microsecond delay */
static void retry_delay_100us(void) {
    uint32_t delay = 2400;
    while (delay--);
}

sfud_err sfud_spi_port_init(sfud_flash *flash) {
    // peripheral is inited by `MX_XXX_Init()`
    sfud_err result = SFUD_SUCCESS;

    switch (flash->index) {
    case SFUD_MAIN_FLASH: {
        /* set the interfaces and data */
        flash->spi.wr = qspi_write_read;
        flash->spi.qspi_read = qspi_read;
        flash->spi.lock = spi_lock;
        flash->spi.unlock = spi_unlock;
        flash->spi.user_data = &ospi1;
        /* about 100 microsecond delay */
        flash->retry.delay = retry_delay_100us;
        /* adout 60 seconds timeout */
        flash->retry.times = 60 * 10000;
        break;
    }
    case SFUD_EXT_FLASH: {
        /* set the interfaces and data */
        flash->spi.wr = spi_write_read;
        flash->spi.lock = spi_lock;
        flash->spi.unlock = spi_unlock;
        flash->spi.user_data = &spi2;
        /* about 100 microsecond delay */
        flash->retry.delay = retry_delay_100us;
        /* adout 60 seconds timeout */
        flash->retry.times = 60 * 10000;
        break;
    }
    }

    return result;
}

/**
 * This function is print debug info.
 *
 * @param file the file which has call this function
 * @param line the line number which has call this function
 * @param format output format
 * @param ... args
 */
void sfud_log_debug(const char *file, const long line, const char *format, ...) {
    va_list args;

    /* args point to the first variable parameter */
    va_start(args, format);
    elog_raw_output("[SFUD](%s:%ld) ", file, line);
    /* must use vprintf to print */
    vsnprintf(log_buf, sizeof(log_buf), format, args);
    elog_raw_output("%s\r\n", log_buf);
    va_end(args);
}

/**
 * This function is print routine info.
 *
 * @param format output format
 * @param ... args
 */
void sfud_log_info(const char *format, ...) {
    va_list args;

    /* args point to the first variable parameter */
    va_start(args, format);
    elog_raw_output("[SFUD]");
    /* must use vprintf to print */
    vsnprintf(log_buf, sizeof(log_buf), format, args);
    elog_raw_output("%s\r\n", log_buf);
    va_end(args);
}

/**
 * This function can send or send then receive QSPI data.
 */
sfud_err qspi_send_then_recv(
    const sfud_spi *spi,
    const void *send_buf, size_t send_length,
    void *recv_buf, size_t recv_length
) {
    assert_param(send_buf);
    assert_param(recv_buf);
    assert_param(send_length != 0);

    OSPI_RegularCmdTypeDef Cmdhandler;
    memset(&Cmdhandler, 0, sizeof(Cmdhandler));
    unsigned char *ptr = (unsigned char *) send_buf;
    size_t count = 0;
    spi_user_data_t spi_dev = (spi_user_data_t) spi->user_data;
    sfud_err result = SFUD_SUCCESS;

    Cmdhandler.OperationType = HAL_OSPI_OPTYPE_COMMON_CFG;             // 通用配置
    Cmdhandler.FlashId = HAL_OSPI_FLASH_ID_1;                    // flash ID

    /* get instruction */
    Cmdhandler.Instruction = ptr[0];
    Cmdhandler.InstructionMode = HAL_OSPI_INSTRUCTION_1_LINE;
    Cmdhandler.InstructionSize = HAL_OSPI_INSTRUCTION_8_BITS;            // 指令长度8位
    Cmdhandler.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;       // 禁止指令DTR模式

    count++;

    /* get address */
    if (send_length > 1) {
        if (send_length >= 4) {
            /* address size is 3 Byte */
            Cmdhandler.Address = (ptr[1] << 16) | (ptr[2] << 8) | (ptr[3]);
            Cmdhandler.AddressSize = HAL_OSPI_ADDRESS_24_BITS;
            count += 3;
        } else {
            return SFUD_ERR_READ;
        }
        Cmdhandler.AddressMode = HAL_OSPI_ADDRESS_1_LINE;
    } else {
        /* no address stage */
        Cmdhandler.Address = 0;
        Cmdhandler.AddressMode = HAL_OSPI_ADDRESS_NONE;
        Cmdhandler.AddressSize = 0;
    }
    Cmdhandler.AddressDtrMode = HAL_OSPI_ADDRESS_DTR_DISABLE;

    Cmdhandler.AlternateBytes = 0;
    Cmdhandler.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    Cmdhandler.AlternateBytesSize = 0;
    Cmdhandler.AlternateBytesDtrMode = HAL_OSPI_ALTERNATE_BYTES_DTR_DISABLE;   // 禁止替字节DTR模式

    Cmdhandler.DQSMode = HAL_OSPI_DQS_DISABLE;                   // 不使用DQS
    Cmdhandler.SIOOMode = HAL_OSPI_SIOO_INST_EVERY_CMD;

    if (send_buf && recv_buf) {
        /* recv data */
        /* set dummy cycles */
        if (count != send_length) {
            Cmdhandler.DummyCycles = (send_length - count) * 8;
        } else {
            Cmdhandler.DummyCycles = 0;
        }

        /* set recv size */
        Cmdhandler.DataMode = HAL_OSPI_DATA_1_LINE;
        Cmdhandler.NbData = recv_length;

        if (HAL_OSPI_Command(spi_dev->ospi_handle, &Cmdhandler, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
//            sfud_log_info("qspi send cmd failed(%d)!", spi_dev->ospi_handle->ErrorCode);
            return SFUD_ERR_READ;
        }

        if (recv_length != 0) {
            if (HAL_OSPI_Receive(spi_dev->ospi_handle, recv_buf, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
//                sfud_log_info("qspi recv data failed(%d)!", spi_dev->ospi_handle->ErrorCode);
                result = SFUD_ERR_READ;
            }
        }

        return result;
    } else {
        /* send data */
        /* set dummy cycles */
        Cmdhandler.DummyCycles = 0;

        /* determine if there is data to send */
        if (send_length - count > 0) {
            Cmdhandler.DataMode = HAL_OSPI_DATA_1_LINE;
        } else {
            Cmdhandler.DataMode = HAL_OSPI_DATA_NONE;
        }

        /* set send buf and send size */
        Cmdhandler.NbData = send_length - count;

        if (HAL_OSPI_Command(spi_dev->ospi_handle, &Cmdhandler, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
//            sfud_log_info("qspi send cmd failed(%d)!", spi_dev->ospi_handle->ErrorCode);
            return SFUD_ERR_READ;
        }

        if (send_length - count > 0) {
            if (HAL_OSPI_Transmit(spi_dev->ospi_handle, ptr + count, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
//                sfud_log_info("qspi send data failed(%d)!", spi_dev->ospi_handle->ErrorCode);
                result = SFUD_ERR_WRITE;
            }
        }

        return result;
    }
}

sfud_err qspi_entry_memory_mapped_mode(sfud_flash *flash) {
    OSPI_RegularCmdTypeDef Cmdhandler = {0};         // QSPI传输配置
    OSPI_MemoryMappedTypeDef sMemMappedCfg = {0};    // 内存映射访问参数


    Cmdhandler.OperationType = HAL_OSPI_OPTYPE_COMMON_CFG;             // 通用配置
    Cmdhandler.FlashId = HAL_OSPI_FLASH_ID_1;                    // flash ID

    /* set cmd struct */
    Cmdhandler.Instruction = flash->read_cmd_format.instruction;
    switch (flash->read_cmd_format.instruction_lines) {
    case 0:
        Cmdhandler.InstructionMode = HAL_OSPI_INSTRUCTION_NONE;
        break;
    case 1:
        Cmdhandler.InstructionMode = HAL_OSPI_INSTRUCTION_1_LINE;
        break;
    case 2:
        Cmdhandler.InstructionMode = HAL_OSPI_INSTRUCTION_2_LINES;
        break;
    case 4:
        Cmdhandler.InstructionMode = HAL_OSPI_INSTRUCTION_4_LINES;
        break;
    case 8:
        Cmdhandler.InstructionMode = HAL_OSPI_INSTRUCTION_8_LINES;
        break;
    default:
        break;
    }
    Cmdhandler.InstructionSize = HAL_OSPI_INSTRUCTION_8_BITS;            // 指令长度8位
    Cmdhandler.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;       // 禁止指令DTR模式


    switch (flash->read_cmd_format.address_lines) {
    case 0:
        Cmdhandler.AddressMode = HAL_OSPI_ADDRESS_NONE;
        break;
    case 1:
        Cmdhandler.AddressMode = HAL_OSPI_ADDRESS_1_LINE;
        break;
    case 2:
        Cmdhandler.AddressMode = HAL_OSPI_ADDRESS_2_LINES;
        break;
    case 4:
        Cmdhandler.AddressMode = HAL_OSPI_ADDRESS_4_LINES;
        break;
    case 8:
        Cmdhandler.AddressMode = HAL_OSPI_ADDRESS_8_LINES;
        break;
    default:
        break;
    }
    Cmdhandler.AddressSize = HAL_OSPI_ADDRESS_24_BITS;
    Cmdhandler.AddressDtrMode = HAL_OSPI_ADDRESS_DTR_DISABLE;

    Cmdhandler.AlternateBytes = 0;
    Cmdhandler.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    Cmdhandler.AlternateBytesSize = 0;
    Cmdhandler.AlternateBytesDtrMode = HAL_OSPI_ALTERNATE_BYTES_DTR_DISABLE;   // 禁止替字节DTR模式

    switch (flash->read_cmd_format.data_lines) {
    case 0:
        Cmdhandler.DataMode = HAL_OSPI_DATA_NONE;
        break;
    case 1:
        Cmdhandler.DataMode = HAL_OSPI_DATA_1_LINE;
        break;
    case 2:
        Cmdhandler.DataMode = HAL_OSPI_DATA_2_LINES;
        break;
    case 4:
        Cmdhandler.DataMode = HAL_OSPI_DATA_4_LINES;
        break;
    case 8:
        Cmdhandler.DataMode = HAL_OSPI_DATA_8_LINES;
        break;
    default:
        break;
    }
    Cmdhandler.NbData = 0;
    Cmdhandler.DataDtrMode = HAL_OSPI_DATA_DTR_DISABLE;

    Cmdhandler.DummyCycles = flash->read_cmd_format.dummy_cycles;

    Cmdhandler.DQSMode = HAL_OSPI_DQS_DISABLE;                   // 不使用DQS
    Cmdhandler.SIOOMode = HAL_OSPI_SIOO_INST_EVERY_CMD;

    spi_user_data_t spi_dev = (spi_user_data_t) flash->spi.user_data;
    if (HAL_OSPI_Command(spi_dev->ospi_handle, &Cmdhandler, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
//        sfud_log_info("QSPI Command Error!");
        return SFUD_ERR_READ;
    }

    sMemMappedCfg.TimeOutActivation = HAL_OSPI_TIMEOUT_COUNTER_DISABLE;
    sMemMappedCfg.TimeOutPeriod = 0;
    if (HAL_OSPI_MemoryMapped(spi_dev->ospi_handle, &sMemMappedCfg) != HAL_OK) {
//        sfud_log_info("QSPI Memory Mapped Error!");
        return SFUD_ERR_READ;
    }
    return SFUD_SUCCESS;
}

sfud_err qspi_exit_memory_mapped_mode(sfud_flash *flash) {
    spi_user_data_t spi_dev = (spi_user_data_t) flash->spi.user_data;
    HAL_OSPI_Abort(spi_dev->ospi_handle);
    return SFUD_SUCCESS;
}
