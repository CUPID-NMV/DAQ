-- lb_int.vhd
-- -----------------------------------------------------------------------
-- V2495 User Local Bus Interface (slave)
-- -----------------------------------------------------------------------
--  Date        : Jul 2016
--  Contact     : support.nuclear@caen.it
-- (c) CAEN SpA - http://www.caen.it   
-- -----------------------------------------------------------------------
-- 
--    Functions
--    ------------------------
--
--    This module implements local bus slave interface.
--    Only single register read/write operations are supported.
--    Registers can be implemented into the 0x1000-0x7EFF address range.
--    Register space is divided into monitor registers (read only) and
--    control registers (read/write access).
--    Both registers are available to the external logic through two
--    ports (mon_regs/ctrl_regs), which are arrays of 32-bit values.
--    A dedicated interface is available for an external gate and dely control
--    IP: its configuration registers are mapped in the 'x7f00-0x7F10 address
--    range over local bus (see package declaration).
--    
-- -----------------------------------------------------------------------
-- $Id$
-- -----------------------------------------------------------------------

library IEEE;
use IEEE.std_logic_1164.all;
use IEEE.numeric_std.all;  
use IEEE.std_logic_unsigned.all;  
use work.V2495_pkg.all;
     
-- ----------------------------------------------
entity lb_int is
-- ----------------------------------------------
    port(
      reset      : in     std_logic;                     -- reset (active high)
      clk        : in     std_logic;                     -- System clock (50 MHz)
      -- Local Bus in/out signals
      nBLAST     : in     std_logic;                     -- Last transfer (active low)
      WnR        : in     std_logic;                     -- Write/Read
      nADS       : in     std_logic;                     -- Address strobe (active low)
      nREADY     : out    std_logic;                     -- Slave ready (active low)
      LAD        : inout  std_logic_vector(15 DOWNTO 0); -- Data/Address bus

      -- Gate/Delay registers  
      gd_ready   : in  std_logic;                        -- Ready from gd_control
      gd_data_rd : in  std_logic_vector(31 downto 0);    -- Data read from G&D 
      gd_write   : out std_logic;                        -- Write strobe for G&D
      gd_read    : out std_logic;                        -- Read Strobe for G&D
      gd_data_wr : out std_logic_vector(31 downto 0);    -- Data to write to G&D
      gd_command : out std_logic_vector(15 downto 0);    -- G&D command
      
      -- Register interface          
      mon_regs   : in     MONITOR_REGS_T;                -- Monitor registers
      ctrl_regs  : out    CONTROL_REGS_T                 -- Control registers
    );
end lb_int;

-- ---------------------------------------------------------------
architecture rtl of lb_int is
-- ---------------------------------------------------------------

    type   LBSTATE_type is (LBIDLE, LBWRITEL, LBWRITEH, LBREADL, LBREADH);
    signal LBSTATE : LBSTATE_type;
    
    -- Output Enable of the LAD bus (from User to Vme)
    signal LAD_oe     : std_logic;
    -- Data Output to the local bus
    signal LAD_out    : std_logic_vector(15 downto 0);
    -- Lower 16 bits of the 32 bit data
    signal dtl       : std_logic_vector(15 downto 0);
    -- Address latched from the LAD bus
    signal addr      : std_logic_vector(15 downto 0);
    
    signal gd_control_reg : std_logic_vector(31 downto 0);
    signal gd_command_reg : std_logic_vector(31 downto 0);

    signal ctrl_regs_int : CONTROL_REGS_T;

    signal gd_data_wr_reg : std_logic_vector(31 downto 0);
    
-----\
begin --
-----/
  
  -- Local Bus data bidirectional control
  LAD <= LAD_out when LAD_oe = '1' else (others => 'Z');

  ctrl_regs  <= ctrl_regs_int;
  
  -- G&D strobes (read/write strobes)
  gd_write   <= gd_control_reg(0);
  gd_read    <= gd_control_reg(1);
  
  gd_command <= gd_command_reg(15 downto 0);
  gd_data_wr <= gd_data_wr_reg;
  
    
  -- --------------------------
  --  Local Bus state machine
  -- --------------------------  
  process(clk, reset)
        variable rreg, wreg   : std_logic_vector(31 downto 0);
  begin
    if (reset = '1') then
      nREADY        <= '1';
      LAD_oe        <= '0';
      rreg          := (others => '0');
      wreg          := (others => '0');
      addr          <= (others => '0');
      dtl           <= (others => '0');
      LAD_out       <= (others => '0');
      ctrl_regs_int <= (others=>(others => '0'));
      LBSTATE       <= LBIDLE;
    elsif rising_edge(clk) then
      
      case LBSTATE is
        
        -- Idle state.
        -- Wait for local bus start of cycle.
        -- If an address strobe is given,
        -- the address is latched and access type is decoded.
        when LBIDLE  =>  
          LAD_oe   <= '0';
          nREADY  <= '1';
          if (nADS = '0') then        -- start cycle
            addr <= LAD;              -- Address Sampling
            if (WnR = '1') then 
              -- Write Access
              nREADY   <= '0';
              LBSTATE  <= LBWRITEL;     
            else                      
              -- Read Access
              nREADY    <= '1';
              LBSTATE   <= LBREADL;
            end if;
          end if;

        -- Latch data to write (lower 16-bit)  
        when LBWRITEL => 
          dtl <= LAD;
          if (nBLAST = '0') then
            LBSTATE  <= LBIDLE;
            nREADY   <= '1';
          else
            LBSTATE  <= LBWRITEH;
          end if;
                       
        -- Write register                       
        when LBWRITEH =>   
          
          wreg  := LAD & dtl;  

          nREADY   <= '1';
          LBSTATE  <= LBIDLE;
          
          if(addr<LOCALBUS_LAST_ADDRESS) then
            --loop on control registers
            for i in N_CONTROL_REGS-1 downto 0 loop 
              if std_logic_vector(unsigned(CONTROL_REGS_REGION_START)+
                                  to_unsigned(4*i,16)) = addr then
                ctrl_regs_int(i) <= wreg;
              end if;
            end loop;      
          else
            case addr is
              when as_gd_data_wr        =>  gd_data_wr_reg   <= wreg;
              when as_gd_command        =>  gd_command_reg   <= wreg;
              when as_gd_control        =>  gd_control_reg   <= wreg;
              when others               =>  null;                                         
            end case;
          end if;
            
        -- Read register 
        -- transfer lower 16-bit register content        
        when LBREADL =>  
          
          nREADY    <= '0';
          rreg      := UNMAPPED_REGISTER_VALUE;

          if(addr<LOCALBUS_LAST_ADDRESS) then
            for i in NREG-1 downto 0 loop 
              if i<N_MONITOR_REGS then
                if std_logic_vector(unsigned(MONITOR_REGS_REGION_START)+
                   to_unsigned(4*i,16))=addr then
                  rreg := mon_regs(i); 
                end if;
              else
                if std_logic_vector(unsigned(CONTROL_REGS_REGION_START)+
                   to_unsigned(4*(i-N_MONITOR_REGS),16))=addr then
                  rreg := ctrl_regs_int(i-N_MONITOR_REGS); 
                end if;
              end if;                 
            end loop;   
            
          else
           
            case addr is
              when as_gd_data_wr        =>  rreg := gd_data_wr_reg;
              when as_gd_data_rd        =>  rreg := gd_data_rd;
              when as_gd_command        =>  rreg := gd_command_reg;
              when as_gd_control        =>  rreg := gd_control_reg;
              when as_gd_status         =>  rreg := (0 => gd_ready, others => '0');
              when others               =>  null;
            end case;
          end if;
     
          LBSTATE <= LBREADH;
          LAD_out <= rreg(15 downto 0);
          LAD_oe  <= '1';
        
        -- Read register
        -- Transfer upper 16-bit register content
        when LBREADH =>  
          
          LAD_out <= rreg(31 downto 16);
          LBSTATE <= LBIDLE;
     
         end case;

    end if;
  end process;

end rtl;

