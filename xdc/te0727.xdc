
# General
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]
set_property CFGBVS VCCO [current_design]
set_property BITSTREAM.CONFIG.UNUSEDPIN PULLUP [current_design]

# HDMI
set_property IOSTANDARD TMDS_33 [get_ports {hdmi_tx_clk_p hdmi_tx_d_p[*]}]
set_property PACKAGE_PIN F12 [get_ports hdmi_tx_clk_p]
set_property PACKAGE_PIN E11 [get_ports hdmi_tx_d_p[0]]
set_property PACKAGE_PIN G15 [get_ports hdmi_tx_d_p[1]]
set_property PACKAGE_PIN F13 [get_ports hdmi_tx_d_p[2]]

# LED
set_property IOSTANDARD LVCMOS33 [get_ports led]
set_property PACKAGE_PIN G14 [get_ports led]

set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins psd/clk_wiz_0/inst/mmcm_adv_inst/CLKOUT0]] -group [get_clocks clk_fpga_0]
