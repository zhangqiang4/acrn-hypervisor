#!/bin/bash
#
# Copyright (C) 2022 Intel Corporation.
#
# SPDX-License-Identifier: BSD-3-Clause
#

# Launch script for VM name: @@__VM_NAME__@@

# Helper functions
vm_name="@@__VM_NAME__@@"
start_time=$(date +%s%N)    # End time in nanoseconds

function log2stderr() {
    echo "[${vm_name}]launchscript: $@" >&2
}

function measure_time() {
    local start_time=$(date +%s%N)
    "$@"
    local end_time=$(date +%s%N)
    local elapsed_time=$((((end_time - start_time) + 1000000 / 2) / 1000000))

    local params="$*"
    local max_length=50
    if [[ ${#params} -gt $max_length ]]; then
        params="${params:0:$max_length}..."
    fi

    log2stderr "$(printf "%-55s[%dms]" "${params}" ${elapsed_time})"
}

shopt -s expand_aliases
for func in "enable_vf" "enable_dgpu_vf" "add_cpus" "add_virtual_device" "add_passthrough_device"; do
    alias $func="measure_time $func"
done

function probe_modules() {
    modprobe pci_stub
}

function offline_cpus() {
    # Each parameter of this function is considered the APIC ID (as is reported in /proc/cpuinfo, in decimal) of a CPU
    # assigned to a post-launched RTVM.
    log2stderr "Guest VM configured to exclusively own pCPU."
    for i in $*; do
        processor_id=$(grep -B 15 "apicid.*: ${i}$" /proc/cpuinfo | grep "^processor" | head -n 1 | cut -d ' ' -f 2)
        if [ -z ${processor_id} ]; then
            continue
        fi
        if [ "${processor_id}" = "0" ]; then
            log2stderr "Warning: processor 0 can't be offlined. Skipping processor 0."
            continue
        fi
        cpu_path="/sys/devices/system/cpu/cpu${processor_id}"
        if [ -f ${cpu_path}/online ]; then
            online=`cat ${cpu_path}/online`
            log2stderr "Offlining cpu${processor_id} from Service OS..."
            if [ "${online}" = "1" ]; then
                echo 0 > ${cpu_path}/online
                online=`cat ${cpu_path}/online`
                # during boot time, cpu hotplug may be disabled by pci_device_probe during a pci module insmod
                while [ "${online}" = "1" ]; do
                    sleep 1
                    echo 0 > ${cpu_path}/online
                    online=`cat ${cpu_path}/online`
                done
                log2stderr "Offlining vcpu${processor_id} from Service VM..."
                echo ${processor_id} > /sys/devices/virtual/misc/acrn_hsm/remove_cpu
            fi
        fi
    done
}

function configure_cpus_governor() {
    # Each parameter of this function is considered the APIC ID (as is reported in /proc/cpuinfo, in decimal) of a CPU
    # assigned to a post-launched VM.
    for i in $*; do
        processor_id=$(grep -B 15 "apicid.*: ${i}$" /proc/cpuinfo | grep "^processor" | head -n 1 | cut -d ' ' -f 2)
        if [ -z ${processor_id} ]; then
            log2stderr "No processor found for APIC ID ${i}. Skipping..."
            continue
        fi

        # Define the path to the CPU governor settings
        governor_path="/sys/devices/system/cpu/cpu${processor_id}/cpufreq/scaling_governor"
        available_governors_path="/sys/devices/system/cpu/cpu${processor_id}/cpufreq/scaling_available_governors"

        # Check if the scaling_available_governors file exists
        if [ -f "$available_governors_path" ]; then
            available_governors=$(cat "$available_governors_path")
            # Check if 'powersave' is available in the list of governors
            if [[ "$available_governors" == *"powersave"* ]]; then
                # Check if the current governor is already set to 'powersave'
                governor=$(cat "$governor_path")
                if [ "$governor" != "powersave" ]; then
                    # Set the governor to 'powersave'
                    echo "powersave" > $governor_path
                    governor=$(cat "$governor_path")

                    # Retry logic in case it fails
                    while [ "$governor" != "powersave" ]; do
                        sleep 1
                        echo "powersave" > $governor_path
                        governor=$(cat "$governor_path")
                    done
                    log2stderr "Configure cpu${processor_id} governor to powersave."
                else
                    log2stderr "Governor of cpu${processor_id} is already set to powersave."
                fi
            else
                log2stderr "Powersave governor is not available for cpu${processor_id}. Skipping..."
            fi
        else
            log2stderr "No scaling_available_governors file for cpu${processor_id}. Skipping governor check."
        fi

    done
}

function unbind_device() {
    physical_bdf=$1

    log2stderr "$1: Preparing passthrough"
    if [ ! -f /sys/bus/pci/devices/${physical_bdf}/vendor ]; then
        log2stderr "$1: ERROR: Device not found."
    else
        vendor_id=$(cat /sys/bus/pci/devices/${physical_bdf}/vendor)
        device_id=$(cat /sys/bus/pci/devices/${physical_bdf}/device)

        log2stderr "$1: Unbinding driver from Service OS..."
        if [ -f /sys/bus/pci/devices/${physical_bdf}/driver/unbind ] && [ ! -d /sys/bus/pci/drivers/pci-stub/${physical_bdf} ]; then
            echo $(printf "%04x %04x" ${vendor_id} ${device_id}) > /sys/bus/pci/drivers/pci-stub/new_id
            echo ${physical_bdf} > /sys/bus/pci/devices/${physical_bdf}/driver/unbind
            echo ${physical_bdf} > /sys/bus/pci/drivers/pci-stub/bind
        else
            log2stderr "$1: No driver bound. Skipping".
        fi
    fi
}

function create_tap() {
    # create a unique tap device for each VM
    tap=$1
    tap_exist=$(ip a show dev $tap)
    if [ "$tap_exist"x != "x" ]; then
        log2stderr "$tap TAP device already available, reusing it."
    else
        ip tuntap add dev $tap mode tap
    fi

    # if acrn-br0 exists, add VM's unique tap device under it
    br_exist=$(ip a | grep acrn-br0 | awk '{print $1}')
    if [ "$br_exist"x != "x" -a "$tap_exist"x = "x" ]; then
        log2stderr "acrn-br0 bridge already exists, adding new $tap TAP device to it..."
        ip link set "$tap" master acrn-br0
        ip link set dev "$tap" down
        ip link set dev "$tap" up
    fi
}

function mount_partition() {
    partition=$1

    tmpdir=`mktemp -d`
    mount ${partition} ${tmpdir}
    echo ${tmpdir}
}

function unmount_partition() {
    tmpdir=$1

    umount ${tmpdir}
    rmdir ${tmpdir}
}

# Generators of device model parameters

function add_cpus() {
    # Each parameter of this function is considered the apicid of processor (as is reported in /proc/cpuinfo) of
    # a CPU assigned to a post-launched RTVM.

    if [ "${vm_type}" = "RTVM" ] || [ "${scheduler}" = "SCHED_NOOP" ] || [ "${own_pcpu}" = "y" ]; then
        offline_cpus $*
    fi

    #Configure governor of CPU to powersave for CPU sharing case.
    if [ "${own_pcpu}" = "None" ]; then
        configure_cpus_governor $*
    fi

    cpu_list=$(local IFS=, ; echo "$*")
    echo -n "--cpu_affinity ${cpu_list}"
}

function add_interrupt_storm_monitor() {
    threshold_per_sec=$1
    probe_period_in_sec=$2
    inject_delay_in_ms=$3
    delay_duration_in_ms=$4

    echo -n "--intr_monitor ${threshold_per_sec},${probe_period_in_sec},${inject_delay_in_ms},${delay_duration_in_ms}"
}

function add_logger_settings() {
    loggers=()

    for conf in $*; do
        case "$conf" in
                console*|kmsg*|disk*)
                        logger=${conf%=*}
                        level=${conf#*=}
                        loggers+=("${logger},level=${level}")
                        ;;
                *)
                        loggers+=("${conf}")
                        ;;
        esac
    done

    cmd_param=$(local IFS=';' ; echo "${loggers[*]}")
    echo -n "--logger_setting ${cmd_param}"
}

function __add_virtual_device() {
    property=$1
    slot=$2
    kind=$3
    options=$4

    if [ "${kind}" = "virtio-net" ]; then
        # Create the tap device
        if [[ ${options} =~ tap=([^,]+) ]]; then
            tap_conf="${BASH_REMATCH[1]}"
            create_tap "${tap_conf}"
        fi
    fi

    if [ "${kind}" = "virtio-input" ]; then
        options=$*
        if [[ "${options}" =~ id:([a-zA-Z0-9_\-]*) ]]; then
            unique_identifier="${BASH_REMATCH[1]}"
            options=${options/",id:${unique_identifier}"/''}
        fi

        if [[ "${options}" =~ (Device name: )(.*),( Device physical path: )(.*) ]]; then
            device_name="${BASH_REMATCH[2]}"
            phys_name="${BASH_REMATCH[4]}"
            local IFS=$'\n'
            device_name_paths=$(grep -r "${device_name}" /sys/class/input/event*/device/name)
            phys_paths=$(grep -r "${phys_name}" /sys/class/input/event*/device/phys)
        fi

        if [ -n "${device_name_paths}" ] && [ -n "${phys_paths}" ]; then
            for device_path in ${device_name_paths}; do
                for phys_path in ${phys_paths}; do
                    if [ "${device_path%/device*}" = "${phys_path%/device*}" ]; then
                        event_path=${device_path}
                        if [[ ${event_path} =~ event([0-9]+) ]]; then
                            event_num="${BASH_REMATCH[1]}"
                            options="/dev/input/event${event_num}"
                            break
                        fi
                    fi
                done
            done
        fi

        if [[ ${options} =~ event([0-9]+) ]]; then
            log2stderr "${options} input device path is available in the service vm."
        else
            log2stderr "${options} input device path is not found in the service vm."
            return
        fi

        if [ -n "${options}" ] && [ -n "${unique_identifier}" ]; then
            options="${options},${unique_identifier}"
        fi

    fi

    if [[ "${property}" =~ (mandatory)(.*) ]]; then
        echo -n "-S "
    else
        echo -n "-s "
    fi

    echo -n "${slot},${kind}"
    if [ -n "${options}" ]; then
        echo -n ",${options}"
    fi
}

function add_virtual_device() {
    __add_virtual_device "optional" "$@"
}

function add_mandatory_virtual_device() {
    __add_virtual_device "mandatory" "$@"
}

function __add_passthrough_device() {
    property=$1
    slot=$2
    physical_bdf=$3
    options=$4

    unbind_device ${physical_bdf%,*}

    # bus, device and function as decimal integers
    bus_temp=${physical_bdf#*:};     bus=$((16#${bus_temp%:*}))
    dev_temp=${physical_bdf##*:};    dev=$((16#${dev_temp%.*}))
    fun=$((16#${physical_bdf#*.}))

    if [[ "${property}" =~ (mandatory)(.*) ]]; then
        echo -n "-S "
    else
        echo -n "-s "
    fi

    printf '%s,passthru,%x/%x/%x' ${slot} ${bus} ${dev} ${fun}
    if [ -n "${options}" ]; then
        echo -n ",${options}"
    fi
}

function add_passthrough_device() {
    __add_passthrough_device "optional" "$@"
}

function add_mandatory_passthrough_device() {
    __add_passthrough_device "mandatory" "$@"
}

# Enable VF
autoprobe_file=/sys/bus/pci/devices/0000\:00\:02.0/sriov_drivers_autoprobe
numvfs_file=/sys/bus/pci/devices/0000\:00\:02.0/sriov_numvfs
totalvfs_file=/sys/bus/pci/devices/0000\:00\:02.0/sriov_totalvfs
numvfs=2

function enable_vf() {
	if [ ! -f $totalvfs_file ]; then
		log2stderr "iGPU SR-IOV not supported, skipping."
	else
		if [ "$numvfs" == "0" ]; then
			log2stderr "Invalid numvfs parameter."
			exit 0
		fi
		local reserved_ggtt_size=268435456
		schedexecq=25
		schedtimeout=500000
		if [ -f /sys/class/drm/card0/iov/pf/gt/ggtt_spare ]; then
			echo $reserved_ggtt_size > /sys/class/drm/card0/iov/pf/gt/ggtt_spare
			echo $schedexecq > /sys/class/drm/card0/iov/pf/gt/exec_quantum_ms
			echo $schedtimeout > /sys/class/drm/card0/iov/pf/gt/preempt_timeout_us
		else
			echo $reserved_ggtt_size > /sys/class/drm/card0/prelim_iov/pf/gt0/ggtt_spare
			echo $schedexecq > /sys/class/drm/card0/prelim_iov/pf/gt0/exec_quantum_ms
			echo $schedtimeout > /sys/class/drm/card0/prelim_iov/pf/gt0/preempt_timeout_us
		fi

		#change auto provision to manual provision(ggtt contexts doorbell)
		local ggtt_quota=$((3221225472/numvfs))
		local contexts_quota=$((57344/numvfs))
		local doorbells_quota=$((256/numvfs))
		for (( i = 1; i <= $numvfs; i++ ))
		do
			if [ -f /sys/class/drm/card0/iov/vf$i/gt/exec_quantum_ms ]; then
				echo $schedexecq > /sys/class/drm/card0/iov/vf$i/gt/exec_quantum_ms
				echo $schedtimeout > /sys/class/drm/card0/iov/vf$i/gt/preempt_timeout_us
				echo $ggtt_quota > /sys/class/drm/card0/iov/vf$i/gt/ggtt_quota
				echo $contexts_quota > /sys/class/drm/card0/iov/vf$i/gt/contexts_quota
				echo $doorbells_quota > /sys/class/drm/card0/iov/vf$i/gt/doorbells_quota
			else
				echo $schedexecq > /sys/class/drm/card0/prelim_iov/vf$i/gt0/exec_quantum_ms
				echo $schedtimeout > /sys/class/drm/card0/prelim_iov/vf$i/gt0/preempt_timeout_us
				echo $ggtt_quota > /sys/class/drm/card0/prelim_iov/vf$i/gt0/ggtt_quota
				echo $contexts_quota > /sys/class/drm/card0/prelim_iov/vf$i/gt0/contexts_quota
				echo $doorbells_quota > /sys/class/drm/card0/prelim_iov/vf$i/gt0/doorbells_quota
			fi
		done

		if [ ! -f $numvfs_file ] || [ "0" == `cat $numvfs_file` ]; then
			log2stderr "Trying to enable $numvfs VF for 00:02.0"
			(echo 0 > $autoprobe_file && echo $numvfs > $numvfs_file) \
				    || { log2stderr "Cannot enable VF. Please make sure you have enabled SR-IOV support in BIOS." && exit 1; }
		fi
	fi
}
enable_vf

check_existence() {
	local file_path=$1
	if [ ! -f "$file_path" ]; then
		echo "File $file_path doesn't exist!"
		exit 1
	fi
}

function enable_dgpu_vf() {
	local dgpu_path="/sys/class/drm/card1"
	local dg2_autoprobe_file="$dgpu_path/device/sriov_drivers_autoprobe"
	local dg2_numvfs_file="$dgpu_path/device/sriov_numvfs"
	local dg2_totalvfs_file="$dgpu_path/device/sriov_totalvfs"

	if [ ! -e $dg2_totalvfs_file ]; then
		log2stderr "File $dg2_totalvfs_file doesn't exist, which means DG2 SR-IOV is not ready yet."
		log2stderr "If dGPU SR-IOV is not required, please set USE_DGPU_SRIOV to 0 in the launch script."
		log2stderr "Otherwise, check your setup carefully (e.g., kernel cmdline arguments)"
		exit 1
	else
		local dg2_numvfs=2
		local iov_dir=""
		if [ -d "$dgpu_path/prelim_iov" ]; then
			iov_dir="$dgpu_path/prelim_iov"
		elif [ -d "$dgpu_path/iov" ]; then
			iov_dir="$dgpu_path/iov"
		else
			log2stderr "Neither $dgpu_path/prelim_iov nor $dgpu_path/iov is found!"
			exit 1
		fi

		if [ ! -f $dg2_numvfs_file ] || [ "$dg2_numvfs" != `cat $dg2_numvfs_file` ]; then
			if [ "0" != `cat $dg2_numvfs_file` ]; then
				log2stderr "Destroy VFs before create them."
				echo "0" > $dg2_numvfs_file
			fi
			log2stderr "Trying to enable $dg2_numvfs VF for 03:00.0"
			(echo 0 > $dg2_autoprobe_file && echo $dg2_numvfs > $dg2_numvfs_file) \
				    || { log2stderr "Cannot enable VF for dGPU" && exit 1; }
		fi
		# Set GGTT size of PF of dGPU to 256MB.  Otherwise, atomic
		# commit when using direct display could fail due to lack of
		# GGTT.
		local reserved_ggtt_size=268435456
		echo "$reserved_ggtt_size" > "$dgpu_path/iov/pf/gt/ggtt_spare"

		local schedexecq=25
		local schedtimeout=500000
		echo $schedexecq | tee -a "$iov_dir/pf/gt/exec_quantum_ms"
		echo $schedtimeout | tee -a "$iov_dir/pf/gt/preempt_timeout_us"
		for (( i = 1; i <= $dg2_numvfs; i++ ))
		do
			check_existence "$iov_dir/vf$i/gt/exec_quantum_ms"
			check_existence "$iov_dir/vf$i/gt/preempt_timeout_us"
			echo $schedexecq | tee -a "$iov_dir/vf$i/gt/exec_quantum_ms"
			echo $schedtimeout | tee -a "$iov_dir/vf$i/gt/preempt_timeout_us"
		done
	fi
}

USE_DGPU_SRIOV=@@__USE_DGPU_SRIOV__@@
if [ "$USE_DGPU_SRIOV" = "1" ]; then
	enable_dgpu_vf
fi

wayland_display="$(find /run/user -name wayland-1)"
if [ -z $wayland_display ]; then
	wayland_display="/run/user/0/wayland-1"
fi
if [ -z ${XDG_RUNTIME_DIR+x} ]; then
    export WAYLAND_DISPLAY=$wayland_display
fi

# Read SSD life of nvme0n1
nvme_percentage_used=$(nvme smart-log /dev/nvme0n1 --raw | od -v -A n -N 1 -j 5 -t d1 | xargs)

