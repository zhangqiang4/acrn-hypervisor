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
mkdir -p /var/lock/acrn

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

# flock protected critical section. Wait timeout 20s, fd 18
# Start of iGPU VF enabling critical section
(flock -w 20 18

# Enable VF
autoprobe_file=/sys/bus/pci/devices/0000\:00\:02.0/sriov_drivers_autoprobe
numvfs_file=/sys/bus/pci/devices/0000\:00\:02.0/sriov_numvfs
totalvfs_file=/sys/bus/pci/devices/0000\:00\:02.0/sriov_totalvfs
numvfs=2

function config_igpu_vf() {
	local reserved_ggtt_size=268435456
	local schedexecq=25
	local schedtimeout=500000

	# for xe driver
	if [ -f /sys/kernel/debug/dri/0/gt0/pf/ggtt_spare ]; then
		echo $reserved_ggtt_size > /sys/kernel/debug/dri/0/gt0/pf/ggtt_spare
		echo $schedexecq > /sys/kernel/debug/dri/0/gt0/pf/exec_quantum_ms
		echo $schedtimeout > /sys/kernel/debug/dri/0/gt0/pf/preempt_timeout_us
		for (( i = 1; i <= $numvfs; i++ ))
		do
			echo $schedexecq > /sys/kernel/debug/dri/0/gt0/vf$i/exec_quantum_ms
			echo $schedtimeout > /sys/kernel/debug/dri/0/gt0/vf$i/preempt_timeout_us
		done
		return 0
	fi

	# for i915 driver
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
	local contexts_quota=$((57344/128/numvfs*128))
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
}

function enable_vf() {
	if [ ! -f $totalvfs_file ]; then
		log2stderr "iGPU SR-IOV not supported, skipping."
	else
		if [ "$numvfs" == "0" ]; then
			log2stderr "Invalid numvfs parameter."
			exit 0
		fi

		config_igpu_vf

		if [ ! -f $numvfs_file ] || [ "0" == `cat $numvfs_file` ]; then
			log2stderr "Trying to enable $numvfs VF for 00:02.0"
			(echo 0 > $autoprobe_file && echo $numvfs > $numvfs_file) \
				    || { log2stderr "Cannot enable VF. Please make sure you have enabled SR-IOV support in BIOS." && exit 1; }
		fi
	fi
}
enable_vf

# End of iGPU VF enabling critical section
)18>/var/lock/acrn/acrn_enable_igpu_vf.lock

check_existence() {
	local file_path=$1
	if [ ! -f "$file_path" ]; then
		echo "File $file_path doesn't exist!"
		exit 1
	fi
}

# flock protected critical section. Wait timeout 20s, fd 19
# Start of dGPU VF enabling critical section
(flock -w 20 19

function enable_dgpu_vf() {
	local dgpu_path="/sys/class/drm/card1"
	local dg2_autoprobe_file="$dgpu_path/device/sriov_drivers_autoprobe"
	local dg2_numvfs_file="$dgpu_path/device/sriov_numvfs"
	local dg2_totalvfs_file="$dgpu_path/device/sriov_totalvfs"

	if [ ! -e $dg2_totalvfs_file ]; then
		log2stderr "dGPU SR-IOV not supported. Skipping."
	else
		local dg2_numvfs=1
		local iov_dir=""
		if [ -d "$dgpu_path/prelim_iov" ]; then
			iov_dir="$dgpu_path/prelim_iov"
		elif [ -d "$dgpu_path/iov" ]; then
			iov_dir="$dgpu_path/iov"
		else
			log2stderr "Neither $dgpu_path/prelim_iov nor $dgpu_path/iov is found!"
			exit 1
		fi

		if [ "$dg2_numvfs" == `cat $dg2_numvfs_file` ]; then
			echo "dGPU VFs have been created, skip creation"
			return;
		fi

		local count=0
		for i in $(ls $dgpu_path); do
			path="$dgpu_path/$i/status"
			if [ -f $path ]; then
				if ( cat $path |grep "connected" |grep -v "disconnected"  > /dev/null); then
					let "count++"
				fi
			fi
		done

		echo "connected monitor count: $count"
		if [ $count -eq "0" ]; then
			count=1
		fi

		if [ "$dg2_numvfs" -eq "2" ]; then
			echo 0 > "$iov_dir"/pf/auto_provisioning

			# Reset VF parameters
			for i in `seq 1 7`; do
				echo 0 > "$iov_dir"/"vf$i"/gt/ggtt_quota
				echo 0 > "$iov_dir"/"vf$i"/gt/lmem_quota
				echo 0 > "$iov_dir"/"vf$i"/gt/contexts_quota
				echo 0 > "$iov_dir"/"vf$i"/gt/doorbells_quota
			done

			local pf_lmem=$((count*128*1024*1024))
			echo "$pf_lmem"   >  "$iov_dir"/pf/gt/lmem_spare
			# Reserve 256MB GGTT size for PF to avoid atomic commit failure
			echo 268435456    >  "$iov_dir"/pf/gt/ggtt_spare
			echo 256          >  "$iov_dir"/pf/gt/contexts_spare
			echo 0            >  "$iov_dir"/pf/gt/doorbells_spare

			# Provision VFs
			local vf1_ggtt=$(cat "$iov_dir"/pf/gt/available/ggtt_max_quota)
			local vf1_ggtt=$((vf1_ggtt/2))
			echo "$vf1_ggtt"  >  "$iov_dir"/vf1/gt/ggtt_quota
			local vf2_ggtt=$(cat "$iov_dir"/pf/gt/available/ggtt_max_quota)
			echo "$vf2_ggtt"  >  "$iov_dir"/vf2/gt/ggtt_quota

			# vf1_lmem:vf2_lmem=3:1
			local vf1_lmem=$(cat "$iov_dir"/pf/gt/available/lmem_max_quota)
			local vf1_lmem=$((vf1_lmem*3/4))
			echo "$vf1_lmem"  >  "$iov_dir"/vf1/gt/lmem_quota
			local vf2_lmem=$(cat "$iov_dir"/pf/gt/available/lmem_max_quota)
			echo "$vf2_lmem"  >  "$iov_dir"/vf2/gt/lmem_quota
			printf "local memory reserved for VF1 = %d MB\n" $((vf1_lmem/1024/1024))
			printf "local memory reserved for VF2 = %d MB\n" $((vf2_lmem/1024/1024))

			echo 28672        >  "$iov_dir"/vf1/gt/contexts_quota
			echo 28672        >  "$iov_dir"/vf2/gt/contexts_quota

			echo 128          >  "$iov_dir"/vf1/gt/doorbells_quota
			echo 128          >  "$iov_dir"/vf2/gt/doorbells_quota
		else
			# Use auto provisioning for other number of VFs
			echo 1 > "$iov_dir"/pf/auto_provisioning
			local pf_lmem=$((count*128*1024*1024))
			echo "$pf_lmem"   >  "$iov_dir"/pf/gt/lmem_spare
			# Reserve 256MB GGTT size for PF to avoid atomic commit failure
			echo 268435456    >  "$iov_dir"/pf/gt/ggtt_spare
		fi

		if [ ! -f $dg2_numvfs_file ] || [ "$dg2_numvfs" != `cat $dg2_numvfs_file` ]; then
			if [ "0" != `cat $dg2_numvfs_file` ]; then
				log2stderr "Destroy VFs before create them."
				echo "0" > $dg2_numvfs_file
			fi
			log2stderr "Trying to enable $dg2_numvfs VF for dGPU"
			(echo 0 > $dg2_autoprobe_file && echo $dg2_numvfs > $dg2_numvfs_file) \
				    || { log2stderr "Cannot enable VF for dGPU" && exit 1; }
		fi

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

# TODO: This symbol is deprecated. We should execute enable_dgpu_vf by default.
USE_DGPU_SRIOV=@@__USE_DGPU_SRIOV__@@
if [ "$USE_DGPU_SRIOV" = "1" ]; then
	enable_dgpu_vf
fi

# End of dGPU VF enabling critical section
)19>/var/lock/acrn/acrn_enable_dgpu_vf.lock

function parse_vdisplay_cfg() {
	local pname=$1
	local params=$2
	local out=""

	local ptype="direct"
	local res=""
	local pt_params=""

	for kv in $(echo $params | sed 's/,/ /g'); do
		k=${kv%%=*}; v=${kv##*=};
		case $k in
			"type") ptype=$v ;;
			"res") res=$v ;;
			*) pt_params="$pt_params:$k=$v" ;;
		esac
	done

	case $ptype in
		"direct")
			# vdisplay-direct doesn't need res=xxx params, ignore it
			out="pipe-display=${pname}${pt_params}"
			;;
		"sdl")
			local connected=$(wayland-info -i wl_output | grep "name:" | grep -v "version")
			if [ -z "$connected" ]; then
				log2stderr "Error: No connected display found for type 'sdl'."
			elif [ -n "$res" ]; then
				if [ "$pname" != "_" ]; then
					log2stderr "Warning: 'res' specified for pipe type 'sdl'. Ignoring specified pipe $pname."
					log2stderr "Warning: Display window will be positioned by display server."
					log2stderr "Warning: To suppress this warning for using windowed virtual display, specify '_' as pipe name."
					log2stderr "Warning: E.g., _,type=sdl,res=$res"
				fi
				out="geometry=${res}${pt_params}"
			else
				local pscreen=$(echo $connected | grep -n -w $pname | cut -d: -f1 | xargs)
				if [ -z $pscreen ]; then
						log2stderr "Error: Pipe $pname doesn't exist, or is disconnected, or is not assigned to SOS."
				else
					out="geometry=fullscreen:$((pscreen-1))${pt_params}"
				fi
			fi
			;;
		"plane")
			if [ -n "$res" ]; then
				out="plane-display=${res}:port=${pname}${pt_params}"
			else
				log2stderr "Error: $pname: resolution not specified for type 'plane'."
				log2stderr "Error: Example: $pname,type=plane,res=1920x1080"
			fi
			;;
		*)
			log2stderr "Error: Unsupported type $ptype."
			log2stderr "Error: Supported types: sdl, direct, plane"
			;;
	esac

	[ -z $out ] && log2stderr "Error: Skipping this pipe ($pname)." || echo $out
}

# Below three takes input from stdin instead of arguments
card2pcibdf() { echo $(find -L /dev/dri/by-path/ -samefile /dev/dri/$(cat) -printf '%f' 2>/dev/null | sed -e 's/pci-//' -e 's/-card//'); }
pipe2card()   { echo $(find /sys/class/drm -name "*$(cat)" -printf "%f" | cut -d'-' -f1); }
is_bdf_dgpu() { [ $(cat) = "0000:00:02.0" ] && printf "0" || printf "1"; }

function pnames2hex() {
	local bstr=""
	for p in $(echo $1 | sed 's/,/ /g'); do
		bstr="$(echo $p | pipe2card | card2pcibdf | is_bdf_dgpu)${bstr}"
	done
	printf "0x%x\n" "$((2#$bstr))"
}

function dgpu_sriov_params() {
	local valid_pnames=$1
	local accconf=$2

	for cfg in $(echo $accconf | sed 's/,/ /g'); do
		card=${cfg%%-*}; dev=${cfg##*-}; cardbdf="$(echo $card | card2pcibdf)";
		if [ -n "$cardbdf" ] && [ "$cardbdf" != "0000:00:02.0" ]; then
			echo ",dgpu-vfid=${dev#vf}:$(pnames2hex $valid_pnames)"
			break
		fi
	done
}

function accel_pt() {
	local slot=$1
	local conf=$2
	local out=""

	local ind=1
	for accelcfg in $(echo $conf | sed 's/,/ /g'); do
		card=${accelcfg%%-*}; dev=${accelcfg##*-};
		pbdf=$(echo $card | card2pcibdf | sed "s/\.[0-9]$/.$(echo $dev | sed 's/vf//')/g")
		if [ -n "$pbdf" ]; then
			out="${out} $(add_passthrough_device "$slot.$ind" $pbdf)"
			ind=$((ind+1))
		else
			log2stderr "Error: $card doesn't exist. Skip accel $accelcfg."
		fi
	done

	echo $out
}

function add_vdisplay() {
	# If we reach here we assume iGPU SR-IOV is enabled, and dGPU SR-IOV is enabled if supported.
	pipe_name_regex="(?<pname>(([a-zA-Z0-9]+(-[a-zA-Z0-9]+)*)|[\_]))"
	key_val_regex="(?<key>[a-zA-Z0-9]+)=(?<val>[^,|\'\"\=\s]+)"
	pipe_conf_regex="(?<pcfg>($pipe_name_regex(?<params>(,$key_val_regex)*)))"
	accel_card_regex="(?<accname>card(?<card>[0-9])-(?<dev>(vf[0-9])))"
	accel_conf_regex="accel=(?<accconf>$accel_card_regex(,$accel_card_regex)*)"
	regex="(?<fullpcfg>^($pipe_conf_regex)(\|$pipe_conf_regex)*)(\|$accel_conf_regex)?$"

	slot=$1
	input=$2

	local out=""
	local parts=($(perl -e "if ('$input' =~ /$regex/g) { print \"$+{fullpcfg} $+{accconf}\"}"))
	local fullpcfg=${parts[0]}
	local accconf=${parts[1]}

	if [ -z $fullpcfg ]; then
		log2stderr "Invalid argument: $input"
	else
		valid_pnames=""
		for pipecfg in $(echo $fullpcfg | sed 's/|/ /g'); do
			parts=($(perl -e "if ('$pipecfg' =~ /$pipe_conf_regex/g) { print \"$+{pname} $+{params}\"}"))
			pname=${parts[0]}; params=${parts[1]};
			local p=$(parse_vdisplay_cfg $pname $params)
			if [ -n "$p" ]; then
				out="${out}${p},"
				valid_pnames="${valid_pnames}${pname},"
			fi
		done

		# Remove trailing comma
		out="${out%,}"

		if [ -n "$valid_pnames" ] && [ -n "$accconf" ] && [ "$USE_DGPU_SRIOV" = "1" ]; then
			out="${out}$(dgpu_sriov_params $valid_pnames $accconf)"
		fi

		if [ -n "$valid_pnames" ]; then
			out=$(add_virtual_device "$slot.0" "virtio-gpu" "$out")
			out="${out} $(accel_pt $slot $accconf)"
		fi
	fi
	echo $out
}

wayland_display="$(find /run/user -name wayland-1)"
if [ -z $wayland_display ]; then
	wayland_display="/run/user/0/wayland-1"
fi
if [ -z ${XDG_RUNTIME_DIR+x} ]; then
    export WAYLAND_DISPLAY=$wayland_display
fi

# Read SSD life of nvme0n1
nvme_percentage_used=$(nvme smart-log /dev/nvme0n1 --raw | od -v -A n -N 1 -j 5 -t d1 | xargs)

