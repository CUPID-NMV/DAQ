-- V2495_GatePattern.vhd
-- -----------------------------------------------------------------------
-- V2495 User Gate Pattern Demo (top level) - MODIFICATO PER MEZZANINO A395D NELLA PORTA E 
-- Segnale NIM in ingresso nei canali 0 e 1 e in uscita nel canale 2
-- -----------------------------------------------------------------------

LIBRARY IEEE;
USE IEEE.std_logic_1164.ALL;
USE IEEE.numeric_std.ALL;

USE work.V2495_pkg.ALL;

-- ----------------------------------------------
ENTITY V2495_GatePattern IS
  -- ----------------------------------------------
  PORT (
    CLK : IN STD_LOGIC; -- System clock (50 MHz)

    -- ------------------------------------------------------
    -- Mainboard I/O ports
    -- ------------------------------------------------------   
    A : IN STD_LOGIC_VECTOR (31 DOWNTO 0); -- Data bus 
    B : IN STD_LOGIC_VECTOR (31 DOWNTO 0); -- Data bus
    C : OUT STD_LOGIC_VECTOR (31 DOWNTO 0); -- Data bus
    GIN : IN STD_LOGIC_VECTOR (1 DOWNTO 0); -- In data
    GOUT : OUT STD_LOGIC_VECTOR (1 DOWNTO 0); -- Out data
    SELG : OUT STD_LOGIC; -- Level select
    nOEG : OUT STD_LOGIC; -- Output Enable

    -- ------------------------------------------------------
    -- Expansion slots
    -- ------------------------------------------------------                                                                  
    IDD : IN STD_LOGIC_VECTOR (2 DOWNTO 0); -- Card ID
    SELD : OUT STD_LOGIC; -- Level select
    nOED : OUT STD_LOGIC; -- Output Enable
    D : INOUT STD_LOGIC_VECTOR (31 DOWNTO 0); -- Data bus

    IDE : IN STD_LOGIC_VECTOR (2 DOWNTO 0); -- Card ID
    SELE : OUT STD_LOGIC; -- Level select
    nOEE : OUT STD_LOGIC; -- Output Enable
    E : INOUT STD_LOGIC_VECTOR (31 DOWNTO 0); -- Data bus

    IDF : IN STD_LOGIC_VECTOR (2 DOWNTO 0); -- Card ID
    SELF : OUT STD_LOGIC; -- Level select
    nOEF : OUT STD_LOGIC; -- Output Enable
    F : INOUT STD_LOGIC_VECTOR (31 DOWNTO 0); -- Data bus

    -- ------------------------------------------------------
    -- Gate & Delay
    -- ------------------------------------------------------
    GD_START : OUT STD_LOGIC_VECTOR(31 DOWNTO 0); -- Start of G&D
    GD_DELAYED : IN STD_LOGIC_VECTOR(31 DOWNTO 0); -- G&D Output
    SPI_MISO : IN STD_LOGIC; -- SPI data in
    SPI_SCLK : OUT STD_LOGIC; -- SPI clock
    SPI_CS : OUT STD_LOGIC; -- SPI chip sel.
    SPI_MOSI : OUT STD_LOGIC; -- SPI data out

    -- ------------------------------------------------------
    -- LED
    -- ------------------------------------------------------
    LED : OUT STD_LOGIC_VECTOR(7 DOWNTO 0); -- User led    

    -- ------------------------------------------------------
    -- Local Bus in/out signals
    -- ------------------------------------------------------
    nLBRES : IN STD_LOGIC; -- Bus reset
    nBLAST : IN STD_LOGIC; -- Last cycle
    WnR : IN STD_LOGIC; -- Read (0)/Write(1)
    nADS : IN STD_LOGIC; -- Address strobe
    nREADY : OUT STD_LOGIC; -- Ready (active low) 
    LAD : INOUT STD_LOGIC_VECTOR (15 DOWNTO 0); -- Address/Data bus
    nINT : OUT STD_LOGIC -- Interrupt request
  );
END V2495_GatePattern;

-- ---------------------------------------------------------------
ARCHITECTURE rtl OF V2495_GatePattern IS
  -- ---------------------------------------------------------------

  SIGNAL mon_regs : MONITOR_REGS_T;
  SIGNAL ctrl_regs : CONTROL_REGS_T;

  -- Gate & Delay control bus signals
  SIGNAL gd_write : STD_LOGIC;
  SIGNAL gd_read : STD_LOGIC;
  SIGNAL gd_ready : STD_LOGIC;
  SIGNAL reset : STD_LOGIC;
  SIGNAL gd_data_wr : STD_LOGIC_VECTOR(31 DOWNTO 0);
  SIGNAL gd_data_rd : STD_LOGIC_VECTOR(31 DOWNTO 0);
  SIGNAL gd_command : STD_LOGIC_VECTOR(15 DOWNTO 0);

  -- Vettori d'appoggio per convertire la logica dei mezzanini
  SIGNAL port_A_fake : STD_LOGIC_VECTOR(31 DOWNTO 0) := (OTHERS => '0');
  SIGNAL port_B_fake : STD_LOGIC_VECTOR(31 DOWNTO 0) := (OTHERS => '0');
  SIGNAL port_C_fake : STD_LOGIC_VECTOR(31 DOWNTO 0);

  -----\
BEGIN --
  -----/

  -- Disattivazione delle porte inutilizzate della scheda madre
  GOUT <= (OTHERS => 'Z');
  SELD <= 'Z';
  nOED <= 'Z';
  D <= (OTHERS => 'Z');
  SELF <= 'Z';
  nOEF <= 'Z';
  F <= (OTHERS => 'Z');

  -- Mettiamo in Alta Impedenza la vecchia porta C LVDS sul pannello frontale principale
  C <= (OTHERS => 'Z');

  GD_START <= (OTHERS => 'Z');

  -- Local bus Interrupt request
  nINT <= '1';

  -- User Led driver
  LED <= STD_LOGIC_VECTOR(to_unsigned(DEMO_NUMBER, 8));

  reset <= NOT(nLBRES);

  -- ===================================================================
  -- CONFIGURAZIONE HARDWARE PORTA E (MEZZANINO A395D)
  -- ===================================================================
  SELE <= '0'; -- '0' forza lo standard NIM sui chip di livellamento
  nOEE <= '0'; -- '0' abilita i buffer di uscita (Active Low)

  -- Lettura Ingressi NIM (Inversione logica: segnale presente = '1' logico nell'FPGA)
  port_A_fake(0) <= NOT E(2); -- Canale 0 Fisico di E (LEMO 0) -> mappato su port_A_fake(0)
  port_B_fake(0) <= NOT E(18); -- Canale 1 Fisico di E (LEMO 1) -> mappato su port_B_fake(0)

  -- Pilotaggio Uscita NIM
  E(1) <= NOT port_C_fake(0); -- Risultato logico bit 0 -> Re-invertito in NIM sul Canale 2 Fisico (LEMO 2)

  -- Gestione del Tri-state del Bus E per evitare cortocircuiti
  -- Il pin 1 è di output (lo pilotiamo noi), tutti gli altri devono rimanere a 'Z'
  -- inclusi il pin 2 e 18, permettendo così la lettura dei segnali esterni.
  E(0) <= 'Z';
  E(31 DOWNTO 2) <= (OTHERS => 'Z');
  -- ===================================================================

  -- --------------------------
  --  Gate Pattern logic
  -- --------------------------  
  I_GATEP : ENTITY work.gatep
    PORT MAP(
      CLK => CLK,
      A => port_A_fake,
      B => port_B_fake,
      C => port_C_fake,
      nOEG => nOEG,
      SELG => SELG,
      GIN => GIN,
      -- Register interface  
      mon_regs => mon_regs,
      ctrl_regs => ctrl_regs
    );

  -- --------------------------
  --  Local Bus slave interface
  -- --------------------------  
  I_LBUS_INTERFACE : ENTITY work.lb_int
    PORT MAP(
      clk => CLK,
      reset => reset,
      -- Local Bus            
      nBLAST => nBLAST,
      WnR => WnR,
      nADS => nADS,
      nREADY => nREADY,
      LAD => LAD,
      -- Register interface  
      ctrl_regs => ctrl_regs,
      mon_regs => mon_regs,
      -- Gate and Delay controls
      gd_data_wr => gd_data_wr,
      gd_data_rd => gd_data_rd,
      gd_command => gd_command,
      gd_write => gd_write,
      gd_read => gd_read,
      gd_ready => gd_ready
    );

  -- --------------------------
  --  Gate and Delay controller
  -- --------------------------  
  I_GD : ENTITY work.gd_control
    PORT MAP(
      reset => reset,
      clk => clk,
      -- Programming interface
      write => gd_write,
      read => gd_read,
      writedata => gd_data_wr,
      command => gd_command,
      ready => gd_ready,
      readdata => gd_data_rd,
      -- Gate&Delay control interface (SPI)        
      spi_sclk => spi_sclk,
      spi_cs => spi_cs,
      spi_mosi => spi_mosi,
      spi_miso => spi_miso
    );

END rtl;