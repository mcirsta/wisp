#!/usr/bin/env python3
import sys
import os
import re

def parse_fat_messages(filepath):
    """
    Parses the FatMessages file into a dictionary structure.
    data[key][lang][plat] = value
    """
    data = {}
    # Regex to match lines: lang.plat.Key:Value
    # Key can contain dots? "Contents of Key _must_ be representable in the US-ASCII character set"
    # Usually Key doesn't contain dots, but let's be careful.
    # The format is strictly: lang.plat.Key:Value
    # lang is code (en, fr, zh_CN).
    # plat is (all, win, gtk, ...).
    
    with open(filepath, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            
            # Find the first colon
            try:
                colon_index = line.index(':')
            except ValueError:
                continue
            
            meta_key = line[:colon_index]
            value = line[colon_index+1:]
            
            # Split meta_key into lang.plat.Key
            # We assume lang and plat do not contain dots, but Key might?
            # Actually, looking at the file: "en.all.NetSurf:NetSurf"
            # It seems to be first dot, second dot.
            parts = meta_key.split('.', 2)
            if len(parts) != 3:
                continue
            
            lang, plat, key = parts
            
            if key not in data:
                data[key] = {}
            if lang not in data[key]:
                data[key][lang] = {}
            
            data[key][lang][plat] = value
            
    return data

def generate_messages(data, target_lang, platforms, output_file):
    """
    Generates the Messages file for the given language and platforms.
    platforms: list of platform strings in priority order (e.g., ['win', 'all'])
    """
    default_lang = 'en'
    
    # We want to preserve the order of keys? 
    # The hash table implementation doesn't care, but for diffs it's nice.
    # Dictionary in Python 3.7+ preserves insertion order, so if we iterate keys we get them in order of appearance (roughly).
    
    with open(output_file, 'wb') as f:
        # Write keys
        for key in data:
            selected_value = None
            
            # Try target language
            if target_lang in data[key]:
                for plat in platforms:
                    if plat in data[key][target_lang]:
                        selected_value = data[key][target_lang][plat]
                        break
            
            # Fallback to default language
            if selected_value is None and default_lang in data[key]:
                for plat in platforms:
                    if plat in data[key][default_lang]:
                        selected_value = data[key][default_lang][plat]
                        break
            
            if selected_value is not None:
                # Output format: Key:Value\n
                # Encoding: UTF-8 (as per FatMessages description)
                # "Values must be UTF-8 encoded strings"
                output_line = f"{key}:{selected_value}\n"
                f.write(output_line.encode('utf-8'))

def main():
    if len(sys.argv) < 5:
        print("Usage: split_messages.py <FatMessages> <output_file> <lang> <platform1> [platform2 ...]")
        sys.exit(1)
    
    fat_messages_path = sys.argv[1]
    output_path = sys.argv[2]
    lang = sys.argv[3]
    platforms = sys.argv[4:]
    
    # Always add 'all' to platforms if not present?
    # The caller should provide it. But usually 'all' is the fallback.
    # If the user provides "win", we should probably check "win" then "all".
    if 'all' not in platforms:
        platforms.append('all')
        
    data = parse_fat_messages(fat_messages_path)
    generate_messages(data, lang, platforms, output_path)

if __name__ == "__main__":
    main()
