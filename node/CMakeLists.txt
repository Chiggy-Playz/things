cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(node)

target_include_directories(app PRIVATE lib)

target_sources(app PRIVATE
    src/main.c
    lib/thread/thread.c
)
target_sources_ifdef(CONFIG_BLINKY app PRIVATE lib/blinky/blinky.c)

if(CONFIG_IR_VOLTAS OR CONFIG_IR_TECO)
    target_sources(app PRIVATE lib/ir/ir.c)
endif()

target_sources_ifdef(CONFIG_IR_VOLTAS app PRIVATE lib/ir/voltas.c)
target_sources_ifdef(CONFIG_IR_TECO app PRIVATE lib/ir/teco.c)
