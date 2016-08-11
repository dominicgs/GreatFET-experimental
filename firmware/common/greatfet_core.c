/*
 * Copyright 2012 Michael Ossmann <mike@ossmann.com>
 * Copyright 2012 Jared Boone <jared@sharebrained.com>
 * Copyright 2013 Benjamin Vernoux <titanmkd@gmail.com>
 * Copyright 2015 Dominic Spill <dominicgs@gmail.com>
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

#include "greatfet_core.h"
#include "greatfet_pins.h"
#include "spi_ssp.h"
#include "spiflash.h"
#include "spiflash_target.h"
#include "i2c_bus.h"
#include "i2c_lpc.h"
#include <libopencm3/lpc43xx/cgu.h>
#include <libopencm3/lpc43xx/scu.h>
#include <libopencm3/lpc43xx/ssp.h>

#include "gpio_lpc.h"

/* TODO: Consolidate ARRAY_SIZE declarations */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define WAIT_CPU_CLOCK_INIT_DELAY   (10000)

/* GPIO Output PinMux */
static struct gpio_t gpio_led[4] = {
	GPIO(PORT_LED1_3_4,  PIN_LED1),
	GPIO(PORT_LED2,      PIN_LED2),
	GPIO(PORT_LED1_3_4,  PIN_LED3),
	GPIO(PORT_LED1_3_4,  PIN_LED4)
};

static struct gpio_t gpio_usb1_en		= GPIO(2, 8);

/* CPLD JTAG interface GPIO pins */
static struct gpio_t gpio_tdo			= GPIO(5, 18);
static struct gpio_t gpio_tck			= GPIO(3,  0);
static struct gpio_t gpio_tms			= GPIO(3,  4);
static struct gpio_t gpio_tdi			= GPIO(3,  1);

i2c_bus_t i2c0 = {
	.obj = (void*)I2C0_BASE,
	.start = i2c_lpc_start,
	.stop = i2c_lpc_stop,
	.transfer = i2c_lpc_transfer,
};

i2c_bus_t i2c1 = {
	.obj = (void*)I2C1_BASE,
	.start = i2c_lpc_start,
	.stop = i2c_lpc_stop,
	.transfer = i2c_lpc_transfer,
};

const i2c_lpc_config_t i2c_config_slow_clock = {
        .duty_cycle_count = 15,
};

const i2c_lpc_config_t i2c_config_fast_clock = {
        .duty_cycle_count = 255,
};

const ssp_config_t ssp_config_spi = {
	.data_bits = SSP_DATA_8BITS,
	.serial_clock_rate = 2,
	.clock_prescale_rate = 2,
};

spi_bus_t spi_bus_ssp0 = {
	.obj = (void*)SSP0_BASE,
	.config = &ssp_config_spi,
	.start = spi_ssp_start,
	.stop = spi_ssp_stop,
	.transfer = spi_ssp_transfer,
	.transfer_gather = spi_ssp_transfer_gather,
};

const ssp_config_t ssp1_config_spi = {
	.data_bits = SSP_DATA_8BITS,
	.serial_clock_rate = 2,
	.clock_prescale_rate = 100,
};

spi_bus_t spi_bus_ssp1 = {
	.obj = (void*)SSP1_BASE,
	.config = &ssp1_config_spi,
	.start = spi_ssp_start,
	.stop = spi_ssp_stop,
	.transfer = spi_ssp_transfer,
	.transfer_gather = spi_ssp_transfer_gather,
};

void delay(uint32_t duration)
{
	uint32_t i;

	for (i = 0; i < duration; i++)
		__asm__("nop");
}

/* clock startup for Jellybean with Lemondrop attached
Configure PLL1 to max speed (204MHz).
Note: PLL1 clock is used by M4/M0 core, Peripheral, APB1. */
void cpu_clock_init(void)
{
	/* use IRC as clock source for APB1 (including I2C0) */
	CGU_BASE_APB1_CLK = CGU_BASE_APB1_CLK_CLK_SEL(CGU_SRC_IRC);

	/* use IRC as clock source for APB3 */
	CGU_BASE_APB3_CLK = CGU_BASE_APB3_CLK_CLK_SEL(CGU_SRC_IRC);

	//FIXME a lot of the details here should be in a CGU driver

	/* set xtal oscillator to low frequency mode */
	CGU_XTAL_OSC_CTRL &= ~CGU_XTAL_OSC_CTRL_HF_MASK;

	/* power on the oscillator and wait until stable */
	CGU_XTAL_OSC_CTRL &= ~CGU_XTAL_OSC_CTRL_ENABLE_MASK;

	/* Wait about 100us after Crystal Power ON */
	delay(WAIT_CPU_CLOCK_INIT_DELAY);

	/* use XTAL_OSC as clock source for BASE_M4_CLK (CPU) */
	CGU_BASE_M4_CLK = (CGU_BASE_M4_CLK_CLK_SEL(CGU_SRC_XTAL) | CGU_BASE_M4_CLK_AUTOBLOCK(1));

	/* use XTAL_OSC as clock source for APB1 */
	CGU_BASE_APB1_CLK = CGU_BASE_APB1_CLK_AUTOBLOCK(1)
			| CGU_BASE_APB1_CLK_CLK_SEL(CGU_SRC_XTAL);

	/* use XTAL_OSC as clock source for APB3 */
	CGU_BASE_APB3_CLK = CGU_BASE_APB3_CLK_AUTOBLOCK(1)
			| CGU_BASE_APB3_CLK_CLK_SEL(CGU_SRC_XTAL);

	cpu_clock_pll1_low_speed();

	/* use PLL1 as clock source for BASE_M4_CLK (CPU) */
	CGU_BASE_M4_CLK = (CGU_BASE_M4_CLK_CLK_SEL(CGU_SRC_PLL1) | CGU_BASE_M4_CLK_AUTOBLOCK(1));

	/* use XTAL_OSC as clock source for PLL0USB */
	CGU_PLL0USB_CTRL = CGU_PLL0USB_CTRL_PD(1)
			| CGU_PLL0USB_CTRL_AUTOBLOCK(1)
			| CGU_PLL0USB_CTRL_CLK_SEL(CGU_SRC_XTAL);
	while (CGU_PLL0USB_STAT & CGU_PLL0USB_STAT_LOCK_MASK);

	/* configure PLL0USB to produce 480 MHz clock from 12 MHz XTAL_OSC */
	/* Values from User Manual v1.4 Table 94, for 12MHz oscillator. */
	CGU_PLL0USB_MDIV = 0x06167FFA;
	CGU_PLL0USB_NP_DIV = 0x00302062;
	CGU_PLL0USB_CTRL |= (CGU_PLL0USB_CTRL_PD(1)
			| CGU_PLL0USB_CTRL_DIRECTI(1)
			| CGU_PLL0USB_CTRL_DIRECTO(1)
			| CGU_PLL0USB_CTRL_CLKEN(1));

	/* power on PLL0USB and wait until stable */
	CGU_PLL0USB_CTRL &= ~CGU_PLL0USB_CTRL_PD_MASK;
	while (!(CGU_PLL0USB_STAT & CGU_PLL0USB_STAT_LOCK_MASK));

	/* use PLL0USB as clock source for USB0 */
	CGU_BASE_USB0_CLK = CGU_BASE_USB0_CLK_AUTOBLOCK(1)
			| CGU_BASE_USB0_CLK_CLK_SEL(CGU_SRC_PLL0USB);

	/* use PLL0USB as clock source for IDIVA */
	/* divide by 4 */
	CGU_IDIVA_CTRL = CGU_IDIVA_CTRL_IDIV(3)
			| CGU_IDIVA_CTRL_AUTOBLOCK(1)
			| CGU_IDIVA_CTRL_CLK_SEL(CGU_SRC_PLL0USB);

	/* use IDIVA as clock source for IDIVB */
	/* divide by 2 */
	CGU_IDIVB_CTRL = CGU_IDIVB_CTRL_IDIV(1)
			| CGU_IDIVB_CTRL_AUTOBLOCK(1)
			| CGU_IDIVA_CTRL_CLK_SEL(CGU_SRC_IDIVA);

	/* Use IDIVB for CLKOUT */
	CGU_BASE_OUT_CLK = CGU_BASE_OUT_CLK_AUTOBLOCK(1)
			| CGU_BASE_OUT_CLK_CLK_SEL(CGU_SRC_IDIVB);

	/* use IDIVB as clock source for USB1 */
	CGU_BASE_USB1_CLK = CGU_BASE_USB1_CLK_AUTOBLOCK(1)
			| CGU_BASE_USB1_CLK_CLK_SEL(CGU_SRC_IDIVB);

	/* Switch peripheral clock over to use PLL1 (204MHz) */
	CGU_BASE_PERIPH_CLK = CGU_BASE_PERIPH_CLK_AUTOBLOCK(1)
			| CGU_BASE_PERIPH_CLK_CLK_SEL(CGU_SRC_PLL1);

	/* Switch APB1 clock over to use PLL1 (204MHz) */
	CGU_BASE_APB1_CLK = CGU_BASE_APB1_CLK_AUTOBLOCK(1)
			| CGU_BASE_APB1_CLK_CLK_SEL(CGU_SRC_PLL1);

	/* Switch APB3 clock over to use PLL1 (204MHz) */
	CGU_BASE_APB3_CLK = CGU_BASE_APB3_CLK_AUTOBLOCK(1)
			| CGU_BASE_APB3_CLK_CLK_SEL(CGU_SRC_PLL1);

	CGU_BASE_SSP0_CLK = CGU_BASE_SSP0_CLK_AUTOBLOCK(1)
			| CGU_BASE_SSP0_CLK_CLK_SEL(CGU_SRC_PLL1);

	CGU_BASE_SSP1_CLK = CGU_BASE_SSP1_CLK_AUTOBLOCK(1)
			| CGU_BASE_SSP1_CLK_CLK_SEL(CGU_SRC_PLL1);
}


/*
Configure PLL1 to low speed (48MHz).
Note: PLL1 clock is used by M4/M0 core, Peripheral, APB1.
This function shall be called after cpu_clock_init().
This function is mainly used to lower power consumption.
*/
void cpu_clock_pll1_low_speed(void)
{
	uint32_t pll_reg;

	/* Configure PLL1 Clock (48MHz) */
	/* Integer mode:
		FCLKOUT = M*(FCLKIN/N)
		FCCO = 2*P*FCLKOUT = 2*P*M*(FCLKIN/N)
	*/
	pll_reg = CGU_PLL1_CTRL;
	/* Clear PLL1 bits */
	pll_reg &= ~( CGU_PLL1_CTRL_CLK_SEL_MASK | CGU_PLL1_CTRL_PD_MASK | CGU_PLL1_CTRL_FBSEL_MASK |  /* CLK SEL, PowerDown , FBSEL */
				  CGU_PLL1_CTRL_BYPASS_MASK | /* BYPASS */
				  CGU_PLL1_CTRL_DIRECT_MASK | /* DIRECT */
				  CGU_PLL1_CTRL_PSEL_MASK | CGU_PLL1_CTRL_MSEL_MASK | CGU_PLL1_CTRL_NSEL_MASK ); /* PSEL, MSEL, NSEL- divider ratios */
	/* Set PLL1 up to 12MHz * 4 = 48MHz. */
	pll_reg |= CGU_PLL1_CTRL_CLK_SEL(CGU_SRC_XTAL)
				| CGU_PLL1_CTRL_PSEL(0)
				| CGU_PLL1_CTRL_NSEL(0)
				| CGU_PLL1_CTRL_MSEL(3)
				| CGU_PLL1_CTRL_FBSEL(1)
				| CGU_PLL1_CTRL_DIRECT(1);
	CGU_PLL1_CTRL = pll_reg;
	/* wait until stable */
	while (!(CGU_PLL1_STAT & CGU_PLL1_STAT_LOCK_MASK));

	/* Wait a delay after switch to new frequency with Direct mode */
	delay(WAIT_CPU_CLOCK_INIT_DELAY);
}

/*
Configure PLL1 (Main MCU Clock) to max speed (204MHz).
Note: PLL1 clock is used by M4/M0 core, Peripheral, APB1.
This function shall be called after cpu_clock_init().
*/
void cpu_clock_pll1_max_speed(void)
{
	uint32_t pll_reg;

	/* Configure PLL1 to Intermediate Clock (between 90 MHz and 110 MHz) */
	/* Integer mode:
		FCLKOUT = M*(FCLKIN/N)
		FCCO = 2*P*FCLKOUT = 2*P*M*(FCLKIN/N)
	*/
	pll_reg = CGU_PLL1_CTRL;
	/* Clear PLL1 bits */
	pll_reg &= ~( CGU_PLL1_CTRL_CLK_SEL_MASK | CGU_PLL1_CTRL_PD_MASK | CGU_PLL1_CTRL_FBSEL_MASK |  /* CLK SEL, PowerDown , FBSEL */
				  CGU_PLL1_CTRL_BYPASS_MASK | /* BYPASS */
				  CGU_PLL1_CTRL_DIRECT_MASK | /* DIRECT */
				  CGU_PLL1_CTRL_PSEL_MASK | CGU_PLL1_CTRL_MSEL_MASK | CGU_PLL1_CTRL_NSEL_MASK ); /* PSEL, MSEL, NSEL- divider ratios */
	/* Set PLL1 up to 12MHz * 8 = 96MHz. */
	pll_reg |= CGU_PLL1_CTRL_CLK_SEL(CGU_SRC_XTAL)
				| CGU_PLL1_CTRL_PSEL(0)
				| CGU_PLL1_CTRL_NSEL(0)
				| CGU_PLL1_CTRL_MSEL(7)
				| CGU_PLL1_CTRL_FBSEL(1);
	CGU_PLL1_CTRL = pll_reg;
	/* wait until stable */
	while (!(CGU_PLL1_STAT & CGU_PLL1_STAT_LOCK_MASK));

	/* Wait before to switch to max speed */
	delay(WAIT_CPU_CLOCK_INIT_DELAY);

	/* Configure PLL1 Max Speed */
	/* Direct mode: FCLKOUT = FCCO = M*(FCLKIN/N) */
	pll_reg = CGU_PLL1_CTRL;
	/* Clear PLL1 bits */
	pll_reg &= ~( CGU_PLL1_CTRL_CLK_SEL_MASK | CGU_PLL1_CTRL_PD_MASK | CGU_PLL1_CTRL_FBSEL_MASK |  /* CLK SEL, PowerDown , FBSEL */
				  CGU_PLL1_CTRL_BYPASS_MASK | /* BYPASS */
				  CGU_PLL1_CTRL_DIRECT_MASK | /* DIRECT */
				  CGU_PLL1_CTRL_PSEL_MASK | CGU_PLL1_CTRL_MSEL_MASK | CGU_PLL1_CTRL_NSEL_MASK ); /* PSEL, MSEL, NSEL- divider ratios */
	/* Set PLL1 up to 12MHz * 17 = 204MHz. */
	pll_reg |= CGU_PLL1_CTRL_CLK_SEL(CGU_SRC_XTAL)
			| CGU_PLL1_CTRL_PSEL(0)
			| CGU_PLL1_CTRL_NSEL(0)
			| CGU_PLL1_CTRL_MSEL(16)
			| CGU_PLL1_CTRL_FBSEL(1)
			| CGU_PLL1_CTRL_DIRECT(1);
	CGU_PLL1_CTRL = pll_reg;
	/* wait until stable */
	while (!(CGU_PLL1_STAT & CGU_PLL1_STAT_LOCK_MASK));

}

void pin_setup(void) {
	/* Release CPLD JTAG pins */
	scu_pinmux(SCU_PINMUX_TDO, SCU_GPIO_NOPULL | SCU_CONF_FUNCTION4);
	scu_pinmux(SCU_PINMUX_TCK, SCU_GPIO_NOPULL | SCU_CONF_FUNCTION0);
	scu_pinmux(SCU_PINMUX_TMS, SCU_GPIO_NOPULL | SCU_CONF_FUNCTION0);
	scu_pinmux(SCU_PINMUX_TDI, SCU_GPIO_NOPULL | SCU_CONF_FUNCTION0);

	gpio_input(&gpio_tdo);
	gpio_input(&gpio_tck);
	gpio_input(&gpio_tms);
	gpio_input(&gpio_tdi);

	/* Configure SCU Pin Mux as GPIO */
	scu_pinmux(SCU_PINMUX_LED1, SCU_GPIO_NOPULL);
	scu_pinmux(SCU_PINMUX_LED2, SCU_GPIO_NOPULL);
	scu_pinmux(SCU_PINMUX_LED3, SCU_GPIO_NOPULL);
	scu_pinmux(SCU_PINMUX_LED4, SCU_GPIO_NOPULL);

	/* Configure all GPIO as Input (safe state) */
	gpio_init();

	gpio_output(&gpio_led[0]);
	gpio_output(&gpio_led[1]);
	gpio_output(&gpio_led[2]);
	gpio_output(&gpio_led[3]);

	/* enable input on SCL and SDA pins */
	SCU_SFSI2C0 = SCU_I2C0_NOMINAL;

	/* Configure external clock in */
	scu_pinmux(CLK0, SCU_CONF_FUNCTION1 | SCU_CLK_OUT);

	/* Enable USB1 controller */
	SCU_SFSUSB = 0x2;
	scu_pinmux(SCU_PINMUX_USB1_EN, SCU_CONF_FUNCTION0);
	gpio_output(&gpio_usb1_en);
	gpio_set(&gpio_usb1_en);
}

void led_on(const led_t led) {
	gpio_clear(&gpio_led[led]);
}

void led_off(const led_t led) {
	gpio_set(&gpio_led[led]);
}

void led_toggle(const led_t led) {
	gpio_toggle(&gpio_led[led]);
}

/* Temporary LED based debugging */
void debug_led(uint8_t val) {
	if(val & 0x1)
		led_on(LED1);
	else
		led_off(LED1);

	if(val & 0x2)
		led_on(LED2);
	else
		led_off(LED2);

	if(val & 0x4)
		led_on(LED3);
	else
		led_off(LED3);

	if(val & 0x8)
		led_on(LED4);
	else
		led_off(LED4);
}
