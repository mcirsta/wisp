import urllib.request
import os
import ssl

RESOURCES = [
    {
        "url": "https://curl.se/ca/cacert.pem",
        "filename": "ca-bundle"
    },
    {
        "url": "https://raw.githubusercontent.com/netsurf-browser/netsurf/master/resources/SearchEngines",
        "filename": "SearchEngines"
    },
    {
        "url": "https://raw.githubusercontent.com/netsurf-browser/netsurf/master/resources/default.css",
        "filename": "default.css"
    },
    {
        "url": "https://raw.githubusercontent.com/netsurf-browser/netsurf/master/resources/quirks.css",
        "filename": "quirks.css"
    },
    {
        "url": "https://raw.githubusercontent.com/netsurf-browser/netsurf/master/resources/internal.css",
        "filename": "internal.css"
    }
]

def update_resources():
    base_dir = os.path.join("src", "resources")
    
    # Create unverified context to avoid certificate issues
    context = ssl._create_unverified_context()
    
    print(f"Updating resources in {base_dir}...")
    
    for resource in RESOURCES:
        url = resource["url"]
        filename = resource["filename"]
        target_path = os.path.join(base_dir, filename)
        
        print(f"[{filename}] Fetching from {url}...")
        
        try:
            with urllib.request.urlopen(url, context=context) as response:
                data = response.read()
            
            with open(target_path, 'wb') as f:
                f.write(data)
                
            print(f"[{filename}] Successfully updated.")
            
        except Exception as e:
            print(f"[{filename}] Error: {e}")
            
if __name__ == "__main__":
    update_resources()
