from PIL import Image
import os

png_path = "voice bangla.png"
ico_path = "resources/app_icon.ico"

os.makedirs("resources", exist_ok=True)

print(f"Opening PNG with Pillow to create ICO")
img = Image.open(png_path)

# Ensure it's RGBA
img = img.convert("RGBA")

# Ensure the image is square by padding it with transparency if necessary
max_dim = max(img.width, img.height)
square_img = Image.new('RGBA', (max_dim, max_dim), (0, 0, 0, 0))
paste_x = (max_dim - img.width) // 2
paste_y = (max_dim - img.height) // 2
square_img.paste(img, (paste_x, paste_y))

icon_sizes = [(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
print(f"Saving to {ico_path} with sizes {icon_sizes}")
square_img.save(ico_path, format="ICO", sizes=icon_sizes)

print("Done!")
