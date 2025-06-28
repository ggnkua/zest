-- glue.vhd - Implementation of the Atari ST GLUE chip
--
-- Copyright (c) 2020-2025 Francois Galea <fgalea at free.fr>
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

entity mono_hde_gen is
	port (
		clk : in std_logic;
		clken : in std_logic;
		resetn : in std_logic;
		wakest : in std_logic_vector(1 downto 0);
		in_hde : in std_logic;
		out_hde : out std_logic
	);
end entity;

architecture hdl of mono_hde_gen is
	constant MAX : integer := 1023;

	signal ccnt : integer range 0 to MAX;
	signal hde1 : std_logic;
	signal begdly : integer range 0 to 31;
begin

	begdly <= to_integer(unsigned(wakest)+1)*4+16;

process(clk,resetn)
	variable nccnt : integer range 0 to MAX;
begin
	if resetn = '0' then
		ccnt <= 0;
		hde1 <= '0';
		out_hde <= '0';
	elsif rising_edge(clk) then
		if clken = '1' then
			hde1 <= in_hde;
			if in_hde = '1' and hde1 = '0' then
				nccnt := 0;
			elsif ccnt < MAX then
				nccnt := ccnt + 1;
			end if;
			ccnt <= nccnt;
			if nccnt = begdly then
				out_hde <= '1';
			elsif nccnt = begdly+640 then
				out_hde <= '0';
			end if;
		end if;
	end if;
end process;

end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity glue is
	port (
		clk         : in std_logic;
		en8rck      : in std_logic;
		en8fck      : in std_logic;
		en2rck      : in std_logic;
		en2fck      : in std_logic;
		en32ck      : in std_logic;
		resetn      : in std_logic;

		iA          : in std_logic_vector(23 downto 1);
		iASn        : in std_logic;
		iRWn        : in std_logic;
		iD          : in std_logic_vector(2 downto 0);
		iUDSn       : in std_logic;
		iLDSn       : in std_logic;
		iDTACKn     : in std_logic;
		oRWn        : out std_logic;
		oDTACKn     : out std_logic;
		BEER        : out std_logic;
		oD          : out std_logic_vector(2 downto 0);

		FC          : in std_logic_vector(2 downto 0);
		IPLn        : out std_logic_vector(2 downto 1);
		VPAn        : out std_logic;
		VMAn        : in std_logic;
		cs6850      : out std_logic;
		FCSn        : out std_logic;
		iRDY        : in std_logic;
		oRDY        : out std_logic;
		RAMn        : out std_logic;
		DMAn        : out std_logic;
		DEVn        : out std_logic;
		rom_r       : out std_logic;
		rom_r_done  : in std_logic;
		turboram_r  : out std_logic;
		turboram_r_done : in std_logic;
		turboram_w  : out std_logic;
		turboram_w_done : in std_logic;
		turboram_ds	: out std_logic_vector(1 downto 0);
		turbo_sync	: out std_logic;

		BRn         : out std_logic;
		BGn         : in std_logic;
		BGACKn      : out std_logic;

		MFPCSn      : out std_logic;
		MFPINTn     : in std_logic;
		IACKn       : out std_logic;

		SNDCSn      : out std_logic;

		VSYNC       : out std_logic;
		HSYNC       : out std_logic;
		BLANKn      : out std_logic;
		DE          : out std_logic;

		vid_vsync   : out std_logic;
		vid_hsync   : out std_logic;
		vid_de	    : out std_logic;

		wakestate   : in std_logic_vector(1 downto 0);
		cfg_memtop	: in std_logic_vector(5 downto 0);
		cfg_extmod  : in std_logic;
		cfg_romsize : in std_logic_vector(1 downto 0);
		cfg_turbo	: in std_logic
	);
end glue;


architecture behavioral of glue is

	-- resolution
	signal mono_i	: std_logic;	-- mono mode, immediate value (combinational)
	signal mono_ff	: std_logic;	-- mono mode, registered
	signal mono		: std_logic;	-- mono mode, reference signal
	signal medres	: std_logic;	-- medium mode
	signal pal_i	: std_logic;	-- PAL mode, immediate
	signal pal_ff	: std_logic;	-- PAL mode, synced with en8fck
	signal pal_ff2	: std_logic;	-- PAL mode, synced with en2rck
	signal pal		: std_logic;	-- PAL mode, reference signal
	signal extmod   : std_logic;    -- extended mode bit

	signal irq_vbl	: std_logic;
	signal irq_hbl	: std_logic;
	signal ack_vbl	: std_logic;
	signal ack_hbl	: std_logic;
	signal ack_mfp	: std_logic;
	signal vpa_irqn	: std_logic;
	signal vpa_acia	: std_logic;
	signal sdtackn	: std_logic;
	signal ymdtackn	: std_logic;
	signal trdtackn	: std_logic;
	signal beercnt	: unsigned(5 downto 0);
	signal rwn_ff	: std_logic;
	signal dma_w	: std_logic;
	type dma_st_t is ( idle, wait_bg, wait_sync, running, wait_rdy );
	signal dma_st	: dma_st_t;
	signal dma_cnt	: unsigned(2 downto 0);
	signal sdma		: std_logic;
	signal sram		: std_logic;	-- RAM access (standard)
	signal srom		: std_logic;	-- ROM access
	signal srturbo	: std_logic;	-- RAM access (turbo)
	signal turbosyn : std_logic;	-- turbo flag, synchronised between memory accesses
	signal mem_over	: std_logic;	-- memory overflow
	signal mem_err	: std_logic;	-- memory error
	signal mmuct	: unsigned(1 downto 0);
	signal idtackff	: std_logic;
	signal sdevn	: std_logic;	-- MMU register select
	signal smfpcsn	: std_logic;	-- MFP select
	signal sdmacsn	: std_logic;	-- DMA registers select
	signal spsgcsn	: std_logic;	-- PSG select

	signal ias		: std_logic;
	signal asn_ff	: std_logic;
	signal ifc2z	: std_logic;	-- supervisor
	signal iiack	: std_logic;	-- cpu space
	signal fcx		: std_logic;	-- data/program
	signal idev		: std_logic;	-- supervisor data/program + address=ffxxxx
	signal mdesel	: std_logic;
	signal syncsel	: std_logic;
	signal isndcsb	: std_logic;
	signal sndcsd	: std_logic_vector(1 downto 0);
	signal ifcsb	: std_logic;
	signal ifcsd	: std_logic;
	signal ia16		: std_logic_vector(15 downto 0);

	-- horizontal sync
	signal hsc		: integer range 0 to 127;
	signal hsync0	: std_logic;
	signal hsync1	: std_logic;
	signal shsync	: std_logic;

	-- vertical sync
	signal vsc		: integer range 0 to 511;
	signal vsync1	: std_logic;
	signal svsync	: std_logic;

	-- horizontal de/blank
	signal cpal		: std_logic;
	signal cntsc	: std_logic;
	signal cextpal	: std_logic;
	signal hdec		: integer range 0 to 255;
	signal hde		: std_logic;
	signal hblank	: std_logic;

	-- vertical de/blank
	signal vdec		: integer range 0 to 511;
	signal vblank	: std_logic;
	signal vde		: std_logic;

	-- signals for the additional de/sync signals management
	signal vid_vde	: std_logic;
	signal vid_hde	: std_logic;
	signal vhsync0	: std_logic;
	signal vhsync1	: std_logic;
	signal vsmono	: std_logic;
	signal vspal	: std_logic;
	signal vscpal	: std_logic;
	signal vscntsc	: std_logic;
	signal vsyncd	: std_logic;
	signal vscnt	: integer range 0 to 3;

	-- mono HDE
	signal mono_hde : std_logic;
	signal vimo_hde : std_logic;


begin

turbo_sync <= turbosyn;
vimo_hde <= vid_hde when vsmono = '0' else mono_hde;
vid_de <= vid_vde and vimo_hde;
VSYNC <= svsync;
HSYNC <= shsync;
VPAn <= vpa_irqn and vpa_acia;
oDTACKn <= sdtackn and trdtackn;
oRDY <= sdma;

ia16 <= iA(15 downto 1) & '0';
ias <= not iASn;
ifc2z <= FC(2);
iiack <= FC(2) and FC(1) and FC(0) and ias;
fcx <= FC(1) xor FC(0);
idev <= '1' when ifc2z = '1' and fcx = '1' and iA(23 downto 16) = x"ff" else '0';
mdesel <= '1' when idev = '1' and iASn = '0' and ia16 = x"8260" else '0';
syncsel <= '1' when idev = '1' and iASn = '0' and ia16 = x"820a" else '0';
isndcsb <= '0' when idev = '1' and iASn = '0' and iA(15 downto 8) = x"88" else '1';
mono_i <= iD(1) when mdesel = '1' and iRWn = '0' and iUDSn = '0' else mono_ff;
pal_i <= iD(1) when syncsel = '1' and iRWn = '0' and iUDSn = '0' else pal_ff;
mono <= mono_i;
pal <= pal_ff;
mem_over <= '1' when unsigned(iA(23 downto 18)) > unsigned(cfg_memtop) else '0';
mem_err <= mem_over when iA(23 downto 22) /= "00" else '0';

-- asynchronous memory access signals for turbo mode to minimise cycles
rom_r <= srom;
turboram_r <= srturbo and iRWn;
process(srturbo,iRWn,mem_over,iUDSn,iLDSn)
begin
	if srturbo = '1' and iRwn = '0' and mem_over = '0' then
		turboram_w <= '1';
		turboram_ds <= not (iUDSn, iLDSn);
	else
		turboram_w <= '0';
		turboram_ds <= "00";
	end if;
end process;

process(clk,resetn)
begin
	if resetn = '0' then
		DMAn <= '1';
		RAMn <= '1';
		asn_ff <= '1';
	elsif rising_edge(clk) then
		DMAn <= sdma;
		RAMn <= sram;
		asn_ff <= iASn;
	end if;
end process;

-- synchronised turbo flag, to prevent mode switches during memory accesses
process(clk,resetn)
begin
	if resetn = '0' then
		turbosyn <= '0';
	elsif rising_edge(clk) then
		if sram = '1' and sdma = '1' and srom = '0' and srturbo = '0' and sdtackn = '1' and trdtackn = '1' and iDTACKn = '1' then
			-- change turbo flag only between memory accesses
			if iASn = '0' and iA(23 downto 20) = x"f" and (unsigned(iA(19 downto 16)) < x"c" or iA(19 downto 16) = x"f") then
				-- switch to 8 MHz during peripheral accesses
				turbosyn <= '0';
			else
				turbosyn <= cfg_turbo;
			end if;
		end if;
	end if;
end process;

-- mfp access
MFPCSn <= smfpcsn;
process(idev,iA,iASn)
begin
	if idev = '1' and iASn = '0' and iA(15 downto 6)&"000000" = x"fa00" then
		smfpcsn <= '0';
	else
		smfpcsn <= '1';
	end if;
end process;

-- dma registers access
FCSn <= sdmacsn;
process(idev,iA,iASn)
begin
	if idev = '1' and iASn = '0' and iA(15 downto 2)&"00" = x"8604" then
		sdmacsn <= '0';
	else
		sdmacsn <= '1';
	end if;
end process;

-- YM registers access
SNDCSn <= spsgcsn;
process(isndcsb,iUDSn)
begin
	if isndcsb = '0' and iUDSn = '0' then
		spsgcsn <= '0';
	else
		spsgcsn <= '1';
	end if;
end process;

-- video registers
process(clk,resetn)
begin
	if resetn = '0' then
		mono_ff <= '0';
		medres <= '0';
		pal_ff <= '0';
		extmod <= '0';
	elsif rising_edge(clk) then
		if en8rck = '1' then
			if mdesel = '1' and iRwn = '0' and iUDSn = '0' then
				-- write to registers
				extmod <= iD(2) and cfg_extmod;
				medres <= iD(0);
			end if;
		end if;
		if en8fck = '1' then
			mono_ff <= mono_i;
			pal_ff <= pal_i;
		end if;
		if en2rck = '1' then
			pal_ff2 <= pal_ff;
		end if;
	end if;
end process;


-- peripheral register access
process(clk,resetn)
begin
	if resetn = '0' then
		oD <= (others => '1');
		ymdtackn <= '1';
		sdtackn <= '1';
		trdtackn <= '1';
		dma_w <= '0';
	elsif rising_edge(clk) then
		trdtackn <= '1';
		if srturbo = '1' then
			-- DTACKn for turbo mode RAM accesses
			if iRWn = '1' and turboram_r_done = '1' then
				trdtackn <= '0';
			end if;
			if iRwn = '0' and turboram_w_done = '1' then
				trdtackn <= '0';
			end if;
		elsif turbosyn = '1' and mem_over = '1' and mem_err = '0' then
			trdtackn <= '0';
		elsif turbosyn = '1' and srom = '1' then
			if rom_r_done = '1' then
				trdtackn <= '0';
			end if;
		end if;
		if en8rck = '1' then
			oD <= (others => '1');
			sdtackn <= '1';
			ymdtackn <= '1';
			if idev = '1' and asn_ff = '0' and (iUDSn = '0' or iLDSn = '0' or (iRwn = '0' and rwn_ff = '1')) then
				-- hardware registers
				if syncsel = '1' and iUDSn = '0' and iRWn = '1' then
					oD <= '0'&pal&'0';
				end if;
				if mdesel = '1' and iUDSn = '0' and iRWn = '1' then
					-- read resolution
					-- should be Shifter's job, but we need to allow read access to extmod so we do it in GLUE
					oD(2) <= cfg_extmod and extmod;
					oD(1) <= mono;
					oD(0) <= medres;
				end if;
				if iA(15 downto 2)&"00" = x"8604" then
					-- assert DTACKn for DMA register access
					sdtackn <= '0';
				end if;
				if isndcsb = '0' then
					-- assert DTACKn for PSG register access (1 extra cycle delay)
					ymdtackn <= '0';
				end if;
			end if;
			if srom = '1' and turbosyn = '0' then
				-- assert DTACKn for ROM access
				sdtackn <= '0';
			end if;
			if idev = '1' and asn_ff = '0' and iUDSn = '0' and iRWn = '0' then
				if ia16 = x"8606" then
					dma_w <= iD(0);
				end if;
			end if;
			if ymdtackn = '0' and asn_ff = '0' then
				sdtackn <= '0';
			end if;
		end if;
	end if;
end process;


-- bus error
BEER <= beercnt(5);
process(clk,resetn,iASn)
begin
	if resetn = '0' or iASn = '1' then
		beercnt <= "100000";
	elsif rising_edge(clk) then
		if en8rck = '1' then
			beercnt <= beercnt + 1;
		end if;
	end if;
end process;

-- 8-bit bus (ACIA) signal management
process(iA,iASn,VMAn)
begin
	vpa_acia <= '1';
	cs6850 <= '0';
	if iA(23 downto 9)&"000000000" = x"fffc00" and iASn = '0' then
		-- accept all addresses in $fffc00 -> $fffdff range
		vpa_acia <= '0';
		if iA(23 downto 3)&"000" = x"fffc00" and VMAn = '0' then
			-- enable ACIAs only for $fffc00 -> $fffc07 range
			cs6850 <= '1';
		end if;
	end if;
end process;


-- RAMn / DEVn bus signals to the MMU
DEVn <= sdevn;
process(clk,resetn)
begin
	if resetn = '0' then
		rwn_ff <= '1';
	elsif rising_edge(clk) then
		if en8rck = '1' then
			rwn_ff <= iRWn;
		end if;
	end if;
end process;
process(FC,iA,iASn,iUDSn,iLDSn,iRWn,rwn_ff,cfg_romsize,mem_over,mem_err,turbosyn)
begin
	sram <= '1';
	srom <= '0';
	srturbo <= '0';
	sdevn <= '1';
	if FC /= "111" and iASn = '0' then
		if iA(23 downto 15) = "111111111" then
			-- hardware registers
			if FC(2) = '1' then
				if iA(15 downto 7)&"0000000" = x"8200" or iA(15 downto 1)&'1' = x"8001" or iA(15 downto 3)&"000" = x"8608" then
					sdevn <= '0';
				end if;
			end if;
		else
			if ((unsigned(iA(23 downto 16)) >= x"fa" and unsigned(iA(23 downto 16)) <= x"fb")
			or (unsigned(iA(23 downto 16)) >= x"fc" and unsigned(iA(23 downto 16)) <= x"fe" and cfg_romsize = "00")
			or (unsigned(iA(23 downto 16)) >= x"e0" and unsigned(iA(23 downto 16)) <= x"e3" and cfg_romsize = "01")
			or (unsigned(iA(23 downto 16)) >= x"e0" and unsigned(iA(23 downto 16)) <= x"e7" and cfg_romsize = "10")
			or (unsigned(iA(23 downto 16)) >= x"e0" and unsigned(iA(23 downto 16)) <= x"ef" and cfg_romsize = "11"))
			and iRWn = '1' then
				-- rom access
				srom <= '1';
			elsif unsigned(iA&'0') < 8 and iRWn = '1' and FC(2) = '1' then
				-- rom access
				srom <= '1';
			elsif unsigned(iA&'0') < x"800" and unsigned(iA&'0') >= 8 and FC(2) = '1' then
				-- protected ram access (supervisor mode only)
				sram <= turbosyn;
				srturbo <= turbosyn;
			elsif unsigned(iA&'0') >= x"800" and mem_err = '0' then
				-- ram access
				sram <= turbosyn;
				srturbo <= turbosyn and not mem_over;
			end if;
		end if;
	end if;
end process;

-- MMU counter
process(clk,resetn)
begin
	if resetn = '0' then
		idtackff <= '1';
		mmuct <= "00";
	elsif rising_edge(clk) then
		if en8fck = '1' then
			idtackff <= iDTACKn;
			if iDTACKn = '0' and idtackff = '1' and sram = '0' then
				-- synchronize with MMU counter
				mmuct <= "11";
			else
				mmuct <= mmuct + 1;
			end if;
		end if;
	end if;
end process;

-- dma operation
process(clk,resetn)
begin
	if resetn = '0' then
		BRn <= '1';
		BGACKn <= '1';
		dma_st <= idle;
		dma_cnt <= "000";
		sdma <= '1';
		oRWn <= '1';
	elsif rising_edge(clk) then
		if en8rck = '1' then
			case dma_st is
			when idle =>
				if iRDY = '1' then
					-- initiate bus request
					BRn <= '0';
					dma_st <= wait_bg;
				end if;
			when wait_bg =>
				if BGn = '0' and iASn = '1' and iDTACKn = '1' then
					BGACKn <= '0';
					dma_cnt <= "111";
					dma_st <= running;
					if mmuct = 0 then
						dma_st <= running;
					else
						dma_st <= wait_sync;
					end if;
				end if;
			when wait_sync =>
				BRn <= '1';
				if mmuct = 0 then
					dma_st <= running;
				end if;
			when running =>
				BRn <= '1';
				if mmuct = 0 then
					oRWn <= '1';
				elsif mmuct = 1 then
					sdma <= '0';
					oRWn <= dma_w;
				elsif mmuct = 3 then
					if dma_cnt = 0 then
						dma_st <= wait_rdy;
					else
						dma_cnt <= dma_cnt - 1;
						dma_st <= running;
					end if;
				end if;
			when wait_rdy =>
				oRWn <= '1';
				BGACKn <= '1';
				if iRDY = '0' then
					dma_st <= idle;
				end if;
			end case;
		elsif en8fck = '1' then
			if sdma = '0' and mmuct = 3 then
				sdma <= '1';
			end if;
		end if;
	end if;
end process;

-- interrupt control
process(FC,iA,iASn)
begin
	ack_mfp <= '0';
	ack_vbl <= '0';
	ack_hbl <= '0';
	if FC = "111" and iA(19 downto 16) = "1111" and iASn = '0' then
		case iA(3 downto 2) is
			when "11" => ack_mfp <= '1';
			when "10" => ack_vbl <= '1';
			when "01" => ack_hbl <= '1';
			when others =>
		end case;
	end if;
end process;

-- vbl irq
process(clk,resetn,ack_vbl)
begin
	if resetn = '0' or ack_vbl = '1' then
		irq_vbl <= '0';
	elsif rising_edge(clk) then
		if en2rck = '1' then
			if vsync1 = '1' then
				irq_vbl <= '1';
			end if;
		end if;
	end if;
end process;

-- hbl irq
process(clk,resetn,ack_hbl)
begin
	if resetn = '0' or ack_hbl = '1' then
		irq_hbl <= '0';
	elsif rising_edge(clk) then
		if en2rck = '1' then
			if hsync1 = '1' then
				irq_hbl <= '1';
			end if;
		end if;
	end if;
end process;

-- compute IPL
process(irq_hbl,irq_vbl,ack_vbl,ack_hbl,MFPINTn)
begin
	if MFPINTn = '0' then
		IPLn <= "00";
	elsif irq_vbl = '1' and ack_vbl = '0' then
		IPLn <= "01";
	elsif irq_hbl = '1' and ack_hbl = '0' then
		IPLn <= "10";
	else
		IPLn <= "11";
	end if;
end process;

-- Autovector interrupt acknowledge (for HBL and VBL)
process(FC,iA,iASn)
begin
	vpa_irqn <= '1';
	if FC = "111" and iA(19 downto 16) = "1111" and iA(3 downto 2) /= "11" and iASn = '0' then
		vpa_irqn <= '0';
	end if;
end process;

-- Vectored interrupt acknowledge (for MFP)
IACKn <= not ack_mfp;

-- horizontal sync
process(mono,hsc)
begin
	hsync0 <= '0';
	hsync1 <= '0';
	if mono = '0' and hsc = 101 then
		hsync0 <= '1';
	end if;
	if mono = '1' and hsc = 121 then
		hsync0 <= '1';
	end if;
	if mono = '0' and hsc = 111 then
		hsync1 <= '1';
	end if;
	if mono = '1' and hsc = 127 then
		hsync1 <= '1';
	end if;
end process;
process(vscpal,vscntsc,vsmono,hsc)
begin
	vhsync0 <= '0';
	vhsync1 <= '0';
	if vscpal = '1' and hsc = 103 then
		vhsync1 <= '1';
	end if;
	if vscpal = '1' and hsc = 109 then
		vhsync0 <= '1';
	end if;
	if vscntsc = '1' and hsc = 103 then
		vhsync1 <= '1';
	end if;
	if vscntsc = '1' and hsc = 109 then
		vhsync0 <= '1';
	end if;
	if vsmono = '1' and hsc = 122 then
		vhsync1 <= '1';
	end if;
	if vsmono = '1' and hsc = 72 then
		vhsync0 <= '1';
	end if;
end process;
process(clk,resetn)
begin
	if resetn = '0' then
		hsc <= 0;
		shsync <= '1';
		vid_hsync <= '0';
	elsif rising_edge(clk) then
		if en2fck = '1' then
			if hsc = 127 then
				if mono = '1' then
					hsc <= 72;
				elsif pal_ff2 = '1' then
					hsc <= 0;
				else	-- ntsc
					hsc <= 1;
				end if;
			else
				hsc <= hsc + 1;
			end if;
		end if;
		if en2rck = '1' then
			if hsync0 = '1' then
				shsync <= '0';
			end if;
			if hsync1 = '1' then
				shsync <= '1';
			end if;
			if vhsync0 = '1' then
				vid_hsync <= '0';
			end if;
			if vhsync1 = '1' then
				vid_hsync <= '1';
			end if;
		end if;
	end if;
end process;

-- vertical sync
vsync1 <= '1' when hsc = 127 and vsc = 511 else '0';
process(clk,resetn)
begin
	if resetn = '0' then
		vsc <= 0;
		svsync <= '1';
		vspal <= '0';
		vsmono <= '0';
		vsyncd <= '1';
		vscnt <= 0;
		vid_vsync <= '0';
	elsif rising_edge(clk) then
		if en2rck = '1' then
			if hsc = 127 then
				if vsc = 511 then
					if mono = '1' then
						vsc <= 11;
					elsif pal = '1' then
						vsc <= 199;
					else
						vsc <= 249;
					end if;
					vspal <= pal;
					vsmono <= mono;
				else
					vsc <= vsc + 1;
				end if;
				if mono = '0' and vsc = 508 then
					svsync <= '0';
				end if;
				if mono = '1' and vsc = 510 then
					svsync <= '0';
				end if;
			end if;
			if vsync1 = '1' then
				svsync <= '1';
			end if;
			if vhsync1 = '1' then
				vsyncd <= svsync;
				if svsync = '1' and vsyncd = '0' then
					vid_vsync <= '1';
					vscnt <= 3;
				elsif vscnt > 0 then
					vscnt <= vscnt - 1;
					if vscnt - 1 = 0 then
						vid_vsync <= '0';
					end if;
				end if;
			end if;
		end if;
	end if;
end process;

-- horizontal DE/blank
cpal <= '1' when mono = '0' and pal = '1' and extmod = '0' else '0';
cntsc <= '1' when mono = '0' and pal = '0' else '0';
cextpal <= '1' when mono = '0' and pal = '1' and extmod = '1' else '0';
vscpal <= '1' when vsmono = '0' and vspal = '1' else '0';
vscntsc <= '1' when vsmono = '0' and vspal = '0' else '0';
process(clk,resetn,shsync)
begin
	if resetn = '0' or shsync = '0' then
		-- ihsyncb
		hdec <= 0;
		hblank <= '0';
		hde <= '0';
		vid_hde <= '0';
	elsif rising_edge(clk) then
		if en2fck = '1' then
			hdec <= hdec + 1;
			if hdec = 114 then
				hblank <= '0';
			end if;
			if cpal = '1' and hdec = 9 then
				hblank <= '1';
			end if;
			if cntsc = '1' and hdec = 8 then
				hblank <= '1';
			end if;
			if mono = '1' and hdec = 3 then
				hde <= '1';
			end if;
			if cpal = '1' and hdec = 16 then
				hde <= '1';
			end if;
			if cntsc = '1' and hdec = 15 then
				hde <= '1';
			end if;
			if mono = '1' and hdec = 43 then
				hde <= '0';
			end if;
			if cpal = '1' and hdec = 96 then
				hde <= '0';
			end if;
			if cntsc = '1' and hdec = 95 then
				hde <= '0';
			end if;
			if cextpal = '1' and hdec = 9 then
				hblank <= '1';
			end if;
			if cextpal = '1' and hdec = 3 then
				hde <= '1';
			end if;
			if cextpal = '1' and hdec = 107 then
				hde <= '0';
			end if;
		end if;
		if en2rck = '1' then
			if vscpal = '1' and hdec = 10 then
				vid_hde <= '1';
			end if;
			if vscpal = '1' and hdec = 115 then
				vid_hde <= '0';
			end if;
			if vscntsc = '1' and hdec = 9 then
				vid_hde <= '1';
			end if;
			if vscntsc = '1' and hdec = 115 then
				vid_hde <= '0';
			end if;
			if vsmono = '1' and hdec = 8 then
				vid_hde <= '1';
			end if;
			if vsmono = '1' and hdec = 50 then
				vid_hde <= '0';
			end if;
		end if;
	end if;
end process;

-- vertical DE/blank
process(clk,resetn,svsync)
begin
	if resetn = '0' or svsync = '0' then
		vdec <= 0;
		vblank <= '0';
		vde <= '0';
		vid_vde <= '0';
	elsif rising_edge(clk) then
		if en2rck = '1' and hsync1 = '1' then
			vdec <= vdec + 1;
			if cpal = '1' and vdec = 24 then
				vblank <= '1';
			end if;
			if cntsc = '1' and vdec = 15 then
				vblank <= '1';
			end if;
			if cpal = '1' and vdec = 307 then
				vblank <= '0';
			end if;
			if cntsc = '1' and vdec = 257 then
				vblank <= '0';
			end if;
			if mono = '1' and vdec = 35 then
				vde <= '1';
			end if;
			if cpal = '1' and vdec = 62 then	-- 46 on old GLUE revisions
				vde <= '1';
			end if;
			if cntsc = '1' and vdec = 33 then
				vde <= '1';
			end if;
			if mono = '1' and vdec = 435 then
				vde <= '0';
			end if;
			if cpal = '1' and vdec = 262 then	-- 246 on old GLUE revisions
				vde <= '0';
			end if;
			if cntsc = '1' and vdec = 233 then
				vde <= '0';
			end if;
			if cextpal = '1' and vdec = 24 then
				vblank <= '1';
			end if;
			if cextpal = '1' and vdec = 33 then
				vde <= '1';
			end if;
			if cextpal = '1' and vdec = 309 then
				vde <= '0';
			end if;
			if cextpal = '1' and vdec = 309 then
				vblank <= '0';
			end if;
		end if;
		if en2rck = '1' and vhsync1 = '1' then
			if vscpal = '1' and vdec = 33 then
				vid_vde <= '1';
			end if;
			if vscntsc = '1' and vdec = 15 then
				vid_vde <= '1';
			end if;
			if vsmono = '1' and vdec = 35 then
				vid_vde <= '1';
			end if;
			if vscpal = '1' and vdec = 309 then
				vid_vde <= '0';
			end if;
			if vscntsc = '1' and vdec = 257 then
				vid_vde <= '0';
			end if;
			if vsmono = '1' and vdec = 435 then
				vid_vde <= '0';
			end if;
		end if;
	end if;
end process;

-- DE/BLANKn
process(clk,resetn,svsync)
begin
	if resetn = '0' or svsync = '0' then
		BLANKn <= '1';
		DE <= '0';
	elsif rising_edge(clk) then
		if en2rck = '1' then
			BLANKn <= vblank and hblank;
			DE <= vde and hde;
		end if;
	end if;
end process;

-- Mono HDE signal generation
hdegen: entity work.mono_hde_gen port map (
	clk => clk,
	clken => en32ck,
	resetn => resetn,
	wakest => wakestate,
	in_hde => vid_hde,
	out_hde => mono_hde
	);

end behavioral;
