from flask import logging
import os

from printer import Printer

__author__ = 'marcus'
_logger = logging.getLogger(__name__)

_reset_pin = "P9_12"
_default_serial_port = "/dev/ttyO1"
_create_serial_port_script = "echo BB-UART1 > /sys/devices/bone_capemgr.9/slots"
pwm_config = [
    {
        'temp': 'P9_39',
        'out': 'P9_14',
        'current_input': 1,
        'high_power': True
    },
    {
        'temp': 'P9_40',
        'out': 'P9_16',
        'current_input': 0,
        'high_power': True
    },
    {
        'temp': 'P9_37',
        'out': 'P8_19',
        'high_power': False
    }
]
ALLOWED_EXTENSIONS = {'gcode'}


def check_for_serial_port():
    if not os.path.exists(_default_serial_port):
        _logger.info("No serial port %s, creating it", _default_serial_port)
        os.system(_create_serial_port_script)
    return _default_serial_port


def create_printer():
    serial_port_ = check_for_serial_port()
    printer = Printer(serial_port=serial_port_, reset_pin=_reset_pin)

    #basically the printer is just a bunch of stuff
    return printer


def allowed_file(filename):
    return '.' in filename and \
           filename.rsplit('.', 1)[1] in ALLOWED_EXTENSIONS
