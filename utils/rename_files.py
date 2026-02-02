#!/usr/bin/env python3
"""
File Renaming Script: Neosurf/Netsurf -> Wisp

Renames files and directories containing 'neosurf' or 'netsurf' to 'wisp'.
Prioritizes 'git mv' if available to preserve history.
"""

import os
import subprocess
import argparse
import sys

def run_command(cmd):
    try:
        subprocess.check_call(cmd, shell=True)
        return True
    except subprocess.CalledProcessError:
        return False

def get_new_name(name):
    lower = name.lower()
    if 'neosurf' in lower:
        return name.replace('neosurf', 'wisp').replace('Neosurf', 'Wisp').replace('NEOSURF', 'WISP')
    if 'netsurf' in lower:
        return name.replace('netsurf', 'wisp').replace('Netsurf', 'Wisp').replace('NETSURF', 'WISP')
    return name

def should_ignore(path):
    parts = path.split(os.sep)
    if '.git' in parts: return True
    if 'build' in parts: return True
    if 'build-ninja' in parts: return True
    if 'build-gcc' in parts: return True
    if '__pycache__' in parts: return True
    if 'utils' in parts and 'rename_files.py' in parts: return True
    return False

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dry-run', action='store_true', help="Don't move files")
    args = parser.parse_args()
    
    root_dir = os.getcwd()
    
    # Check if git usage is allowed/possible
    use_git = os.path.isdir(os.path.join(root_dir, '.git'))
    
    # Collect all items to rename
    # We need to collect them first, because renaming typical directory walking can get confused if we rename dirs on the fly.
    # Also, we should rename deep files BEFORE renaming their parent directories.
    
    to_rename = []
    
    for root, dirs, files in os.walk(root_dir, topdown=False): # Bottom up
        # Files first
        for name in files:
            msg_name = name.lower()
            if 'neosurf' in msg_name or 'netsurf' in msg_name:
                full_path = os.path.join(root, name)
                if not should_ignore(full_path):
                    to_rename.append((full_path, 'file'))
        
        # Then dirs
        for name in dirs:
            msg_name = name.lower()
            if 'neosurf' in msg_name or 'netsurf' in msg_name:
                full_path = os.path.join(root, name)
                if not should_ignore(full_path):
                    to_rename.append((full_path, 'dir'))
                    
    print(f"Found {len(to_rename)} items to rename.")
    
    for old_path, type_ in to_rename:
        dirname = os.path.dirname(old_path)
        basename = os.path.basename(old_path)
        new_basename = get_new_name(basename)
        if new_basename == basename:
            continue
            
        new_path = os.path.join(dirname, new_basename)
        
        rel_old = os.path.relpath(old_path)
        rel_new = os.path.relpath(new_path)
        
        print(f"Rename: {rel_old} -> {rel_new}")
        
        if not args.dry_run:
            if use_git:
                # git mv
                # Note: if it's not tracked by git, git mv might fail, fallback to os.rename
                cmd = f'git mv "{old_path}" "{new_path}"'
                if not run_command(cmd):
                    # Fallback
                    print(f"  git mv failed, using plain mv")
                    os.rename(old_path, new_path)
            else:
                os.rename(old_path, new_path)

if __name__ == '__main__':
    main()
