#!/usr/bin/env python3
import sys

def main():
    if len(sys.argv) != 4:
        sys.exit("Usage: makeopv.py <opcode> <flags> <value>")

    try:
        opcode = int(sys.argv[1], 0)
        flags = int(sys.argv[2], 0)
        value = int(sys.argv[3], 0)
    except ValueError:
        sys.exit("Error: Arguments must be numbers (e.g. 10, 0x10)")

    result = opcode | (flags << 10) | (value << 18)
    print("0x{:08x}".format(result))

if __name__ == "__main__":
    main()
