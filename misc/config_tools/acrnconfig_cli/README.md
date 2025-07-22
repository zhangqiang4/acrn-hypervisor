### ACRN Command Line Interface

This folder contains ACRN CLI tool that assists ACRN's configuration and deployment.

A typical usage including creating ACRN scenario's from scratch,
or import an existing scenario.

It is recommended to run this tool directly on top of the machine that needs
to deploy ACRN (target machine).

First copy the folder containing this doc to target machine.

> Note: If you copy the folder from within ACRN hypervisor source folder
> using `cp` command, make sure to de-reference symlinks (add `-L` to `cp` command)
> as the `schema` folder is a symbolic link to `<acrn-hypervisor-src>/misc/config_tools/schema`.

Add the copied folder to `$PATH` if necessary (so that you get `acrnconfig` command)

Install necessary dependencies:
```
$ sudo apt install python3-lxml python3-defusedxml python3-xmlschema
```

### Create or import scenario:

Start by initialize an empty workspace
```
$ mkdir -p /path/to/new/scenario
$ cd /path/to/new/scenario
$ acrnconfig init myscenario
```

By default the `init` action creates a scenario with one Service VM, and
some default HV configuration.

If you plan on importing some scenarios later and don't want a default Service VM,
you can add `--create-empty` option, like this
```
$ acrnconfig init --create-empty myscenario
```

Then you can import a scenario
```
$ acrnconfig import /path/to/scenario.xml
```

To view currently recorded VMs
```
$ acrnconfig lsvm
```
You can add `-v` for a more verbose output.

You can add a VM by
```
$ acrnconfig addvm \
    --vm-type post \
    --cpus 2-5,7,9 \
    --memory 8G \
    --host-dev 0000:03:00.0 \
    --virt-dev block,img=/path/to/img \
    --virt-dev net,ifname=yaag \
    --virt-dev input,backend_device_file=/dev/input/event5,id=0 \
    my-vm
```
For more information on parameters, check `acrnconfig addvm --help`.

Also edit a VM
```
$ acrnconfig editvm <vmname>
```
or delete a VM
```
$ acrnconfig delvm <vmname>
```

### Edit Hypervisor configuration

Default hypervisor configurations work in most cases. However if you need to change
the default settings, you can do the following

First check what default value is configured for specific setting. For example,
we would like to check the default scheduler being used:
```
$ acrnconfig hv --get FEATURES/SCHEDULER
SCHED_BVT
```
The parameter to `--get` is an XML xpath specifying certain configuration.

Similarly, we can set a value for an xpath:
```
$ acrnconfig hv --set FEATURES/SCHEDULER=SCHED_NOOP
```

To add hv-land ivshmem:
```
$ acrnconfig hv --add-ivshmem \
    name=<name>,src=myvm1,dst=myvm2,srcbdf=<00:bb.f>,dstbdf=<00:bb.f>,size=4M,region_id=<id>
```

The changed configurations are stored to `hv.xml`. If you need to edit the hv settings,
another alternative is to directly edit `hv.xml`.

### Export or deploy configuration

Once the changes are made, you can export the final scenario XML
```
$ acrnconfig export --out /path/to/exported/xml
```
Or you can simply `acrnconfig export`, and `exported_scenario.xml` will be generated
under current directory.

You can also deploy ACRN. Note that the following command requires
additional dependencies as this action compiles ACRN. For a complete
list of dependencies required, please check
[this link](https://projectacrn.github.io/latest/getting-started/getting-started.html),
step 1.4.
```
$ sudo acrnconfig deploy --acrn-src=/path/to/acrn/src myboard-name
```
The `--acrn-src` can be the path to a folder containing ACRN source code, a `.tar.gz` file
or `.zip` file that compresses ACRN source code without intermediate folder
(e.g., an archive produced by `git archive`)
