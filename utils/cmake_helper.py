#!/usr/bin/env python3
import sys
import os

def convert_eol(input_path, output_path):
    """
    Reads input_path in binary mode, strips UTF-8 BOM if present,
    converts CRLF to LF, trims trailing spaces/tabs per line,
    and writes to output_path.
    """
    try:
        with open(input_path, 'rb') as f:
            content = f.read()

        # Strip UTF-8 BOM if present
        if content.startswith(b'\xef\xbb\xbf'):
            content = content[3:]

        # Convert CRLF to LF
        content = content.replace(b'\r\n', b'\n')

        # Trim trailing whitespace on each line
        lines = content.split(b'\n')
        lines = [ln.rstrip(b' \t') for ln in lines]
        content = b'\n'.join(lines)

        with open(output_path, 'wb') as f:
            f.write(content)

    except Exception as e:
        print("Error converting EOL: {}".format(e))
        sys.exit(1)

def replace_text(file_path, old_text, new_text):
    """
    Reads file_path, replaces old_text with new_text, and writes back.
    """
    try:
        with open(file_path, 'r') as f:
            content = f.read()
            
        new_content = content.replace(old_text, new_text)
        
        with open(file_path, 'w') as f:
            f.write(new_content)
            
    except Exception as e:
        print("Error replacing text: {}".format(e))
        sys.exit(1)

def file_to_hex(input_path, output_path, var_name):
    """
    Reads input_path, converts to C hex array, and writes to output_path.
    """
    try:
        with open(input_path, 'rb') as f:
            content = f.read()
            
        hex_values = ["0x{:02x}".format(b) for b in content]
        
        lines = []
        for i in range(0, len(hex_values), 12):
            chunk = hex_values[i:i+12]
            lines.append("  " + ", ".join(chunk) + ",")
            
        output_content = "unsigned char {}[] = {{\n{}\n}};\nunsigned int {}_len = {};\n".format(var_name, "\n".join(lines), var_name, len(content))
        
        with open(output_path, 'w') as f:
            f.write(output_content)
            
    except Exception as e:
        print("Error converting file to hex: {}".format(e))
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python cmake_helper.py convert-eol <input> <output>")
        print("  python cmake_helper.py replace <file> <old> <new>")
        print("  python cmake_helper.py file-to-hex <input> <output> <var_name>")
        sys.exit(1)

    command = sys.argv[1]

    if command == "convert-eol":
        if len(sys.argv) != 4:
            print("Usage: python cmake_helper.py convert-eol <input> <output>")
            sys.exit(1)
        convert_eol(sys.argv[2], sys.argv[3])

    elif command == "replace":
        if len(sys.argv) != 5:
            print("Usage: python cmake_helper.py replace <file> <old> <new>")
            sys.exit(1)
        replace_text(sys.argv[2], sys.argv[3], sys.argv[4])

    elif command == "file-to-hex":
        if len(sys.argv) != 5:
            print("Usage: python cmake_helper.py file-to-hex <input> <output> <var_name>")
            sys.exit(1)
        file_to_hex(sys.argv[2], sys.argv[3], sys.argv[4])

    else:
        print("Unknown command: {}".format(command))
        sys.exit(1)
