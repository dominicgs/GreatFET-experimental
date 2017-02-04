/*
 * Copyright 2016 Dominic Spill
 *
 * This file is part of GreatFET.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "usb_api_spi.h"
#include "usb_queue.h"

#include <stddef.h>
#include <greatfet_core.h>
#include <spi_bus.h>
#include <gpio_lpc.h>
#include <libopencm3/lpc43xx/scu.h>
#include <pins.h>

uint8_t spi_buffer[256U];
/* SSP1 SSEL (CS) pin, used as GPIO so that we control
 * it rather than the SSP.
 */
static struct gpio_t gpio_spi_select = GPIO(0, 15);

static spi_target_t spi1_target = {
	.bus = &spi_bus_ssp1,
	.gpio_select = &gpio_spi_select,
};


void spi1_init(spi_target_t* const target) {
	/* configure SSP pins */
	scu_pinmux(SCU_SSP1_MISO, (SCU_SSP_IO | SCU_CONF_FUNCTION5));
	scu_pinmux(SCU_SSP1_MOSI, (SCU_SSP_IO | SCU_CONF_FUNCTION5));
	scu_pinmux(SCU_SSP1_SCK,  (SCU_SSP_IO | SCU_CONF_FUNCTION1));
	scu_pinmux(SCU_SSP1_SSEL, (SCU_GPIO_FAST | SCU_CONF_FUNCTION0));
}

static spiflash_driver_t spi1_target_drv = {
	.target = &spi1_target,
    .target_init = spi1_init,
};

usb_request_status_t usb_vendor_request_init_spi(
		usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage) {
	if ((stage == USB_TRANSFER_STAGE_SETUP)) {
		
		spi_bus_start(spi1_target_drv.target, &ssp1_config_spi);
		spi1_init(spi1_target_drv.target);
		usb_transfer_schedule_ack(endpoint->in);
	}
	return USB_REQUEST_STATUS_OK;
}

usb_request_status_t usb_vendor_request_spi_write(
	usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	if (stage == USB_TRANSFER_STAGE_SETUP) 
	{
		usb_transfer_schedule_block(endpoint->out, &spi_buffer[0],
									endpoint->setup.length, NULL, NULL);
	} else if (stage == USB_TRANSFER_STAGE_DATA) {
		spi_bus_transfer(&spi1_target, spi_buffer, endpoint->setup.length);
		usb_transfer_schedule_ack(endpoint->in);
	}
	return USB_REQUEST_STATUS_OK;
}

usb_request_status_t usb_vendor_request_spi_read(
	usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	if (stage == USB_TRANSFER_STAGE_SETUP) 
	{
		usb_transfer_schedule_block(endpoint->in, &spi_buffer,
									endpoint->setup.length, NULL, NULL);
	} else if (stage == USB_TRANSFER_STAGE_DATA) {
		usb_transfer_schedule_ack(endpoint->out);
	}
	return USB_REQUEST_STATUS_OK;
}

usb_request_status_t usb_vendor_request_spi_dump_flash(
	usb_endpoint_t* const endpoint, const usb_transfer_stage_t stage)
{
	uint32_t addr;
	if (stage == USB_TRANSFER_STAGE_SETUP) 
	{
		spi1_target_drv.page_len = 256;
		spi1_target_drv.num_pages = 8192;
		spi1_target_drv.num_bytes = 256*8192;
		spi1_target_drv.device_id = 0x14;
		addr = (endpoint->setup.value << 16) | endpoint->setup.index;
		spiflash_read(&spi1_target_drv, addr, endpoint->setup.length, spi_buffer);
		usb_transfer_schedule_block(endpoint->in, &spi_buffer[0],
									endpoint->setup.length, NULL, NULL);
	} else if (stage == USB_TRANSFER_STAGE_DATA) {
		usb_transfer_schedule_ack(endpoint->out);
	}
	return USB_REQUEST_STATUS_OK;
}
