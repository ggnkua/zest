-- atarist_main.vhd - Main Atari ST-like architecture
--
-- Copyright (c) 2020 Francois Galea <fgalea at free.fr>
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

entity atarist_main is
	port (
		clk : in std_logic;
		resetn : in std_logic;

		clken_error : out std_logic;

		clken : out std_logic;
		de : out std_logic;
		hsync : out std_logic;
		vsync : out std_logic;
		rgb : out std_logic_vector(8 downto 0);

		a : out std_logic_vector(23 downto 1);
		ds : out std_logic_vector(1 downto 0);
		r : out std_logic;
		r_done : in std_logic;
		w : out std_logic;
		w_done : in std_logic;
		od : in std_logic_vector(15 downto 0);
		id : out std_logic_vector(15 downto 0)
	);
end atarist_main;


architecture structure of atarist_main is
	component fx68k is
		port(
			clk		: in std_logic;
			HALTn	: in std_logic;			-- Used for single step only. Force high if not used

			-- These two signals don't need to be registered. They are not async reset.
			extReset	: in std_logic;		-- External sync reset on emulated system
			pwrUp		: in std_logic;		-- Asserted together with reset on emulated system coldstart
			enPhi1		: in std_logic;
			enPhi2		: in std_logic;		-- Clock enables. Next cycle is PHI1 or PHI2

			eRWn 		: out std_logic;
			ASn 		: out std_logic;
			LDSn 		: out std_logic;
			UDSn 		: out std_logic;
			E 			: out std_logic;
			VMAn 		: out std_logic;
			FC0 		: out std_logic;
			FC1 		: out std_logic;
			FC2 		: out std_logic;
			BGn 		: out std_logic;
			oRESETn 	: out std_logic;
			oHALTEDn 	: out std_logic;
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

	component glue is
		port (
			clk		: in std_logic;
			enPhi1	: in std_logic;
			enPhi2	: in std_logic;
			resetn	: in std_logic;

			iA		: in std_logic_vector(23 downto 1);
			iASn	: in std_logic;
			iRWn	: in std_logic;
			iD		: in std_logic_vector(1 downto 0);
			iUDSn	: in std_logic;
			iLDSn	: in std_logic;
			DTACKn	: out std_logic;
			BEER	: out std_logic;
			oD		: out std_logic_vector(1 downto 0);

			FC		: in std_logic_vector(2 downto 0);
			IPLn	: out std_logic_vector(2 downto 1);
			VPAn	: out std_logic;

			MFPCSn	: out std_logic;
			MFPINTn	: in std_logic;
			IACKn	: out std_logic;

			VSYNC	: out std_logic;
			HSYNC	: out std_logic;
			BLANKn	: out std_logic;
			DE		: out std_logic
		);
	end component;

	component mmu is
		port (
			clk		: in std_logic;
			enPhi1	: in std_logic;
			enPhi2	: in std_logic;

			iA		: in std_logic_vector(23 downto 1);
			iASn	: in std_logic;
			iRWn	: in std_logic;
			iD		: in std_logic_vector(7 downto 0);
			iUDSn	: in std_logic;
			iLDSn	: in std_logic;
			oD		: out std_logic_vector(7 downto 0);
			DTACKn	: in std_logic;

			-- load request from shifter
			DCYCn	: in std_logic;
			-- register request from bus
			CMPCSn	: out std_logic;

			-- vertical sync
			VSYNC	: in std_logic;

			-- interface to RAM. Using own signals instead of hardware specific ones
			ram_A		: out std_logic_vector(23 downto 1);
			ram_W		: out std_logic;
			ram_R		: out std_logic;
			ram_DS		: out std_logic_vector(1 downto 0);
			ram_W_DONE	: in std_logic;
			ram_R_DONE	: in std_logic
		);
	end component;

	component clock_enabler is
		port (
			clk			: in std_logic;
			reset		: in std_logic;
			enNC1		: in std_logic;
			enNC2		: in std_logic;
			en8rck		: out std_logic;
			en8fck		: out std_logic;
			en32ck		: out std_logic;
			en4rck		: out std_logic;
			en4fck		: out std_logic;
			en2_4576	: out std_logic;
			error		: out std_logic
		);
	end component;

	component shifter is
		port (
			clk		: in std_logic;
			enPhi1	: in std_logic;
			enPhi2	: in std_logic;
			en32ck	: in std_logic;

			CSn		: in std_logic;
			RWn		: in std_logic;
			A		: in std_logic_vector(5 downto 1);
			iD		: in std_logic_vector(15 downto 0);
			oD		: out std_logic_vector(15 downto 0);
			DE		: in std_logic;
			LOADn	: out std_logic;

			rgb		: out std_logic_vector(8 downto 0)
		);
	end component;

	component mc68901 is
		port (
			clk		: in std_logic;
			clkren	: in std_logic;
			clkfen	: in std_logic;
			xtlcken	: in std_logic;
			resetn	: in std_logic;

			-- cpu bus I/O
			id		: in std_logic_vector(7 downto 0);
			od		: out std_logic_vector(7 downto 0);
			rs		: in std_logic_vector(5 downto 1);
			csn		: in std_logic;
			rwn		: in std_logic;
			dsn		: in std_logic;
			dtackn	: out std_logic;

			-- interrupt control
			irqn	: out std_logic;
			iackn	: in std_logic;
			iein	: in std_logic;
			ieon	: out std_logic;

			-- general purpose I/O -- interrupts
			ii		: in std_logic_vector(7 downto 0);
			io		: out std_logic_vector(7 downto 0);

			-- timer control
			tai		: in std_logic;
			tbi		: in std_logic;
			tao		: out std_logic;
			tbo		: out std_logic;
			tco		: out std_logic;
			tdo		: out std_logic;

			-- serial I/O control
			si		: in std_logic;
			rc		: in std_logic;
			so		: out std_logic;
			tc		: in std_logic;

			-- DMA control
			rrn		: out std_logic;
			trn		: out std_logic
		);
	end component;

	signal reset		: std_logic;

	signal enNC1 		: std_logic;
	signal enNC2 		: std_logic;
	signal en8rck 		: std_logic;
	signal en8fck 		: std_logic;
	signal en4rck 		: std_logic;
	signal en4fck 		: std_logic;
	signal en32ck 		: std_logic;
	signal en2_4576ck	: std_logic;
	signal clken_err	: std_logic;
	signal clken_video	: std_logic;
	signal clken_bus	: std_logic;
	signal clken_bus2	: std_logic;

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
	signal glue_DTACKn	: std_logic;
	signal glue_oD		: std_logic_vector(1 downto 0);
	signal vsyncn		: std_logic;
	signal hsyncn		: std_logic;
	signal blankn		: std_logic;
	signal sde			: std_logic;

	signal mmu_iA		: std_logic_vector(23 downto 1);
	signal mmu_iASn		: std_logic;
	signal mmu_iRWn		: std_logic;
	signal mmu_iD		: std_logic_vector(7 downto 0);
	signal mmu_iUDSn	: std_logic;
	signal mmu_iLDSn	: std_logic;
	signal mmu_oD		: std_logic_vector(7 downto 0);

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

begin
	reset <= not resetn;
	clken_error <= clken_err;
	clken <= en32ck;
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

	bus_A <= cpu_A;
	bus_ASn <= cpu_ASn;
	bus_RWn <= cpu_RWn;
	bus_D <= (cpu_oD or (15 downto 0 => cpu_RWn)) and shifter_oD and ram_oD and (x"ff" & (mmu_oD and mfp_oD)) and ("111111" & glue_oD & x"ff");
	bus_LDSn <= cpu_LDSn;
	bus_UDSn <= cpu_UDSn;
	bus_DTACKn <= glue_DTACKn and mfp_dtackn;

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
	cpu_BRn <= '1';
	cpu_BGACKn <= '1';
	cpu_IPLn(0) <= '1';

	clkgen:clock_enabler port map (clk,reset,enNC1,enNC2,en8rck,en8fck,en32ck,en4rck,en4fck,en2_4576ck,clken_err);
	enNC1 <= '1';
	enNC2 <= clken_bus and clken_video;
	clken_bus <= (ram_R_DONE or ram_W_DONE) or bus_DTACKn or clken_bus2;
	clken_video <= loadn or not ram_R or ram_R_DONE;
	process(cpu_A)
	begin
		if cpu_A(23 downto 15) = "111111111" then
			clken_bus2 <= '1';
		else
			clken_bus2 <= '0';
		end if;
	end process;

	glu:glue port map(
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
		DTACKn => glue_DTACKn,
		BEER => cpu_BERRn,
		oD => glue_oD,
		FC => cpu_FC,
		IPLn => cpu_IPLn(2 downto 1),
		VPAn => cpu_VPAn,
		MFPCSn => mfp_csn,
		MFPINTn	=> mfp_irqn,
		IACKn => mfp_iackn,

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

	mm:mmu port map (
		clk => clk,
		enPhi1 => en8rck,
		enPhi2 => en8fck,

		iA => mmu_iA,
		iASn => mmu_iASn,
		iRWn => mmu_iRWn,
		iD => mmu_iD,
		iUDSn => mmu_iUDSn,
		iLDSn => mmu_iLDSn,
		oD => mmu_oD,
		DTACKn => bus_DTACKn,

		DCYCn => loadn,
		CMPCSn => shifter_CSn,

		vsync => vsyncn,

		ram_A => ram_A,
		ram_W => ram_W,
		ram_R => ram_R,
		ram_DS => ram_DS,
		ram_W_DONE => ram_W_DONE,
		ram_R_DONE => ram_R_DONE
	);
	mmu_iA <= bus_A;
	mmu_iASn <= bus_ASn;
	mmu_iRWn <= bus_RWn;
	mmu_iD <= bus_D(7 downto 0);
	mmu_iUDSn <= bus_UDSn;
	mmu_iLDSn <= bus_LDSn;

	ram_iD <= bus_D;

	shift:shifter port map (
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
	shifter_iD <= bus_D;
	shifter_RWn <= bus_RWn;
	shifter_A <= bus_A(5 downto 1);

	mfp:mc68901 port map (
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
	mfp_ii <= x"ff";
	mfp_tai <= '1';
	mfp_tbi <= sde;
	mfp_si <= '0';
	mfp_rc <= '0';
	mfp_tc <= '0';

end structure;