#!/bin/bash
set -e

echo "================================================================"
echo "Git History Cleanup Script"
echo "================================================================"
echo ""
echo "This script will remove 1,908 files from git history:"
echo "  - HotNews.mhtml (89.82 MB)"
echo "  - 1,907 libnsbmp test files with invalid Windows filenames"
echo ""
echo "WARNING: This will rewrite git history!"
echo "================================================================"
echo ""

# Check if git-filter-repo is installed
if ! command -v git-filter-repo &> /dev/null; then
    echo "git-filter-repo is not installed."
    echo "Installing via pip3..."
    pip3 install --user git-filter-repo
    echo ""
fi

# Verify we're in a git repository
if [ ! -d .git ]; then
    echo "ERROR: Not in a git repository!"
    exit 1
fi

# Check if invalid_files.txt exists
if [ ! -f invalid_files.txt ]; then
    echo "ERROR: invalid_files.txt not found!"
    exit 1
fi

# Count files to be removed
FILE_COUNT=$(wc -l < invalid_files.txt)
echo "Found $FILE_COUNT files to remove in invalid_files.txt"
echo ""

# Confirm before proceeding
read -p "Proceed with history cleanup? (yes/no): " CONFIRM
if [ "$CONFIRM" != "yes" ]; then
    echo "Aborted."
    exit 0
fi

echo ""
echo "Running git-filter-repo..."
echo ""

# Run the filter
git-filter-repo --paths-from-file invalid_files.txt --invert-paths --force

echo ""
echo "================================================================"
echo "SUCCESS! Git history has been cleaned."
echo "================================================================"
echo ""
echo "Next steps:"
echo "  1. Verify the cleanup with: git log --all -- HotNews.mhtml"
echo "     (should return nothing)"
echo "  2. Check repository size: du -sh .git"
echo "  3. Re-add remote: git remote add origin https://github.com/mcirsta/neosurf.git"
echo "  4. Force push: git push origin main --force"
echo ""
echo "IMPORTANT: Other collaborators will need to re-clone or rebase!"
echo "================================================================"
