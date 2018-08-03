/*
 * This file is part of GreatFET
 */

#ifndef __I2C_BUS_H__
#define __I2C_BUS_H__

#include <stdint.h>
#include <stddef.h>

struct i2c_bus_t;
typedef struct i2c_bus_t i2c_bus_t;

struct i2c_bus_t {
	void* const obj;
	void (*start)(i2c_bus_t* const bus, const uint16_t config);
	void (*stop)(i2c_bus_t* const bus);
	void (*transfer)(
		i2c_bus_t* const bus,
		const uint_fast8_t slave_address,
		const uint8_t* const tx, const size_t tx_count,
		uint8_t* const rx, const size_t rx_count
	);
};

void i2c_bus_start(i2c_bus_t* const bus, const uint16_t config);
void i2c_bus_stop(i2c_bus_t* const bus);
void i2c_bus_transfer(
	i2c_bus_t* const bus,
	const uint_fast8_t slave_address,
	const uint8_t* const tx, const size_t tx_count,
	uint8_t* const rx, const size_t rx_count
);

#endif/*__I2C_BUS_H__*/
