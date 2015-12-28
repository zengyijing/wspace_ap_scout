#!/usr/bin/python

import sys
sys.path.append("/home/wdr-2/openwrt/setup")
from util import run_cmd

if __name__ == '__main__':
	if len(sys.argv) != 6:
		print "%s use_fec rate_adapt_version is_duplicate num_retrans dirname_log" % sys.argv[0]
		exit(0)

	ACK_TIME_OUT = 450
	RTT = 150  # Should be at least 150ms if the cellular delay is 100ms + Batch sending duration (50ms)
	BATCH_TIME_OUT = 10 # ms
	rate = 10  # Starting rate
	coherence_time = 50e3  # 50ms
	MAX_BATCH_SIZE = 10
	max_contiguous_time_out = 3

	# Tunable params
	use_fec = int(sys.argv[1])
	rate_adapt_version = int(sys.argv[2])
	is_duplicate = int(sys.argv[3])
	NUM_RETRANS = int(sys.argv[4])
	dirname = sys.argv[5]

	# Construct log file
	cmd = "mkdir -p %s" % dirname
	run_cmd(cmd)
	filename_gps = "%s/gps.dat" % dirname
	filename_server = "%s/server.dat" % dirname

	# kill the iperf
	cmd = "killall iperf"
	run_cmd(cmd)

	# Set the MTU properly
	PER_PKT_LEN = 2
	TUNNEL_PKT_SIZE = 1448
	MTU = TUNNEL_PKT_SIZE - MAX_BATCH_SIZE * PER_PKT_LEN
	cmd = "ifconfig tun0 mtu %d" % MTU
	run_cmd(cmd)

	cmd = "./wspace_ap_scout -S 128.105.22.249 -s 192.168.10.1 -m 192.168.10.255 -i tun0 -R %d -r %d -T %d -t %d -B %d -V %d -v %d -M %d -O %d -n %d -f %s > %s" % \
	(NUM_RETRANS, rate, ACK_TIME_OUT, RTT, BATCH_TIME_OUT, use_fec, \
	 rate_adapt_version, coherence_time, is_duplicate, max_contiguous_time_out, filename_gps, filename_server)
	run_cmd(cmd)
