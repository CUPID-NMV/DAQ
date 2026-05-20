-- gd_control.vhd
-- -----------------------------------------------------------------------
-- V2495 Gate and Delay Control
-- -----------------------------------------------------------------------
--  Date        : Jul 2016
--  Contact     : support.nuclear@caen.it
-- (c) CAEN SpA - http://www.caen.it   
-- -----------------------------------------------------------------------
--
--    Functions
--    ------------------------
--
--    Gate and Delay control (gd_control) allows to configure the 
--    V2495 gate and delay generator component through a dedicated serial
--    connection (SPI).
--    The gate and delay parameters can be changed by using a simple
--    register interface.
--    
--    Programming interface
--    ----------------------
--  
--    In order to read/write registers or to serialize a command to the
--    external G&D component a command register must be conveniently set.
--
--             15      12 11              8  7         0
--             +---------+------------------+----------+
--    Command  |  Opcode | SPI Write Opcode | DelaySel | 
--             +---------+------------------+----------+  
--
--    Opcodes:
--      Write:
--        2 = update delay register       (steps) 
--        3 = update gate width register  (steps)
--        others => skip register update
--
--      Write sub-opcodes (SPI request):
--        0 = reset state
--        1 = program Gate and Delay from register content
--        2 = reserved
--        3 = reserved
--        4 = reserved
--        5 = Program registers content to all channels (broadcast)
--
--      Read:
--        2 = read delay from G&D generator      (steps) 
--        3 = read gate width from G&D generator (steps)
--
--        The channel is selected by the DelaySel subfield in command.
--
--
--        The channel is selected by the DelaySel subfield in command.
--        In case of SPI broadcast command, there is no need for a channel
--        selection, since all 32 channels are automatically written.
-- -----------------------------------------------------------------------
-- $Id$
-- -----------------------------------------------------------------------

library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;

-- ----------------------------------------------
entity gd_control is
-- ----------------------------------------------
    port
    (
        reset        : in  std_logic;                        -- Reset
        clk          : in  std_logic;                        -- Clock 
        
        -- Programming interface
        write        : in  std_logic;                        -- Write command strobe
        read         : in  std_logic;                        -- Read  command strobe
        command      : in  std_logic_vector(15 downto 0);    -- Command 
        writedata    : in  std_logic_vector(31 downto 0);    -- Data for command ops    
        readdata     : out std_logic_vector(31 downto 0);    -- Data read result
        ready        : out std_logic;                        -- Interface ready
        
        -- Gate&Delay control interface (SPI)        
        spi_miso     : in    std_logic;                      -- SPI Master In Slave Out
        spi_sclk     : out   std_logic;                      -- SPI clock
        spi_cs       : out   std_logic;                      -- SPI chip select
        spi_mosi     : out   std_logic                       -- SPI Master Out Slave In
                                          
    );
end gd_control;

-- ---------------------------------------------------------------
architecture rtl of gd_control is
-- ---------------------------------------------------------------

    constant TAPS : natural := 32;
    
    type   state_machine is (idle, write1, write2, write3, write4,  
                             write7, write_end, read0, read_end);
    
    signal state              : state_machine :=  idle;

    signal Delay           : std_logic_vector(15 downto 0);
    signal GateWidth       : std_logic_vector(15 downto 0);
    signal Resolution      : std_logic_vector( 7 downto 0);

    signal spi_address        : std_logic_vector(31 downto 0);
    signal spi_data_write     : std_logic_vector(31 downto 0);
    signal spi_data_read      : std_logic_vector(31 downto 0);
    signal spi_write_strobe   : std_logic;
    signal spi_read_strobe    : std_logic;
    signal data_valid         : std_logic;
    
    signal address_ch         : Unsigned(7 downto 0):=x"00";
      
begin
 
  P_CONTROL:process(reset, clk)
  begin
  
    if (reset='1') then 
      state <= idle;
      ready <= '1';
      spi_write_strobe <= '0';
      spi_read_strobe  <= '0';
    elsif rising_edge(clk) then
          
      case state is
        when idle => 
          ready <='1';
          if write ='1' then
              ready <= '0';             
              case command(15 downto 12) is   
                  when x"1" => null; --strobe command
                  when x"2" => Delay      <= writedata(15 downto 0); 
                  when x"3" => GateWidth  <= writedata(15 downto 0); 
                  when others => null;
              end case;
              state<=write1;
          
          elsif read='1' then
              ready<='0';
              case command (15 downto 12)is   
                  when x"2" =>                    
                      spi_address<= x"0000" & x"10" & command(7 downto 0);
                      spi_read_strobe<='1';                           
                      state<=read0;
                  when x"3" =>                    
                      spi_address<= x"0000" & x"20" & command(7 downto 0);
                      spi_read_strobe<='1';
                      state<=read0;
                  when others => null;
              end case;
          end if;
                
        when write1 =>
          case command (11 downto 8) is
            when x"0" => 
              state<= idle;
            when x"1" =>
              spi_data_write<=x"0000" & 
                              Std_logic_vector((Unsigned(Delay) +
                              Unsigned(GateWidth))) ; --N1-A
              spi_address<= x"0000" & x"10" & command(7 downto 0);
              spi_write_strobe<='1';
              if data_valid='1' then
                state<=write2;
                spi_write_strobe<='0';
              end if;
            when x"5" => --BROADCAST --N1-A
              spi_data_write<=x"0000" & 
                              Std_Logic_Vector(Unsigned(Delay) + 
                              Unsigned(GateWidth)) ;--N1-A
              spi_address<= x"0000" & x"10" & Std_Logic_vector(address_ch);
              spi_write_strobe<='1';
              if data_valid='1' then
                if address_ch/=(To_Unsigned(TAPS, address_ch'length)-1) then    
                  address_ch<=address_ch+1;
                  spi_write_strobe<='0';
                else
                  address_ch<=x"00";
                  state<=write7;
                  spi_write_strobe<='0';
                end if;
              end if; 
            when others => state<= idle;
          end case;
          
        when write2 =>      
          spi_data_write<=x"0000" & Delay;
          spi_address<= x"0000" & x"20" & command(7 downto 0);
          spi_write_strobe<='1';              
          if data_valid='1' then
            state<=write_end;
            spi_write_strobe<='0';
          end if;
        when write3 =>  --PROG DELAY 1      
          spi_data_write<=x"00000001";
          spi_address<= x"0000" & x"F011";
          spi_write_strobe<='1';              
          if data_valid='1' then
            state<=write4;
            spi_write_strobe<='0';
          end if;
        when write4 =>  --PROG DELAY 0      
          spi_data_write<=x"00000000";
          spi_address<= x"0000" & x"F011";
          spi_write_strobe<='1';              
          if data_valid='1' then
            state<=write_end;
            spi_write_strobe<='0';
          end if;
        when write7 =>  --BROADCAST --N1-B
          spi_data_write<=x"0000" & Delay;
          spi_address<= x"0000" & x"20" & Std_logic_Vector(address_ch);
          spi_write_strobe<='1';              
          if data_valid='1' then
            if address_ch/=(To_Unsigned(TAPS, address_ch'length)-1) then    
              address_ch<=address_ch+1;
              spi_write_strobe<='0';
            else
              address_ch<=x"00";
              state<=write3;
              spi_write_strobe<='0';
            end if;
          end if;

        when write_end =>           
          ready<='1';
          state<=idle;
        when read0 => 
          spi_read_strobe<='0';
          if data_valid='1' then
            readdata<=spi_data_read;
            state<=read_end;
          end if;
        when read_end =>            
          ready<='1';
          state<=idle;
            
        when others =>  state<= idle;         
      end case;           
    end if;
  end process P_CONTROL;
  
  -- ------------------------------------
  I_SPI : entity work.SPI_INTERFACE 
  -- ------------------------------------
  port map(
    
        sys_clk            => clk,
        reset              => reset, 
        reg_data           => spi_data_write,
        reg_address        => spi_address (15 downto 0),  
        write_strobe       => spi_write_strobe,
        read_strobe        => spi_read_strobe,
        reg_data_out       => spi_data_read,
        data_valid         => data_valid,
        spi_sclk           => spi_sclk,
        spi_cs             => spi_cs,    
        spi_mosi           => spi_mosi,               
        spi_miso           => spi_miso  
  );
  
 

end rtl;
