
import sys

if len(sys.argv) < 2:
  print("Usage: python convert_elf.py filename.elf.txt")
  sys.exit(-1)

dumped_elf = open(sys.argv[1], "r")

in_section = False
in_array = False
array_num = 0
print_sections = (".text", ".rodata", ".binary_info", ".data", ".scratch_x", ".scratch_y", ".usb_ram")
address = 0
for line in dumped_elf:
  if line.startswith("Contents of section"):
    in_section = False
    for valid_section in print_sections:
      if valid_section in line:
        in_section = True
        break
    continue
    
  if in_section and line.startswith(" "):
    line_address = int(line[1:9], 16)
    if address != line_address:
      if in_array:
        print("};")
      address = line_address
      print("constexpr uint elf_data{}_addr = 0x{:08x};".format(array_num, address))
      print("const uint elf_data{}[] = {{".format(array_num))
      in_array = True
      array_num += 1
    try:
      for i in range(10,45,9):
        data_swapped = int(line[i:i+8], 16)
        data = ((data_swapped & 0xFF) << 24) | ((data_swapped & 0xFF00) << 8) | ((data_swapped & 0xFF0000) >> 8) | ((data_swapped & 0xFF000000) >> 24)
        print("0x{:08x},".format(data))
        address += 4
    except:
      pass

if in_array:
  print("};")

print("const uint section_addresses[] = {")
for i in range(array_num):
  print("elf_data{}_addr,".format(i))
print("};")
print("const uint* section_data[] = {")
for i in range(array_num):
  print("elf_data{},".format(i))
print("};")
print("const uint section_data_len[] = {")
for i in range(array_num):
  print("sizeof(elf_data{}),".format(i))
print("};")
