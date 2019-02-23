#
# This file is part of GreatFET
#

from ..peripheral import GreatFETPeripheral
import struct

class JTAG(GreatFETPeripheral):
    CoreID=0
    DeviceID=0
    JTAGID=0

    def __init__(self, board):
        """
            Initialize a new MSP430 JTAG instance.

            Args:
                board -- The GreatFET board connected to the target.
        """
        self.board = board

    def setup(self):
        """Initialise the JTAG hardware and target device."""
        self.board.apis.jtag.setup()
        
    def stop(self):
        """Stop debugging."""
        self.board.apis.jtag.stop()
    
    def get_deviceid(self, chip):
        """
            Get the Device ID.

            Args:
                chip -- Target chip number.
        """
        device_id = self.board.apis.jtag.get_device_id()
        return device_id
 
    def shift_ir_8(self, instruction):
        """Shift the 8-bit Instruction Register."""
        data=[ins]
        self.writecmd(self.MSP430APP,0x80,1,data)
        return ord(self.data[0])
    
    def shift_dr_16(self, data):
        """Shift the 16-bit Data Register."""
        data=[dat&0xFF,(dat&0xFF00)>>8]
        self.writecmd(self.MSP430APP,0x81,2,data)
        return ord(self.data[0])#+(ord(self.data[1])<<8)
