-- spi_interface.vhd
-- -----------------------------------------------------------------------
-- V2495 User Gate&Dealy generator SPI interface
-- -----------------------------------------------------------------------
--  Date        : Jul 2016
--  Contact     : support.nuclear@caen.it
-- (c) CAEN SpA - http://www.caen.it   
-- -----------------------------------------------------------------------
-- 
-- -----------------------------------------------------------------------
-- $Id$
-- -----------------------------------------------------------------------


library IEEE;
    use IEEE.std_logic_1164.all;
    use IEEE.std_logic_arith.all;
    use IEEE.std_logic_unsigned.all;


entity spi_interface is

	port
	(
		sys_clk     : in    std_logic; 
		reset     : in    std_logic; 
		reg_data			: in std_logic_vector(31 downto 0);
		reg_address			: in std_logic_vector(15 downto 0);
		write_strobe : in    std_logic; 
		read_strobe : in    std_logic; 
		reg_data_out		: out std_logic_vector(31 downto 0);
		data_valid: out std_logic;
		spi_sclk     : out    std_logic;                     
		spi_cs       : out     std_logic;                    
		spi_mosi     : out 	std_logic;                     
		spi_miso     : in    std_logic
			  
					  
	);
end spi_interface;


architecture bv of spi_interface is

	type   state_type is (idle, write0, write1, write2, write3, write4, write5,
						write6, write7, write8, write9, write10, write11, load_reg_address, read0,
						read1, read2, read3, read4, read5, read6, read7);
	signal state : state_type;

	signal int_reg_data : std_logic_vector(31 downto 0):=x"00000000";
	signal int_reg_data_out : std_logic_vector(31 downto 0):=x"00000000";
	signal rx_reg_data : std_logic_vector(31 downto 0):=x"00000000";
	signal int_reg_address : std_logic_vector(15 downto 0):=x"0000";
	signal command : std_logic_vector(15 downto 0):=x"0000";
	signal int_write_strobe : std_logic:='0';
	signal old_int_write_strobe : std_logic:='0';
	signal old_int_read_strobe : std_logic:='0';
	signal int_read_strobe : std_logic:='0';
	signal int_data_valid : std_logic:='0';	
	signal int2_data_valid : std_logic:='0';	

	signal di_i : std_logic_vector(31 downto 0):=x"00000000";
	signal di_req_o : std_logic:='0';
	signal wren_i : std_logic:='0';
	signal wr_ack_o : std_logic:='0';
	signal int_spi_cs : std_logic:='0';
	signal int_dummy_cs : std_logic:='0';
	signal int_int_dummy_cs : std_logic:='0';
	
COMPONENT spi_master IS
    Generic (   
        N : positive ;                                              -- 32bit serial word length is default
        CPOL : std_logic ;                                          -- SPI mode selection (mode 0 default)
        CPHA : std_logic ;                                          -- CPOL = clock polarity, CPHA = clock phase.
        PREFETCH : positive;                                        -- prefetch lookahead cycles
        SPI_2X_CLK_DIV : positive );                                -- for a 100MHz sclk_i, yields a 10MHz SCK
    Port (  
        sclk_i : in std_logic ;                                     -- high-speed serial interface system clock
        pclk_i : in std_logic ;                                     -- high-speed parallel interface system clock
        rst_i : in std_logic ;                                      -- reset core
        ---- serial interface ----
        spi_ssel_o : out std_logic;                                 -- spi bus slave select line
        spi_sck_o : out std_logic;                                  -- spi bus sck
        spi_mosi_o : out std_logic;                                 -- spi bus mosi output
        spi_miso_i : in std_logic;                                  -- spi bus spi_miso_i input
        ---- parallel interface ----
        di_req_o : out std_logic;                                   -- preload lookahead data request line
        di_i : in  std_logic_vector (N-1 downto 0);                 -- parallel data in (clocked on rising spi_clk after last bit)
        wren_i : in std_logic ;                                     -- user data write enable, starts transmission when interface is idle
        wr_ack_o : out std_logic;                                   -- write acknowledge
        do_valid_o : out std_logic;                                 -- do_o data valid signal, valid during one spi_clk rising edge.
        do_o : out  std_logic_vector (N-1 downto 0);                -- parallel output (clocked on rising spi_clk after last bit)
        --- debug ports: can be removed or left unconnected for the application circuit ---
        sck_ena_o : out std_logic;                                  -- debug: internal sck enable signal
        sck_ena_ce_o : out std_logic;                               -- debug: internal sck clock enable signal
        do_transfer_o : out std_logic;                              -- debug: internal transfer driver
        wren_o : out std_logic;                                     -- debug: internal state of the wren_i pulse stretcher
        rx_bit_reg_o : out std_logic;                               -- debug: internal rx bit
        state_dbg_o : out std_logic_vector (3 downto 0);            -- debug: internal state register
        core_clk_o : out std_logic;
        core_n_clk_o : out std_logic;
        core_ce_o : out std_logic;
        core_n_ce_o : out std_logic;
        sh_reg_dbg_o : out std_logic_vector (N-1 downto 0)          -- debug: internal shift register
    );                      
END COMPONENT;


begin


	
spi_core: spi_master   
	Generic map(   
        N               => 32,                                      -- 32bit serial word length is default
        CPOL            => '0',                                     -- SPI mode selection (mode 0 default)
        CPHA            => '0',                                     -- CPOL = clock polarity, CPHA = clock phase.
        PREFETCH        => 2,                                       -- prefetch lookahead cycles
        SPI_2X_CLK_DIV  => 5 )                                      -- for a 100MHz sclk_i, yields a 10MHz SCK									
	Port map(  
		  sclk_i      => sys_clk,
		  pclk_i      => sys_clk,
		  rst_i       => reset,
		  ---- serial interface ----
		  spi_ssel_o  => spi_cs,
		  spi_sck_o   => spi_sclk,
		  spi_mosi_o  => spi_mosi,
		  spi_miso_i  => spi_miso,
		  ---- parallel interface ----
		  di_req_o    => di_req_o,         
		  di_i        => di_i,
		  wren_i      => wren_i,
		  wr_ack_o    => wr_ack_o,
		  sck_ena_ce_o=> open,
		  do_valid_o  => int_data_valid,
		  do_o        => rx_reg_data

    );  


	

	reg_interface : process (sys_clk)
	begin

		if reset='1' then

		elsif rising_edge(sys_clk) then

			case state is
				when idle =>					
					data_valid<='0';
					int_reg_data<=reg_data;
					int_reg_address<= reg_address;
					int_write_strobe<=write_strobe;
					int_read_strobe<= read_strobe;	 
					old_int_write_strobe<=int_write_strobe;
					old_int_read_strobe<=int_read_strobe;
					if (old_int_read_strobe='0' and int_read_strobe='1') then
						state<=load_reg_address;
						command<=x"8001";						
					elsif (old_int_write_strobe='0' and int_write_strobe='1') then						
						state<=write0;
						command<=x"8000";
					end if;
					
				--WRITE SEQUENCE
				when write0 =>
					state<=write1;
				when write1 =>
					di_i <= command & int_reg_address;
					state<=write2;
				when write2 =>
					state<=write3;
				when write3=>
					wren_i<='1';
					state<=write4;
				when write4=>
					wren_i<='0';
					state<=write5;
				when write5=>
					if int_data_valid='1' then
						state<=write6;						
					end if;
				when write6 =>
					state<=write7;
				when write7 =>
					di_i <= int_reg_data;
					state<=write8;
				when write8 =>
					state<=write9;
				when write9=>
					wren_i<='1';
					state<=write10;
				when write10=>
					wren_i<='0';
					state<=write11;
				when write11=>
					if int_data_valid='1' then
						data_valid<='1';
						state<=idle;
					end if;
					
				--READ SEQUENCE	
				when load_reg_address =>
					di_i <= command & int_reg_address;
					state<=read0;			
				when read0 =>  --trasferimento 1
					wren_i<='1';
					if wr_ack_o='1'then	
						state<=read1;
						wren_i<='0';
					end if;
				when read1 => --trasferimento 2
					if di_req_o='1' then
						state<=read2;
						wren_i<='1';
					end if;
				when read2 => 
					if wr_ack_o='1' then
						state<=read3;
						wren_i<='0';
					end if;
				when read3 => --trasferimento 3
					if di_req_o='1' then
						state<=read4;
						wren_i<='1';
					end if;
				when read4 =>				
					if wr_ack_o='1' then
						state<=read5;
						wren_i<='0';
					end if;		
				when read5 =>					
					if di_req_o='1' then
						state<=read6;
					end if;
				when read6 =>
					reg_data_out<=rx_reg_data;
					int2_data_valid<=int_data_valid;
					if int_data_valid='1' then
						data_valid<='1';
						state<=idle;
					end if;
				when others=> null;
			end case;
		end if;
		
	end process;
	
end architecture;
