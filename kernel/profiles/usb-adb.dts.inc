
/*
 * Vendor-equivalence fixes, applied after the bundle DTS.
 *
 * 1. WL_REG_ON (GPIO1_IO05) must be driven active-high.  The bundle DTS
 *    inherited an active-low override; the vendor 3.14 DT uses
 *    enable-active-high, and with active-low the RTL8822BS stays powered
 *    down, so SDIO never enumerates and no Wi-Fi AP appears.
 * 2. The vendor wires UART2/UART3 as two-wire TX/RX only; Realtek H5 uses
 *    software flow control.  The bundle DTS inherits uart-has-rtscts.
 */
&reg_sd1_vmmc {
	regulator-name = "wlreg_on";
	gpio = <&gpio1 5 GPIO_ACTIVE_HIGH>;
	enable-active-high;
	/delete-property/ enable-active-low;
	/delete-property/ off-on-delay-us;
};

&uart2 {
	/delete-property/ uart-has-rtscts;
};

&uart3 {
	/delete-property/ uart-has-rtscts;
};

/*
 * USB-A host port (OTG1): the bundle inherits the EVK GPIO1_IO00 ->
 * ANATOP_OTG1_ID pinmux with a pull-up, which makes the controller act as a
 * B-device and prevents the USB-A socket from hosting a stick.  The vendor DT
 * does not mux the ID pin.
 */
&usbotg1 {
	/delete-property/ pinctrl-names;
	/delete-property/ pinctrl-0;
};
