#~/.platformio/penv/bin/python3 -m pip install pillow
#python -m pip install pillow
#pip install pillow
import os
from PIL import Image

IMAGES_DIR = "images"
OUTPUT_DIR = "src/generated"

def rgb888_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def convert_png_to_header(png_path, header_path):
    img = Image.open(png_path).convert("RGB")
    width, height = img.size
    pixels = list(img.getdata())

    with open(header_path, "w") as f:
        name = os.path.splitext(os.path.basename(png_path))[0]
        array_name = f"{name}_img"

        f.write(f"// Auto-generated from {png_path}\n")
        f.write(f"const int {name}_width = {width};\n")
        f.write(f"const int {name}_height = {height};\n")
        f.write(f"const uint16_t {array_name}[] = {{\n")

        for i, (r, g, b) in enumerate(pixels):
            rgb565 = rgb888_to_rgb565(r, g, b)
            f.write(f"0x{rgb565:04X},")
            if (i + 1) % width == 0:
                f.write("\n")

        f.write("};\n")

def main():
    if not os.path.exists(OUTPUT_DIR):
        os.makedirs(OUTPUT_DIR)

    for file in os.listdir(IMAGES_DIR):
        if file.lower().endswith(".png"):
            png_path = os.path.join(IMAGES_DIR, file)
            header_path = os.path.join(OUTPUT_DIR, file.replace(".png", ".h"))
            print(f"Converting {file} → {header_path}")
            convert_png_to_header(png_path, header_path)

main()
