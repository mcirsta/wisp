# Git History Cleanup Instructions

## Problem

The repository contains:
- `HotNews.mhtml` (89.82 MB) - triggering GitHub large file warnings
- 1,907 libnsbmp test files with colons (`:`) in filenames - illegal on Windows

## Solution

Use `git-filter-repo` on a Linux machine to remove these files from history.

## Files Included

1. **invalid_files.txt** - List of 1,908 files to remove
2. **cleanup-git-history.sh** - Automated cleanup script

## Prerequisites (Linux)

- Git 2.22.0 or newer
- Python 3.x
- pip3

## Instructions

### Step 1: Transfer to Linux

Transfer the entire repository to a Linux machine, or use WSL on Windows:

```bash
# If using WSL
cd /mnt/c/proj/neosurf-mcirsta
```

### Step 2: Run the Script

```bash
chmod +x cleanup-git-history.sh
./cleanup-git-history.sh
```

The script will:
1. Check for `git-filter-repo` (install if missing)
2. Show you what will be removed
3. Ask for confirmation
4. Execute the cleanup
5. Show next steps

### Step 3: Verify

```bash
# Verify HotNews.mhtml is gone
git log --all -- HotNews.mhtml
# Should return nothing

# Check one of the invalid test files
git log --all -- "libnsbmp/test/afl-bmp/id:000023,src:000000,op:flip1,pos:28,+cov.bmp"
# Should return nothing

# Check repository size (should be much smaller)
git count-objects -vH
```

### Step 4: Update Remote

```bash
# Re-add the remote (filter-repo removes it as a safety measure)
git remote add origin https://github.com/mcirsta/neosurf.git

# Force push to update GitHub
git push origin main --force
```

## Manual Execution (Alternative)

If you prefer to run the command manually:

```bash
# Install git-filter-repo
pip3 install --user git-filter-repo

# Run the cleanup
git-filter-repo --paths-from-file invalid_files.txt --invert-paths --force
```

## What Gets Removed

### HotNews.mhtml
- Size: 89.82 MB
- Location: Repository root
- Reason: GitHub large file warning

### libnsbmp Test Files (1,907 files)
- Location: `libnsbmp/test/afl-bmp/`, `libnsbmp/test/afl-ico/`, `libnsbmp/test/ns-afl-bmp/`
- Reason: Filenames contain colons (`:`) which are illegal on Windows
- Type: AFL (American Fuzzy Lop) fuzzing test cases

Example filenames:
- `id:000023,src:000000,op:flip1,pos:28,+cov.bmp`
- `id:000010,src:000000,op:flip1,pos:4,+cov.ico`
- `id:000007,orig:g32def.bmp`

## Safety Notes

✅ **Only these 1,908 files will be removed**  
✅ **All other files remain intact**  
✅ **Source code, documentation, and valid test files are unaffected**

⚠️ **This rewrites git history** - commit hashes will change  
⚠️ **Requires force push** - coordinate with collaborators  
⚠️ **Backup recommended** - though git-filter-repo creates safety backups in `.git/filter-repo/`

## Troubleshooting

### "git-filter-repo: command not found"
```bash
pip3 install --user git-filter-repo
# Add to PATH if needed
export PATH="$HOME/.local/bin:$PATH"
```

### "invalid_files.txt: No such file"
Make sure you're in the repository root where the file was created.

### Permission denied on script
```bash
chmod +x cleanup-git-history.sh
```

## After Cleanup

Once the cleanup is complete and pushed:

1. **Windows users** will be able to clone without issues
2. **Repository size** will decrease significantly
3. **GitHub warnings** will disappear
4. **All collaborators** should re-clone or rebase their work

## Questions?

This cleanup removes only files that:
- Cannot be checked out on Windows anyway (invalid filenames)
- Are causing GitHub warnings (large files)

The repository functionality remains completely intact.
