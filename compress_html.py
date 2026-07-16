import gzip
import os

html_path = "/Users/james/Documents/PlatformIO/Projects/midi-monitor/src/index.html"
h_path = "/Users/james/Documents/PlatformIO/Projects/midi-monitor/src/index_html.h"

print(f"Opening HTML file: {html_path}")
with open(html_path, 'rb') as f_in:
    html_data = f_in.read()

print(f"Compressing HTML data of length: {len(html_data)} bytes...")
compressed_data = gzip.compress(html_data)

print(f"Compressed length: {len(compressed_data)} bytes.")

array_content = []
for i, b in enumerate(compressed_data):
    if i % 12 == 0:
        array_content.append("\n    ")
    array_content.append(f"0x{b:02X},")

c_array = "".join(array_content).strip()
if c_array.endswith(","):
    c_array = c_array[:-1]

h_content = f"""#pragma once
#include <Arduino.h>

// Gzipped HTML contents of index.html
const uint32_t INDEX_HTML_GZ_LEN = {len(compressed_data)};
const uint8_t INDEX_HTML_GZ[] PROGMEM = {{
    {c_array}
}};
"""

print(f"Writing output header: {h_path}")
with open(h_path, 'w') as f_out:
    f_out.write(h_content)

print("Done successfully!")
