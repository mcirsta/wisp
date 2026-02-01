#!/bin/bash

# Find PIDs for nsqt
# -x matches the exact process name
PIDS=$(pgrep -x nsqt)

if [ -z "$PIDS" ]; then
    echo "Error: 'nsqt' process not found. Make sure the application is running."
    exit 1
fi

for PID in $PIDS; do
    echo "=================================================="
    echo "Process: nsqt (PID: $PID)"
    echo "--------------------------------------------------"
    
    # Check if pmap is available
    if ! command -v pmap &> /dev/null; then
        echo "Error: 'pmap' command not found. Please install it (usually in procps package)."
        exit 1
    fi

    echo "Fetching memory statistics..."
    
    # Process pmap output with awk
    pmap -x "$PID" | awk '
    BEGIN {
        total_rss = 0;   total_dirty = 0;
        qt_rss = 0;      qt_dirty = 0;
    }
    # Skip header lines
    /^Address/ { next }
    /^------/ { next }
    /^total/ { next } # Skip the summary line provided by pmap
    
    # Process mapping lines
    {
        # pmap -x output format: Address Kbytes RSS Dirty Mode Mapping
        # Fields: $1=Address, $2=Kbytes, $3=RSS, $4=Dirty, $5=Mode, $6=Mapping
        
        rss = $3;
        dirty = $4;
        mapping = $6;

        # Add to totals
        total_rss += rss;
        total_dirty += dirty;

        # Check if mapping is a Qt library
        if (mapping ~ /libQt/ || mapping ~ /libqt/) {
            qt_rss += rss;
            qt_dirty += dirty;
        }
    }
    
    END {
        # Convert KB to MB
        total_rss_mb = total_rss / 1024.0;
        total_dirty_mb = total_dirty / 1024.0;
        
        qt_rss_mb = qt_rss / 1024.0;
        qt_dirty_mb = qt_dirty / 1024.0;
        
        app_rss_mb = (total_rss - qt_rss) / 1024.0;
        app_dirty_mb = (total_dirty - qt_dirty) / 1024.0;

        printf "%-20s %10s %10s\n", "Category", "RSS (MB)", "Dirty (MB)"
        printf "--------------------------------------------\n"
        printf "%-20s %10.2f %10.2f\n", "Total Usage", total_rss_mb, total_dirty_mb
        printf "%-20s %10.2f %10.2f\n", "Qt Libraries", qt_rss_mb, qt_dirty_mb
        printf "--------------------------------------------\n"
        printf "%-20s %10.2f %10.2f\n", "Excluding Qt", app_rss_mb, app_dirty_mb
        printf "==================================================\n"
    }'
done
