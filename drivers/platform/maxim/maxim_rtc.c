/***************************************************************************//**
 *   @file   maxim/maxim_rtc.c
 *   @brief  Implementation of RTC driver.
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

#include <stdlib.h>
#include <errno.h>
#include "no-os/irq.h"
#include "no-os/rtc.h"
#include "no-os/util.h"
#include "rtc.h"
#include "rtc_extra.h"
#include "rtc_regs.h"

#define MS_TO_RSSA(x) (0 - ((x * 256) / 1000))

/**
* @brief Callback descriptor that contains a function to be called when an
* interrupt occurs.
*/
static struct callback_desc *cb;

/******************************************************************************/
/************************ Functions Definitions *******************************/
/******************************************************************************/

void RTC_IRQHandler()
{
	if(!cb)
		return;

	mxc_rtc_regs_t *rtc_regs = MXC_RTC;
	volatile uint32_t rtc_ctrl = rtc_regs->ctrl;
	uint8_t n_int = 0;
	/** Sub-second alarm flag clear */
	rtc_regs->ctrl &= ~BIT(7);
	/** Time-of-day alarm flag clear */
	rtc_regs->ctrl &= ~BIT(6);
	/** RTC (read) ready flag */
	rtc_regs->ctrl &= ~BIT(5);

	/** Shift right so the interrupt flags will be the first 3 bits */
	rtc_ctrl >>= 5UL;
	/** Clear the remaining bits */
	rtc_ctrl &= 0x7UL;
	while(rtc_ctrl) {
		if((rtc_ctrl & 1) && (rtc_regs->ctrl & BIT(n_int))) {
			cb->callback(cb->ctx, n_int, cb->config);
		}
		n_int++;
		rtc_ctrl >>= 1;
	}
}

/**
 * @brief Initialize the RTC peripheral.
 * @param device - The RTC descriptor.
 * @param init_param - The structure that contains the RTC initialization.
 * @return 0 in case of success, errno codes otherwise.
 */
int32_t rtc_init(struct rtc_desc **device, struct rtc_init_param *init_param)
{
	int32_t ret = 0;
	struct rtc_desc *dev;
	sys_cfg_rtc_t sys_cfg;

	if(!init_param)
		return -EINVAL;

	struct rtc_init_maxim *maxim_init_param = init_param->extra;
	dev = calloc(1, sizeof(*dev));
	if(!dev) {
		ret = -ENOMEM;
		goto error;
	}

	dev->id = init_param->id;
	dev->freq = init_param->freq;
	dev->load = init_param->load;

	TMR_Enable(MXC_TMR0);

	if(RTC_Init(MXC_RTC, dev->load, maxim_init_param->ms_load,
		    &sys_cfg) != E_NO_ERROR) {
		ret = -1;
		goto error;
	}

	*device = dev;

	return 0;

error:
	free(dev);

	return ret;
}

/**
 * @brief Free the resources allocated by rtc_init().
 * @param dev - The RTC descriptor.
 * @return 0 in case of success, errno codes otherwise.
 */
int32_t rtc_remove(struct rtc_desc *dev)
{
	if(!dev)
		return -EINVAL;
	rtc_unregister_callback();
	free(dev);
	return 0;
}

/**
 * @brief Start the real time clock.
 * @param dev - The RTC descriptor.
 * @return 0 in case of success, errno codes otherwise.
 */
int32_t rtc_start(struct rtc_desc *dev)
{
	RTC_EnableRTCE(MXC_RTC);

	/** Wait for synchronization */
	if(RTC_CheckBusy())
		return -EBUSY;

	return 0;
}

/**
 * @brief Stop the real time clock.
 * @param dev - The RTC descriptor.
 * @return 0 in case of success, errno codes otherwise.
 */
int32_t rtc_stop(struct rtc_desc *dev)
{
	RTC_DisableRTCE(MXC_RTC);

	return 0;
}

/**
 * @brief Get the current count for the real time clock.
 * @param dev - The RTC descriptor.
 * @param tmr_cnt - Pointer where the read counter will be stored.
 * @return 0 in case of success, errno codes otherwise.
 */
int32_t rtc_get_cnt(struct rtc_desc *dev, uint32_t *tmr_cnt)
{
	if(RTC_CheckBusy())
		return -EBUSY;

	*tmr_cnt = RTC_GetSecond();

	return 0;
}

/**
 * @brief Set the current count for the real time clock.
 * @param desc - The RTC descriptor.
 * @param tmr_cnt - New value of the timer counter.
 * @return 0 in case of success, errno codes otherwise.
 */
int32_t rtc_set_cnt(struct rtc_desc *desc, uint32_t tmr_cnt)
{
	mxc_rtc_regs_t *rtc_regs = MXC_RTC;
	if(RTC_CheckBusy())
		return -EBUSY;

	rtc_regs->ctrl |= MXC_F_RTC_CTRL_WE;
	rtc_stop(desc);

	if(RTC_CheckBusy())
		return -EBUSY;

	rtc_regs->sec = tmr_cnt;
	rtc_start(desc);

	if(RTC_CheckBusy())
		return -EBUSY;

	rtc_regs->ctrl &= ~MXC_F_RTC_CTRL_WE;

	return 0;
}

/**
 * @brief Get the seconds and subseconds counter value of the RTC.
 * @param sec - The seconds counter.
 * @param ssec - The subseconds counter.
 * @return 0 in case of success, errno codes otherwise.
 */
int32_t rtc_get_time(uint32_t *sec, uint32_t *ssec)
{
	if(RTC_CheckBusy())
		return -EBUSY;
	*sec = RTC_GetSecond();
	if(RTC_CheckBusy())
		return -EBUSY;
	*ssec = (RTC_GetSubSecond() * 1000) / 256;

	return 0;
}

/**
 * @brief Register a function to be called when an interrupt occurs.
 * @param desc - the callback descriptor.
 * @return 0 in case of success, errno codes otherwise.
 */
int32_t rtc_register_callback(struct callback_desc *desc)
{
	if(!desc)
		return -EINVAL;

	if(!cb) {
		cb = calloc(1, sizeof(*cb));
		if(!cb)
			return -ENOMEM;
	}

	cb->ctx = desc->ctx;
	cb->callback = desc->callback;
	cb->config = desc->config;

	return 0;
}

/**
 * @brief Unregister a callback function.
 * @return 0 in case of success, errno codes otherwise.
 */
int32_t rtc_unregister_callback()
{
	if(!cb)
		return -EINVAL;

	free(cb);

	return 0;
}

/**
 * @brief Enable a specific interrupt.
 * @param int_id - the interrupt identifier.
 * @param irq_time - the time at which the interrupt must occur (one-shot).
 * @return 0 in case of success, errno codes otherwise.
 */
int32_t rtc_enable_irq(enum rtc_interrupt_id int_id, uint32_t irq_time)
{

	if(int_id == RTC_TIMEOFDAY_INT) {
		RTC_EnableTimeofdayInterrupt(MXC_RTC);
		RTC_SetTimeofdayAlarm(MXC_RTC, irq_time);
	} else if(int_id == RTC_SUBSEC_INT) {
		RTC_EnableSubsecondInterrupt(MXC_RTC);
		RTC_SetSubsecondAlarm(MXC_RTC, irq_time);
	} else
		return -EINVAL;

	return 0;
}

/**
 * @brief Disable a specific interrupt.
 * @param int_id - the interrupt identifier.
 * @return 0 in case of success, errno codes otherwise.
 */
int32_t rtc_disable_irq(enum rtc_interrupt_id int_id)
{
	if(int_id == RTC_TIMEOFDAY_INT)
		RTC_DisableTimeofdayInterrupt(MXC_RTC);
	else if(int_id == RTC_SUBSEC_INT)
		RTC_DisableSubsecondInterrupt(MXC_RTC);
	else
		return -EINVAL;

	return 0;
}
