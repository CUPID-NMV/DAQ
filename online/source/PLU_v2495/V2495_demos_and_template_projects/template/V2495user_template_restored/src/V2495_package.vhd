-- V2495_package.vhd
-- -----------------------------------------------------------------------
-- Package for V2495 Template demo (V2495_pkg)
-- -----------------------------------------------------------------------
--  Date        : Jul 2016
--  Contact     : support.nuclear@caen.it
-- (c) CAEN SpA - http://www.caen.it   
-- -----------------------------------------------------------------------
--
--    Functions
--    ------------------------
--
--    Declaration of constants and types for register arrays.
--
-- -----------------------------------------------------------------------
-- $Id$
-- -----------------------------------------------------------------------
library IEEE;
    use IEEE.std_logic_1164.all;

package V2495_pkg is

    -- Firmware revision
    constant FWREV : integer := 1;
    
    -- Demo Identification number
    constant DEMO_NUMBER : integer := 0;
    
    -- Default value for unmapped registers
    constant UNMAPPED_REGISTER_VALUE : std_logic_vector(31 downto 0) := X"DEADFACE";

    constant N_MONITOR_REGS : integer := 0; -- Number of mapped monitor regs
    constant N_CONTROL_REGS : integer := 0; -- Number of mapped control regs 
    
    -- Types for monitor and control register arrays
    type CONTROL_REGS_T is ARRAY (0 to N_CONTROL_REGS-1) of 
                           STD_LOGIC_VECTOR(31 downto 0);
    type MONITOR_REGS_T is ARRAY (0 to N_MONITOR_REGS-1) of 
                           STD_LOGIC_VECTOR(31 downto 0);
    
    -- Register constants
    ----------------------------------------------------------------------------
    -- Registers are divided into monitors (read only) and control (R/W).
    -- Monitor registers are mapped onto the lcoal bus address map starting from
    -- address MONITOR_REGS_REGION_START.
    -- Control registers are mapped onto the lcoal bus address map starting from
    -- address CONTROL_REGS_REGION_START.
    -- Register address is 32-bit aligned and it can be calculated based on its
    -- index in the registers array.
    -- Monitor registers are mapped to address :
    --     MONITOR_REGS_REGION_START+4*i (i = index in the monitor regs array)
    -- Control registers are mapped to address :
    --     CONTROL_REGS_REGION_START+4*i (i = index in the control regs array)
    constant MONITOR_REGS_REGION_START : 
           std_logic_vector(15 downto 0) := X"1000"; -- start of monitor regs
    constant CONTROL_REGS_REGION_START : 
           std_logic_vector(15 downto 0) := X"1800"; -- start of control regs

    constant LOCALBUS_LAST_ADDRESS : 
           std_logic_vector(15 downto 0) := X"7EFF"; -- last address of local 
                                                     -- bus space
    
    constant NREG : integer := 
             N_CONTROL_REGS+N_MONITOR_REGS; -- Total number of mapped registers
    
    
    -- Addresses of gate and delay controller internal registers
    constant as_gd_data_wr : std_logic_vector(15 downto 0) := X"7F00";
    constant as_gd_command : std_logic_vector(15 downto 0) := X"7F04";
    constant as_gd_control : std_logic_vector(15 downto 0) := X"7F08";
    constant as_gd_data_rd : std_logic_vector(15 downto 0) := X"7F0C";
    constant as_gd_status  : std_logic_vector(15 downto 0) := X"7F10";

end V2495_pkg;