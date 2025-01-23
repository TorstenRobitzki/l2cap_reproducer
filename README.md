# l2cap_reproducer
zephyr project to reproduce l2cap issue


Use one nrf52 eval board as HCI controller (683115854 has to be replaced with the actual serial number of the eval board):
> nrfjprog -f nrf52 -s 683115854 --program ./zephyr.hex --verify --chiperase

# Build:
> rm -fr ./build && ZEPHYR_BASE=$(pwd)/../zephyr ZEPHYR_NRF_MODULE_DIR=$(pwd)/../nrf python -m west build  -b"nrf5340dk/nrf5340/cpuapp" --build-dir ./build --sysbuild  .

# debug
python -m west debug --domain 'l2cap_reproducer' --tool-opt="-select usb=1050012012"