# l2cap_reproducer
zephyr project to reproduce l2cap issue


Use one nrf52 eval board as HCI controller (683115854 has to be replaced with the actual serial number of the eval board):
> nrfjprog -f nrf52 -s 683115854 --program ./zephyr.hex --verify --chiperase

# Build the reproducer firmware and install it on an nRF5340 eval board
> rm -fr ./build && ZEPHYR_BASE=$(pwd)/../zephyr ZEPHYR_NRF_MODULE_DIR=$(pwd)/../nrf python -m west build  -b"nrf5340dk/nrf5340/cpuapp" --build-dir ./build --sysbuild  .

# debug (replace 1050012012 with the serial number of your nRF5340 eval board)
python -m west debug --domain 'l2cap_reproducer' --tool-opt="-select usb=1050012012"

It helps to set a break point at `assert_post_action` to get a good callback of the situation.

# Start Client
Change UART and ADDR in bluetooth.py to match your nRF52 uart name and your nRF5340s MAC address.

Run `python bluetooth.py` to start the client, that will connect to the nRF5340 eval board and will start to receive 4kB large L2CAP SDUs.

To trigger the bug: switch off the nRF52 eval board to cause a disconnect.