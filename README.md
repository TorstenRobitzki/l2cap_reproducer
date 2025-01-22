# l2cap_reproducer
zephyr project to reproduce l2cap issue

# Build:
> rm -fr ./build && ZEPHYR_BASE=$(pwd)/../zephyr ZEPHYR_NRF_MODULE_DIR=$(pwd)/../nrf python -m west build  -b"nrf5340dk/nrf5340/cpuapp" --build-dir ./build --sysbuild  .
