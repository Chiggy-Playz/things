cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(node)

target_include_directories(app PRIVATE lib)

target_sources(app PRIVATE
    src/main.c
    lib/thread/thread.c
    lib/battery/battery.c
)
target_sources_ifdef(CONFIG_BLINKY app PRIVATE lib/blinky/blinky.c)
target_sources_ifdef(CONFIG_THREAD_STATUS_LED app PRIVATE lib/thread_status_led/thread_status_led.c)
target_sources_ifdef(CONFIG_BLINKY_THREAD app PRIVATE lib/blinky_thread/blinky_thread.c)

if(CONFIG_IR_VOLTAS OR CONFIG_IR_TECO)
    target_sources(app PRIVATE lib/ir/ir.c)
endif()

target_sources_ifdef(CONFIG_IR_VOLTAS app PRIVATE lib/ir/voltas.c)
target_sources_ifdef(CONFIG_IR_TECO app PRIVATE lib/ir/teco.c)
