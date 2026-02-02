#!/usr/bin/env python3
"""
Project Renaming Script: Neosurf/Netsurf -> Wisp

Rules:
1. CODE: Replace 'neosurf'/'netsurf' with 'wisp' (case-preserved).
2. STRINGS/COMMENTS (Copyright line):
   - If 'Marius Cirsta' found -> Replace 'Marius Cirsta' with 'Wisp'.
   - If 'Neosurf'/'Netsurf' found -> KEEP (do NOT rename).
3. COMMENTS (General):
   - KEEP 'neosurf'/'netsurf' (do NOT rename).
"""

import os
import re
import argparse
import sys

# Extensions mapping to comment style
EXT_MAP = {
    # C-Style: // and /* */
    '.c': 'c', '.cpp': 'c', '.h': 'c', '.hpp': 'c', '.cc': 'c',
    '.js': 'c', '.ts': 'c', '.java': 'c', '.css': 'c', '.scss': 'c',
    '.go': 'c', '.rs': 'c', '.php': 'c', '.swift': 'c',
    
    # Python-Style: # (Docstrings treated as strings usually, but we'll simplistic parse)
    '.py': 'py', '.sh': 'py', '.bash': 'py', '.yaml': 'py', '.yml': 'py',
    '.cmake': 'py', '.txt': 'py', 'makefile': 'py', '.mk': 'py',
    '.conf': 'py', '.ini': 'py', '.cfg': 'py', '.toml': 'py',
    
    # HTML-Style: <!-- -->
    '.html': 'html', '.xml': 'html', '.svg': 'html', '.htm': 'html'
}

# Regex basics
RE_NEOSURF = re.compile(r'(neosurf|netsurf)', re.IGNORECASE)
RE_MARIUS = re.compile(r'Marius\s+Cirsta', re.IGNORECASE)

def is_copyright_line(line):
    return 'copyright' in line.lower()

def replace_token(text):
    """
    Standard replacement for CODE tokens.
    neosurf -> wisp
    Neosurf -> Wisp
    NEOSURF -> WISP
    netsurf -> wisp
    """
    def sub_func(match):
        word = match.group(1)
        lower = word.lower()
        replacement = 'wisp'
        
        if word.isupper():
            return replacement.upper()
        elif word[0].isupper():
            return replacement.capitalize()
        else:
            return replacement
            
    return RE_NEOSURF.sub(sub_func, text)

    # For non-Marius copyright lines, we do NOT rename neosurf/netsurf
    return line

def process_code_part(text):
    """
    Process a code segment.
    If it contains a Copyright notice, apply copyright rules.
    Otherwise, apply standard code renaming.
    """
    if is_copyright_line(text):
        return process_copyright_line_logic(text)
    return replace_token(text)

def process_file_content(content, ext):
    """
    Parse content based on extension and apply rules.
    Returns (new_content, changed_bool)
    """
    style = EXT_MAP.get(ext, 'c') # Default to C-style for unknown
    if os.path.basename(ext).lower() in ['makefile', 'cmakelists.txt', 'dockerfile']:
        style = 'py'
    
    # Simplified parser: split by lines and simplistic comment detection
    # Note: Full tokenization is safer but complex. 
    # For this task, we will iterate line by line and try to identify comment parts.
    
    lines = content.splitlines(keepends=True)
    new_lines = []
    changes_made = False
    
    in_block_comment = False
    
    for line in lines:
        original_line = line
        
        if style == 'c':
            line, in_block_comment = process_c_line(line, in_block_comment)
        elif style == 'py':
            line = process_py_line(line)
        elif style == 'html':
            line, in_block_comment = process_html_line(line, in_block_comment)
        else:
            # Treat as text/code hybrid? For now, treat as code but respect copyright lines
            line = process_generic_line(line)
            
        if line != original_line:
            changes_made = True
            
        new_lines.append(line)
        
    return ''.join(new_lines), changes_made

def process_c_line(line, in_block_comment):
    # This is a heuristic line processor.
    # It attempts to separate Code, Line Comment, and Block Comment parts.
    
    # If we are inside a /* ... */ block
    if in_block_comment:
        if '*/' in line:
            # End of block
            pre, post = line.split('*/', 1)
            # 'pre' is purely comment
            new_pre = process_comment_text(pre + '*/')
            # 'post' needs standard processing (might contain code or more comments)
            new_post, still_in_block = process_c_line(post, False) 
            # Note: process_c_line returns (str, bool), we need to reconstruct
            return new_pre + new_post, still_in_block
        else:
            # Still in block comment
            return process_comment_text(line), True

    # Check for //
    if '//' in line:
        # Check if // is inside a string? 
        # Ideally we'd scan char by char, but let's assume valid source code rarely puts // inside strings for project logic
        # Exception: URLs like http://...
        # Simple heuristic: if quote count before // is even, it's likely a comment
        idx = line.find('//')
        # Check valid comment
        pre_comment = line[:idx]
        if pre_comment.count('"') % 2 == 0 and pre_comment.count("'") % 2 == 0:
            # It IS a comment
            code_part = pre_comment
            comment_part = line[idx:]
            
            new_code = process_code_part(code_part)
            new_comment = process_comment_text(comment_part)
            return new_code + new_comment, False

    # Check for /* (Start of block)
    if '/*' in line:
        idx = line.find('/*')
        # Check validity (quotes)
        pre_comment = line[:idx]
        if pre_comment.count('"') % 2 == 0 and pre_comment.count("'") % 2 == 0:
            code_part = pre_comment
            rest = line[idx:]
            
            # Does it end on same line?
            if '*/' in rest:
                # /* ... */ inline
                comment_end_idx = rest.find('*/') + 2
                comment_part = rest[:comment_end_idx]
                rem_code = rest[comment_end_idx:]
                
                new_code = process_code_part(code_part)
                new_comment = process_comment_text(comment_part)
                # Recurse for the remainder (could be code or another comment)
                new_rem, _ = process_c_line(rem_code, False)
                return new_code + new_comment + new_rem, False
            else:
                # Starts block comment
                new_code = process_code_part(code_part)
                new_comment = process_comment_text(rest)
                return new_code + new_comment, True

    # If no comment found, treat as code (but check for Copyright strings)
    if is_copyright_line(line):
        return process_copyright_line_logic(line), False
        
    return replace_token(line), False

def process_py_line(line):
    # Python style: # comments
    if '#' in line:
        idx = line.find('#')
        pre_comment = line[:idx]
        # Quote check
        if pre_comment.count('"') % 2 == 0 and pre_comment.count("'") % 2 == 0:
            code_part = pre_comment
            comment_part = line[idx:]
            
            new_code = process_code_part(code_part)
            new_comment = process_comment_text(comment_part)
            return new_code + new_comment
            
    # Check for Copyright string
    if is_copyright_line(line):
        return process_copyright_line_logic(line)
        
    return replace_token(line)

def process_html_line(line, in_block_comment):
    # HTML comments <!-- ... -->
    if in_block_comment:
        if '-->' in line:
            pre, post = line.split('-->', 1)
            new_pre = process_comment_text(pre + '-->')
            new_post, still_in_block = process_html_line(post, False)
            return new_pre + new_post, still_in_block
        else:
            return process_comment_text(line), True

    if '<!--' in line:
        idx = line.find('<!--')
        code_part = line[:idx]
        rest = line[idx:]
        
        if '-->' in rest:
            end_idx = rest.find('-->') + 3
            comment_part = rest[:end_idx]
            rem_code = rest[end_idx:]
            
            new_code = process_code_part(code_part)
            new_comment = process_comment_text(comment_part)
            new_rem, _ = process_html_line(rem_code, False)
            return new_code + new_comment + new_rem, False
        else:
            new_code = process_code_part(code_part)
            new_comment = process_comment_text(rest)
            return new_code + new_comment, True
            
    if is_copyright_line(line):
        return process_copyright_line_logic(line), False
        
    return replace_token(line), False

def process_generic_line(line):
    if is_copyright_line(line):
        return process_copyright_line_logic(line)
    return replace_token(line)

def process_comment_text(text):
    """
    Process text identified as a comment.
    Rule:
    - If Copyright line: Marius Cirsta -> Wisp. Neosurf/Netsurf -> Keep.
    - Else: Keep Neosurf/Netsurf.
    """
    if is_copyright_line(text):
        return process_copyright_line_logic(text)
    
    # Generic Comment: Do NOT rename neosurf/netsurf
    return text

def process_copyright_line_logic(line):
    """
    Apply copyright specific rules.
    1. If Marius Cirsta -> Replace Neosurf/Netsurf -> Wisp (Keep Name)
    2. If Other -> Keep Neosurf/Netsurf (do NOT run replace_token)
    """
    # First, handle the Marius Cirsta replacement
    if RE_MARIUS.search(line):
        # User Rule: "Copyright 2024 Marius Cirsta for Neosurf" -> "... for Wisp"
        # We process this line to replace neosurf/netsurf tokens, but we keep the name.
        return replace_token(line)
    
    # For non-Marius copyright lines, we do NOT rename neosurf/netsurf
    return line

def should_ignore(path):
    parts = path.split(os.sep)
    if '.git' in parts: return True
    if 'build' in parts: return True
    if 'build-ninja' in parts: return True
    if 'build-gcc' in parts: return True
    if '__pycache__' in parts: return True
    # Ignore the script itself
    if path.endswith('rename_project.py'): return True
    return False

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dry-run', action='store_true', help="Don't write changes")
    parser.add_argument('--target', help="Specific file to safeguard/test")
    args = parser.parse_args()
    
    root_dir = os.getcwd()
    
    targets = []
    if args.target:
        targets.append(os.path.abspath(args.target))
    else:
        for root, dirs, files in os.walk(root_dir):
            # prune directories
            dirs[:] = [d for d in dirs if not d.startswith('.')]
            if 'build' in dirs: dirs.remove('build')
            
            for file in files:
                full_path = os.path.join(root, file)
                if not should_ignore(full_path):
                    targets.append(full_path)
    
    print(f"Checking {len(targets)} files...")
    
    for file_path in targets:
        try:
            # Attempt to read as UTF-8
            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
                
            ext = os.path.splitext(file_path)[1].lower()
            if os.path.basename(file_path) == 'Makefile': ext = 'makefile'
            
            new_content, changed = process_file_content(content, ext)
            
            if changed:
                print(f"Modified: {os.path.relpath(file_path)}")
                if not args.dry_run:
                    with open(file_path, 'w', encoding='utf-8') as f:
                        f.write(new_content)
                else:
                    # Print preview of changes for first file
                    pass
                    
        except Exception as e:
            print(f"Skipping {os.path.relpath(file_path)}: {e}")

if __name__ == '__main__':
    main()
