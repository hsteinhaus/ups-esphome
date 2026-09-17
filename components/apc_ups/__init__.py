import esphome.codegen as cg
from esphome.components import usb_host

DEPENDENCIES = ["usb_host"]
CODEOWNERS = ["@hsteinhaus"]

APC_VID = 0x051D  # American Power Conversion
BX950MI_PID = 0x0002

apc_ups_ns = cg.esphome_ns.namespace("apc_ups")
APCUPSClient = apc_ups_ns.class_("APCUPSClient", usb_host.USBClient)

CONFIG_SCHEMA = usb_host.usb_device_schema(APCUPSClient, vid=APC_VID, pid=BX950MI_PID)


async def to_code(config):
    await usb_host.register_usb_client(config)
