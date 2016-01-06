void metadata_extract(uint16_t registers[128]);
void metadata_dump_registers(uint16_t registers[128]);

int metadata_get_gain(uint16_t registers[128]);
int metadata_get_dark_offset(uint16_t registers[128]);
double metadata_get_exposure(uint16_t registers[128]);
