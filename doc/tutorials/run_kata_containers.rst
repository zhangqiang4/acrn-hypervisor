.. _run-kata-containers:

Run Kata Containers on a Service VM
###################################

This tutorial describes how to install, configure, and run `Kata Containers
<https://katacontainers.io/>`_ on the Ubuntu based Service VM with the ACRN
hypervisor. In this configuration,
Kata Containers leverage the ACRN hypervisor instead of QEMU, which is used by
default. Refer to the `Kata Containers with ACRN presentation
<https://www.slideshare.net/ProjectACRN/acrn-kata-container-on-acrn>`_
for more details on Kata Containers and how the integration with ACRN
has been done.

Prerequisites
**************

#. Refer to the :ref:`ACRN supported hardware <hardware>`.
#. For a default prebuilt ACRN binary in the end-to-end (E2E) package, you must have 4
   CPU cores or enable "CPU Hyper-threading" in order to have 4 CPU threads for 2 CPU cores.
#. Follow the :ref:`gsg` to set up the ACRN Service VM
   based on Ubuntu.
#. This tutorial is validated on the following configurations:

   - ACRN v3.0 (branch: ``release_3.0``)
   - Ubuntu 20.04

#. Kata Containers are supported for ACRN hypervisors configured for
   the Industry or SDC scenarios.


Install Containerd
******************

The following instructions install Containerd on the Ubuntu Service VM.

#. Install the following prerequisite packages:

   .. code-block:: none

      $ sudo apt-get install containerd

#. Install required kernel modules:

   .. code-block:: none

      $ sudo modprobe vhost_vsock

#. Create required directories:

   .. code-block:: none

      $ sudo mkdir -pv /run/vc/vm

Install devmapper snapshotter
****************************

It's a containerd plugin to manage image pool.

#. Check if it's installed:

   .. code-block:: none

      $ sudo ctr plugins ls |grep devmapper

    If the output is:

   .. code-block:: none

      io.containerd.snapshotter.v1    devmapper                linux/amd64  ok

    Then you can skip this. Otherwise, follow below steps to set it up.

    Create following script:

   .. code-block:: none

        #!/bin/bash
        set -ex

        DATA_DIR=/var/lib/containerd/io.containerd.snapshotter.v1.devmapper
        POOL_NAME=containerd-pool

        mkdir -p ${DATA_DIR}

        # Create data file
        sudo touch "${DATA_DIR}/data"
        sudo truncate -s 100G "${DATA_DIR}/data"

        # Create metadata file
        sudo touch "${DATA_DIR}/meta"
        sudo truncate -s 10G "${DATA_DIR}/meta"

        # Allocate loop devices
        DATA_DEV=$(sudo losetup --find --show "${DATA_DIR}/data")
        META_DEV=$(sudo losetup --find --show "${DATA_DIR}/meta")

        # Define thin-pool parameters.
        # See https://www.kernel.org/doc/Documentation/device-mapper/thin-provisioning.txt for details.
        SECTOR_SIZE=512
        DATA_SIZE="$(sudo blockdev --getsize64 -q ${DATA_DEV})"
        LENGTH_IN_SECTORS=$(bc <<< "${DATA_SIZE}/${SECTOR_SIZE}")
        DATA_BLOCK_SIZE=128
        LOW_WATER_MARK=32768

        # Create a thin-pool device
        sudo dmsetup create "${POOL_NAME}" \
            --table "0 ${LENGTH_IN_SECTORS} thin-pool ${META_DEV} ${DATA_DEV}
            ${DATA_BLOCK_SIZE} ${LOW_WATER_MARK}"

        cat << EOF
        #
        # Add this to your config.toml configuration file and restart containerd
        daemon
        #
        [plugins]
          [plugins.devmapper]
            pool_name = "${POOL_NAME}"
            root_path = "${DATA_DIR}"
            base_image_size = "10GB"
            discard_blocks = true
        EOF>>

    Make it executable and run it:

   .. code-block:: none

      $ sudo chmod +x ~/scripts/devmapper/create.sh && \
          cd ~/scripts/devmapper/ && \
          sudo ./create.sh

    Now, we can add the devmapper configuration provided from the script to
    ``/etc/containerd/config.toml`` and restart containerd.

   .. code-block:: none

      $ sudo systemctl restart containerd

    We can use ``dmsetup`` to verify the thin-pool was created successfully.
    We should also check that devmapper is registered and running:

   .. code-block:: none

      $ sudo dmsetup ls
      # devpool (253:0)
      $ sudo ctr plugins ls | grep devmapper
      # io.containerd.snapshotter.v1    devmapper                linux/amd64 ok

    This script needs to be run only once, while setting up the devmapper
    snapshotter for containerd. Afterwards, make sure that on each reboot, the
    thin-pool is initialized from the same data dir. Otherwise, all the
    fetched containers (or the ones that you’ve created) will be
    re-initialized. A simple script that re-creates the thin-pool from the
    same data dir is shown below:

   .. code-block:: none

        #!/bin/bash
        set -ex

        DATA_DIR=/var/lib/containerd/io.containerd.snapshotter.v1.devmapper
        POOL_NAME=containerd-pool

        # Allocate loop devices
        DATA_DEV=$(sudo losetup --find --show "${DATA_DIR}/data")
        META_DEV=$(sudo losetup --find --show "${DATA_DIR}/meta")

        # Define thin-pool parameters.
        # See https://www.kernel.org/doc/Documentation/device-mapper/thin-provisioning.txt for details.
        SECTOR_SIZE=512
        DATA_SIZE="$(sudo blockdev --getsize64 -q ${DATA_DEV})"
        LENGTH_IN_SECTORS=$(bc <<< "${DATA_SIZE}/${SECTOR_SIZE}")
        DATA_BLOCK_SIZE=128
        LOW_WATER_MARK=32768

        # Create a thin-pool device
        sudo dmsetup create "${POOL_NAME}" \
            --table "0 ${LENGTH_IN_SECTORS} thin-pool ${META_DEV} ${DATA_DEV}
            ${DATA_BLOCK_SIZE} ${LOW_WATER_MARK}"

    We can create a systemd service to run above script on each reboot, save
    following content to ``/lib/systemd/system/devmapper_reload.service``

   .. code-block:: none

        [Unit]
        Description=Devmapper reload script

        [Service]
        ExecStart=/path/to/script/reload.sh

        [Install]
        WantedBy=multi-user.target

    And enable the newly created service:

   .. code-block:: none

        $ sudo systemctl daemon-reload
        $ sudo systemctl enable devmapper_reload.service
        $ sudo systemctl start devmapper_reload.service


Install Kata Containers
***********************

Install Kata from release package
=================================

#. Download the kata-3.0.0 release (not ready!!!) and install the binaries:

   .. code-block:: none

      $ wget https://github.com/kata-containers/kata-containers/releases/download/3.0.0-alpha1/kata-static-3.0.0-alpha1-x86_64.tar.xz
      $ sudo tar -C / -xvf kata-static-3.0.0-alpha1-x86_64.tar.xz

#. Create symbolic links so that containerd could find kata binaries:

   .. code-block:: none

      $ sudo ln -svf /opt/kata/bin/containerd-shim-kata-v2 /usr/local/bin/containerd-shim-kata-v2
      $ sudo ln -svf /opt/kata/bin/kata-collect-data.sh /usr/local/bin/kata-collect-data.sh
      $ sudo ln -svf /opt/kata/bin/kata-runtime /usr/local/bin/kata-runtime
      $ sudo ln -svf /opt/kata/libexec/virtiofsd /usr/libexec/virtiofsd

#. Check installation by showing version details:

   .. code-block:: none

      $ kata-runtime --version

#. Configure Kata to use ACRN.

   Copy the default ACRN configuration to Kata configuration path:

   .. code-block:: none

      $ sudo mkdir /etc/kata-containers
      $ sudo cp opt/kata/share/defaults/kata-containers/configuration-acrn.toml /etc/kata-containers/configuration.toml

   Modify the ``[hypervisor.acrn]`` section
   file.

   .. code-block:: none
      :emphasize-lines: 2,3
      :name: configuration-acrn.toml
      :caption: /opt/kata/share/defaults/kata-containers/configuration-acrn.toml

      [hypervisor.acrn]
      path = "/usr/bin/acrn-dm"
      ctlpath = "/usr/bin/acrnctl"
      kernel = "/opt/kata/share/kata-containers/vmlinuz.container"
      image = "/opt/kata/share/kata-containers/kata-containers.img"

    Run following command to check ACRN configuration:

    .. code-block:: console

       $ /opt/kata/bin/kata-runtime --kata-config /opt/kata/share/defaults/kata-containers/configuration-acrn.toml kata-env | awk -v RS= '/\[Hypervisor\]/'
        [Hypervisor]
          MachineType = ""
          Version = "DM version is: 3.0-v3.0-1-g4b4455167-dirty-4b4455167-dirty (daily tag:acrn-2022w27.1-180000p), build by acrn@2022-07-27 06:46:50"
          Path = "/usr/bin/acrn-dm"
          BlockDeviceDriver = "virtio-blk"
          EntropySource = "/dev/urandom"
          SharedFS = ""
          VirtioFSDaemon = ""
          SocketPath = ""
          Msize9p = 0
          MemorySlots = 10
          PCIeRootPort = 0
          HotplugVFIOOnRootBus = false
          Debug = false

Install Kata from source
========================

We just build runtime here. Kata agent and Guest images come from
release package. So continue below steps if above steps have been done.

#. Get Kata source code from github repository:

    .. code-block:: console

      $ git clone https://github.com/kata-containers/kata-containers

#. Apply patches if any.

#. Build runtime

   Generated binaries will overwrite pre-installed binaries.

    .. code-block:: console

      $ cd kata-containers/src/runtime
      $ make && sudo make install

Run a Kata Container With ACRN
******************************

Restart containerd service to make sure the devmapper plugin is registered.

    .. code-block:: console

      $ sudo systemctl restart containerd

Run following commands to launch an ACRN Kata container. The successful output
should be the kernel uname version.

    .. code-block:: console

        $ image="docker.io/library/busybox:latest"
        $ sudo ctr image pull "$image
        $ sudo ctr run --snapshotter devmapper --runtime "io.containerd.kata.v2" --rm -t "$image" test-kata uname -r


