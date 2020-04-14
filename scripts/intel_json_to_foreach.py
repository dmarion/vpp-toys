#!/usr/bin/env python3

import sys, json, textwrap

objects = json.load(sys.stdin)

c = sys.stdout

wrapper = textwrap.TextWrapper(width=70, drop_whitespace=False)

for obj in objects:
    MSRIndex = obj["MSRIndex"]
    if MSRIndex != "0":
      continue
    if "," in obj["EventCode"]:
        continue

    c.write (" \n  _({}, ".format(obj["EventCode"]))
    c.write ("{}, ".format(obj["UMask"]))
    c.write ("{}, ".format(obj["EdgeDetect"]))
    c.write ("{}, ".format(obj["AnyThread"]))
    c.write ("{}, ".format(obj["Invert"]))
    c.write ("0x{:02x}, ".format(int(obj["CounterMask"])))
    c.write ("0, {},".format(obj["EventName"]).replace(".", ", "))
    wrapped_description = wrapper.wrap(text=obj["BriefDescription"])
    for line in wrapped_description:
      c.write (" \\\n    \"{}\"".format(line))
    c.write(") \\")

c.write ("\n")
