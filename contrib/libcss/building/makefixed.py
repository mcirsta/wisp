import sys

def main():
    if len(sys.argv) != 2:
        sys.exit("Usage: makefixed.py <value>")

    try:
        val = float(sys.argv[1])
    except ValueError:
        sys.exit("Error: Argument must be a number")

    val *= (1 << 10)

    # Round away from zero
    if val < 0:
        val -= 0.5
    else:
        val += 0.5

    # Truncates back towards zero
    val = int(val)
    
    # Mask to 32-bit unsigned for hex printing (like Perl's printf %x)
    print("0x{:08x}".format(val & 0xffffffff))

if __name__ == "__main__":
    main()
