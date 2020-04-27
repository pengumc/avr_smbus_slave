#include <stdint.h>
#include <stddef.h>

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/twi.h>

#include "smbus_slave.h"
#include "smbus_slave_config.h"
#define I2CIE (1<<TWIE)

smbus_slave_t* global_smbus_slave = NULL;


void smbus_slave_init(uint8_t i2c_addr, uint8_t twbr, uint8_t prescaler,
					  uint8_t* read_mem, uint8_t* write_mem)
{
	//global_smbus_slave = (smbus_slave_t*)malloc(sizeof(*global_smbus_slave));
	global_smbus_slave->data_count = 0;
	global_smbus_slave->addr = 0;
	global_smbus_slave->response = 0;
	global_smbus_slave->read_mem = read_mem;
	global_smbus_slave->write_mem = write_mem;
	TWSR = prescaler;
	TWBR = twbr;
	TWAR = (i2c_addr << 1) | 0x01;
	// f_scl = f_clock / (16 + 2 * TWBR * prescale)
	// with 8 MHz: 8e6 / (16+2*32*1) = 100 kHz
	TWBR = 32;  
	TWCR = (1<<TWEN) | (1<<TWEA) | I2CIE;
}


ISR(TWI_vect)
{
	switch(TW_STATUS) {
		case TW_SR_SLA_ACK: {  // slave receiver ack
			TWCR = (1<<TWEN) | (1<<TWEA) | (1<<TWINT) | I2CIE;
			global_smbus_slave->data_count = 0;
			break;
		}
	  
		case TW_SR_DATA_ACK: {  // data received.
			if (global_smbus_slave->data_count == 0) {
				// first byte. either a single byte command, or a start address
				global_smbus_slave->addr = TWDR;
				TWCR = (1<<TWEN) | (1<<TWEA) | (1<<TWINT) | I2CIE;
				// select the byte to respond with
				global_smbus_slave->response = global_smbus_slave->read_mem[global_smbus_slave->addr];
			}
			else // not first byte
			{
				if (global_smbus_slave->data_count == 1) // second byte?
				{
					TWCR = (1<<TWEN) | (1<<TWEA) | (1<<TWINT) | I2CIE;
					// So we're doing a block
					// we can actually ignore the data length since we'll just keep going
				}
				else // third byte or more, so it's a block write
				{ 
				  global_smbus_slave->write_mem[global_smbus_slave->addr] = TWDR;
				  TWCR = (1<<TWEN) | (1<<TWEA) | (1<<TWINT) | I2CIE;
				  ++global_smbus_slave->addr;
				}
			}
			++global_smbus_slave->data_count;
			break;
		}
	  
		case TW_SR_STOP: {  // slave receiver stop
			if (global_smbus_slave->data_count == 1)
			{
				// received a single byte + stop --> single command
				#if ( SMBUS_SLAVE_USE_COMMAND_HOOK == 1 )
				smbus_slave_command_hook(global_smbus_slave->addr);
				#endif
			}
			else if (global_smbus_slave->data_count > 2)
			{
				#if ( SMBUS_SLAVE_USE_BLOCK_WRITE_DONE_HOOK == 1 )
				smbus_slave_block_write_done();
				#endif
			}
			TWCR = (1<<TWEN) | (1<<TWEA) | (1<<TWINT) | I2CIE;
			break;
		}
	  
		case TW_ST_DATA_ACK:
		case TW_ST_SLA_ACK: {  // slave transmitter
			TWDR = global_smbus_slave->response;
			TWCR = (1<<TWEN) | (1<<TWEA) | (1<<TWINT) | I2CIE;
			global_smbus_slave->addr++;
			// select next response byte
			global_smbus_slave->response = global_smbus_slave->read_mem[global_smbus_slave->addr];
			//if (global_smbus_slave->addr <= SMBUS_MAX_ADDR) {
				//smbus_mem->response = smbus_mem->bytes[smbus_mem->addr];
				//} else {
				//smbus_mem->response = 0;
			//}
			break;
		}
	  
		case TW_BUS_ERROR: {
			TWCR = (1<<TWEN) | (1<<TWSTO) | (1<<TWINT) | I2CIE;
			break;
		}
		default: TWCR = (1<<TWEN) | (1<<TWEA) | (1<<TWINT) | I2CIE;
	}
}

