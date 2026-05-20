-- gatep.vhd
-- -----------------------------------------------------------------------
-- V2495 User Gate Pattern core logic
-- -----------------------------------------------------------------------
--  Date        : Jul 2016
--  Contact     : support.nuclear@caen.it
-- (c) CAEN SpA - http://www.caen.it   
-- -----------------------------------------------------------------------
--
--    Functions
--    ------------------------
-- 
--    This module implements some simple logic operation between A and B port
--    input signals. 
--    The main purpose of this demo design is to demonstrate latching of
--    input ports inside a gate window.
--
--    Register map
--    ------------------------
--
--    | Type         | Address | Access |  Description                        |
--    |--------------|---------|--------|-------------------------------------|
--    | mon_regs(0)  | 0x1000  |   R    | Firmware revision                   |
--    | mon_regs(1)  | 0x1004  |   R    | Value of port A                     |
--    | mon_regs(2)  | 0x1008  |   R    | Value of port B                     |
--    | mon_regs(3)  | 0x100C  |   R    | Value of port C                     |
--    | mon_regs(4)  | 0x1010  |   R    | Status register                     |
--    | ctrl_regs(0) | 0x1800  |   R/W  | Set Mask for port A                 |
--    | ctrl_regs(1) | 0x1804  |   R/W  | Set Mask for port B                 |
--    | ctrl_regs(2) | 0x1808  |   R/W  | Control register                    |
--    | ctrl_regs(3) | 0x180C  |   R/W  | Set C user value                    |
--  
--    Status register bit map
--    ------------------------
--              4     3    2   1    0                  
--            +----+----+----+----+----+
--    0x1010  | CRS| GRS| EC | EG | DR | 
--            +----+----+----+----+----+  
--               |    |    |    |    |----------> Data Ready
--               |    |    |    |---------------> External Gate status
--               |    |    |--------------------> External Clear status
--               |    |-------------------------> Gate status (control reg)
--               |------------------------------> Clear status (control reg)
--          
--    Control register bit map
--    ------------------------
--               8   7     6    5   4    3      0                  
--            +----+---------+----+----+---------+
--    0x1808  |LEV |         |CLR |GATE| CSEL    | 
--            +----+---------+----+----+---------+  
--               |      |      |    |       |-----> C register function select
--               |      |      |    |-------------> SW Gate
--               |      |      |------------------> SW clear
--               |      |-------------------------> Reserved
--               |--------------------------------> NIM(0)/TTL(1) select
---
-- -----------------------------------------------------------------------
-- $Id$
-- -----------------------------------------------------------------------

library ieee;
   use IEEE.std_logic_1164.all;
    use IEEE.std_logic_arith.all;
    use IEEE.std_logic_unsigned.all;
   use IEEE.numeric_std.all;

   use work.V2495_pkg.all;
   
-- ----------------------------------------------
entity gatep is
-- ----------------------------------------------
    port(
        CLK           : in std_logic;
        
        -- I/O ports
        A             : in  std_logic_vector(31 downto 0);  -- in a  (32 x lvds/ecl)
        B             : in  std_logic_vector(31 downto 0);  -- in b  (32 x lvds/ecl)
        C             : out std_logic_vector(31 downto 0);  -- out c (32 x lvds)
        nOEG          : out std_logic;
        SELG          : out std_logic;
        GIN           : in  std_logic_vector( 1 downto 0);
                
        -- Register interface          
        mon_regs   : out  MONITOR_REGS_T;
        ctrl_regs  : in   CONTROL_REGS_T     
    );
end gatep ;

-- ---------------------------------------------------------------
architecture rtl of gatep is
-- ---------------------------------------------------------------

  type gate_sig is array(0 to 3) of std_logic_vector(31 downto 0);
  type capt_sig is array(0 to 3) of std_logic_vector(31 downto 0);
  
  signal ext_clear         : std_logic;
  signal ext_gate          : std_logic;
  
  -- Address latched from the LAD bus
  signal clear_reg         : std_logic;
  signal gate              : std_logic;    
  signal en_gate           : std_logic;
  signal data_ready        : std_logic;
  signal chan_A            : std_logic_vector(31 downto 0);
  signal chan_B            : std_logic_vector(31 downto 0);
  signal chan_AandB        : std_logic_vector(31 downto 0);
  signal chan_AorB         : std_logic_vector(31 downto 0);
  signal en_gate32         : std_logic_vector(31 downto 0);    
  signal gate32            : std_logic_vector(31 downto 0);
  signal gated             : gate_sig;
  signal captd             : capt_sig; 
    
  -- Data Producer signals
  signal mask_A            : std_logic_vector(31 downto 0);
  signal mask_B            : std_logic_vector(31 downto 0);
  signal ctrl_reg          : std_logic_vector(31 downto 0);
  signal user_value_C      : std_logic_vector(31 downto 0);
  signal reg_status        : std_logic_vector(31 downto 0);
  signal output_C          : std_logic_vector(31 downto 0);
  signal clkcnt32          : std_logic_vector(31 downto 0);
  
-----\
begin --
-----/

  -- Port G 
  -----------------------------------
  
  -- Port G is input only in this demo.
  -- NIM/TTL selection is configurable through control register.
  nOEG         <=  '1';
  SELG         <=  ctrl_reg(8);
 
  -- G Input
  -- G(0) is an asynchronous clear for internal counters and flags.
  -- G(1) is an external gate
  -- 
  ext_clear <=  GIN(0) xnor ctrl_reg(8);
  ext_gate  <=  GIN(1) xnor ctrl_reg(8);
  
  -- Monitor Registers drivers
  -----------------------------------
  
  mon_regs(0)   <= std_logic_vector(To_Unsigned(FWREV,32));  -- Firmware release
  mon_regs(1)   <= A;                                        -- Port A status
  mon_regs(2)   <= B;                                        -- Port B status
  mon_regs(3)   <= output_C;
  mon_regs(4)   <= reg_status;
  
  reg_status(0) <=  data_ready;
  reg_status(1) <=  ext_gate;
  reg_status(2) <=  ext_clear;
  reg_status(3) <=  ctrl_reg(4);
  reg_status(4) <=  ctrl_reg(5);
  reg_status(31 downto 5) <= (others => '0');

  -- Control Registers 
  -----------------------------------
  mask_A       <= ctrl_regs(0);
  mask_B       <= ctrl_regs(1);
  ctrl_reg     <= ctrl_regs(2);
    
  -- Port C driver 
  -----------------------------------
  output_C <=  gated(0)       when ctrl_reg(3 downto 0) = X"0" else -- gate A
               gated(1)       when ctrl_reg(3 downto 0) = X"1" else -- gate B
               gated(2)       when ctrl_reg(3 downto 0) = X"2" else -- gate A AND B
               gated(3)       when ctrl_reg(3 downto 0) = X"3" else -- gate A OR B
               captd(0)       when ctrl_reg(3 downto 0) = X"4" else -- capture A
               captd(1)       when ctrl_reg(3 downto 0) = X"5" else -- capture B
               captd(2)       when ctrl_reg(3 downto 0) = X"6" else -- capture A AND B
               captd(3)       when ctrl_reg(3 downto 0) = X"7" else -- capture A OR B              
               clkcnt32       when ctrl_reg(3 downto 0) = X"8" else -- clock counter
               user_value_C;                                        -- register value
    
  C <= output_C; 

  chan_A      <=  A         and mask_A;
  chan_B      <=  B         and mask_B; 
  chan_AandB  <=  chan_A    and chan_B;
  chan_AorB   <=  chan_A    or  chan_B;  
  clear_reg   <=  ext_clear or  ctrl_reg(5);
  gate        <=  ext_gate  or  ctrl_reg(4); -- Gate active low
  gate32      <=  (others => gate);
  en_gate32   <=  (others => en_gate); 
     
  -- Output C generation
  -- 0x0
  gated(0) <= chan_A and gate32 and en_gate32;
  -- 0x1
  gated(1) <= chan_B and gate32 and en_gate32;
  -- 0x2
  gated(2) <= chan_AandB and gate32 and en_gate32;
  -- 0x3
  gated(3)  <= chan_AorB and gate32 and en_gate32;
  -- 0x4/7
  gen_evt_captured: for j in 0 to 3 generate
    gen_chan_captured: for i in 0 to 31 generate  
      capture_process: process(gated(j)(i), clear_reg)
      begin
          if(clear_reg = '1') then
              captd(j)(i) <= '0';
          elsif(rising_edge(gated(j)(i))) then
              captd(j)(i) <= '1';
          end if;
      end process capture_process;          
    end generate gen_chan_captured;
  end generate gen_evt_captured;
  -- 0x8  
  clkcnt_proc: process(clk,clear_reg)
    begin   
        if(clear_reg='1') then
            clkcnt32 <= (others=>'0');
        elsif(rising_edge(clk)) then
            clkcnt32 <= clkcnt32 + 1;
        end if;
    end process;  
  -- 0x9
  user_value_C <= ctrl_regs(3);
  ----------------
          
  -- Updating Status Register
    process(gate, clear_reg)
    begin
        if(clear_reg = '1') then
            data_ready   <= '0';
        elsif(gate'event and gate = '1') then
            data_ready <= '1';
        end if;
    end process;
  
    process(gate, clear_reg)
    begin
        if(clear_reg = '1') then
            en_gate  <= '1';
        elsif(gate'event and gate = '0') then
            en_gate  <=  not data_ready;
        end if;
    end process;
                    
END RTL;



