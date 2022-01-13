/***************************************************************************//**
 *   @file   maxim/maxim_gpio.c
 *   @brief  Implementation of gpio driver.
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

#include <errno.h>
#include <stdlib.h>
#include "no-os/gpio.h"
#include "no-os/irq.h"
#include "no-os/util.h"
#include "gpio.h"
#include "gpio_regs.h"
#include "gpio_extra.h"
#include "max32660.h"

#define N_INT	14

static struct callback_desc *gpio_callback[N_INT];

/******************************************************************************/
/************************ Functions Definitions *******************************/
/******************************************************************************/

void GPIO0_IRQHandler()
{
	uint32_t pin = 0;
	mxc_gpio_regs_t *gpio_regs = MXC_GPIO_GET_GPIO(0);
	uint32_t stat_reg = gpio_regs->int_stat;

	/** Clear interrupt flags */
	gpio_regs->int_clr = stat_reg;
	while(stat_reg) {
		if(stat_reg & 1) {
			if(!gpio_callback[pin])
				return;
			void *ctx = gpio_callback[pin]->ctx;
			gpio_callback[pin]->callback(ctx, pin, NULL);
		}
		pin++;
		stat_reg >>= 1;
	}
}

/**
 * @brief Obtain the GPIO decriptor.
 * @param desc - The GPIO descriptor.
 * @param param - GPIO initialization parameters
 * @return 0 in case of success, errno error codes otherwise.
 */
int32_t max_gpio_get(struct gpio_desc **desc,
		     const struct gpio_init_param *param)
{
	if(!param || param->number >= N_PINS)
		return -EINVAL;

	struct gpio_desc *descriptor = calloc(1, sizeof(*descriptor));
	if(!descriptor)
		return -ENOMEM;

	descriptor->number = param->number;
	descriptor->platform_ops = param->platform_ops;
	descriptor->extra = param->extra;
	((gpio_cfg_t *)descriptor->extra)->mask = BIT(param->number);

	GPIO_Config(descriptor->extra);
	*desc = descriptor;

	return 0;
}

/**
 * @brief Get the value of an optional GPIO.
 * @param desc - The GPIO descriptor.
 * @param param - GPIO initialization parameters
 * @return 0 in case of success, errno error codes otherwise.
 */
int32_t max_gpio_get_optional(struct gpio_desc **desc,
			      const struct gpio_init_param *param)
{
	if(param == NULL) {
		*desc = NULL;
		return 0;
	}

	return max_gpio_get(desc, param);
}

/**
 * @brief Free the resources allocated by gpio_get().
 * @param desc - The GPIO descriptor.
 * @return 0 in case of success, errno error codes otherwise.
 */
int32_t max_gpio_remove(struct gpio_desc *desc)
{
	if(!desc)
		return -EINVAL;

	max_gpio_unregister_callback(desc->number);
	free(desc);

	return 0;
}

/**
 * @brief Enable the input direction of the specified GPIO.
 * @param desc - The GPIO descriptor.
 * @return 0 in case of success, errno error codes otherwise.
 */
int32_t max_gpio_direction_input(struct gpio_desc *desc)
{
	if(!desc || !desc->extra || desc->number >= N_PINS)
		return -EINVAL;

	gpio_cfg_t *maxim_extra = (gpio_cfg_t *)desc->extra;

	if(maxim_extra->port >= N_PORTS)
		return -EINVAL;

	maxim_extra->mask = BIT(desc->number);
	maxim_extra->func = GPIO_FUNC_IN;
	GPIO_Config(maxim_extra);

	return 0;
}

/**
 * @brief Enable the output direction of the specified GPIO.
 * @param desc - The GPIO descriptor.
 * @param value - The value.
 *                Example: GPIO_HIGH
 *                         GPIO_LOW
 * @return 0 in case of success, errno error codes otherwise.
 */
int32_t max_gpio_direction_output(struct gpio_desc *desc, uint8_t value)
{
	if(!desc || !desc->extra || desc->number >= N_PINS)
		return -EINVAL;

	gpio_cfg_t *maxim_extra = (gpio_cfg_t *)desc->extra;

	maxim_extra->mask = BIT(desc->number);
	maxim_extra->func = GPIO_FUNC_OUT;
	GPIO_Config(maxim_extra);

	if(value == 0)
		GPIO_OutClr(maxim_extra);
	else
		GPIO_OutSet(maxim_extra);

	return 0;
}

/**
 * @brief Get the direction of the specified GPIO.
 * @param desc - The GPIO descriptor.
 * @param direction - The direction.
 *                    Example: GPIO_OUT
 *                             GPIO_IN
 * @return 0 in case of success, errno error codes otherwise.
 */
int32_t max_gpio_get_direction(struct gpio_desc *desc, uint8_t *direction)
{
	if(!desc || !desc->extra || desc->number >= N_PINS)
		return -EINVAL;

	gpio_cfg_t *maxim_extra = (gpio_cfg_t *)desc->extra;

	if(maxim_extra->func == GPIO_FUNC_OUT)
		*direction = GPIO_OUT;
	else if(maxim_extra->func == GPIO_FUNC_IN)
		*direction = GPIO_IN;
	else
		return -EINVAL;

	return 0;
}

/**
 * @brief Set the value of the specified GPIO.
 * @param desc - The GPIO descriptor.
 * @param value - The value.
 *                Example: GPIO_HIGH
 *                         GPIO_LOW
 * @return 0 in case of success, errno error codes otherwise.
 */
int32_t max_gpio_set_value(struct gpio_desc *desc, uint8_t value)
{

	if(!desc || !desc->extra || desc->number >= N_PINS)
		return -EINVAL;

	gpio_cfg_t *maxim_extra = (gpio_cfg_t *)desc->extra;
	mxc_gpio_regs_t *gpio_regs = MXC_GPIO_GET_GPIO(maxim_extra->port);

	switch(value) {
	case GPIO_LOW:
		GPIO_OutClr(maxim_extra);
		break;
	case GPIO_HIGH:
		GPIO_OutSet(maxim_extra);
		break;
	case GPIO_HIGH_Z:
		gpio_regs->en &= ~maxim_extra->mask;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/**
 * @brief Get the value of the specified GPIO.
 * @param desc - The GPIO descriptor.
 * @param value - The value.
 *                Example: GPIO_HIGH
 *                         GPIO_LOW
 * @return 0 in case of success, errno error codes otherwise.
 */
int32_t max_gpio_get_value(struct gpio_desc *desc, uint8_t *value)
{
	if(!desc || desc->number >= N_PINS)
		return -EINVAL;

	gpio_cfg_t *maxim_extra = desc->extra;
	if(!maxim_extra)
		return -EINVAL;

	if(maxim_extra->func == GPIO_FUNC_IN)
		*value = GPIO_InGet(maxim_extra);
	else if(maxim_extra->func == GPIO_FUNC_OUT)
		*value = GPIO_OutGet(maxim_extra);
	else
		return -EINVAL;

	return 0;
}

/**
 * @brief Set the trigger condition for an interrupt.
 * @param desc - The GPIO descriptor.
 * @param trig_l - The trigger condition
 * @return 0 in case of success, errno error codes otherwise.
 */
int32_t max_gpio_irq_set_trigger_level(struct gpio_desc *desc,
				       enum irq_trig_level trig_l)
{
	if(!desc || !desc->extra || desc->number >= N_PINS)
		return -EINVAL;

	gpio_cfg_t *maxim_extra = (gpio_cfg_t *)desc->extra;
	mxc_gpio_regs_t *gpio_regs = MXC_GPIO_GET_GPIO(maxim_extra->port);

	uint8_t is_enabled = gpio_regs->int_en & BIT(desc->number);

	/** Disable interrupts for pin desc->number */
	gpio_regs->int_en &= ~(BIT(desc->number));
	/** Clear pending interrupts for pin desc->number */
	gpio_regs->int_clr |= BIT(desc->number);

	switch(trig_l) {
	case IRQ_EDGE_RISING:
		/** Select edge triggered interrupt mode */
		gpio_regs->int_mod |= BIT(desc->number);
		/** Select rising edge trigger condition */
		gpio_regs->int_pol &= ~(BIT(desc->number));
		break;
	case IRQ_EDGE_FALLING:
		/** Select edge triggered interrupt mode */
		gpio_regs->int_mod |= BIT(desc->number);
		/** Select falling edge trigger condition */
		gpio_regs->int_pol |= BIT(desc->number);
		break;
	case IRQ_LEVEL_HIGH:
		/** Select level triggered interrupt mode */
		gpio_regs->int_mod &= ~(BIT(desc->number));
		/** Select level high trigger condition */
		gpio_regs->int_pol &= ~(BIT(desc->number));
		break;
	case IRQ_LEVEL_LOW:
		/** Select level triggered interrupt mode */
		gpio_regs->int_mod &= ~(BIT(desc->number));
		/** Select level low trigger condition */
		gpio_regs->int_pol |= BIT(desc->number);
		break;
	case IRQ_EDGE_BOTH:
		/** Edge triggered on both rising and falling */
		gpio_regs->int_dual_edge |= BIT(desc->number);
		break;
	default:
		if(is_enabled)
			gpio_regs->int_en |= BIT(desc->number);
		return -EINVAL;
	}
	/** Enable interupts for pin desc->number if they were already enabled*/
	if(is_enabled)
		gpio_regs->int_en |= BIT(desc->number);

	return 0;
}

/**
 * @brief Register a function to be called when an interrupt occurs.
 * @param ctrl_desc - The IRQ descriptor
 * @param desc - The callback descriptor.
 * @return 0 in case of success, errno error codes otherwise.
 */
int32_t max_gpio_register_callback(struct irq_ctrl_desc *ctrl_desc,
				   struct callback_desc *desc)
{
	if(!desc || !ctrl_desc || !ctrl_desc->extra)
		return -EINVAL;

	int32_t error = 0;
	struct gpio_irq_config *g_irq = ctrl_desc->extra;
	struct gpio_desc *g_desc = g_irq->desc;
	enum irq_trig_level trig_level = g_irq->mode;

	struct callback_desc *descriptor = calloc(1, sizeof(*descriptor));
	if(!descriptor)
		return -ENOMEM;

	error = max_gpio_direction_input(g_desc);
	error = max_gpio_irq_set_trigger_level(g_desc, trig_level);
	if(error) {
		free(descriptor);
		return error;
	}
	descriptor->ctx = desc->ctx;
	descriptor->callback = desc->callback;
	descriptor->config = desc->config;

	gpio_callback[g_desc->number] = descriptor;

	return 0;
}

/**
 * @brief Unregister a callback function.
 * @param desc - The IRQ descriptor
 * @return 0 in case of success, errno error codes otherwise.
 */
int32_t max_gpio_unregister_callback(struct irq_ctrl_desc *desc)
{
	if(!desc || !desc->extra)
		return -EINVAL;

	struct gpio_irq_config *g_cfg = desc->extra;
	struct gpio_desc *g_desc = g_cfg->desc;

	max_gpio_disable_irq(desc);
	free(gpio_callback[g_desc->number]);

	return 0;
}

/**
 * @brief Enable interrupts on a GPIO pin.
 * @param desc - The IRQ descriptor
 * @return 0 in case of success, errno error codes otherwise.
 */
int32_t max_gpio_enable_irq(struct irq_ctrl_desc *desc)
{
	if(!desc || !desc->extra)
		return -EINVAL;

	struct gpio_irq_config *g_cfg = desc->extra;
	struct gpio_desc *g_desc = g_cfg->desc;
	const gpio_cfg_t *cfg = g_desc->extra;
	GPIO_IntEnable(cfg);

	return 0;
}

/**
 * @brief Disable interrupts on a GPIO pin.
 * @param desc - The IRQ descriptor
 * @return 0 in case of success, errno error codes otherwise.
 */
int32_t max_gpio_disable_irq(struct irq_ctrl_desc *desc)
{
	if(!desc || !desc->extra)
		return -EINVAL;

	struct gpio_irq_config *g_cfg = desc->extra;
	struct gpio_desc *g_desc = g_cfg->desc;
	const gpio_cfg_t *cfg = g_desc->extra;
	GPIO_IntDisable(cfg);

	return 0;
}

/**
 * @brief maxim platform specific GPIO platform ops structure
 */
const struct gpio_platform_ops gpio_ops = {
	.gpio_ops_get = &max_gpio_get,
	.gpio_ops_get_optional = &max_gpio_get_optional,
	.gpio_ops_remove = &max_gpio_remove,
	.gpio_ops_direction_input = &max_gpio_direction_input,
	.gpio_ops_direction_output = &max_gpio_direction_output,
	.gpio_ops_get_direction = &max_gpio_get_direction,
	.gpio_ops_set_value = &max_gpio_set_value,
	.gpio_ops_get_value = &max_gpio_get_value
};

