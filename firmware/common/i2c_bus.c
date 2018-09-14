/*
 * This file is part of GreatFET
 */

#include "i2c_bus.h"

void i2c_bus_start(i2c_bus_t* const bus, const uint16_t config) {
	bus->start(bus, config);
}

void i2c_bus_stop(i2c_bus_t* const bus) {
	bus->stop(bus);
}

void i2c_bus_transfer(
	i2c_bus_t* const bus,
	const uint_fast8_t slave_address,
	const uint8_t* const tx, const size_t tx_count,
	uint8_t* const rx, const size_t rx_count
) {
	bus->transfer(bus, slave_address, tx, tx_count, rx, rx_count);
}
