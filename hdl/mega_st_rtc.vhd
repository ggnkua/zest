-- mega_st_rtc.vhd - Mega ST RTC module
--
-- Copyright (c) 2025-2026 Francois Galea <fgalea at free.fr>
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

entity mega_st_rtc is
	port (
		clk : in std_logic;
		clken : in std_logic;
		resetn : in std_logic;

		s_units : in std_logic_vector(3 downto 0);
		s_tens : in std_logic_vector(3 downto 0);
		m_units : in std_logic_vector(3 downto 0);
		m_tens : in std_logic_vector(3 downto 0);
		h_units : in std_logic_vector(3 downto 0);
		h_tens : in std_logic_vector(3 downto 0);
		weekday : in std_logic_vector(3 downto 0);
		day_units : in std_logic_vector(3 downto 0);
		day_tens : in std_logic_vector(3 downto 0);
		mon_units : in std_logic_vector(3 downto 0);
		mon_tens : in std_logic_vector(3 downto 0);
		yr_units : in std_logic_vector(3 downto 0);
		yr_tens : in std_logic_vector(3 downto 0);

		a : in std_logic_vector(15 downto 1);
		id : in std_logic_vector(0 downto 0);
		od : out std_logic_vector(3 downto 0);
		devn : in std_logic;
		rwn : in std_logic;
		vma : in std_logic;
		lds : in std_logic
	);
end mega_st_rtc;


architecture rtl of mega_st_rtc is
	signal cs : std_logic;
	signal rd : std_logic;
	signal wr : std_logic;
	signal addr : integer range 0 to 15;
	signal bank : std_logic;

begin
	cs <= '1' when devn = '0' and lds = '0' and vma = '0' and a(15 downto 5)&"00000" = x"fc20" else '0';
	rd <= cs and rwn;
	wr <= cs and not rwn;
	addr <= to_integer(unsigned(a(4 downto 1)));

process(clk,resetn)
begin
	if resetn = '0' then
		od <= "1111";
		bank <= '0';
	elsif rising_edge(clk) then
		if clken = '1' then
			od <= "1111";
			if rd = '1' then
				if bank = '0' then
					case addr is
					when 0 => od <= s_units;
					when 1 => od <= s_tens;
					when 2 => od <= m_units;
					when 3 => od <= m_tens;
					when 4 => od <= h_units;
					when 5 => od <= h_tens;
					when 6 => od <= weekday;
					when 7 => od <= day_units;
					when 8 => od <= day_tens;
					when 9 => od <= mon_units;
					when 10 => od <= mon_tens;
					when 11 => od <= yr_units;
					when 12 => od <= yr_tens;
					when others => od <= "1111";
					end case;
				else
					-- provide just the necessary logic to allow detection by TOS and EmuTOS
					case addr is
					when 2 => od <= x"a";
					when 3 => od <= x"5";
					when others => od <= "1111";
					end case;
				end if;
			end if;
			if wr = '1' then
				if addr = 13 then
					bank <= id(0);
				end if;

			end if;
		end if;
	end if;
end process;

end architecture;
