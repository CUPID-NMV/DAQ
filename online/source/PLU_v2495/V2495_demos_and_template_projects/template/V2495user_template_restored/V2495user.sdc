#************************************************************
# THIS IS A WIZARD-GENERATED FILE.                           
#
# Version 13.1.0 Build 162 10/23/2013 SJ Web Edition
#
#************************************************************

# Copyright (C) 1991-2013 Altera Corporation
# Your use of Altera Corporation's design tools, logic functions 
# and other software and tools, and its AMPP partner logic 
# functions, and any output files from any of the foregoing 
# (including device programming or simulation files), and any 
# associated documentation or information are expressly subject 
# to the terms and conditions of the Altera Program License 
# Subscription Agreement, Altera MegaCore Function License 
# Agreement, or other applicable license agreement, including, 
# without limitation, that your use is for the sole purpose of 
# programming logic devices manufactured by Altera and sold by 
# Altera or its authorized distributors.  Please refer to the 
# applicable agreement for further details.



# Clock constraints

create_clock -name "clk" -period 20.000ns [get_ports {clk}] -waveform {0.000 10.000}
create_clock -name "gate" -period 20.000ns [get_ports {GIN[1]}] -waveform {0.000 10.000}
create_clock -name "lbclk" -period 20.000ns


# Automatically constrain PLL and other generated clocks
derive_pll_clocks -create_base_clocks

# Automatically calculate clock uncertainty to jitter and other effects.
derive_clock_uncertainty

# Local bus
set_input_delay  -clock lbclk  -add_delay 5 [get_ports {nADS}]
set_input_delay  -clock lbclk  -add_delay 5 [get_ports {nBLAST}]
set_input_delay  -clock lbclk  -add_delay 5 [get_ports {WnR}]
set_input_delay  -clock lbclk  -add_delay 5 [get_ports {LAD*}]

set_output_delay  -clock lbclk  -add_delay 5 [get_ports {nREADY}]
set_output_delay  -clock lbclk  -add_delay 5 [get_ports {LAD*}]


# ************************************************
# SignalTap JTAG hub constrints
# ************************************************
create_clock -name altera_reserved_tck [get_ports {altera_reserved_tck}] -period "24 MHz"

set_clock_groups -asynchronous \
-group {altera_reserved_tck}

set_false_path -from [get_ports altera_reserved_tdi]
set_false_path -to [get_ports altera_reserved_tdi]

set_false_path -from [get_ports altera_reserved_tms]
set_false_path -to [get_ports altera_reserved_tms]

set_false_path -from [get_ports altera_reserved_ntrst]
set_false_path -to [get_ports altera_reserved_ntrst]

set_false_path -from [get_ports altera_reserved_tdo]
set_false_path -to [get_ports altera_reserved_tdo]


# ************************************************
# Constrints SPI interface
# ************************************************
create_generated_clock -name sclk_reg -source [get_ports {CLK}] -divide_by 50 [get_registers {gd_control:I_GD|spi_interface:I_SPI|spi_master:spi_core|spi_clk_reg}]
create_generated_clock -name spi_clock -source [get_registers {gd_control:I_GD|spi_interface:I_SPI|spi_master:spi_core|spi_clk_reg}] [get_ports {SPI_SCLK}]

 # apply timing constraints
 # data is latched at the rising edge of SPI_SCLK pin 
 set_output_delay -add_delay -clock { spi_clock } -max 10 [get_ports {SPI_CS SPI_MOSI}]
 set_output_delay -add_delay -clock { spi_clock } -min 2  [get_ports {SPI_CS SPI_MOSI}]
 
 # data is launched at the rising edge of SPI_SCLK pin 
 set_input_delay -add_delay  -clock { spi_clock } -max 10 [get_ports {SPI_MISO}]
 set_input_delay -add_delay  -clock { spi_clock } -min 2 [get_ports {SPI_MISO}]
 
 # set multicycle clock constraints
 set_multicycle_path -setup -start -from [get_clocks {clk}] -to [get_clocks {spi_clock}] 50
 set_multicycle_path -hold -start -from [get_clocks  {clk}] -to [get_clocks {spi_clock}] 50
 set_multicycle_path -setup -end -from [get_clocks {spi_clock}] -to [get_clocks {clk}] 50
 set_multicycle_path -hold -end -from [get_clocks spi_clock] -to [get_clocks {clk}] 50

# false paths
set_false_path -from [get_ports *A\[*\]]
set_false_path -from [get_ports *B\[*\]]
set_false_path -to [get_ports *C\[*\]]
set_false_path -to [get_ports {SELG}]
set_false_path -from [get_ports {nLBRES}]
set_false_path -from [get_ports *GIN\[0\]]
set_false_path -to [get_ports {SPI_SCLK}]


set_false_path -from [get_clocks {gate}] -to [get_clocks {clk}]
set_false_path -from [get_clocks {clk}] -to [get_clocks {gate}]