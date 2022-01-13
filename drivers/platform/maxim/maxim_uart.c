/***************************************************************************//**
 *   @file   maxim/maxim_uart.c
 *   @brief  Source file of UART driver for STM32
 *   @author Ciprian Regus (ciprian.regus@analog.com)
********************************************************************************
 * Copyright 2022(c) Analog Devices, Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Analog Devices, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *  - The use of this software may or may not infringe the patent rights
 *    of one or more patent holders.  This license does not release you
 *    from the requirement that you obtain separate licenses from these
 *    patent holders to use this software.
 *  - Use of the software either in source or binary form, must be run
 *    on or directly connected to an Analog Devices Inc. component.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, NON-INFRINGEMENT,
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL ANALOG DEVICES BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, INTELLECTUAL PROPERTY RIGHTS, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

/******************************************************************************/
/************************* Include Files **************************************/
/******************************************************************************/

#include <errno.h>
#include <stdlib.h>
#include "maxim_stdio.h"
#include "mxc_sys.h"
#include "uart.h"
#include "maxim_uart.h"
#include "no-os/uart.h"
#include "no-os/irq.h"
#include "no-os/util.h"

/**
* @brief Callback descriptor that contains a function to be called when an
* interrupt occurs.
*/
static struct callback_desc *cb[2];

/******************************************************************************/
/************************ Functions Definitions *******************************/
/******************************************************************************/

void UART0_IRQHandler()
{
	if(!cb[0])
		return;

	mxc_uart_regs_t *uart_regs = MXC_UART0;
	uint8_t	n_int = 0;
	volatile uint32_t reg_int = uart_regs->int_fl;

	/** Clear all interrupt flags */
	uart_regs->int_fl = 0x0;

	while(reg_int) {
		if((reg_int & 1) && uart_regs->int_en && BIT(n_int)) {
			void *ctx = cb[0]->ctx;
			void *config = cb[0]->config;
			cb[0]->callback(ctx, n_int, config);
		}
		n_int++;
		reg_int >>= 1;
	}
}

void UART1_IRQHandler()
{
	if(!cb[1])
		return;

	mxc_uart_regs_t *uart_regs = MXC_UART1;
	uint8_t	n_int = 0;
	volatile uint32_t reg_int = uart_regs->int_fl;

	/** Clear all interrupt flags */
	uart_regs->int_fl = 0x0;

	while(reg_int) {
		if((reg_int & 1) && (uart_regs->int_en & BIT(n_int))) {
			void *ctx = cb[1]->ctx;
			void *config = cb[1]->config;
			cb[1]->callback(ctx, n_int, config);
		}
		n_int++;
		reg_int >>= 1;
	}
}

/**
 * @brief Read data from UART device. Blocking function.
 * @param desc - Instance of UART.
 * @param data - Pointer to buffer containing data.
 * @param bytes_number - Number of bytes to read.
 * @return positive number of received bytes in case of success, negative error code otherwise.
 */
int32_t uart_read(struct uart_desc *desc, uint8_t *data, uint32_t bytes_number)
{
	if(!desc || !data || !bytes_number)
		return -EINVAL;

	int32_t err = 0;
	uint32_t bytes_read = 0;

	int32_t len = bytes_number;
	err = UART_Read(MXC_UART_GET_UART(desc->device_id), data, len, &bytes_read);

	if(err < 0)
		return -EIO;

	return bytes_read;
}

/**
 * @brief Write data to UART device. Blocking function.
 * @param desc - Instance of UART.
 * @param data - Pointer to buffer containing data.
 * @param bytes_number - Number of bytes to read.
 * @return SUCCESS in case of success, FAILURE otherwise.
 */
int32_t uart_write(struct uart_desc *desc, const uint8_t *data,
		   uint32_t bytes_number)
{
	if(!desc || !data || !bytes_number)
		return -EINVAL;

	uint32_t err = 0;

	int32_t len = bytes_number;
	err = UART_Write(MXC_UART_GET_UART(desc->device_id), data, len);

	if(err < 0)
		return -EIO;

	return 0;
}

/**
 * @brief Read data from UART device. Non blocking function.
 * @param desc - Instance of UART.
 * @param data - Pointer to buffer containing data.
 * @param bytes_number - Number of bytes to read.
 * @return positive number of received bytes in case of success, negative error code otherwise.
 */
int32_t uart_read_nonblocking(struct uart_desc *desc, uint8_t *data,
			      uint32_t bytes_number)
{
	if(!desc || !data || !bytes_number)
		return -EINVAL;

	uint32_t err = 0;
	uart_req_t read_req;

	read_req.data = data;
	read_req.len = bytes_number;
	read_req.callback = NULL;
	err = UART_ReadAsync(MXC_UART_GET_UART(desc->device_id), &read_req);

	return err;
}

/**
 * @brief Write data to UART device. Non blocking function.
 * @param desc - Instance of UART.
 * @param data - Pointer to buffer containing data.
 * @param bytes_number - Number of bytes to read.
 * @return 0 in case of success, errno codes otherwise.
 */

int32_t uart_write_nonblocking(struct uart_desc *desc, const uint8_t *data,
			       uint32_t bytes_number)
{
	if(!desc || !data || !bytes_number)
		return -EINVAL;

	uint32_t err = 0;
	uart_req_t write_req;

	write_req.data = data;
	write_req.len = bytes_number;
	write_req.callback = NULL;
	err = UART_WriteAsync(MXC_UART_GET_UART(desc->device_id), &write_req);

	return err;
}

/**
 * @brief Initialize the UART communication peripheral.
 * @param desc - The UART descriptor.
 * @param param - The structure that contains the UART parameters.
 * @return 0 in case of success, errno codes otherwise.
 */
int32_t uart_init(struct uart_desc **desc, struct uart_init_param *param)
{
	if(!param || !desc)
		return -EINVAL;
	int ret = 0;
	uint32_t did = param->device_id;

	struct maxim_uart_desc *maxim_uart = calloc(1, sizeof(*maxim_uart));
	struct uart_desc *descriptor = calloc(1, sizeof(*descriptor));

	uart_cfg_t maxim_desc;
	sys_cfg_uart_t maxim_desc_sys;

	if(!maxim_uart || !descriptor) {
		ret = -ENOMEM;
		goto error;
	}

	NVIC_ClearPendingIRQ(MXC_UART_GET_IRQ(did));
	NVIC_DisableIRQ(MXC_UART_GET_IRQ(did));
	NVIC_SetPriority(MXC_UART_GET_IRQ(did), 1);
	NVIC_EnableIRQ(MXC_UART_GET_IRQ(did));

	maxim_desc_sys.map = MAP_A;
	maxim_desc_sys.flow_flag = UART_FLOW_DISABLE;

	descriptor->extra = maxim_uart;
	descriptor->baud_rate = param->baud_rate;
	descriptor->device_id = param->device_id;

	maxim_desc.parity = UART_PARITY_DISABLE;

	switch(param->size) {
	case UART_CS_5:
		maxim_desc.size = UART_DATA_SIZE_5_BITS;
		break;
	case UART_CS_6:
		maxim_desc.size = UART_DATA_SIZE_6_BITS;
		break;
	case UART_CS_7:
		maxim_desc.size = UART_DATA_SIZE_7_BITS;
		break;
	case UART_CS_8:
		maxim_desc.size = UART_DATA_SIZE_8_BITS;
		break;
	default:
		ret = -EINVAL;
		goto error;
	}

	switch(param->stop) {
	case UART_STOP_1_BIT:
		maxim_desc.stop = UART_STOP_1;
		break;
	case UART_STOP_2_BIT:
		maxim_desc.stop = UART_STOP_2;
		break;
	default:
		ret = -EINVAL;
		goto error;
	}

	maxim_desc.baud = param->baud_rate;
	maxim_desc.flow = UART_FLOW_CTRL_EN;
	maxim_desc.pol = UART_FLOW_POL_EN;

	uint32_t err = UART_Init(MXC_UART_GET_UART(did), &maxim_desc, &maxim_desc_sys);
	if(err != E_NO_ERROR) {
		ret = -EIO;
		goto error;
	}

	*desc = descriptor;
	return 0;
error:
	free(descriptor);
	free(maxim_uart);

	return ret;
}

/**
 * @brief Free the resources allocated by uart_init().
 * @param desc - The UART descriptor.
 * @return 0 in case of success, errno codes otherwise.
 */
int32_t uart_remove(struct uart_desc *desc)
{
	if(!desc)
		return -EINVAL;

	uart_unregister_callback(desc->device_id);
	free(desc);

	return 0;
}

/* Check if UART errors occurred. */
uint32_t uart_get_errors(struct uart_desc *desc)
{
	return 0;
}

/**
 * @brief Register a function to be called when an interrupt occurs.
 * @param port - The UART port.
 * @param desc - The callback descriptor.
 * @return 0 in case of success, errno codes otherwise.
 */
int32_t uart_register_callback(uint8_t port, struct callback_desc *desc)
{
	if(!desc || port >= N_PORTS)
		return -EINVAL;
	if(!cb[port]) {
		cb[port] = calloc(1, sizeof(*cb[port]));
		if(!cb[port])
			return -ENOMEM;
	}

	cb[port]->ctx = desc->ctx;
	cb[port]->callback = desc->callback;
	cb[port]->config = desc->config;

	return 0;
}

/**
 * @brief Unregister a callback function.
 * @param port - The UART port.
 * @return 0 in case of success, errno codes otherwise.
 */
int32_t uart_unregister_callback(uint8_t port)
{
	if(port >= N_PORTS || !cb[port])
		return -EINVAL;

	free(cb[port]);

	return 0;
}

