import asyncio
import contextlib
import logging
import datetime
from enum import Enum
from typing import Optional

from bumble.core import UUID
from bumble.device import Advertisement, AdvertisingData, Device, Peer
from bumble.gatt_client import ServiceProxy
from bumble.hci import Address
from bumble.keys import JsonKeyStore
from bumble.l2cap import LeCreditBasedChannelSpec
from bumble.pairing import PairingConfig
from bumble.transport import Transport, open_transport_or_link

UART = "/dev/cu.usbmodem0006831158541"
ADDR = Address("F8:A1:02:83:D9:75", Address.RANDOM_DEVICE_ADDRESS)

class State(Enum):
    IDLE = 1
    CONNECTING = 2
    CONNECTED = 3
    CLOSING = 4


class BluetoothController:
    hci: Optional[Transport]
    device: Optional[Device]

    def __init__(
        self,
        transport_spec,
        keystore_file,
        on_advertisement,
        on_connection,
        on_disconnect,
    ):
        self.state = State.IDLE
        self.should_stop = False
        self.logger = logging.getLogger(name="bluetooth")
        self.logger.setLevel(logging.DEBUG)
        self.transport_spec = transport_spec
        self.keystore_file = keystore_file
        self._on_advertisement = on_advertisement
        self._on_connection = on_connection
        self._on_disconnect = on_disconnect

    def _on_event_disconnection(self, reason):
        if self._on_disconnect is not None:
            self._on_disconnect(reason)

    def _on_event_advertisement(self, advertisement: Advertisement):

        if advertisement.address == ADDR:
            self.logger.info(f"found reproducer {advertisement.address}")
            if self.pod_address_future is not None:
                self.pod_address_future.set_result(advertisement.address)
            if self._on_advertisement is not None:
                self._on_advertisement(advertisement.address, "TODO: USE ACTUAL SERIAL")

    async def scan(self):
        self.logger.info("scanning")
        self.state = State.CONNECTING
        self.pod_address_future = asyncio.get_running_loop().create_future()
        self.device.on("advertisement", self._on_event_advertisement)
        await self.device.start_scanning()
        pod_address = await self.pod_address_future
        self.pod_address_future = None
        await self.device.stop_scanning()
        return pod_address

    def on_mtu_and_psm(self, data: [bytes]):
        if self.control_point_future is not None:
            if data[0] == 0:
                self.control_point_future.set_result(
                    (data[1] | data[2] << 8, data[3] | data[4] << 8)
                )
            elif data[0] == 1:
                self.logger.info("MQTT SN over L2CAP started")
                self.control_point_future.set_result(None)

    async def connect(self, address):
        self.state = State.CONNECTING
        self.logger.info(f"connecting to {address}")
        try:
            connection = await self.device.connect(address)
        except Exception as e:
            self._on_event_disconnection(e)
        connection.on("disconnection", self._on_event_disconnection)

        mtu = 2048
        psm = 128

        self.logger.debug(f"using MTU {mtu} and PSM {psm}")
        channel = await connection.create_l2cap_channel(
            LeCreditBasedChannelSpec(psm=128, mtu=2048)
        )

        if self._on_connection is not None:
            self._on_connection(channel)

        return channel

    async def start(self):
        self.logger.info("requesting MQTT start")
        self.control_point_future = asyncio.get_running_loop().create_future()
        await self.characteristic.write_value([1])
        await self.control_point_future
        self.state = State.CONNECTED

    async def open(self):
        self.state = State.CONNECTING
        try:
            self.hci = await open_transport_or_link(self.transport_spec)
            self.device = Device.with_hci(
                "Lovgw",
                Address("F0:F1:F2:F3:F4:F5"),
                self.hci.source,
                self.hci.sink,
            )

            self.device.pairing_config_factory = lambda _: PairingConfig(
                sc=True,
                mitm=False,
                bonding=False,
                identity_address_type=PairingConfig.AddressType.PUBLIC,
            )
            self.device.keystore = JsonKeyStore.from_device(
                self.device, filename=self.keystore_file
            )
            self.device.listener
            await self.device.power_on()
        except BaseException as e:
            await self.close()
            raise e

    async def open_and_scan(self):
        try:
            await self.open()
        except BaseException as e:
            if self._on_disconnect is not None:
                self._on_disconnect(e)

        try:
            await self.scan()
        except BaseException as e:
            await self.close()
            if self._on_disconnect is not None:
                self._on_disconnect(e)

    async def close(self):
        self.state = State.CLOSING
        try:
            if self.device is not None:
                await self.device.host.reset()
                await self.device.power_off()
                self.device = None
            if self.hci is not None:
                await self.hci.close()
                self.hci = None
        finally:
            self.state = State.IDLE


async def run():
    bt = BluetoothController(f"serial:{UART}", "keystore.json", None, None, None)
    await bt.open()
    address = await bt.scan()
    channel = await bt.connect(address)

    def datagram_reader(data: bytes):
        print(f"received {data}")
        channel.write(b"\x00\x01\x02\x03")

    print("got channel")
    channel.sink = datagram_reader

    loop = asyncio.get_running_loop()
    end_time = loop.time() + 500.0
    while True:
        print(datetime.datetime.now())
        if (loop.time() + 1.0) >= end_time:
            break
        await asyncio.sleep(1)

    bt.disconnect()

if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    asyncio.run(run())
