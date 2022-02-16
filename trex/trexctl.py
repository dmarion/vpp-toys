#!/usr/bin/python3

import os
import sys
import signal
import argparse
import ipaddress

sys.path.insert(0, os.path.join(os.environ['HOME'], 'src', 'trex-core',
                                'scripts', 'automation', 'trex_control_plane',
                                'interactive'))

from trex.stl.api import *
from scapy.contrib.geneve import *

class TRexCtl:
    def handler(self, signum, frame):
        print('Interrupted...')
        self.c.stop(ports=self.ports)
        self.print_stats()
        self.c.disconnect()
        sys.exit(1)

    def __init__(self):
        signal.signal(signal.SIGINT, self.handler)
        self.c = STLClient()
        self.ports = [0, 1]
        profiles = ["l3-base-ip4", "l3-scale-ip4", "l3-geneve-ip4"]
        p = argparse.ArgumentParser()
        p.add_argument('-l', '--pkt-len', type=int, action="store",
                       dest="pkt_len", default="64", help="packet length")
        p.add_argument('-m', '--multiplier', action="store",
                       dest="mult", default="100%", help="multiplier")
        p.add_argument('-p', '--profile', action='store',
                       help='profile', choices=profiles, default=profiles[0])
        p.add_argument('--ip-addr', action='store', dest="ip_addr",
                       help='ip address', default=["172.16.0.2", "172.16.1.2"])
        p.add_argument('--default-gw', action='store', dest="default_gw",
                       help='default gateway', default=["172.16.0.1", "172.16.1.1"])
        p.add_argument('--range-addr', action='store', dest="range_addr",
                       help='range address', default=['10.0.0.1', '10.128.0.1'])
        p.add_argument('--range-mask', action='store', dest="range_mask",
                       help='range mask', default=['0.127.255.248', '0.127.255.248'])
        p.add_argument('--tunnel-src-addr', action='store', dest="tunnel_src_addr",
                       help='tunnel src ip address', default=["1.1.1.1", "2.2.2.2"])
        p.add_argument('--tunnel-dst-addr', action='store', dest="tunnel_dst_addr",
                       help='tunnel dst ip address', default=["9.9.9.9", "9.9.9.9"])
        p.add_argument('--vni', action='store', dest="vni",
                       help='VNI', default=[101,102])
        p.add_argument('--inner-ip-addr', action='store', dest="inner_ip_addr",
                       help='inner ip address', default=["192.168.1.1",
                       "192.168.2.1"])

        self.args = p.parse_args()

    def get_streams(self, direction=0, **kwargs):
        if direction == 0:
            s, d = 0, 1
        else:
            s, d = 1, 0

        raw = []

        if self.args.profile == "l3-scale-ip4":
            print("{:15} {}/{}".format("Source:", self.args.range_addr[s],
                                       self.args.range_mask[s]))
            print("{:15} {}/{}".format("Destination:", self.args.range_addr[d],
                                       self.args.range_mask[d]))
            src_ip = self.args.range_addr[s]
            dst_ip = self.args.range_addr[d]
            src_mask = int(ipaddress.IPv4Address(self.args.range_mask[s]))
            dst_mask = int(ipaddress.IPv4Address(self.args.range_mask[d]))
            raw = [
                STLVmFlowVar(name="sip", size=4, op="random",
                             max_value="255.255.255.255"),
                STLVmFlowVar(name="dip", size=4, op="random",
                             max_value="255.255.255.255"),
                STLVmWrMaskFlowVar(fv_name="sip", pkt_offset="IP.src",
                                   pkt_cast_size=4, mask=src_mask),
                STLVmWrMaskFlowVar(fv_name="dip", pkt_offset="IP.dst",
                                   pkt_cast_size=4, mask=dst_mask),
                STLVmFixIpv4(offset="IP")
            ]
        if self.args.profile == "l3-base-ip4":
            src_ip = self.args.ip_addr[s]
            dst_ip = self.args.ip_addr[d]

        if self.args.profile == "l3-geneve-ip4":
            src_ip = self.args.inner_ip_addr[s]
            dst_ip = self.args.inner_ip_addr[d]
            tunnel_src_addr = self.args.tunnel_src_addr[s]
            tunnel_dst_addr = self.args.tunnel_dst_addr[s]
            pkt = Ether()
            pkt = pkt/IP(src=tunnel_src_addr, dst=tunnel_dst_addr)
            pkt = pkt/UDP()
            pkt = pkt/GENEVE(vni=self.args.vni[s])
        else:
            pkt = Ether()

        pkt = pkt/IP(src=src_ip, dst=dst_ip)
        pkt = pkt/UDP(dport=1234, sport=1234)
        pad = max(0, self.args.pkt_len - len(pkt)) * 'x'
        pkt = pkt/pad

        if raw:
            pkt = STLPktBuilder(pkt=pkt, vm=STLScVmRaw(raw))
        else:
            pkt = STLPktBuilder(pkt=pkt)

        return [STLStream(packet=pkt, mode=STLTXCont())]

    def print_stats(self):
        s = self.c.get_stats(ports=self.ports)
        print("")
        print("Statistics:")
        line = "{:>15} | ".format("")
        for p in self.ports:
            line += "{:>15} | ".format("Port {}".format(p))
        line += "{:>15}".format("Total")
        print(line)
        for f in ["ipackets", "opackets", "ibytes", "obytes",
                  "ierrors", "oerrors"]:
            line = "{:>15} | ".format(f)
            for p in self.ports:
                line += "{:>15} | ".format(s[p][f])
            line += "{:>15}".format(s['total'][f])
            print(line)

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

            print("Entering service mode...")
            c.set_service_mode(ports=ports, enabled=True)

            for p in ports:
                print("Setting port {} into L3 mode...".format(p))
                print("{:18} {}".format("IP Address:", self.args.ip_addr[p]))
                print("{:18} {}".format("Default Gateway:",
                                        self.args.default_gw[p]))
                c.set_l3_mode(port=p, src_ipv4=self.args.ip_addr[p],
                              dst_ipv4=self.args.default_gw[p])

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

            print("Clearing stats...")
            c.clear_stats()

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
