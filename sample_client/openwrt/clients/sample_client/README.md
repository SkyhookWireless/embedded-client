## Prepare OpenWrt build environment

The OpenWrt git repository can be cloned using:

    $ git clone https://git.openwrt.org/openwrt/openwrt.git
    $ cd /home/{user}/openwrt
    
Checkout to a stable code revision:

    $ git checkout v17.01.2
    
Then we can configure the tool, using:
    
    $ make menuconfig
    
From the menu, choose the suitable 'Target System', 'Subtarget' and 'Target Profile'.
Remember to save your settings before closing the menu.
For testing purpose, I would suggest: 
- 'Target System': x86
- 'Subtarget': x86_64
- 'Target Profile': Generic

Now you can build the target tools and cross-compilation toolchain:

    $ make toolchain/install
    
The target-independent tools, and the toolchain are deployed to the staging_dir/host/ and staging_dir/toolchain/ directories.
_Note that the toolchain directory has a set of identifying variables on the directory name that relate to your target system. These variables specify the computer architecture, sub-architecture, used C compiler, its version, the name of the C standard library, and the version of this library._

## Add your package/config/makefile into OpenWrt build system
    
    $ cd /home/{user}/openwrt
    $ vi feeds.conf
    
Modify the file with the local feed package:

    src-link sample_client /home/{user}/embedded-client/sample_client/openwrt
    
Update and install feeds:

    $ ./scripts/feeds update sample_client
    $ ./scripts/feeds install -a -p sample_client
    
Now we can use `make menucinfig` to add our application to a build.
    
    $ make menuconfig
    <Select> Clients
    <Y> sample_client
    <Save>
    <Exit>
    
## Building, deploying, testing

    $ cd /home/{user}/openwrt
    
Build your application image:
    
    $ make package/sample_client/compile
    
In case of successful build, we can find our `.ipk` image in `bin/packages/<arch>/sample_client` folder.
For testing purpose we can use virtualization tool like: Virtual Box, or docker.
Let's deploy and test our app, using docker:

    $ docker run --rm -it openwrtorg/rootfs

Note: 'Subtarget' of the docker image - is `x86_64`, so you can run images only with this 'Subtarget' specified.

To transfer package to your target device you can use: `scp` or `docker cp` utils.
Assuming you transferred the `.ipk` image to the `/tmp` package.

    $ opkg install /tmp/{image-name}.ipk
    $ sample_client ./usr/sbin/sample_client.conf