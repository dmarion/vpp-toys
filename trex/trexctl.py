#!/usr/bin/python3

import os
import sys
import signal
import argparse

sys.path.insert(0, os.path.join(os.environ['HOME'], 'src', 'trex-core',
                                'scripts', 'automation', 'trex_control_plane',
                                'interactive'))

from trex.stl.api import *

src_ipv4 = ["172.16.0.2", "172.16.1.2"]
dst_ipv4 = ["172.16.0.1", "172.16.1.1"]
start_range = ["10.0.0.3", "10.128.0.3"]
end_range = ["10.127.255.251", "10.255.255.251"]


class TRexCtl:
    def handler(self, signum, frame):
        print('Interrupted...')
        self.c.stop(ports=self.ports)
        self.c.stop()
        self.c.disconnect()
        sys.exit(1)

    def __init__(self):
        signal.signal(signal.SIGINT, self.handler)
        self.c = STLClient()
        self.ports = [0, 1]
        profiles = ["l3-base-ip4", "l3-scale-ip4"]
        p = argparse.ArgumentParser()
        p.add_argument('-l', '--pkt-len', type=int, action="store",
                       dest="pkt_len", default="64", help="packet length")
        p.add_argument('-m', '--multiplier', action="store",
                       dest="mult", default="100%", help="multiplier")
        p.add_argument('-p', '--profile', action='store',
                       help='profile', choices=profiles, default=profiles[0])
        self.args = p.parse_args()

    def get_streams(self, direction=0, **kwargs):
        if direction == 0:
            src_ip = src_ipv4[0]
            dst_ip = src_ipv4[1]
            range_src_start = start_range[0]
            range_src_end = end_range[0]
            range_dst_start = start_range[1]
            range_dst_end = end_range[1]
        else:
            src_ip = src_ipv4[1]
            dst_ip = src_ipv4[0]
            range_src_start = start_range[1]
            range_src_end = end_range[1]
            range_dst_start = start_range[0]
            range_dst_end = end_range[0]

        pkt = Ether()
        pkt = pkt/IP(src=src_ip, dst=dst_ip)
        pkt = pkt/UDP(dport=2010, sport=2020)
        pad = max(0, self.args.pkt_len - len(pkt)) * 'x'
        pkt = pkt/pad

        if self.args.profile == "l3-scale-ip4":
            vm = STLVM()
            vm.var(name="sip", min_value=range_src_start,
                   max_value=range_src_end, size=4, op="inc", step=8)
            vm.var(name="dip", min_value=range_dst_start,
                   max_value=range_dst_end, size=4, op="inc", step=8)
            vm.write(fv_name="sip", pkt_offset="IP.src")
            vm.write(fv_name="dip", pkt_offset="IP.dst")
            vm.fix_chksum()
            pkt = STLPktBuilder(pkt=pkt, vm=vm)
        else:
            pkt = STLPktBuilder(pkt=pkt)
        return [STLStream(packet=pkt, mode=STLTXCont())]

    def run(self):
        c = self.c
        ports = self.ports

        port_info_fields = ["pci_addr", "driver", "hw_mac",  "src_mac", "arp",
                            "src_ipv4", "speed", "numa", "link", "status"]
        try:
            c.connect()
            ssi = c.get_server_system_info()
            sv = c.get_server_version()
            line = "Connected to " + ssi['hostname']
            line += " running TRex " + sv['version']
            line += " with {} cores (".format(ssi['dp_core_count'])
            line += "{} cores per port)...".format(
                ssi['dp_core_count_per_port'])
            print(line)

            c.reset(ports=ports)

            print("Clearing stats...")
            c.clear_stats()

            print("Entering service mode...")
            c.set_service_mode(ports=ports, enabled=True)

            for p in ports:
                print("Setting port {} into L3 mode...".format(p))
                c.set_l3_mode(
                    port=p, src_ipv4=src_ipv4[p], dst_ipv4=dst_ipv4[p])

            print("Resolve ...")
            c.resolve(ports=ports)

            print("Leaving service mode...")
            c.set_service_mode(ports=ports, enabled=False)

            print("Port info:")
            line = "{:>12}".format("")
            for p in ports:
                line += " | {:^20}".format("Port {}".format(p))
            print(line)

            port_info = c.get_port_info(ports=ports)
            for f in port_info_fields:
                line = "{:>12}".format(f)
                for p in ports:
                    line += " | {:<20}".format(port_info[p][f])
                print(line)
            print("")

            print("{:15} {}".format("Packet length:", self.args.pkt_len))
            print("{:15} {}".format("Multiplier:", self.args.mult))
            print("{:15} {}".format("Profile:", self.args.profile))
            print("")

            for p in ports:
                print("Setting up stream on port {}...".format(p))
                c.add_streams(self.get_streams(direction=p), ports=p)

            print("Start traffic...")
            self.c.start(ports=ports, mult=self.args.mult)

            n_lines = 999999
            while True:
                fields = ["tx_pps", "rx_pps"]
                if n_lines > 25:
                    print("")
                    n_lines = 0
                    line = ""
                    for p in ports:
                        line += "{:^32}".format("Port {}".format(p))
                        line += " | "
                    line += "{:^32}".format("Global")
                    print(line)

                    line = ""
                    chunk = "{:^16}{:^16}".format("TX", "RX")
                    for p in ports:
                        line += chunk + " | "
                    line += chunk
                    print(line)

                    line = ""
                    chunk = "{:>8}{:>8}".format("[Mpps]", "[Gb/s]")
                    for p in ports:
                        line += chunk + chunk + " | "
                    line += chunk + chunk
                    print(line)

                n_lines += 1

                s = c.get_stats(ports=ports)
                line = ""
                for p in ports:
                    line += "{:>8.2f}".format(s[p]['tx_pps'] / 1e6)
                    line += "{:>8.2f}".format(s[p]['tx_bps'] / 1e9)
                    line += "{:>8.2f}".format(s[p]['rx_pps'] / 1e6)
                    line += "{:>8.2f}".format(s[p]['rx_bps'] / 1e9)
                    line += " | "
                line += "{:>8.2f}".format(s['global']['tx_pps'] / 1e6)
                line += "{:>8.2f}".format(s['global']['tx_bps'] / 1e9)
                line += "{:>8.2f}".format(s['global']['rx_pps'] / 1e6)
                line += "{:>8.2f}".format(s['global']['rx_bps'] / 1e9)
                print(line)
                time.sleep(1)

        except STLError as ex_error:
            print(ex_error, file=sys.stderr)
            sys.exit(1)
        finally:
            c.disconnect()


if __name__ == u"__main__":
    trexctl = TRexCtl()
    trexctl.run()
