/*
 * arch/powerpc/platforms/83xx/mpc831x_rdb.c
 *
 * Description: MPC831x RDB board specific routines.
 * This file is based on mpc834x_sys.c
 * Author: Lo Wlison <r43300@freescale.com>
 *
 * Copyright (C) Freescale Semiconductor, Inc. 2006. All rights reserved.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/pci.h>
#include <linux/of_platform.h>
#include <linux/spi/spi.h>
#include <linux/fsl_devices.h>
#include <linux/can/platform/mcp251x.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include <asm/time.h>
#include <asm/ipic.h>
#include <asm/udbg.h>
#include <sysdev/fsl_pci.h>

#include "mpc83xx.h"

/*
 * Setup the architecture
 */
static void __init mpc831x_rdb_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("mpc831x_rdb_setup_arch()", 0);

	mpc83xx_setup_pci();
	mpc831x_usb_cfg();
}

static const char *board[] __initdata = {
	"MPC8313ERDB",
	"fsl,mpc8315erdb",
	NULL
};

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init mpc831x_rdb_probe(void)
{
	return of_flat_dt_match(of_get_flat_dt_root(), board);
}

machine_device_initcall(mpc831x_rdb, mpc83xx_declare_of_platform_devices);

define_machine(mpc831x_rdb) {
	.name			= "MPC831x RDB",
	.probe			= mpc831x_rdb_probe,
	.setup_arch		= mpc831x_rdb_setup_arch,
	.init_IRQ		= mpc83xx_ipic_init_IRQ,
	.get_irq		= ipic_get_irq,
	.restart		= mpc83xx_restart,
	.time_init		= mpc83xx_time_init,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};

static int __init of_fsl_spi_probe(char *type, char *compatible, u32 sysclk,
				   struct spi_board_info *board_infos,
				   unsigned int num_board_infos,
				   void (*cs_control)(struct spi_device *dev,
						      bool on))
{
	struct device_node *np;
	unsigned int i = 0;

	for_each_compatible_node(np, type, compatible) {
		int ret;
		unsigned int j;
		const void *prop;
		struct resource res[2];
		struct platform_device *pdev;
		struct fsl_spi_platform_data pdata = {
			.cs_control = cs_control,
		};

		memset(res, 0, sizeof(res));

		pdata.sysclk = sysclk;

		prop = of_get_property(np, "reg", NULL);
		if (!prop)
			goto err;
		pdata.bus_num = *(u32 *)prop;

		pdata.bus_num = 32766;	//WTO, using hard-coded bus number

		prop = of_get_property(np, "cell-index", NULL);
		if (prop)
			i = *(u32 *)prop;

		prop = of_get_property(np, "mode", NULL);
		if (prop && !strcmp(prop, "cpu-qe"))
			pdata.flags = SPI_QE_CPU_MODE;

		for (j = 0; j < num_board_infos; j++) {
			if (board_infos[j].bus_num == pdata.bus_num)
				pdata.max_chipselect++;
		}

		if (!pdata.max_chipselect)
			continue;

		ret = of_address_to_resource(np, 0, &res[0]);
		if (ret)
			goto err;

		ret = of_irq_to_resource(np, 0, &res[1]);
		if (ret == NO_IRQ)
			goto err;

		pdev = platform_device_alloc("mpc83xx_spi", i);
		if (!pdev)
			goto err;

		ret = platform_device_add_data(pdev, &pdata, sizeof(pdata));
		if (ret)
			goto unreg;

		ret = platform_device_add_resources(pdev, res,
						    ARRAY_SIZE(res));
		if (ret)
			goto unreg;

		ret = platform_device_add(pdev);
		if (ret)
			goto unreg;

		goto next;
unreg:
		platform_device_del(pdev);
err:
		pr_err("%s: registration failed\n", np->full_name);
next:
		i++;
	}

	return i;
}

static int __init fsl_spi_init(struct spi_board_info *board_infos,
			       unsigned int num_board_infos,
			       void (*cs_control)(struct spi_device *spi,
						  bool on))
{
	u32 sysclk = -1;
	int ret;

	ret = of_fsl_spi_probe(NULL, "fsl,spi", sysclk, board_infos,
			       num_board_infos, cs_control);
	if (!ret)
		of_fsl_spi_probe("spi", "fsl_spi", sysclk, board_infos,
				 num_board_infos, cs_control);

	printk( KERN_INFO "mpc831x: Registering SPI board info\n" );	
	return spi_register_board_info(board_infos, num_board_infos);
}

int mpc_setup( struct spi_device *spi );

static struct mcp251x_platform_data mcp251x_info = {
	.oscillator_frequency = 12000000,
	.board_specific_setup = mpc_setup,
	.power_enable = NULL,
	.transceiver_enable = NULL,
};

static struct spi_board_info mpc_spi_device = {
	.modalias           = "mcp2515",
	.platform_data      = &mcp251x_info,
	.irq                = 0,
	.max_speed_hz       = 1000000,
	.bus_num            = 32766,
	.chip_select        = 0,
	.mode			= SPI_MODE_0, 
};

static int get_mcp2515_gpio( char *pzName )
{
	struct device_node *np, *child;
	int line = -1;

	np = of_find_compatible_node(NULL, NULL, "xcp2515");
	if (!np) {
		printk(KERN_ERR __FILE__ ": Unable to find mcp2515 info\n");
		return -ENOENT;
	}

	for_each_child_of_node(np, child)
		if (strcmp(child->name, pzName) == 0)
			line = of_get_gpio(child, 0);

	return line;
}

int mpc_setup( struct spi_device *spi )
{
	int iStandbyLine;

	iStandbyLine = get_mcp2515_gpio( "standby_line" );	
	gpio_request( iStandbyLine, NULL );	
	gpio_direction_output( iStandbyLine, 0 );

	return 0;
}

static int __init mpc832x_spi_init(void)
{
	int iIntLine;

	iIntLine = get_mcp2515_gpio( "int_line" );
	mpc_spi_device.irq = __gpio_to_irq(iIntLine);	

	return fsl_spi_init(&mpc_spi_device, 1, NULL);
}
machine_device_initcall(mpc831x_rdb, mpc832x_spi_init);

