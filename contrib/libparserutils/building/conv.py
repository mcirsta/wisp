import sys
import os

def main():
    if len(sys.argv) != 2:
        sys.exit("Usage: conv.py <input_file>")

    input_file = sys.argv[1]
    table = []

    try:
        with open(input_file, 'r', encoding='utf-8', errors='replace') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                
                parts = line.split()
                
                # Check for hex code
                try:
                    val = int(parts[0], 16)
                except ValueError:
                    continue

                # Ignore ASCII part
                if val < 0x80:
                    continue

                # Convert undefined entries to U+FFFF
                # Perl: if ($parts[1] =~ /^#/)
                if len(parts) > 1:
                    if parts[1].startswith('#'):
                        table.append("0xFFFF")
                    else:
                        table.append(parts[1])
                else:
                    table.append("0xFFFF")

    except FileNotFoundError:
        sys.exit(f"Failed opening {input_file}")

    # Print C structure
    # Note: The Perl script prints the filename as the array name, 
    # expecting the user to fix it. We replicate this behavior.
    var_name = os.path.basename(input_file)
    print(f"static uint32_t {var_name}[128] = {{\n\t", end='')

    count = 0
    for item in table:
        print(f"{item}, ", end='')
        count += 1

        if count % 8 == 0 and count != 128:
            print("\n\t", end='')

    print("\n};\n")

if __name__ == "__main__":
    main()
