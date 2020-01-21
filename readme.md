CFS LLC MISS MONITORING PATCH INSTALLATION:

In order to correctly install the patch, first download a fresh clean kernel 4.14.69 source tree:
$ wget https://mirrors.edge.kernel.org/pub/linux/kernel/v4.x/linux-4.14.69.tar.gz

Unzip it:
$ tar -xvzf linux-4.14.69.tar.gz

Go to the new source tree directory ( cd <source_tree_download_path>/linux-4.14.69/ ) and make clean + destroy any previous configuration (just in case !):
$ make mrproper

Apply the patch:
$ patch -p1 < CFS_cm_penalty.patch

Once the patch is applied, you need to configure the patched kernel and install it. Use the menuconfig utility to generate a config file:
$ make menuconfig

The patch is built on top of the open-source tool PMCTrack kernel patch. Apart from personal customisations, i.e. drivers, etc, the only thing you need to check is that CONFIG_PMCTRACK=y. You can search for it within the menuconfig interface.

Once you save and exit from menuconfig, you should find a .config file (find it with ls -a), and you can double check that CONFIG_PMCTRACK=y . You can now compile the kernel objects (the first time it will take long):
$ make -j$(nproc)

Finally install kernel modules and kernel:
$ sudo make modules_install
$ sudo make install


Once the kernel is correctly installed, you should find it in grub menu when you power up your machine. 


Now you need to compile and install the whole set of PMCTrack tool. You can find more information on the following links (you can ignore the kernel patch installation, since it is already done)

https://pmctrack.dacya.ucm.es/install/
https://pmctrack.dacya.ucm.es/getting-started/

The kernel patch works if the kernel module "llc_monitoring_mm.ko" is inserted. 
Once the build process is completed, you should find the obj file llc_monitoring_mm.ko. You just need to insert it and activate it. You can find more information on the above links.

