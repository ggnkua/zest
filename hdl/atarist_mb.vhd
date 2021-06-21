-- atarist_mb.vhd - Main Atari ST motherboard-like architecture
--
-- Copyright (c) 2020-2022 Francois Galea <fgalea at free.fr>
-- This program is free software: you can redistribute it and/or modify
-- it under the terms of the GNU General Public License as published by
-- the Free Software Foundation, either version 3 of the License, or
-- (at your option) any later version.
--
-- This program is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
-- GNU General Public License for more details.
--
-- You should have received a copy of the GNU General Public License
-- along with this program.  If not, see <https://www.gnu.org/licenses/>.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.all;

entity atarist_mb is
	port (
		clk : in std_logic;
		resetn : in std_logic;

		clken_error : out std_logic;
		monomon : in std_logic;
		mem_top	: in std_logic_vector(3 downto 0);

		pclken : out std_logic;
		de : out std_logic;
		hsync : out std_logic;
		vsync : out std_logic;
		rgb : out std_logic_vector(8 downto 0);

		sound : out std_logic_vector(15 downto 0);

		ikbd_clkren : out std_logic;
		ikbd_clkfen : out std_logic;
		ikbd_rx : in std_logic;
		ikbd_tx : out std_logic;

		fdd_clken : out std_logic;
		fdd_read_datan : in std_logic;
		fdd_side0 : out std_logic;
		fdd_indexn : in std_logic;
		fdd_drv0_select : out std_logic;
		fdd_drv1_select : out std_logic;
		fdd_motor_on : out std_logic;
		fdd_direction : out std_logic;
		fdd_step : out std_logic;
		fdd_write_data : out std_logic;
		fdd_write_gate : out std_logic;
		fdd_track0n : in std_logic;
		fdd_write_protn : in std_logic;

		a : out std_logic_vector(23 downto 1);
		ds : out std_logic_vector(1 downto 0);
		r : out std_logic;
		r_done : in std_logic;
		w : out std_logic;
		w_done : in std_logic;
		od : in std_logic_vector(15 downto 0);
		id : out std_logic_vector(15 downto 0)
	);
end atarist_mb;


architecture structure of atarist_mb is
	component fx68k is
		port(
			clk		: in std_logic;
			HALTn	: in std_logic;			-- Used for single step only. Force high if not used

			-- These two signals don't need to be registered. They are not async reset.
			extReset	: in std_logic;		-- External sync reset on emulated system
			pwrUp		: in std_logic;		-- Asserted together with reset on emulated system coldstart
			enPhi1		: in std_logic;
			enPhi2		: in std_logic;		-- Clock enables. Next cycle is PHI1 or PHI2

			eRWn		: out std_logic;
			ASn			: out std_logic;
			LDSn		: out std_logic;
			UDSn		: out std_logic;
			E			: out std_logic;
			VMAn		: out std_logic;
			FC0			: out std_logic;
			FC1			: out std_logic;
			FC2			: out std_logic;
			BGn			: out std_logic;
			oRESETn		: out std_logic;
			oHALTEDn	: out std_logic;
			DTACKn		: in std_logic;
			VPAn		: in std_logic;
			BERRn		: in std_logic;
			BRn			: in std_logic;
			BGACKn		: in std_logic;
			IPL0n		: in std_logic;
			IPL1n		: in std_logic;
			IPL2n		: in std_logic;
			iEdb		: in std_logic_vector (15 downto 0);
			oEdb		: out std_logic_vector (15 downto 0);
			eab			: out std_logic_vector (23 downto 1)
		);
	end component;

	signal reset		: std_logic;

	signal enNC1		: std_logic;
	signal enNC2		: std_logic;
	signal en8rck		: std_logic;
	signal en8fck		: std_logic;
	signal en4rck		: std_logic;
	signal en4fck		: std_logic;
	signal en2rck		: std_logic;
	signal en2fck		: std_logic;
	signal en32ck		: std_logic;
	signal en2_4576ck	: std_logic;
	signal ck05			: std_logic;
	signal clken_err	: std_logic;
	signal clken_video	: std_logic;
	signal clken_bus	: std_logic;
	signal clken_bus2	: std_logic;
	signal clken_dma	: std_logic;

	signal bus_A		: std_logic_vector(23 downto 1);
	signal bus_ASn		: std_logic;
	signal bus_RWn		: std_logic;
	signal bus_D		: std_logic_vector(15 downto 0);
	signal bus_LDSn		: std_logic;
	signal bus_UDSn		: std_logic;
	signal bus_DTACKn	: std_logic;

	signal cpu_HALTn	: std_logic;
	signal cpu_A		: std_logic_vector(23 downto 1);
	signal cpu_ASn		: std_logic;
	signal cpu_RWn		: std_logic;
	signal cpu_oD		: std_logic_vector(15 downto 0);
	signal cpu_LDSn		: std_logic;
	signal cpu_UDSn		: std_logic;
	signal cpu_E		: std_logic;
	signal cpu_VMAn		: std_logic;
	signal cpu_FC		: std_logic_vector(2 downto 0);
	signal cpu_BGn		: std_logic;
	signal cpu_oRESETn	: std_logic;
	signal cpu_oHALTEDn	: std_logic;
	signal cpu_VPAn		: std_logic;
	signal cpu_BERRn	: std_logic;
	signal cpu_BRn		: std_logic;
	signal cpu_BGACKn	: std_logic;
	signal cpu_IPLn		: std_logic_vector(2 downto 0);

	signal glue_iA		: std_logic_vector(23 downto 1);
	signal glue_iASn	: std_logic;
	signal glue_iRWn	: std_logic;
	signal glue_iD		: std_logic_vector(1 downto 0);
	signal glue_iUDSn	: std_logic;
	signal glue_iLDSn	: std_logic;
	signal glue_oRWn	: std_logic;
	signal glue_DTACKn	: std_logic;
	signal glue_oD		: std_logic_vector(1 downto 0);
	signal cs6850		: std_logic;
	signal vsyncn		: std_logic;
	signal hsyncn		: std_logic;
	signal blankn		: std_logic;
	signal sde			: std_logic;

	signal mmu_RAMn		: std_logic;
	signal mmu_DMAn		: std_logic;
	signal mmu_DEVn		: std_logic;
	signal mmu_iA		: std_logic_vector(23 downto 1);
	signal mmu_iASn		: std_logic;
	signal mmu_iRWn		: std_logic;
	signal mmu_iD		: std_logic_vector(7 downto 0);
	signal mmu_iUDSn	: std_logic;
	signal mmu_iLDSn	: std_logic;
	signal mmu_oD		: std_logic_vector(7 downto 0);
	signal mmu_DTACKn	: std_logic;
	signal RDATn		: std_logic;

	signal ram_A		: std_logic_vector(23 downto 1);
	signal ram_iD		: std_logic_vector(15 downto 0);
	signal ram_oD		: std_logic_vector(15 downto 0);
	signal ram_W		: std_logic;
	signal ram_R		: std_logic;
	signal ram_DS		: std_logic_vector(1 downto 0);
	signal ram_W_DONE	: std_logic;
	signal ram_R_DONE	: std_logic;

	signal shifter_CSn	: std_logic;
	signal shifter_RWn	: std_logic;
	signal shifter_A	: std_logic_vector(5 downto 1);
	signal shifter_iD	: std_logic_vector(15 downto 0);
	signal shifter_oD	: std_logic_vector(15 downto 0);
	signal loadn		: std_logic;

	signal mfp_oD		: std_logic_vector(7 downto 0);
	signal mfp_csn		: std_logic;
	signal mfp_dtackn	: std_logic;
	signal mfp_irqn		: std_logic;
	signal mfp_iackn	: std_logic;
	signal mfp_iein		: std_logic;
	signal mfp_ieon		: std_logic;
	signal mfp_ii		: std_logic_vector(7 downto 0);
	signal mfp_io		: std_logic_vector(7 downto 0);
	signal mfp_tai		: std_logic;
	signal mfp_tbi		: std_logic;
	signal mfp_tao		: std_logic;
	signal mfp_tbo		: std_logic;
	signal mfp_tco		: std_logic;
	signal mfp_tdo		: std_logic;
	signal mfp_si		: std_logic;
	signal mfp_rc		: std_logic;
	signal mfp_so		: std_logic;
	signal mfp_tc		: std_logic;
	signal mfp_rrn		: std_logic;
	signal mfp_trn		: std_logic;

	signal acia_ikbd_cs		: std_logic;
	signal acia_ikbd_od		: std_logic_vector(7 downto 0);
	signal acia_ikbd_irq	: std_logic;
	signal acia_ikbd_rxd	: std_logic;
	signal acia_ikbd_txd	: std_logic;
	signal acia_ikbd_dcd_n	: std_logic;
	signal acia_ikbd_cts_n	: std_logic;
	signal acia_ikbd_rts_n	: std_logic;
	signal acia_midi_cs		: std_logic;
	signal acia_midi_od		: std_logic_vector(7 downto 0);
	signal acia_midi_irq	: std_logic;
	signal acia_midi_rxd	: std_logic;
	signal acia_midi_txd	: std_logic;
	signal acia_midi_dcd_n	: std_logic;
	signal acia_midi_cts_n	: std_logic;
	signal acia_midi_rts_n	: std_logic;
	signal acia_irq			: std_logic;

	signal dma_fcsn			: std_logic;
	signal dma_iD			: std_logic_vector(15 downto 0);
	signal dma_oD			: std_logic_vector(15 downto 0);
	signal dma_iRDY			: std_logic;
	signal dma_oRDY			: std_logic;
	signal dma_HDCSn		: std_logic;
	signal dma_HDRQ			: std_logic;
	signal dma_FDCSn		: std_logic;
	signal dma_FDRQ			: std_logic;
	signal dma_CRWn			: std_logic;
	signal dma_CA			: std_logic_vector(1 downto 0);
	signal dma_oCD			: std_logic_vector(7 downto 0);
	signal dma_iCD			: std_logic_vector(7 downto 0);

	signal fdc_INTRQ		: std_logic;

	signal psg_csn			: std_logic;
	signal psg_bdir			: std_logic;
	signal psg_bc1			: std_logic;
	signal psg_bc2			: std_logic;
	signal psg_od			: std_logic_vector(7 downto 0);
	signal psg_ia			: std_logic_vector(7 downto 0);
	signal psg_oa			: std_logic_vector(7 downto 0);
	signal psg_ib			: std_logic_vector(7 downto 0);
	signal psg_ob			: std_logic_vector(7 downto 0);
	signal psg_a			: std_logic_vector(15 downto 0);
	signal psg_b			: std_logic_vector(15 downto 0);
	signal psg_c			: std_logic_vector(15 downto 0);
	signal sndsum			: signed(17 downto 0);

begin
	reset <= not resetn;
	clken_error <= clken_err;
	pclken <= en32ck;
	ikbd_clkren <= en2rck;
	ikbd_clkfen <= en2fck;
	fdd_clken <= en8rck;
	de <= blankn;
	hsync <= not hsyncn;
	vsync <= not vsyncn;

	a <= ram_A;
	ds <= ram_DS;
	r <= ram_R;
	ram_R_DONE <= r_done;
	w <= ram_W;
	ram_W_DONE <= w_done;
	ram_oD <= od;
	id <= ram_iD;

	stbus:entity atarist_bus port map(
		cpu_d => cpu_oD,
		cpu_e => cpu_RWn,
		shifter_d => shifter_oD,
		ram_d => ram_oD,
		shifram_e => RDATn,
		mfp_d => mfp_oD,
		mmu_d => mmu_oD,
		glue_d => glue_oD,
		acia_ikbd_d => acia_ikbd_od,
		acia_ikbd_e => acia_ikbd_cs,
		acia_midi_d => acia_midi_od,
		acia_midi_e => acia_midi_cs,
		dma_d => dma_oD,
		psg_d => psg_od,
		psg_e => psg_csn,
		d => bus_D
	);

	bus_A <= cpu_A;
	bus_ASn <= cpu_ASn;
	bus_RWn <= cpu_RWn and glue_oRWn;
	bus_LDSn <= cpu_LDSn;
	bus_UDSn <= cpu_UDSn;
	bus_DTACKn <= glue_DTACKn and mfp_dtackn and mmu_dtackn;

	cpu:fx68k port map(
		clk => clk,
		HALTn => cpu_HALTn,
		extReset => reset,
		pwrUp => reset,
		enPhi1 => en8rck,
		enPhi2 => en8fck,

		eRWn => cpu_RWn,
		ASn => cpu_ASn,
		LDSn => cpu_LDSn,
		UDSn => cpu_UDSn,
		E => cpu_E,
		VMAn => cpu_VMAn,
		FC0 => cpu_FC(0),
		FC1 => cpu_FC(1),
		FC2 => cpu_FC(2),
		BGn => cpu_BGn,
		oRESETn => cpu_oRESETn,
		oHALTEDn => cpu_oHALTEDn,
		DTACKn => bus_DTACKn,
		VPAn => cpu_VPAn,
		BERRn => cpu_BERRn,
		BRn => cpu_BRn,
		BGACKn => cpu_BGACKn,
		IPL0n => cpu_IPLn(0),
		IPL1n => cpu_IPLn(1),
		IPL2n => cpu_IPLn(2),
		iEdb => bus_D,
		oEdb => cpu_oD,
		eab => cpu_A
	);
	cpu_HALTn <= '1';
	cpu_IPLn(0) <= '1';

	clkgen:entity clock_enabler port map (clk,reset,enNC1,enNC2,en8rck,en8fck,en32ck,en4rck,en4fck,en2rck,en2fck,en2_4576ck,ck05,clken_err);
	enNC1 <= '1';
	enNC2 <= clken_bus and clken_video and clken_dma;
	clken_bus <= ((not ram_R or ram_R_DONE) and (not ram_W or ram_W_DONE)) or bus_DTACKn or clken_bus2;
	clken_video <= loadn or not ram_R or ram_R_DONE;
	clken_dma <= ((not ram_R or ram_R_DONE) and (not ram_W or ram_W_DONE)) or mmu_DMAn;
	process(cpu_A)
	begin
		if cpu_A(23 downto 15) = "111111111" then
			clken_bus2 <= '1';
		else
			clken_bus2 <= '0';
		end if;
	end process;

	glu:entity glue port map(
		clk => clk,
		enPhi1 => en8rck,
		enPhi2 => en8fck,
		resetn => resetn,

		iA => glue_iA,
		iASn => glue_iASn,
		iRWn => glue_iRWn,
		iD => glue_iD,
		iUDSn => glue_iUDSn,
		iLDSn => glue_iLDSn,
		iDTACKn => bus_DTACKn,
		oRWn => glue_oRWn,
		oDTACKn => glue_DTACKn,
		BEER => cpu_BERRn,
		oD => glue_oD,
		FC => cpu_FC,
		IPLn => cpu_IPLn(2 downto 1),
		VPAn => cpu_VPAn,
		VMAn => cpu_VMAn,
		cs6850 => cs6850,
		FCSn => dma_fcsn,
		iRDY => dma_oRDY,
		oRDY => dma_iRDY,
		RAMn => mmu_RAMn,
		DMAn => mmu_DMAn,
		DEVn => mmu_DEVn,
		BRn => cpu_BRn,
		BGn => cpu_BGn,
		BGACKn => cpu_BGACKn,
		MFPCSn => mfp_csn,
		MFPINTn	=> mfp_irqn,
		IACKn => mfp_iackn,
		SNDCSn => psg_csn,

		VSYNC => vsyncn,
		HSYNC => hsyncn,
		BLANKn => blankn,
		DE => sde
	);
	glue_iA <= bus_A;
	glue_iASn <= bus_ASn;
	glue_iRWn <= bus_RWn;
	glue_iD	<= bus_D(9 downto 8);
	glue_iUDSn <= bus_UDSn;
	glue_iLDSn <= bus_LDSn;

	mm:entity mmu port map (
		clk => clk,
		enPhi1 => en8rck,
		enPhi2 => en8fck,
		resetn => resetn,

		RAMn => mmu_RAMn,
		DMAn => mmu_DMAn,
		DEVn => mmu_DEVn,

		iA => mmu_iA,
		iASn => mmu_iASn,
		iRWn => mmu_iRWn,
		iD => mmu_iD,
		iUDSn => mmu_iUDSn,
		iLDSn => mmu_iLDSn,
		oD => mmu_oD,
		DTACKn => mmu_DTACKn,

		RDATn => RDATn,

		DCYCn => loadn,
		CMPCSn => shifter_CSn,

		vsync => vsyncn,

		mem_top	=> mem_top,
		ram_A => ram_A,
		ram_W => ram_W,
		ram_R => ram_R,
		ram_DS => ram_DS
	);
	mmu_iA <= bus_A;
	mmu_iASn <= bus_ASn;
	mmu_iRWn <= bus_RWn;
	mmu_iD <= bus_D(7 downto 0);
	mmu_iUDSn <= bus_UDSn;
	mmu_iLDSn <= bus_LDSn;

	ram_iD <= bus_D;

	shift:entity shifter port map (
		clk => clk,
		enPhi1 => en8rck,
		enPhi2 => en8fck,
		en32ck => en32ck,
		CSn => shifter_CSn,
		RWn => shifter_RWn,
		A => shifter_A,
		iD => shifter_iD,
		oD => shifter_oD,
		DE => sde,
		LOADn => loadn,
		rgb => rgb
	);
	shifter_iD <= (bus_D or (15 downto 0 => shifter_CSn)) and ram_oD;
	shifter_RWn <= bus_RWn;
	shifter_A <= bus_A(5 downto 1);

	mfp:entity mc68901 port map (
		clk => clk,
		clkren => en4rck,
		clkfen => en4fck,
		xtlcken	=> en2_4576ck,
		resetn => resetn,
		id => bus_D(7 downto 0),
		od => mfp_oD,
		rs => bus_A(5 downto 1),
		csn	=> mfp_csn,
		rwn	=> bus_RWn,
		dsn	=> bus_LDSn,
		dtackn => mfp_dtackn,
		irqn => mfp_irqn,
		iackn => mfp_iackn,
		iein => mfp_iein,
		ieon => mfp_ieon,
		ii => mfp_ii,
		io => mfp_io,
		tai => mfp_tai,
		tbi => mfp_tbi,
		tao => mfp_tao,
		tbo => mfp_tbo,
		tco => mfp_tco,
		tdo => mfp_tdo,
		si => mfp_si,
		rc => mfp_rc,
		so => mfp_so,
		tc => mfp_tc,
		rrn => mfp_rrn,
		trn => mfp_trn
	);
	mfp_iein <= '0';
	mfp_ii <= not monomon & '1' & not fdc_INTRQ & acia_irq & "1111";
	mfp_tai <= '1';
	mfp_tbi <= sde;
	mfp_si <= '0';
	mfp_rc <= '0';
	mfp_tc <= '0';

	acia_ikbd:entity acia6850 port map (
		clk => clk,
		rst => reset,
		cs => acia_ikbd_cs,
		addr => bus_A(1),
		rw => bus_RWn,
		data_in => bus_D(15 downto 8),
		data_out => acia_ikbd_od,
		irq => acia_ikbd_irq,
		RxC => ck05,
		TxC => ck05,
		RxD => acia_ikbd_rxd,
		TxD => acia_ikbd_txd,
		DCD_n => acia_ikbd_dcd_n,
		CTS_n => acia_ikbd_cts_n,
		RTS_n => acia_ikbd_rts_n
	);
	acia_ikbd_cs <= cs6850 and not bus_A(2);
	acia_ikbd_rxd <= ikbd_rx;
	ikbd_tx <= acia_ikbd_txd;
	acia_ikbd_dcd_n <= '0';
	acia_ikbd_cts_n <= '0';

	acia_midi:entity acia6850 port map (
		clk => clk,
		rst => reset,
		cs => acia_midi_cs,
		addr => bus_A(1),
		rw => bus_RWn,
		data_in => bus_D(15 downto 8),
		data_out => acia_midi_od,
		irq => acia_midi_irq,
		RxC => ck05,
		TxC => ck05,
		RxD => acia_midi_rxd,
		TxD => acia_midi_txd,
		DCD_n => acia_midi_dcd_n,
		CTS_n => acia_midi_cts_n,
		RTS_n => acia_midi_rts_n
	);
	acia_midi_cs <= cs6850 and bus_A(2);
	acia_midi_rxd <= '1';
	acia_midi_dcd_n <= '0';
	acia_midi_cts_n <= '0';
	acia_irq <= acia_ikbd_irq nor acia_midi_irq;

	dma:entity dma_controller port map (
		clk => clk,
		cken => en8rck,
		resetn => resetn,
		FCSn => dma_fcsn,
		iRDY => dma_iRDY,
		oRDY => dma_oRDY,
		RWn => bus_RWn,
		A1 => bus_A(1),
		iD => dma_iD,
		oD => dma_oD,
		HDCSn => dma_HDCSn,
		HDRQ => dma_HDRQ,
		FDCSn => dma_FDCSn,
		FDRQ => dma_FDRQ,
		CRWn => dma_CRWn,
		CA => dma_CA,
		oCD => dma_oCD,
		iCD => dma_iCD
	);
	dma_iD <= bus_D;
	dma_HDRQ <= '0';

	fdc:entity wd1772 port map (
		clk => clk,
		clken => en8rck,
		resetn => resetn,
		CSn => dma_FDCSn,
		RWn => dma_CRWn,
		A => dma_CA,
		iDAL => dma_oCD,
		oDAL => dma_iCD,
		INTRQ => fdc_INTRQ,
		DRQ => dma_FDRQ,
		DDENn => '0',
		WPRTn => fdd_write_protn,
		IPn => fdd_indexn,
		TR0n => fdd_track0n,
		WD => fdd_write_data,
		WG => fdd_write_gate,
		MO => fdd_motor_on,
		RDn => fdd_read_datan,
		DIRC => fdd_direction,
		STEP => fdd_step
	);

	fdd_side0 <= psg_oa(0);
	fdd_drv0_select <= psg_oa(1);
	fdd_drv1_select <= psg_oa(2);

	psg_bdir <= psg_csn nor bus_RWn;
	psg_bc1 <= psg_csn nor bus_A(1);
	psg_bc2 <= '1';
	psg_ia <= (others => '0');
	psg_ib <= (others => '0');
	sndsum <= resize(signed(psg_a),sndsum'length) + resize(signed(psg_b),sndsum'length) + resize(signed(psg_c),sndsum'length);
	sound <= std_logic_vector(sndsum(17 downto 2));
	psg:entity ym2149 port map (
		clk => clk,
		aclken => en2rck,
		resetn => resetn,
		bdir => psg_bdir,
		bc1 => psg_bc1,
		bc2 => psg_bc2,
		ida => bus_D(15 downto 8),
		oda => psg_od,
		ia => psg_ia,
		oa => psg_oa,
		ib => psg_ib,
		ob => psg_ob,
		a => psg_a,
		b => psg_b,
		c => psg_c
	);

end structure;