import urllib.request
import re

url = "https://www.w3.org/TR/css-color-4/"
try:
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req) as response:
        html = response.read().decode("utf-8")
        
        # Look for OKLab to sRGB Javascript/pseudocode block
        oklab_match = re.search(r"function OKLab_to_sRGB(.*?)return(.*?)\]", html, re.DOTALL)
        if oklab_match:
            print("--- OKLab to sRGB ---")
            print(oklab_match.group(0))
            
        print("Done")
except Exception as e:
    print(e)
