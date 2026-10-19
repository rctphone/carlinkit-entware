/* Match this unit's vendor reset and USB-A ID pin configuration. */
&wdog1 {
    /delete-property/ fsl,ext-reset-output;
};

&usbotg1 {
    /delete-property/ pinctrl-names;
    /delete-property/ pinctrl-0;
};
