#include "fnt.h"
#include "little.h"

bool WriteFntDirectory(FILE *output, unsigned int base, unsigned int id, unsigned int &entry_start,
	unsigned int first_file, unsigned int parent, const std::vector<FntName> &names)
{
	if (fseek(output, base + 8*(id & 0xFFF), SEEK_SET)) return false;
	unsigned_int offset = entry_start;
	unsigned_short file_id = first_file;
	unsigned_short parent_id = parent;
	if (fwrite(&offset, 1, sizeof(offset), output) != sizeof(offset)
		|| fwrite(&file_id, 1, sizeof(file_id), output) != sizeof(file_id)
		|| fwrite(&parent_id, 1, sizeof(parent_id), output) != sizeof(parent_id)) return false;
	if (fseek(output, base + entry_start, SEEK_SET)) return false;
	for (const FntName &entry : names)
	{
		if (!entry.length || entry.length > 127) return false;
		if (fputc(entry.directory ? entry.length | 128 : entry.length, output) == EOF
			|| fwrite(entry.name, 1, entry.length, output) != entry.length) return false;
		entry_start += 1 + entry.length;
		if (entry.directory)
		{
			unsigned_short directory = entry.directory;
			if (fwrite(&directory, 1, sizeof(directory), output) != sizeof(directory)) return false;
			entry_start += sizeof(directory);
		}
	}
	if (fputc(0, output) == EOF) return false;
	entry_start++;
	return true;
}
